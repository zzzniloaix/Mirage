// mirage_hlscheck — does an HLS master playlist declare what its media
// holds? BANDWIDTH and AVERAGE-BANDWIDTH against the segments' bit rates,
// CODECS against the decoder configurations, RESOLUTION, FRAME-RATE and
// VIDEO-RANGE against the decoded video, the audio renditions' CHANNELS, and
// every segment read: its first picture and its duration against EXTINF
// (src/HlsConform.h, read by src/HlsConformProbe.h); and the break markers
// of every variant, audio and subtitle playlist, EXT-X-DATERANGE and the
// tags named by --marker-tag, against each other (src/HlsMarkers.h). No
// window, GL or ImGui. Prints a table and the findings; --json writes the
// whole report, --html the same report as a page to read in a browser
// (src/HlsReportHtml.h).
//
// Exit codes:
//   0 — checked, no error found (warnings and notes may be listed)
//   1 — checked, errors found (conformance or markers)
//   2 — bad arguments, or the master playlist could not be read

#include "HlsConformProbe.h"
#include "HlsReportHtml.h"
#include "JsonText.h"

#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <unistd.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/log.h>
}

namespace {

const char* kUsage =
    "usage: mirage_hlscheck [options] <master playlist URL or path>\n"
    "\n"
    "Checks each EXT-X-STREAM-INF of an HLS master playlist against its media:\n"
    "BANDWIDTH / AVERAGE-BANDWIDTH against the peak / average segment bit rate\n"
    "(RFC 8216), CODECS against the streams' decoder configurations, RESOLUTION,\n"
    "FRAME-RATE and VIDEO-RANGE against the decoded video, CHANNELS of the audio\n"
    "renditions, and the first picture and duration of every segment read.\n"
    "Then the break markers of every variant, audio and subtitle playlist:\n"
    "EXT-X-DATERANGE, and the tags named by --marker-tag, matched by ID; a tag\n"
    "should come before the segment nearest the break, an interstitial's\n"
    "START-DATE should fall on a segment boundary.\n"
    "Encrypted segments: SAMPLE-AES / SAMPLE-AES-CTR are read without the key\n"
    "(container and timestamps, nothing decoded); AES-128 ones are only sized.\n"
    "\n"
    "options:\n"
    "  --segments N|all        segments read in full per variant playlist (default 3:\n"
    "                          the first, the last and evenly between)\n"
    "  --audio-segments N|all  per audio rendition playlist (default 1)\n"
    "  --no-sizes              size only the segments read: no request per segment,\n"
    "                          BANDWIDTH is then checked against a lower bound\n"
    "  --workers N             requests at a time (default 4)\n"
    "  --marker-tag NAME       also match this tag as a marker (without '#', e.g.\n"
    "                          EXT-X-CUE-OUT); its ID attribute, else its value,\n"
    "                          pairs it with the others; repeatable\n"
    "  --markers-only          the marker check only: playlists, no segment request\n"
    "  --json FILE             write the report as JSON\n"
    "  --html FILE             write the report as an HTML page: self-contained, no\n"
    "                          request, opens offline in any browser\n"
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

std::string short_uri(const std::string& uri)
{
    const std::string p = uri.substr(0, uri.find_first_of("?#"));
    const auto last = p.rfind('/');
    if (last == std::string::npos || last == 0) return p;
    const auto prev = p.rfind('/', last - 1);
    return prev == std::string::npos ? p : p.substr(prev + 1);
}

void print_report(const ConformProbe::Result& r, bool quiet)
{
    const auto& m   = r.master;
    const auto& rep = r.report;
    if (!quiet) {
        int reads = 0, sized = 0, segs = 0;
        for (const auto& p : r.playlists) {
            reads += static_cast<int>(p.reads.size());
            segs += static_cast<int>(p.tl.segments.size());
            for (const auto b : p.sizes) sized += b >= 0 ? 1 : 0;
        }
        std::printf("%s\n%zu variants, %zu media playlists: %d of %d segments sized, %d read, in %.1f s\n\n",
                    r.source.c_str(), m.variants.size(), r.playlists.size(), sized, segs, reads, r.seconds);
        std::printf("%3s  %-11s %-11s %-6s %-6s %-6s %-6s %-6s %-6s %-6s %s\n", "#", "BANDWIDTH", "peak", "AVG",
                    "CODECS", "RES", "FPS", "RANGE", "START", "DUR", "playlist");
        for (std::size_t i = 0; i < m.variants.size() && i < rep.rows.size(); ++i) {
            const auto& row = rep.rows[i];
            using hls::Check;
            std::printf("%3zu  %-11s %-11s %-6s %-6s %-6s %-6s %-6s %-6s %-6s %s\n", i,
                        row.cell(Check::Bandwidth).declared.c_str(),
                        row.cell(Check::Bandwidth).measured.empty() ? "-" : row.cell(Check::Bandwidth).measured.c_str(),
                        mark(row.cell(Check::AverageBandwidth)), mark(row.cell(Check::Codecs)),
                        mark(row.cell(Check::Resolution)), mark(row.cell(Check::FrameRate)),
                        mark(row.cell(Check::VideoRange)), mark(row.cell(Check::SegmentStart)),
                        mark(row.cell(Check::Duration)), short_uri(m.variants[i].uri).c_str());
        }
        std::printf("\n");
    }
    int notes = 0;
    for (const auto& f : rep.findings) notes += f.sev == hls::Severity::Info ? 1 : 0;
    std::printf("%d error%s, %d warning%s, %d note%s\n", rep.errors, rep.errors == 1 ? "" : "s", rep.warnings,
                rep.warnings == 1 ? "" : "s", notes, notes == 1 ? "" : "s");
    for (const auto& f : rep.findings) {
        const std::string vs = hls::format_list(f.renditions);
        const char* tag = f.sev == hls::Severity::Error ? "ERROR" : f.sev == hls::Severity::Warning ? "warn " : "note ";
        std::printf("%s %s%s\n", tag, f.title.c_str(), vs.empty() ? "" : std::format("  [variants {}]", vs).c_str());
        if (!f.detail.empty() && !quiet) std::printf("      %s\n", f.detail.c_str());
    }
}

void print_markers(const ConformProbe::Result& r, const std::vector<std::string>& tags, bool quiet)
{
    const auto& rep = r.markers;
    if (!rep.any()) {
        std::string what = "EXT-X-DATERANGE";
        for (const auto& t : tags) what += ", " + t;
        if (!quiet) std::printf("\nmarkers: none (no %s)\n", what.c_str());
        return;
    }
    const auto& in = r.marker_in;
    int notes = 0;
    for (const auto& f : rep.findings) notes += f.sev == hls::Severity::Info ? 1 : 0;
    std::size_t splices = 0;
    for (const auto& ev : rep.events) splices += ev.splice ? 1 : 0;
    std::printf("\nmarkers: %zu splice points over %zu playlists, reference %s\n", splices, in.size(),
                rep.reference >= 0 ? in[static_cast<std::size_t>(rep.reference)].label.c_str() : "-");
    if (!quiet) {
        std::size_t others = 0, others_marked = 0;
        for (const auto& ev : rep.events) {
            if (!ev.splice) {
                ++others;
                others_marked = std::max(others_marked, ev.places.size());
                continue;
            }
            int bad = 0;
            for (const auto& p : ev.places) bad += p.sev == hls::Severity::Error || p.sev == hls::Severity::Warning;
            std::printf("  %-20s %12s  marked in %zu, missing in %zu%s%s\n", ev.id.c_str(),
                        std::isnan(ev.time) ? "-" : std::format("{:.3f} s", ev.time).c_str(), ev.places.size(),
                        ev.missing.size(), bad ? std::format(", {} off", bad).c_str() : "",
                        ev.cls.empty() ? "" : std::format("  [{}]", ev.cls).c_str());
        }
        if (others > 0)
            std::printf("  and %zu other EXT-X-DATERANGE%s (no interstitial CLASS or SCTE-35: not splice points, "
                        "not checked; in up to %zu playlist%s; listed in --json)\n", others, others == 1 ? "" : "s",
                        others_marked, others_marked == 1 ? "" : "s");
    }
    std::printf("%d error%s, %d warning%s, %d note%s\n", rep.errors, rep.errors == 1 ? "" : "s", rep.warnings,
                rep.warnings == 1 ? "" : "s", notes, notes == 1 ? "" : "s");
    for (const auto& f : rep.findings) {
        const char* tag = f.sev == hls::Severity::Error ? "ERROR" : f.sev == hls::Severity::Warning ? "warn " : "note ";
        std::printf("%s %s\n", tag, f.title.c_str());
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
    ConformProbe::Options opts;
    std::string url, json_path, html_path;
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
                std::fprintf(stderr, "mirage_hlscheck: --segments takes a count or \"all\"\n");
                return 2;
            }
        } else if (a == "--audio-segments") {
            if (!value(v) || !parse_count(v, opts.audio_segments)) {
                std::fprintf(stderr, "mirage_hlscheck: --audio-segments takes a count or \"all\"\n");
                return 2;
            }
        } else if (a == "--no-sizes") {
            opts.sizes = false;
        } else if (a == "--workers") {
            if (!value(v) || !parse_count(v, opts.workers) || opts.workers == 0 || opts.workers > 16) {
                std::fprintf(stderr, "mirage_hlscheck: --workers takes 1 to 16\n");
                return 2;
            }
        } else if (a == "--marker-tag") {
            if (!value(v) || v.empty() || v.starts_with("#") || v.find(':') != std::string_view::npos) {
                std::fprintf(stderr, "mirage_hlscheck: --marker-tag takes a tag name without '#', e.g. EXT-X-CUE-OUT\n");
                return 2;
            }
            opts.marker_tags.emplace_back(v);
        } else if (a == "--markers-only") {
            opts.markers_only = true;
        } else if (a == "--json") {
            if (!value(v)) {
                std::fprintf(stderr, "mirage_hlscheck: --json takes a file name\n");
                return 2;
            }
            json_path = v;
        } else if (a == "--html") {
            if (!value(v)) {
                std::fprintf(stderr, "mirage_hlscheck: --html takes a file name\n");
                return 2;
            }
            html_path = v;
        } else if (a == "--quiet") {
            quiet = true;
        } else if (a.starts_with("-") && a.size() > 1) {
            std::fprintf(stderr, "mirage_hlscheck: unknown option %s\n%s", argv[i], kUsage);
            return 2;
        } else if (url.empty()) {
            url = a;
        } else {
            std::fprintf(stderr, "mirage_hlscheck: one master playlist at a time\n");
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

    ConformProbe probe;
    probe.start(url, opts);
    const bool tty = !quiet && isatty(fileno(stderr));
    while (probe.running()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (!tty) continue;
        const auto p = probe.progress();
        std::fprintf(stderr, "\rplaylists %d/%d · segments read %d/%d · sized %d/%d · %.1f MB   ", p.playlists,
                     p.playlists_total, p.reads, p.reads_total, p.sizes, p.sizes_total,
                     static_cast<double>(p.bytes) / 1e6);
    }
    if (tty) std::fprintf(stderr, "\r%80s\r", "");
    const ConformProbe::Result r = probe.result();
    if (probe.state() == ConformProbe::State::Failed) {
        std::fprintf(stderr, "mirage_hlscheck: %s\n", r.error.c_str());
        return 2;
    }
    if (!opts.markers_only) print_report(r, quiet);
    print_markers(r, opts.marker_tags, quiet);
    if (!json_path.empty() || !html_path.empty()) {
        const std::string text = hls::with_markers(hls::conformance_json(r.master, r.playlists, r.report, r.source),
                                                   r.marker_in, r.markers);
        auto write = [](const std::string& path, const std::string& body) {
            std::ofstream out(path, std::ios::binary);
            out << body;
            if (out) return true;
            std::fprintf(stderr, "mirage_hlscheck: cannot write %s\n", path.c_str());
            return false;
        };
        if (!json_path.empty() && !write(json_path, text)) return 2;
        if (!html_path.empty()
            && !write(html_path, hls::report_html(text, hls::run_meta_json(r.seconds, opts.markers_only, opts.marker_tags))))
            return 2;
    }
    if (opts.markers_only) return r.markers.errors > 0 ? 1 : 0;
    return r.report.errors > 0 || r.markers.errors > 0 ? 1 : 0;
}
