// Unit tests of src/TimelineMatch.h (eval/phase3-survey/design.md §9.1): written by WP12, a
// playlist opened alone among equal bytes by WP12b.
//
// eval/phase3-survey/ui.md §5.1's matching, mapping, seek decision and the
// click's interval, on the ad ladder that tests/test_timeline_build.cpp
// builds by hand (testgen's shape, eval/phase3-survey/testgen.md §2.3: three
// rungs of eight 25-frame segments at 25 fps, the reference v360 and the
// pairs v270 and v180, the ad in segments 3 and 4 restarting its timestamps
// at the main content's origin), whose builder this copies:
// - the match: the master's SHA-256, entries by their order, the top rung
//   on screen the reference; the same bytes served from elsewhere; changed
//   bytes at the same address, by URL, with a warning; a blank line between
//   EXT-X-STREAM-INF and its URI, which VMAFLadder reads and ManifestParser
//   drops, refusing the order, the URLs matching there and from another
//   host; a URI the two parsers resolve apart; a mezzanine reference and a
//   variant's playlist opened alone; another stream's master. The report's
//   entries are VMAFLadder's own reading of the master
//   (vmafladder::parse_master), the player's ManifestParser's;
// - a variant's playlist opened alone from another address among variants
//   whose playlists have its bytes (D-121 (d), Q89 (b)): each of D-069's
//   three rungs, which share one playlist text, by the path its URL ends
//   with, from another host, a local copy, a URL with a query, the final URL
//   after a redirect; a variant's playlist location; the longest run of path
//   components, the first of two as long; the first of them all, with
//   WP12's warning, where none shares more than its file name, a playlist
//   opened by a path relative to its own directory among them;
// - the playhead: the player's PTS to the frame of the sequence played,
//   across the ad's restart of its timestamps, after a seek, at a forward
//   jump of more than 10 s, and on a coarser grid;
// - the seek decision (D-114 (b), Q37 (a)): the main content by its PTS, a
//   segment's last frame by its own segment's start, the first DTS's lead
//   being at least a frame, and the frame before it too for a lead of two
//   frames, given as the decoder's reorder delay or in seconds (the
//   first-DTS tolerance), an ad's frame by the segment before its
//   discontinuity, content after the ad by its PTS; the probe's shapes of
//   tests/test_hls_seek_behaviour.cpp, its content after a reset ad among
//   them; a file sought by its PTS; a refused map;
// - a click (D-114 (d), Q39 (b)): inside a detected drop, the document's
//   attribution; elsewhere the clicked frame's segment of the curve's
//   variant, a session's span's variant's, and the frame alone without a
//   segment map.

#include <catch2/catch_test_macros.hpp>

#include "FileHash.h"
#include "LadderReportRead.h"
#include "ManifestParser.h"
#include "PlaylistEvents.h"
#include "SessionPlan.h"
#include "TimelineBuild.h"
#include "TimelineLoad.h"
#include "TimelineMatch.h"
#include "TimelineModel.h"
#include "TimelineQuery.h"
#include "VMAFLadder.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using timeline::CurveRef;
using timeline::SeekDecision;
using timeline::StreamMatch;

namespace {

bool contains(std::string_view s, std::string_view part) { return s.find(part) != std::string_view::npos; }

// ── testgen's ad ladder, as tests/test_timeline_build.cpp builds it ──────────

constexpr AVRational   kTs      = { 1, 90000 };   // MPEG-TS's time base
constexpr std::int64_t kOrigin  = 900000;         // 10 s: where testgen's main content starts
constexpr std::int64_t kFrame25 = 3600;           // a frame at 25 fps, in 1/90000
constexpr std::int64_t kFrames  = 200;            // eight segments of 25 frames
constexpr const char*  kRungs[] = { "v360", "v270", "v180" };

const std::string kAdPlaylist =
    "#EXTM3U\n"
    "#EXT-X-VERSION:3\n"
    "#EXT-X-TARGETDURATION:1\n"
    "#EXT-X-MEDIA-SEQUENCE:0\n"
    "#EXT-X-DISCONTINUITY-SEQUENCE:0\n"
    "#EXT-X-PLAYLIST-TYPE:VOD\n"
    "#EXTINF:1.000000,\nmain_000.ts\n"
    "#EXTINF:1.000000,\nmain_001.ts\n"
    "#EXTINF:1.000000,\nmain_002.ts\n"
    "#EXT-X-DISCONTINUITY\n"
    "#EXT-X-CUE-OUT:2.000\n"
    "#EXTINF:1.000000,\nad_000.ts\n"
    "#EXT-X-CUE-OUT-CONT:1.000/2.000\n"
    "#EXTINF:1.000000,\nad_001.ts\n"
    "#EXT-X-DISCONTINUITY\n"
    "#EXT-X-CUE-IN\n"
    "#EXTINF:1.000000,\nmain_005.ts\n"
    "#EXTINF:1.000000,\nmain_006.ts\n"
    "#EXTINF:1.000000,\nmain_007.ts\n"
    "#EXT-X-ENDLIST\n";

const std::string kMasterUrl = "http://test.invalid/ladder/master.m3u8";

// The ladder's master, its entries in ad_report()'s order.
const std::string kMaster =
    "#EXTM3U\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=1000000,RESOLUTION=640x360,CODECS=\"avc1.64001e\"\n"
    "v360/index.m3u8\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=600000,RESOLUTION=480x270,CODECS=\"avc1.64001e\"\n"
    "v270/index.m3u8\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=350000,RESOLUTION=320x180,CODECS=\"avc1.64001e\"\n"
    "v180/index.m3u8\n";

std::string url_of(int variant)
{
    return std::format("http://test.invalid/ladder/{}/index.m3u8", kRungs[variant]);
}

// The reference's PTS: the main content from 10 s, the ad's 50 frames from
// 10 s again, the main content back at 15 s, where it would be without the ad.
std::int64_t ref_pts(std::int64_t i)
{
    return i < 75 || i >= 125 ? kOrigin + i * kFrame25 : kOrigin + (i - 75) * kFrame25;
}

double score(int pair, std::int64_t i)
{
    const double wobble = 0.25 * static_cast<double>(i % 4);
    if (pair == 0) return i == 75 ? 80.0 : 97.0 + wobble;
    return i >= 75 && i < 125 ? 25.0 + wobble : 91.0 + wobble;
}

timeline::LadderScores ad_report()
{
    timeline::LadderScores s;
    s.status                  = "complete";
    s.reference_mode          = "top-rung";
    s.model                   = "vmaf_v0.6.1";
    s.model_source            = "built-in";
    s.scaler                  = "bicubic";
    s.libvmaf_version         = "1.2.3";
    s.libvmaf_package_version = "1.2.4";
    s.master_url              = kMasterUrl;
    s.master_location         = kMasterUrl;
    s.master_sha256           = filehash::sha256_hex(kMaster);
    const std::int64_t bandwidth[]  = { 1000000, 600000, 350000 };
    const char* const  resolution[] = { "640x360", "480x270", "320x180" };
    for (int k = 0; k < 3; ++k) {
        timeline::MasterEntry e;
        e.index      = static_cast<std::size_t>(k);
        e.line       = static_cast<std::size_t>(2 + 2 * k);
        e.uri        = std::format("{}/index.m3u8", kRungs[k]);
        e.url        = url_of(k);
        e.bandwidth  = bandwidth[k];
        e.resolution = resolution[k];
        e.codecs     = "avc1.64001e";
        e.video      = true;
        s.entries.push_back(e);
    }
    s.reference_url     = url_of(0);
    s.reference_entries = { 0 };
    for (int pair = 0; pair < 2; ++pair) {
        timeline::PairScores p;
        p.index         = static_cast<std::size_t>(pair);
        p.entries       = { static_cast<std::size_t>(pair + 1) };
        p.ref_url       = url_of(0);
        p.dis_url       = url_of(pair + 1);
        p.ref_time_base = kTs;
        p.dis_time_base = kTs;
        p.ref_rate      = { 25, 1 };
        p.dis_rate      = { 25, 1 };
        for (std::int64_t i = 0; i < kFrames; ++i) {
            p.ref_index.push_back(i);
            p.dis_index.push_back(i);
            p.ref_pts.push_back(ref_pts(i));
            p.dis_pts.push_back(ref_pts(i));
            p.vmaf.push_back(score(pair, i));
        }
        p.context_first = { 0, 75 };
        p.ref_sequences = { { .first_index = 0, .first_pts = kOrigin, .frames = 75 },
                            { .first_index = 75, .first_pts = kOrigin, .frames = 125 } };
        p.dis_sequences = p.ref_sequences;
        s.pairs.push_back(std::move(p));
    }
    s.n_threads   = 4;
    s.n_subsample = 1;
    return s;
}

timeline::VariantMedia ad_media()
{
    timeline::VariantMedia m;
    for (std::int64_t k = 0; k < 8; ++k) {
        m.segments.push_back({ .seq = k, .first_index = 25 * k, .frames = 25, .bytes = 12000 + k });
        m.keyframes.push_back(25 * k);
    }
    for (std::int64_t i = 0; i < kFrames; ++i) m.pts.push_back(ref_pts(i));
    return m;
}

timeline::BuildInputs ad_inputs()
{
    timeline::BuildInputs in;
    in.scores = ad_report();
    for (int v = 0; v < 3; ++v) {
        timeline::VariantRead r;
        r.playlist = timeline::parse_playlist_events(kAdPlaylist, url_of(v), v);
        REQUIRE(r.playlist->error.empty());
        r.playlist_location = url_of(v);
        r.playlist_sha256   = filehash::sha256_hex(kAdPlaylist);
        r.media             = ad_media();
        in.variants.push_back(std::move(r));
    }
    return in;
}

timeline::Timeline built(const timeline::BuildInputs& in)
{
    std::string err;
    timeline::Timeline t = timeline::build(in, err);
    INFO(err);
    REQUIRE(err.empty());
    return t;
}

// ── The master on either side ────────────────────────────────────────────────

// `scores` with the master `text` read from `location` as VMAFLadder reads
// it, its entries as the ladder report lists them.
timeline::LadderScores with_master(timeline::LadderScores s, const std::string& text, const std::string& location)
{
    const vmafladder::Master m = vmafladder::parse_master(text, location);
    INFO(m.error);
    REQUIRE(m.error.empty());
    s.entries.clear();
    for (const vmafladder::Entry& e : m.entries)
        s.entries.push_back({ .index = e.index, .line = e.line, .uri = e.uri, .url = e.url,
                              .bandwidth = e.bandwidth, .average_bandwidth = e.average_bandwidth,
                              .resolution = e.resolution, .codecs = e.codecs, .frame_rate = e.frame_rate,
                              .video = e.video });
    s.master_url      = location;
    s.master_location = location;
    s.master_sha256   = filehash::sha256_hex(text);
    return s;
}

// The player's side: `path` opened, its ManifestParser having read `text`
// from `final_url`.
timeline::OpenStream opened(const std::string& path, const std::string& text, const std::string& final_url)
{
    ManifestParser p;
    REQUIRE(p.parse_hls_text(text, final_url));
    return { .path = path, .final_url = final_url, .text = text, .variants = p.variants() };
}

bool any_warning(const StreamMatch& m, std::string_view part)
{
    return std::any_of(m.warnings.begin(), m.warnings.end(),
                       [&](const std::string& w) { return contains(w, part); });
}

std::string warnings_of(const StreamMatch& m)
{
    std::string out;
    for (const std::string& w : m.warnings) out += w + "\n";
    return out;
}

// ── Clocks by hand ───────────────────────────────────────────────────────────

// test_hls_seek_behaviour.cpp's shapes: three 2 s segments of 50 frames at
// 25 fps, from playlist time 0, 2 and 4 s, their PTS from `starts`.
timeline::VariantClock probe_clock(const double (&starts)[3])
{
    timeline::VariantClock c;
    c.variant    = 0;
    c.playlist   = true;
    c.time_base  = kTs;
    c.frame_rate = { 25, 1 };
    for (int s = 0; s < 3; ++s) {
        for (int i = 0; i < 50; ++i)
            c.frames.push_back({ .index = s * 50 + i, .ref_index = s * 50 + i,
                                 .pts = std::llround(starts[s] * 90000) + i * kFrame25, .sequence = s });
        c.segments.push_back({ .media_sequence = s, .discontinuity_sequence = s, .playlist_time = 2.0 * s,
                               .extinf = 2.0, .ref_index = s * 50, .ref_end = (s + 1) * 50,
                               .first = static_cast<std::size_t>(s * 50),
                               .last = static_cast<std::size_t>((s + 1) * 50) });
    }
    return c;
}

bool near(double a, double b) { return std::abs(a - b) < 1e-9; }

} // namespace

// ── The match (ui.md §2.3) ───────────────────────────────────────────────────

TEST_CASE("timeline::match_stream: the master's SHA-256, its entries by their order, the top rung on "
          "screen the reference; the same bytes served from elsewhere",
          "[timeline][timeline_match]")
{
    const timeline::Timeline     t      = built(ad_inputs());
    const timeline::LadderScores scores = with_master(ad_report(), kMaster, kMasterUrl);
    StreamMatch m = timeline::match_stream(t, scores, opened(kMasterUrl, kMaster, kMasterUrl));
    INFO(warnings_of(m));
    CHECK(m.by == StreamMatch::By::MasterBytes);
    CHECK(m.entries == StreamMatch::Entries::Index);
    CHECK(m.variant_of == std::vector<int>{ 0, 1, 2 });
    CHECK(m.warnings.empty());
    CHECK(timeline::match_name(m.by) == "master SHA-256");
    // The top rung on screen is the reference, the variant of no pair, which
    // the panel marks as not scored in top-rung mode (ui.md §2.3).
    CHECK(t.variants[static_cast<std::size_t>(m.variant_of[0])].pair == -1);
    CHECK(t.variants[static_cast<std::size_t>(m.variant_of[2])].pair == 1);

    // The same bytes from another host: the URIs resolve to the player's
    // URLs as its own parser resolves them, so the order still holds.
    const std::string elsewhere = "http://cdn.invalid/copy/master.m3u8";
    m = timeline::match_stream(t, scores, opened(elsewhere, kMaster, elsewhere));
    CHECK(m.by == StreamMatch::By::MasterBytes);
    CHECK(m.entries == StreamMatch::Entries::Index);
    CHECK(m.variant_of == std::vector<int>{ 0, 1, 2 });
}

TEST_CASE("timeline::match_stream: changed bytes at the same address, by URL with a warning; a "
          "redirect elsewhere matches no URL",
          "[timeline][timeline_match]")
{
    const timeline::Timeline     t      = built(ad_inputs());
    const timeline::LadderScores scores = with_master(ad_report(), kMaster, kMasterUrl);
    const std::string changed = "#EXTM3U\n#EXT-X-INDEPENDENT-SEGMENTS\n" + kMaster.substr(8);
    StreamMatch m = timeline::match_stream(t, scores, opened(kMasterUrl, changed, kMasterUrl));
    INFO(warnings_of(m));
    CHECK(m.by == StreamMatch::By::Address);
    CHECK(m.entries == StreamMatch::Entries::Url);
    CHECK(m.variant_of == std::vector<int>{ 0, 1, 2 });
    CHECK(any_warning(m, "has changed since it was scored"));
    CHECK(any_warning(m, filehash::sha256_hex(changed)));

    // ladder.url is what the player opened, but the master came from another
    // host after a redirect: its URLs are that host's, and none matches.
    const std::string other = "http://other.invalid/ladder/master.m3u8";
    m = timeline::match_stream(t, scores, opened(kMasterUrl, changed, other));
    CHECK(m.by == StreamMatch::By::Address);
    CHECK(m.variant_of == std::vector<int>{ -1, -1, -1 });
    CHECK(any_warning(m, "no entry of the report has the URL of the player's entry 0"));
}

TEST_CASE("timeline::match_stream: a blank line between EXT-X-STREAM-INF and its URI, which "
          "VMAFLadder reads and ManifestParser drops, refuses the order; the URLs match, the same "
          "bytes from another host too",
          "[timeline][timeline_match]")
{
    const timeline::Timeline t = built(ad_inputs());
    const std::string blank =
        "#EXTM3U\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=1000000,RESOLUTION=640x360,CODECS=\"avc1.64001e\"\n"
        "v360/index.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=600000,RESOLUTION=480x270,CODECS=\"avc1.64001e\"\n"
        "\n"
        "v270/index.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=350000,RESOLUTION=320x180,CODECS=\"avc1.64001e\"\n"
        "v180/index.m3u8\n";
    const timeline::LadderScores scores = with_master(ad_report(), blank, kMasterUrl);
    REQUIRE(scores.entries.size() == 3);
    const timeline::OpenStream open = opened(kMasterUrl, blank, kMasterUrl);
    REQUIRE(open.variants.size() == 2);   // ManifestParser's: v360 and v180
    const StreamMatch m = timeline::match_stream(t, scores, open);
    INFO(warnings_of(m));
    CHECK(m.by == StreamMatch::By::MasterBytes);
    CHECK(m.entries == StreamMatch::Entries::Url);
    CHECK(m.variant_of == std::vector<int>{ 0, 2 });
    CHECK(any_warning(m, "don't correspond in order"));
    CHECK(any_warning(m, "the player lists 2 entries and the report 3"));
    CHECK_FALSE(any_warning(m, "no entry of the report"));

    // The same bytes from another host: the report's URIs resolved by the
    // player's parser against where the player read the master match its
    // entries, as the order check resolves them.
    const std::string elsewhere = "http://cdn.invalid/copy/master.m3u8";
    const StreamMatch e = timeline::match_stream(t, scores, opened(elsewhere, blank, elsewhere));
    INFO(warnings_of(e));
    CHECK(e.by == StreamMatch::By::MasterBytes);
    CHECK(e.entries == StreamMatch::Entries::Url);
    CHECK(e.variant_of == std::vector<int>{ 0, 2 });
    CHECK(any_warning(e, "don't correspond in order"));
    CHECK_FALSE(any_warning(e, "no entry of the report"));
}

TEST_CASE("timeline::match_stream: a URI the two parsers resolve apart: by its order when the bytes "
          "are the same, unmatched by URL when they aren't",
          "[timeline][timeline_match]")
{
    // A local master whose third URI has a scheme of its own: VMAFLadder
    // keeps it as it is, ManifestParser puts the master's directory before it.
    const timeline::Timeline t = built(ad_inputs());
    const std::string local = "/data/ladder/master.m3u8";
    const std::string text =
        "#EXTM3U\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=1000000,RESOLUTION=640x360\n"
        "v360/index.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=600000,RESOLUTION=480x270\n"
        "v270/index.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=350000,RESOLUTION=320x180\n"
        "rtmp://media.invalid/v180/index.m3u8\n";
    const timeline::LadderScores scores = with_master(ad_report(), text, local);
    REQUIRE(scores.entries[2].url == "rtmp://media.invalid/v180/index.m3u8");
    const timeline::OpenStream open = opened(local, text, local);
    REQUIRE(open.variants[2].url == "/data/ladder/rtmp://media.invalid/v180/index.m3u8");

    StreamMatch m = timeline::match_stream(t, scores, open);
    CHECK(m.by == StreamMatch::By::MasterBytes);
    CHECK(m.entries == StreamMatch::Entries::Index);
    CHECK(m.variant_of == std::vector<int>{ 0, 1, 2 });

    const std::string changed = text + "#EXT-X-INDEPENDENT-SEGMENTS\n";
    m = timeline::match_stream(t, scores, opened(local, changed, local));
    INFO(warnings_of(m));
    CHECK(m.by == StreamMatch::By::Address);
    CHECK(m.variant_of == std::vector<int>{ 0, 1, -1 });
    CHECK(any_warning(m, "entry 2 (/data/ladder/rtmp://media.invalid/v180/index.m3u8)"));

    // The bytes scored, the order refused by a blank line that ManifestParser
    // drops v270 at: by URL, the report's URIs resolved by the player's
    // parser, so that the URI the two parsers resolve apart matches.
    const std::string blank =
        "#EXTM3U\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=1000000,RESOLUTION=640x360\n"
        "v360/index.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=600000,RESOLUTION=480x270\n"
        "\n"
        "v270/index.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=350000,RESOLUTION=320x180\n"
        "rtmp://media.invalid/v180/index.m3u8\n";
    const timeline::LadderScores blank_scores = with_master(ad_report(), blank, local);
    const timeline::OpenStream   blank_open   = opened(local, blank, local);
    REQUIRE(blank_open.variants.size() == 2);
    m = timeline::match_stream(t, blank_scores, blank_open);
    INFO(warnings_of(m));
    CHECK(m.by == StreamMatch::By::MasterBytes);
    CHECK(m.entries == StreamMatch::Entries::Url);
    CHECK(m.variant_of == std::vector<int>{ 0, 2 });
    CHECK_FALSE(any_warning(m, "no entry of the report"));
}

TEST_CASE("timeline::match_stream: a mezzanine reference, a variant's playlist opened alone, and "
          "another stream's master",
          "[timeline][timeline_match]")
{
    // Mezzanine mode: every rung a pair's, the reference a file of its own.
    timeline::Timeline t;
    t.variants.push_back({ .index = 0, .pair = -1, .url = "/data/mezzanine.mov" });
    for (int k = 0; k < 3; ++k)
        t.variants.push_back({ .index = k + 1, .pair = k, .entries = { static_cast<std::size_t>(k) },
                               .url = url_of(k), .playlist_location = url_of(k),
                               .playlist_sha256 = filehash::sha256_hex(kAdPlaylist) });
    const timeline::LadderScores scores = with_master(ad_report(), kMaster, kMasterUrl);
    StreamMatch m = timeline::match_stream(t, scores, opened(kMasterUrl, kMaster, kMasterUrl));
    CHECK(m.by == StreamMatch::By::MasterBytes);
    CHECK(m.variant_of == std::vector<int>{ 1, 2, 3 });

    // The mezzanine opened alone, by its path, lexically normalized.
    m = timeline::match_stream(t, scores, { .path = "/data/./mezzanine.mov" });
    CHECK(m.by == StreamMatch::By::Variant);
    CHECK(m.opened_variant == 0);
    CHECK(m.variant_of.empty());

    // A rung's media playlist by its address; elsewhere by its bytes, which
    // all three rungs share, the first taken with a warning.
    m = timeline::match_stream(t, scores, opened(url_of(2), kAdPlaylist, url_of(2)));
    CHECK(m.by == StreamMatch::By::Variant);
    CHECK(m.opened_variant == 3);
    CHECK(m.warnings.empty());
    const std::string copy = "http://cdn.invalid/v.m3u8";
    m = timeline::match_stream(t, scores, opened(copy, kAdPlaylist, copy));
    CHECK(m.by == StreamMatch::By::Variant);
    CHECK(m.opened_variant == 1);
    CHECK(any_warning(m, "the open playlist's bytes are those of 3 of the document's variants"));

    // Another stream.
    const std::string other = "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\nx.m3u8\n";
    m = timeline::match_stream(t, scores, opened("http://x.invalid/m.m3u8", other, "http://x.invalid/m.m3u8"));
    CHECK(m.by == StreamMatch::By::None);
    CHECK(m.entries == StreamMatch::Entries::None);
    CHECK(m.variant_of == std::vector<int>{ -1 });
    CHECK(any_warning(m, kMasterUrl));
    CHECK(timeline::match_name(m.by) == "none");
}

TEST_CASE("timeline::match_stream: a rung's playlist opened alone from another address, among rungs of one "
          "playlist text, by the path its URL ends with; under another name, the first (D-121 (d))",
          "[timeline][timeline_match]")
{
    // D-069's shape: the three rungs' playlists have one text, so that their
    // bytes can't tell them apart. The warning where the path picks the rung
    // is a reading pending the owner (src/TimelineMatch.h).
    const timeline::Timeline     t      = built(ad_inputs());
    const timeline::LadderScores scores = with_master(ad_report(), kMaster, kMasterUrl);
    REQUIRE(t.variants.size() == 3);
    for (int k = 0; k < 3; ++k) {
        INFO("rung " << kRungs[k]);
        const timeline::Variant& v = t.variants[static_cast<std::size_t>(k)];
        REQUIRE(v.url == url_of(k));
        REQUIRE(v.playlist_sha256 == filehash::sha256_hex(kAdPlaylist));
        const std::string warning =
            std::format("the open playlist's bytes are those of 3 of the document's variants: variant {}, "
                        "whose URL ends with the open playlist's {}/index.m3u8, is taken", k, kRungs[k]);

        // From another host and directory.
        const std::string copy = std::format("http://cdn.invalid/copy/{}/index.m3u8", kRungs[k]);
        StreamMatch m = timeline::match_stream(t, scores, opened(copy, kAdPlaylist, copy));
        INFO(warnings_of(m));
        CHECK(m.by == StreamMatch::By::Variant);
        CHECK(m.entries == StreamMatch::Entries::None);
        CHECK(m.opened_variant == k);
        CHECK(m.warnings == std::vector<std::string>{ warning });

        // A local copy, by its path; a URL's query and fragment aren't its
        // path's (RFC 3986 §3).
        const std::string local = std::format("/data/copy/{}/./index.m3u8", kRungs[k]);
        m = timeline::match_stream(t, scores, opened(local, kAdPlaylist, local));
        CHECK(m.opened_variant == k);
        const std::string signed_url = copy + "?token=a/b#t=1";
        m = timeline::match_stream(t, scores, opened(signed_url, kAdPlaylist, signed_url));
        CHECK(m.opened_variant == k);
        CHECK(m.warnings == std::vector<std::string>{ warning });

        // The path the player was given names no rung, but its final URL,
        // after a redirect, does; and the reverse.
        m = timeline::match_stream(t, scores, opened("http://short.invalid/p?id=7", kAdPlaylist, copy));
        CHECK(m.opened_variant == k);
        m = timeline::match_stream(t, scores, opened(copy, kAdPlaylist, "http://store.invalid/obj/7"));
        CHECK(m.opened_variant == k);
    }

    // Under another name, sharing only the file name, or a rung's directory
    // without its file name: none shares more than its file name, so the
    // first of them all, the reference, with the warning as WP12 built it.
    // The last two are what a player started in a rung's own directory is
    // given for its playlist, compared as given, the current directory not
    // taken into account: a reading pending the owner (src/TimelineMatch.h).
    for (const std::string other : { "http://cdn.invalid/v.m3u8", "http://cdn.invalid/copy/index.m3u8",
                                     "http://cdn.invalid/v270/other.m3u8", "index.m3u8", "./index.m3u8" }) {
        INFO(other);
        const StreamMatch m = timeline::match_stream(t, scores, opened(other, kAdPlaylist, other));
        CHECK(m.by == StreamMatch::By::Variant);
        CHECK(m.opened_variant == 0);
        CHECK(m.warnings == std::vector<std::string>{ "the open playlist's bytes are those of 3 of the document's "
                                                      "variants: the first, variant 0, is taken" });
    }

    // Bytes that no variant's playlist has: another stream's, whatever the path.
    const std::string edited = kAdPlaylist + "#EXT-X-INDEPENDENT-SEGMENTS\n";
    const std::string copy   = "http://cdn.invalid/copy/v270/index.m3u8";
    const StreamMatch none   = timeline::match_stream(t, scores, opened(copy, edited, copy));
    CHECK(none.by == StreamMatch::By::None);
    CHECK(none.opened_variant == -1);
}

TEST_CASE("timeline::match_stream: among equal bytes, a variant's playlist location, the longest run of "
          "path components, and the first of the longest (D-121 (d))",
          "[timeline][timeline_match]")
{
    // Four variants of one playlist text: the first two named in the report
    // by a redirector's URLs and read by the build from their rungs'
    // directories, the last two copies of v180 at two addresses.
    const std::string sha256 = filehash::sha256_hex(kAdPlaylist);
    timeline::Timeline t;
    t.variants.push_back({ .index = 0, .pair = -1, .url = "http://redirect.invalid/r?rung=0",
                           .playlist_location = "http://origin.invalid/ladder/v360/index.m3u8",
                           .playlist_sha256 = sha256 });
    t.variants.push_back({ .index = 1, .pair = 0, .url = "http://redirect.invalid/r?rung=1",
                           .playlist_location = "http://origin.invalid/ladder/v270/index.m3u8",
                           .playlist_sha256 = sha256 });
    t.variants.push_back({ .index = 2, .pair = 1, .url = "http://a.invalid/x/v180/index.m3u8",
                           .playlist_sha256 = sha256 });
    t.variants.push_back({ .index = 3, .pair = 2, .url = "http://b.invalid/ladder/v180/index.m3u8",
                           .playlist_sha256 = sha256 });
    const timeline::LadderScores scores = with_master(ad_report(), kMaster, kMasterUrl);
    auto match_at = [&](const std::string& path) {
        return timeline::match_stream(t, scores, opened(path, kAdPlaylist, path));
    };

    // By the playlist location, the URL ending otherwise.
    StreamMatch m = match_at("http://cdn.invalid/copy/v270/index.m3u8");
    CHECK(m.opened_variant == 1);
    CHECK(m.warnings == std::vector<std::string>{ "the open playlist's bytes are those of 4 of the document's "
                                                  "variants: variant 1, whose playlist location ends with the "
                                                  "open playlist's v270/index.m3u8, is taken" });
    // The longest run: three components, though variant 2 shares two first.
    m = match_at("http://cdn.invalid/ladder/v180/index.m3u8");
    CHECK(m.opened_variant == 3);
    CHECK(contains(warnings_of(m), "variant 3, whose URL ends with the open playlist's ladder/v180/index.m3u8"));
    // Two of the longest: the first of them.
    m = match_at("http://cdn.invalid/copy/v180/index.m3u8");
    CHECK(m.opened_variant == 2);
    CHECK(contains(warnings_of(m), "variant 2, whose URL ends with the open playlist's v180/index.m3u8"));
    // Nothing more than the file name: the first of all.
    m = match_at("http://cdn.invalid/copy/v540/index.m3u8");
    CHECK(m.opened_variant == 0);
    CHECK(contains(warnings_of(m), "the first, variant 0, is taken"));

    // One variant of those bytes alone: it, with no warning, whatever its path.
    t.variants.resize(1);
    m = match_at("http://cdn.invalid/copy/v540/index.m3u8");
    CHECK(m.by == StreamMatch::By::Variant);
    CHECK(m.opened_variant == 0);
    CHECK(m.warnings.empty());
}

// ── The playhead (ui.md §2.4) ────────────────────────────────────────────────

TEST_CASE("timeline::track_playhead: the player's PTS to the frame of the sequence played, across the "
          "ad's restart of its timestamps and after a seek",
          "[timeline][timeline_match]")
{
    const timeline::BuildInputs in = ad_inputs();
    const timeline::Timeline    t  = built(in);
    for (const int variant : { 0, 2 }) {   // the reference, and v180's distorted input
        INFO("variant " << variant);
        const timeline::VariantClock c = timeline::variant_clock(t, in.scores, variant);
        REQUIRE(c.frames.size() == 200);
        timeline::PlayheadState st;
        auto at = [&](double pts) {
            const std::optional<timeline::PlayheadFrame> f = timeline::track_playhead(c, st, pts);
            REQUIRE(f);
            return *f;
        };
        CHECK(at(10.0).ref_index == 0);     // not known yet: the first sequence within half a frame
        CHECK(at(10.04).ref_index == 1);
        CHECK(at(12.96).ref_index == 74);
        CHECK(at(10.0).ref_index == 75);    // the ad's step back: the next sequence
        CHECK(st.sequence == 1);
        CHECK(at(11.0).ref_index == 100);
        CHECK(at(15.0).ref_index == 125);   // 3.04 s forward: the same sequence
        const timeline::PlayheadFrame mid = at(15.02);   // halfway: the earlier
        CHECK(mid.ref_index == 125);
        CHECK(near(mid.distance, -0.02));
        // A PTS the ad and the main content share: after a seek to sequence
        // 0 the main content's frame, and without one the ad's.
        timeline::playhead_seeked(st, 0);
        CHECK(at(10.48).ref_index == 12);
        timeline::PlayheadState other{ .sequence = 1, .last_pts = 1350000 };   // at 15 s
        const std::optional<timeline::PlayheadFrame> ad = timeline::track_playhead(c, other, 10.48);
        REQUIRE(ad);
        CHECK(ad->ref_index == 87);          // a step back with no sequence after it: it stays
        timeline::PlayheadState fresh;
        CHECK(timeline::track_playhead(c, fresh, 10.48)->ref_index == 12);
    }
}

TEST_CASE("timeline::track_playhead: a forward jump of more than 10 s, and a coarser grid",
          "[timeline][timeline_match]")
{
    // Two sequences, the second 30 s on.
    timeline::VariantClock c;
    c.time_base  = kTs;
    c.frame_rate = { 25, 1 };
    for (int i = 0; i < 100; ++i)
        c.frames.push_back({ .index = i, .ref_index = i,
                             .pts = (i < 50 ? 0 : 30 * 90000) + (i % 50) * kFrame25, .sequence = i < 50 ? 0 : 1 });
    timeline::PlayheadState st;
    CHECK(timeline::track_playhead(c, st, 1.96)->ref_index == 49);
    CHECK(timeline::track_playhead(c, st, 30.0)->ref_index == 50);
    CHECK(st.sequence == 1);

    // A distorted input at 12.5 fps against the 25 fps reference: a point at
    // every other reference frame (src/VMAFAlign.h's coarser grid).
    timeline::LadderScores scores;
    timeline::PairScores p;
    p.index         = 0;
    p.dis_time_base = kTs;
    p.dis_rate      = { 25, 2 };
    for (std::int64_t j = 0; j < 50; ++j) {
        p.dis_index.push_back(j);
        p.ref_index.push_back(2 * j);
        p.dis_pts.push_back(kOrigin + j * 2 * kFrame25);
        p.ref_pts.push_back(kOrigin + j * 2 * kFrame25);
        p.vmaf.push_back(90.0);
    }
    p.dis_sequences = { { .first_index = 0, .first_pts = kOrigin, .frames = 50 } };
    scores.pairs.push_back(p);
    timeline::Timeline t;
    t.variants.push_back({ .index = 0, .pair = -1 });
    t.variants.push_back({ .index = 1, .pair = 0 });
    const timeline::VariantClock coarse = timeline::variant_clock(t, scores, 1);
    REQUIRE(coarse.frames.size() == 50);
    timeline::PlayheadState cs;
    const std::optional<timeline::PlayheadFrame> f = timeline::track_playhead(coarse, cs, 10.0 + 7 * 0.08);
    REQUIRE(f);
    CHECK(f->index == 7);
    CHECK(f->ref_index == 14);
    CHECK(timeline::track_playhead(coarse, cs, 10.0 + 7 * 0.08 + 0.04)->ref_index == 14);   // halfway
    CHECK(timeline::track_playhead(coarse, cs, 10.0 + 7 * 0.08 + 0.05)->ref_index == 16);
    // No frames, or no time base: no playhead.
    timeline::PlayheadState none;
    CHECK_FALSE(timeline::track_playhead(timeline::VariantClock{}, none, 10.0));
}

// ── The seek decision (ui.md §2.4; D-114 (b)) ────────────────────────────────

TEST_CASE("timeline::decide_seek on D-069's ad ladder: the main content by its PTS, a segment's last "
          "frame from its own segment's start, an ad's frame from the segment before its "
          "discontinuity, content after the ad by its PTS",
          "[timeline][timeline_match]")
{
    const timeline::BuildInputs in = ad_inputs();
    const timeline::Timeline    t  = built(in);
    const timeline::SeekClock seek{ .start = 10.0, .duration = 8.0, .dts_lead = 0.04 };
    for (const int variant : { 0, 2 }) {
        INFO("variant " << variant);
        const timeline::VariantClock c = timeline::variant_clock(t, in.scores, variant);
        REQUIRE(c.segments.size() == 8);
        CHECK(c.segments[3].discontinuity_sequence == 1);
        CHECK(near(c.segments[3].playlist_time, 3.0));
        CHECK(near(c.segments[3].extinf, 1.0));
        CHECK(c.segments[3].first == 75);
        CHECK(c.segments[3].last == 100);

        SeekDecision d = timeline::decide_seek(c, seek, 10);
        CHECK(d.kind == SeekDecision::Kind::Seek);
        CHECK(near(d.target, 10.4));
        CHECK(d.frame == std::optional<std::size_t>{ 10 });

        // Segment 0's last frame: with the first packet's DTS a frame before
        // the start the demuxer looks it up in segment 1, so its own
        // segment's start is offered; with no lead it would be shown. The
        // decision allows a frame's lead whatever the clock says (ui.md
        // §2.4), and more where it says more, as a reorder delay of frames
        // or as seconds: two frames' lead carry the frame before it into
        // segment 1 too.
        CHECK(timeline::simulate_seek(c, seek, 10.96, 0.0).frame == 24);
        CHECK(timeline::simulate_seek(c, seek, 10.96, 0.04).frame == 25);
        d = timeline::decide_seek(c, seek, 24);
        CHECK(d.kind == SeekDecision::Kind::Offer);
        CHECK(near(d.target, 10.0));
        CHECK(d.segment == std::optional<std::size_t>{ 0 });
        CHECK(contains(d.why, "would show the frame of PTS 11.000 s in segment 1 instead, with the "
                              "first packet's DTS 0.040 s before the start"));
        timeline::SeekClock no_lead = seek;
        no_lead.dts_lead = 0.0;
        CHECK(timeline::decide_seek(c, no_lead, 24).kind == SeekDecision::Kind::Offer);
        CHECK(timeline::decide_seek(c, no_lead, 23).kind == SeekDecision::Kind::Seek);
        timeline::SeekClock two = seek;
        two.dts_lead = 0.08;
        CHECK(timeline::decide_seek(c, two, 23).kind == SeekDecision::Kind::Offer);
        CHECK(timeline::decide_seek(c, two, 22).kind == SeekDecision::Kind::Seek);
        // The same two frames as the decoder's reorder delay, which the panel
        // takes from the Demuxer, with no lead in seconds: libx264's
        // (tests/test_hls_seek_behaviour.cpp). Each segment's last two
        // frames are offered their own segment's start.
        const timeline::SeekClock delayed{ .start = 10.0, .duration = 8.0, .video_delay = 2 };
        d = timeline::decide_seek(c, delayed, 23);
        CHECK(d.kind == SeekDecision::Kind::Offer);
        CHECK(d.segment == std::optional<std::size_t>{ 0 });
        CHECK(near(d.target, 10.0));
        CHECK(contains(d.why, "would show the frame of PTS 11.000 s in segment 1 instead, with the "
                              "first packet's DTS 0.080 s before the start"));
        CHECK(timeline::decide_seek(c, delayed, 22).kind == SeekDecision::Kind::Seek);
        d = timeline::decide_seek(c, delayed, 173);   // segment 6's second-to-last
        CHECK(d.kind == SeekDecision::Kind::Offer);
        CHECK(d.segment == std::optional<std::size_t>{ 6 });
        CHECK(near(d.target, 16.0));
        CHECK(timeline::decide_seek(c, delayed, 172).kind == SeekDecision::Kind::Seek);

        // An ad's frame: its PTS show the main content's frame of that PTS,
        // and a seek to its segments' starts too; the segment before the
        // discontinuity, segment 2, is offered (D-114 (b)).
        for (const std::int64_t f : { 75, 80, 99, 100, 124 }) {
            INFO("frame " << f);
            d = timeline::decide_seek(c, seek, f);
            CHECK(d.kind == SeekDecision::Kind::Offer);
            CHECK(near(d.target, 12.0));
            CHECK(d.segment == std::optional<std::size_t>{ 2 });
            CHECK(c.segments[*d.segment].media_sequence == 2);
        }
        d = timeline::decide_seek(c, seek, 80);
        CHECK(contains(d.why, "a seek to its PTS, 10.200 s, would show the frame of PTS 10.200 s in segment 0"));

        // Content after the ad, its PTS where it would be without it.
        d = timeline::decide_seek(c, seek, 130);
        CHECK(d.kind == SeekDecision::Kind::Seek);
        CHECK(near(d.target, 15.2));
        CHECK(timeline::decide_seek(c, seek, 125).kind == SeekDecision::Kind::Seek);
    }
}

TEST_CASE("timeline::decide_seek on the probe's shapes: content after a reset ad by its PTS, the ad from "
          "the segment before it; a file by its PTS; a refused map",
          "[timeline][timeline_match]")
{
    const timeline::SeekClock seek{ .start = 10.0, .duration = 6.0, .dts_lead = 0.04 };
    constexpr double kReset[3] = { 10.0, 1.0, 12.0 };
    const timeline::VariantClock reset = probe_clock(kReset);
    SeekDecision d = timeline::decide_seek(reset, seek, 125);   // 13.0 s, after the ad
    CHECK(d.kind == SeekDecision::Kind::Seek);
    CHECK(near(d.target, 13.0));
    d = timeline::decide_seek(reset, seek, 60);   // the ad's, 1.4 s: before the start
    CHECK(d.kind == SeekDecision::Kind::Offer);
    CHECK(d.segment == std::optional<std::size_t>{ 0 });
    CHECK(near(d.target, 10.0));
    CHECK(contains(d.why, "is before the stream's start, 10.000 s"));
    // The third segment's last frame: reached with no lead, dropped with
    // one; its own segment's start is offered.
    d = timeline::decide_seek(reset, seek, 149);
    CHECK(d.kind == SeekDecision::Kind::Offer);
    CHECK(d.segment == std::optional<std::size_t>{ 2 });
    CHECK(near(d.target, 12.0));
    CHECK(contains(d.why, "would show nothing"));

    constexpr double kContinuous[3] = { 10.0, 12.0, 14.0 };
    const timeline::VariantClock cont = probe_clock(kContinuous);
    for (const std::int64_t f : { 0, 25, 48, 50, 77, 100, 149 }) {
        INFO("frame " << f);
        CHECK(timeline::decide_seek(cont, seek, f).kind == SeekDecision::Kind::Seek);
    }
    CHECK(timeline::decide_seek(cont, seek, 49).kind == SeekDecision::Kind::Offer);   // a segment's last
    // A frame between two of the clock's: the last frame at or before it.
    CHECK(timeline::decide_seek(cont, seek, 1000).frame == std::optional<std::size_t>{ 149 });
    CHECK(timeline::decide_seek(cont, seek, -5).frame == std::optional<std::size_t>{ 0 });

    // A file opened alone, a mezzanine: by its PTS whatever its frames.
    timeline::VariantClock file = cont;
    file.playlist = false;
    file.segments.clear();
    d = timeline::decide_seek(file, seek, 49);
    CHECK(d.kind == SeekDecision::Kind::Seek);
    CHECK(near(d.target, 11.96));

    // A refused map: where the seek lands can't be told.
    timeline::VariantClock refused = cont;
    refused.segments.clear();
    refused.segment_map = "refused: the test's";
    d = timeline::decide_seek(refused, seek, 10);
    CHECK(d.kind == SeekDecision::Kind::None);
    CHECK(contains(d.why, "refused: the test's"));
    CHECK(timeline::decide_seek(timeline::VariantClock{}, seek, 0).kind == SeekDecision::Kind::None);
}

// ── A click (design §10; D-114 (d)) ──────────────────────────────────────────

TEST_CASE("timeline::click_interval: the clicked frame's segment of the curve's variant, a session's "
          "span's variant's, and the frame alone without a segment map",
          "[timeline][timeline_match]")
{
    const timeline::BuildInputs in = ad_inputs();
    timeline::Timeline t = built(in);
    const CurveRef v180{ .kind = CurveRef::Kind::Pair, .index = 1 };
    REQUIRE(t.curves[1].variant == 2);
    timeline::Interval i = timeline::click_interval(t, v180, 80);
    CHECK(i.ref_index == 75);
    CHECK(i.ref_end == 100);
    i = timeline::click_interval(t, v180, 30);
    CHECK(i.ref_index == 25);
    CHECK(i.ref_end == 50);
    i = timeline::click_interval(t, v180, 199);
    CHECK(i.ref_index == 175);
    CHECK(i.ref_end == 200);
    // A curve the timeline doesn't have: the frame alone.
    i = timeline::click_interval(t, CurveRef{ .kind = CurveRef::Kind::Pair, .index = 5 }, 80);
    CHECK(i.ref_index == 80);
    CHECK(i.ref_end == 81);

    // A session (design §9.3's plan): its curve plays v270 over segments 0
    // to 2 and 5 to 7, v180 over 3 and 4.
    std::string err;
    REQUIRE(timeline::add_session(t, in.scores.entries,
                                  timeline::SessionPlan{ .name  = "down at the ad",
                                                         .spans = { { .entry = 1, .first_seq = 0, .last_seq = 2 },
                                                                    { .entry = 2, .first_seq = 3, .last_seq = 4 },
                                                                    { .entry = 1, .first_seq = 5, .last_seq = 7 } } },
                                  err));
    const CurveRef session{ .kind = CurveRef::Kind::Session, .index = 0 };
    i = timeline::click_interval(t, session, 80);
    CHECK(i.ref_index == 75);
    CHECK(i.ref_end == 100);
    i = timeline::click_interval(t, session, 30);
    CHECK(i.ref_index == 25);
    CHECK(i.ref_end == 50);

    // v180's map refused: none of its segments is on the timeline.
    timeline::BuildInputs refused = ad_inputs();
    refused.variants[2].media.refused = "the test's";
    const timeline::Timeline r = built(refused);
    i = timeline::click_interval(r, v180, 80);
    CHECK(i.ref_index == 80);
    CHECK(i.ref_end == 81);
}

TEST_CASE("timeline::click_answer: inside a detected drop the document's attribution, elsewhere the "
          "query of the clicked frame's segment",
          "[timeline][timeline_match]")
{
    const timeline::BuildInputs in = ad_inputs();
    timeline::Snapshot s;
    s.timeline = built(in);
    s.scores   = in.scores;
    std::string err;
    s.attributions = timeline::attribute_drops(s.timeline, timeline::pair_curve_refs(s.timeline),
                                               timeline::DetectOptions{ .threshold = 6, .window = 50, .min_points = 12 },
                                               {}, err);
    REQUIRE(err.empty());
    REQUIRE(s.attributions.size() == 1);   // v180's ad, frames 75 to 124
    const CurveRef v180{ .kind = CurveRef::Kind::Pair, .index = 1 };

    timeline::ClickAnswer a = timeline::click_answer(s, v180, 80);
    CHECK(a.drop == std::optional<std::size_t>{ 0 });
    CHECK(a.attribution.detected);
    CHECK(a.attribution.interval.ref_index == 75);
    CHECK(a.attribution.interval.ref_end == 125);

    // Outside it: segment 1 of v180, its events and the reference's copies.
    a = timeline::click_answer(s, v180, 30);
    CHECK_FALSE(a.drop);
    CHECK_FALSE(a.attribution.detected);
    CHECK(a.attribution.interval.ref_index == 25);
    CHECK(a.attribution.interval.ref_end == 50);
    CHECK(a.attribution.points == 25);
    std::vector<std::string> segments;
    for (const timeline::Found& f : a.attribution.by_source[static_cast<std::size_t>(timeline::Source::Playlist)])
        if (s.timeline.events[f.event].type == timeline::kTypeSegment) segments.push_back(s.timeline.events[f.event].id);
    std::sort(segments.begin(), segments.end());
    CHECK(segments == std::vector<std::string>{ "playlist/segment/0/1", "playlist/segment/2/1" });
    // The other pair's curve at the drop's frame: no drop of its own there.
    a = timeline::click_answer(s, CurveRef{ .kind = CurveRef::Kind::Pair, .index = 0 }, 80);
    CHECK_FALSE(a.drop);
    CHECK(a.attribution.interval.ref_index == 75);
    CHECK(a.attribution.interval.ref_end == 100);
}
