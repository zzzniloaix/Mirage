// manifest_probe: what the repository's playlist parsers make of one HLS
// playlist, compiled from the repository's own sources (src/ManifestParser.cpp,
// src/HlsAlign.cpp, src/HlsVariants.cpp) and run on the text of a file:
//   ManifestParser::parse_hls_text()   its tags(): kind, pts, disc_seq, raw, value
//   hls::parse_media_timeline()        its segments, and its problems
//   hls::parse_media_playlist()        the EXTINF durations and the flags
//
//   manifest_probe <playlist file> <base URL the URIs resolve against>

#include "HlsAlign.h"
#include "HlsVariants.h"
#include "ManifestParser.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace {

const char* kind_name(ManifestTagKind k)
{
    switch (k) {
    case ManifestTagKind::Discontinuity:         return "Discontinuity";
    case ManifestTagKind::DiscontinuitySequence: return "DiscontinuitySequence";
    case ManifestTagKind::Map:                   return "Map";
    case ManifestTagKind::ProgramDateTime:       return "ProgramDateTime";
    case ManifestTagKind::CueOut:                return "CueOut";
    case ManifestTagKind::CueIn:                 return "CueIn";
    case ManifestTagKind::Period:                return "Period";
    case ManifestTagKind::Event:                 return "Event";
    case ManifestTagKind::Unknown:               return "Unknown";
    }
    return "?";
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::fprintf(stderr, "usage: manifest_probe <playlist file> <base URL>\n");
        return 2;
    }
    std::ifstream f(argv[1], std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string text = ss.str();
    const std::string base = argv[2];

    ManifestParser mp;
    const bool ok = mp.parse_hls_text(text, base);
    std::printf("== ManifestParser::parse_hls_text: %s, %zu tags, %zu variants ==\n", ok ? "true" : "false",
                mp.tags().size(), mp.variants().size());
    std::printf("kind\tpts\tdisc_seq\traw\tvalue\n");
    for (const ManifestTag& t : mp.tags())
        std::printf("%s\t%.6f\t%d\t%s\t%s\n", kind_name(t.kind), t.pts, t.disc_seq, t.raw.c_str(), t.value.c_str());
    for (const VariantStream& v : mp.variants())
        std::printf("variant\turl=%s\tbandwidth=%lld\t%dx%d\n", v.url.c_str(), static_cast<long long>(v.bandwidth),
                    v.width, v.height);

    const hls::MediaTimeline tl = hls::parse_media_timeline(text, base);
    std::printf("== hls::parse_media_timeline: ok=%d media_seq=%lld disc_seq=%lld discontinuities=%d total=%.6f "
                "target=%.6f endlist=%d type=%s ==\n",
                tl.ok ? 1 : 0, static_cast<long long>(tl.media_seq), static_cast<long long>(tl.disc_seq),
                tl.discontinuities, tl.total, tl.target, tl.endlist ? 1 : 0, tl.type.c_str());
    std::printf("seq\tstart\tduration\tdiscontinuity\tdisc_seq\tpdt\tpdt_explicit\tgap\tmap\tkey\tline\turi\trange\n");
    for (const hls::Segment& s : tl.segments)
        std::printf("%lld\t%.6f\t%.6f\t%d\t%lld\t%.3f\t%d\t%d\t%d\t%s\t%d\t%s\t%lld@%lld\n",
                    static_cast<long long>(s.seq), s.start, s.duration, s.discontinuity ? 1 : 0,
                    static_cast<long long>(s.disc_seq), std::isnan(s.pdt) ? -1.0 : s.pdt, s.pdt_explicit ? 1 : 0,
                    s.gap ? 1 : 0, s.map, s.key.c_str(), s.line, s.uri.c_str(),
                    static_cast<long long>(s.range.length), static_cast<long long>(s.range.offset));
    for (const std::string& p : tl.problems) std::printf("problem\t%s\n", p.c_str());

    const hls::MediaPlaylist pl = hls::parse_media_playlist(text);
    std::printf("== hls::parse_media_playlist: %zu durations, vod=%d discontinuity=%d gap=%d independent=%d ==\n",
                pl.durations.size(), pl.vod ? 1 : 0, pl.discontinuity ? 1 : 0, pl.gap ? 1 : 0,
                pl.independent ? 1 : 0);
    for (const double d : pl.durations) std::printf("%.6f\n", d);
    return 0;
}
