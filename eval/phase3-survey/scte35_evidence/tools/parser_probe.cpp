// Runs the repository's two HLS playlist parsers, unchanged, on a playlist
// file: ManifestParser::parse_hls_text (src/ManifestParser.cpp) and
// hls::parse_media_timeline (src/HlsAlign.cpp), and prints what each keeps.
// Linked with the objects of the debug build's mirage_tests target; the
// three NetworkLogger functions ManifestParser's fetch path calls are
// stubbed here (parse_hls_text never calls them).
// usage: parser_probe <playlist> [...]
#include "HlsAlign.h"
#include "ManifestParser.h"
#include "NetworkLogger.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

void NetworkLogger::track_open_begin(NetInitiator, const char*, std::int64_t, std::int64_t) {}
void NetworkLogger::track_open_end(int, AVIOContext*) {}
void NetworkLogger::track_close(AVIOContext*) {}

static const char* kind_name(ManifestTagKind k)
{
    switch (k) {
        case ManifestTagKind::Discontinuity: return "Discontinuity";
        case ManifestTagKind::DiscontinuitySequence: return "DiscontinuitySequence";
        case ManifestTagKind::Map: return "Map";
        case ManifestTagKind::ProgramDateTime: return "ProgramDateTime";
        case ManifestTagKind::CueOut: return "CueOut";
        case ManifestTagKind::CueIn: return "CueIn";
        case ManifestTagKind::Period: return "Period";
        case ManifestTagKind::Event: return "Event";
        case ManifestTagKind::Unknown: return "Unknown";
    }
    return "?";
}

int main(int argc, char** argv)
{
    for (int a = 1; a < argc; ++a) {
        std::ifstream in(argv[a]);
        std::stringstream ss;
        ss << in.rdbuf();
        const std::string text = ss.str();
        std::printf("== %s\n", argv[a]);
        ManifestParser mp;
        const bool ok = mp.parse_hls_text(text, "https://example.invalid/p/media.m3u8");
        std::printf("  ManifestParser::parse_hls_text: %s, %zu tags\n", ok ? "true" : "false", mp.tags().size());
        for (const auto& t : mp.tags()) {
            std::string raw = t.raw.size() > 110 ? t.raw.substr(0, 110) + "..." : t.raw;
            std::printf("    %-15s pts %9.3f  disc_seq %d  raw \"%s\"\n", kind_name(t.kind), t.pts, t.disc_seq, raw.c_str());
        }
        const hls::MediaTimeline tl = hls::parse_media_timeline(text, "https://example.invalid/p/media.m3u8");
        std::printf("  hls::parse_media_timeline: ok %d, %zu segments, total %.3f s, discontinuities %d, endlist %d, type \"%s\"\n",
                    tl.ok, tl.segments.size(), tl.total, tl.discontinuities, tl.endlist, tl.type.c_str());
        for (const auto& s : tl.segments) {
            std::printf("    seq %lld  start %8.3f  dur %7.3f  disc_seq %lld%s  pdt %s  line %d\n",
                        static_cast<long long>(s.seq), s.start, s.duration, static_cast<long long>(s.disc_seq),
                        s.discontinuity ? " (DISCONTINUITY)" : "",
                        std::isnan(s.pdt) ? "-" : std::to_string(s.pdt).c_str(), s.line);
        }
        for (const auto& p : tl.problems) std::printf("    problem: %s\n", p.c_str());
    }
    return 0;
}
