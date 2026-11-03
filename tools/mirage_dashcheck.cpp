// mirage_dashcheck — does an MPEG-DASH MPD declare what its media holds?
// @bandwidth against the segment sizes with @minBufferTime (ISO/IEC
// 23009-1's definition), @codecs against the decoder configurations,
// @width / @height / @sar, @frameRate and the transfer characteristics
// against the decoded video, @audioSamplingRate and AudioChannelConfiguration
// against the decoded audio, @startWithSAP against the first picture of
// every segment read, the timeline against the segments' earliest
// presentation times and durations, and @segmentAlignment across each
// AdaptationSet (src/DashConform.h, read by src/DashConformProbe.h). No
// window, GL or ImGui. Prints a table and the findings; --json writes the
// whole report.
//
// Exit codes:
//   0 — checked, no error found (warnings and notes may be listed)
//   1 — checked, errors found
//   2 — bad arguments, or the MPD could not be read

#include "DashConformProbe.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>

#include <unistd.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/log.h>
}

namespace {

const char* kUsage =
    "usage: mirage_dashcheck [options] <MPD URL or path>\n"
    "\n"
    "Checks each Representation of an MPEG-DASH MPD against its media:\n"
    "@bandwidth against the segment sizes with @minBufferTime (ISO/IEC 23009-1),\n"
    "@codecs against the decoder configurations, @width/@height/@sar, @frameRate\n"
    "and TransferCharacteristics against the decoded video, @audioSamplingRate and\n"
    "AudioChannelConfiguration against the decoded audio, @startWithSAP against\n"
    "the first picture of the segments read, the timeline against their earliest\n"
    "presentation times and durations, @segmentAlignment across each AdaptationSet.\n"
    "\n"
    "options:\n"
    "  --segments N|all        segments read in full per video Representation\n"
    "                          (default 3: the first, the last and evenly between)\n"
    "  --audio-segments N|all  per audio Representation (default 1)\n"
    "  --no-sizes              size only the segments read or indexed: no request\n"
    "                          per segment, @bandwidth is then checked against a\n"
    "                          lower bound\n"
    "  --workers N             requests at a time (default 4)\n"
    "  --json FILE             write the report as JSON\n"
    "  --quiet                 the findings only\n"
    "  --help\n"
    "\n"
    "exit status: 0 no error found, 1 errors found, 2 not checked\n";

const char* mark(const hls::Cell& c)
{
    if (!c.checked) return "-";
    switch (c.sev) {
        case hls::Severity::Error:   return "ERROR";
        case hls::Severity::Warning: return "warn";
        case hls::Severity::Info:    return "note";
        case hls::Severity::Ok:      return "ok";
    }
    return "?";
}

void print_report(const DashProbe::Result& r, bool quiet)
{
    const dash::Mpd&    m   = r.mpd;
    const dash::Report& rep = r.report;
    if (!quiet) {
        int reads = 0, sized = 0, segs = 0;
        for (std::size_t i = 0; i < r.reads.size(); ++i) {
            reads += static_cast<int>(r.reads[i].reads.size());
            segs += static_cast<int>(m.reps[i].segments.size());
            for (const auto b : r.reads[i].sizes) sized += b >= 0 ? 1 : 0;
        }
        std::printf("%s\n%s MPD, %zu Representations in %zu AdaptationSets, @minBufferTime %s: %d of %d segments "
                    "sized, %d read, in %.1f s\n\n",
                    r.source.c_str(), m.dynamic ? "dynamic" : "static", m.reps.size(), m.sets.size(),
                    std::isfinite(m.min_buffer_time) ? std::format("{:.3f} s", m.min_buffer_time).c_str() : "absent",
                    sized, segs, reads, r.seconds);
        // needs / safe: the bucket model's two rates (DashConform.h, Bucket), below which playout runs dry
        // whatever the order of each segment's bytes / at or above which it never does.
        std::printf("%3s  %-11s %-11s %-11s %-6s %-6s %-6s %-6s %-6s %-6s %-6s %-6s %s\n", "#", "@bandwidth", "needs",
                    "safe", "CODECS", "SIZE", "FPS", "AUDIO", "COLOUR", "SAP", "TIME", "ALIGN", "Representation");
        for (std::size_t i = 0; i < m.reps.size() && i < rep.rows.size(); ++i) {
            const auto& row = rep.rows[i];
            using dash::Check;
            const hls::Cell& bw = row.cell(Check::Bandwidth);
            const std::string safe = bw.checked ? hls::format_bitrate(row.bucket.safe) : "-";
            std::printf("%3zu  %-11s %-11s %-11s %-6s %-6s %-6s %-6s %-6s %-6s %-6s %-6s %s\n", i, bw.declared.c_str(),
                        bw.measured.empty() ? "-" : bw.measured.c_str(), safe.c_str(), mark(row.cell(Check::Codecs)),
                        mark(row.cell(Check::Resolution)), mark(row.cell(Check::FrameRate)), mark(row.cell(Check::Audio)),
                        mark(row.cell(Check::Colour)), mark(row.cell(Check::Sap)), mark(row.cell(Check::Timeline)),
                        mark(row.cell(Check::Alignment)), dash::rep_label(m, static_cast<int>(i)).c_str());
        }
        std::printf("\n");
    }
    int notes = 0;
    for (const auto& f : rep.findings) notes += f.sev == hls::Severity::Info ? 1 : 0;
    std::printf("%d error%s, %d warning%s, %d note%s\n", rep.errors, rep.errors == 1 ? "" : "s", rep.warnings,
                rep.warnings == 1 ? "" : "s", notes, notes == 1 ? "" : "s");
    for (const auto& f : rep.findings) {
        std::string ids;
        for (std::size_t k = 0; k < f.renditions.size(); ++k) {
            if (k == 6) {
                ids += std::format(" and {} more", f.renditions.size() - k);
                break;
            }
            ids += (k ? ", " : "") + m.reps[static_cast<std::size_t>(f.renditions[k])].id;
        }
        const char* tag = f.sev == hls::Severity::Error ? "ERROR" : f.sev == hls::Severity::Warning ? "warn " : "note ";
        std::printf("%s %s%s\n", tag, f.title.c_str(), ids.empty() ? "" : std::format("  [{}]", ids).c_str());
        if (!f.detail.empty() && !quiet) std::printf("      %s\n", f.detail.c_str());
    }
}

bool parse_count(std::string_view v, int& out)
{
    if (v == "all") {
        out = 0;
        return true;
    }
    char* end = nullptr;
    const std::string s(v);
    const long n = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str() || *end || n < 1 || n > 100000) return false;
    out = static_cast<int>(n);
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    DashProbe::Options opts;
    std::string url, json_path;
    bool quiet = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        auto value = [&](std::string_view& out) {
            if (i + 1 >= argc) return false;
            out = argv[++i];
            return true;
        };
        std::string_view v;
        if (a == "--help" || a == "-h") {
            std::fputs(kUsage, stdout);
            return 0;
        } else if (a == "--segments") {
            if (!value(v) || !parse_count(v, opts.segments)) {
                std::fprintf(stderr, "mirage_dashcheck: --segments takes a count or \"all\"\n");
                return 2;
            }
        } else if (a == "--audio-segments") {
            if (!value(v) || !parse_count(v, opts.audio_segments)) {
                std::fprintf(stderr, "mirage_dashcheck: --audio-segments takes a count or \"all\"\n");
                return 2;
            }
        } else if (a == "--no-sizes") {
            opts.sizes = false;
        } else if (a == "--workers") {
            if (!value(v) || !parse_count(v, opts.workers) || opts.workers == 0 || opts.workers > 16) {
                std::fprintf(stderr, "mirage_dashcheck: --workers takes 1 to 16\n");
                return 2;
            }
        } else if (a == "--json") {
            if (!value(v)) {
                std::fprintf(stderr, "mirage_dashcheck: --json takes a file name\n");
                return 2;
            }
            json_path = v;
        } else if (a == "--quiet") {
            quiet = true;
        } else if (a.starts_with("-") && a.size() > 1) {
            std::fprintf(stderr, "mirage_dashcheck: unknown option %s\n%s", argv[i], kUsage);
            return 2;
        } else if (url.empty()) {
            url = a;
        } else {
            std::fprintf(stderr, "mirage_dashcheck: one MPD at a time\n");
            return 2;
        }
    }
    if (url.empty()) {
        std::fputs(kUsage, stderr);
        return 2;
    }

    // FFmpeg's errors only: a malformed segment is reported in the findings.
    av_log_set_level(AV_LOG_ERROR);
    avformat_network_init();

    DashProbe probe;
    probe.start(url, opts);
    const bool tty = !quiet && isatty(fileno(stderr));
    while (probe.running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (!tty) continue;
        const auto p = probe.progress();
        std::fprintf(stderr, "\rindexes %d/%d · segments read %d/%d · sized %d/%d · %.1f MB   ", p.indexes,
                     p.indexes_total, p.reads, p.reads_total, p.sizes, p.sizes_total, static_cast<double>(p.bytes) / 1e6);
    }
    if (tty) std::fprintf(stderr, "\r%80s\r", "");
    const DashProbe::Result r = probe.result();
    if (probe.state() == DashProbe::State::Failed) {
        std::fprintf(stderr, "mirage_dashcheck: %s\n", r.error.c_str());
        return 2;
    }
    print_report(r, quiet);
    if (!json_path.empty()) {
        std::ofstream out(json_path, std::ios::binary);
        out << dash::conformance_json(r.mpd, r.reads, r.report, r.source);
        if (!out) {
            std::fprintf(stderr, "mirage_dashcheck: cannot write %s\n", json_path.c_str());
            return 2;
        }
    }
    return r.report.errors > 0 ? 1 : 0;
}
