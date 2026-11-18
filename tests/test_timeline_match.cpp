// Unit tests of src/TimelineMatch.h (eval/phase3-survey/design.md §9.1): written by WP12, a
// playlist opened alone among equal bytes by WP12b, one opened by a relative path by WP12c, the
// playhead not known where its sequence isn't by WP13b, the owner's answers of D-125 by WP13c.
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
//   WP12's warning, where none shares more than its file name; and a rung's
//   playlist opened by a path relative to its own directory, index.m3u8 or
//   ./index.m3u8 with the current directory the rung's, which the test sets,
//   matched to the rung by that path made absolute (D-124 (g) 2, Q97 2 (b)),
//   another rung's through .., a URL and an absolute path compared as they
//   were given, and from a directory that names no rung the first of all;
//   with the rungs and the master named by absolute local paths, as a
//   document names them whose ladder was scored from its master's absolute
//   path, a relative open path matched by its address made absolute as the
//   absolute path is (D-125 (a), Q98 (b)): a rung's with no warning, and
//   once its bytes have changed with the warning that says so, the master's,
//   changed since it was scored, its entries by their URLs made absolute,
//   and with the rungs named by a longer path the bytes and the path picking
//   the rung, the warning naming the open path whole from its root; and with
//   the rungs and the master named by relative paths, as a document names
//   them whose ladder was scored from a relative master path, the same text
//   matched by its address as given, from a directory where made absolute
//   it names other files, as D-125 (a) keeps it: a rung's with no warning,
//   and once its bytes have changed with the warning that says so, and the
//   master's, changed since it was scored, its entries by their URLs as
//   given;
// - the playhead: the player's PTS to the frame of the sequence played,
//   across the ad's restart of its timestamps, after a seek, at a forward
//   jump of more than 10 s, and on a coarser grid; and where the PTS on
//   screen lies in two or more sequences that nothing has told apart, none
//   (D-124 (c), Q93 (c)): at first sight inside the ad, whose PTS repeat the
//   main content's first 2 s, and in those 2 s, until a seek (the panel's
//   own, and another by where it lands), a switch (by where the Demuxer's
//   reposition lands, in the main content, D-124 (e)), a jump of the PTS
//   (back, and forward by more than 10 s, on a clock of five sequences, and
//   back where two of the sequences it leads from lead to one) or a PTS in
//   one sequence only tells it; a seek or a switch whose landing can't be
//   told telling nothing, among them a seek to one of a libx264 segment's
//   last two frames before the ad, whose landing depends on the first
//   packet's DTS lead (D-121 (c)), and one that shows no frame for a lead;
//   a jump that leads to two sequences the PTS lies in, telling neither;
//   then tracked as before; at first sight in one sequence only, at once; a
//   PTS between frames lying in each sequence with a frame within half a
//   frame of it, half a frame itself included; and a PTS that lies in no
//   sequence taken to lie in the sequences whose nearest frames are as near,
//   in ticks, as the nearest of all, one telling it and a tie of two giving
//   no playhead, and telling nothing while the playhead isn't known, its PTS
//   then the last, so that a jump after it is seen from it (D-125 (b) 4,
//   Q99 4 (b));
// - a step back (D-125 (b) 6, Q99 6 (a)): told by where the scrub decoder's
//   seek by PTS lands, from the ad's first frame the main content's first,
//   twice in a row inside the ad, and nothing where the landing can't be
//   told; the playback pipeline's next frame tracked from the pipeline's own
//   last frame, and a seek or a switch dropping what was held for it; a
//   seek and a step back's frame in one iteration, the seek by its own
//   target first; a panel's first sight of a step back's frame, the
//   pipeline's state not known; and a keyframe step by its keyframe's PTS;
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
#include "TestTemp.h"
#include "TimelineBuild.h"
#include "TimelineLoad.h"
#include "TimelineMatch.h"
#include "TimelineModel.h"
#include "TimelineQuery.h"
#include "VMAFLadder.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
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

// The current directory set to `dir` while the guard lives, as a player
// started there has it, and set back when it goes, a failed REQUIRE's
// exception included (as tests/test_timeline_load.cpp's).
class InDirectory {
public:
    explicit InDirectory(const std::filesystem::path& dir) { std::filesystem::current_path(dir); }
    ~InDirectory()
    {
        std::error_code ec;
        std::filesystem::current_path(before_, ec);
    }
    InDirectory(const InDirectory&)            = delete;
    InDirectory& operator=(const InDirectory&) = delete;

private:
    std::filesystem::path before_ = std::filesystem::current_path();
};

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
    // is as built, which the owner kept (D-124 (g) 3, Q97 3 (a);
    // src/TimelineMatch.h).
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
    // What a player started in a rung's own directory is given for its
    // playlist, index.m3u8 or ./index.m3u8, is made absolute against the
    // current directory first (D-124 (g) 2): the next test case.
    for (const std::string other : { "http://cdn.invalid/v.m3u8", "http://cdn.invalid/copy/index.m3u8",
                                     "http://cdn.invalid/v270/other.m3u8", "/data/copy/index.m3u8" }) {
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

TEST_CASE("timeline::match_stream: a rung's playlist opened by a path relative to its own directory, "
          "index.m3u8 or ./index.m3u8 with the current directory the rung's, named by that path made "
          "absolute (D-124 (g) 2)",
          "[timeline][timeline_match]")
{
    // D-069's shape, its three rungs of one playlist text, and a copy of the
    // ladder's directories, ladder/v360, ladder/v270 and ladder/v180, with no
    // file in them: nothing but the current directory is asked for, nothing
    // looked up on disk. The player started in a rung's directory is given
    // its playlist by its file name, compared once made absolute against the
    // current directory, lexically, so that its path ends with the rung's
    // ladder/<rung>/index.m3u8, as the rung's URL does.
    const timeline::Timeline     t      = built(ad_inputs());
    const timeline::LadderScores scores = with_master(ad_report(), kMaster, kMasterUrl);
    REQUIRE(t.variants.size() == 3);
    const std::filesystem::path root = testtemp::path("q97-cwd");
    std::filesystem::remove_all(root);
    for (const char* rung : kRungs) std::filesystem::create_directories(root / "ladder" / rung);
    std::filesystem::create_directories(root / "elsewhere");
    const std::string first_of_all =
        "the open playlist's bytes are those of 3 of the document's variants: the first, variant 0, is taken";

    for (int k = 0; k < 3; ++k) {
        INFO("rung " << kRungs[k]);
        const InDirectory cwd(root / "ladder" / kRungs[k]);
        const std::string warning =
            std::format("the open playlist's bytes are those of 3 of the document's variants: variant {}, "
                        "whose URL ends with the open playlist's ladder/{}/index.m3u8, is taken", k, kRungs[k]);
        for (const std::string path : { "index.m3u8", "./index.m3u8" }) {
            INFO(path);
            const StreamMatch m = timeline::match_stream(t, scores, opened(path, kAdPlaylist, path));
            INFO(warnings_of(m));
            CHECK(m.by == StreamMatch::By::Variant);
            CHECK(m.entries == StreamMatch::Entries::None);
            CHECK(m.opened_variant == k);
            CHECK(m.warnings == std::vector<std::string>{ warning });
        }
        // Another rung's, from this one's directory: that rung.
        const int other = (k + 1) % 3;
        const std::string sibling = std::format("../{}/index.m3u8", kRungs[other]);
        CHECK(timeline::match_stream(t, scores, opened(sibling, kAdPlaylist, sibling)).opened_variant == other);
        // A URL is compared as it was given, and an absolute path as it is:
        // sharing only the file name, the first of them all.
        for (const std::string as_given : { "http://cdn.invalid/copy/index.m3u8", "/data/copy/index.m3u8" }) {
            INFO(as_given);
            const StreamMatch m = timeline::match_stream(t, scores, opened(as_given, kAdPlaylist, as_given));
            CHECK(m.opened_variant == 0);
            CHECK(m.warnings == std::vector<std::string>{ first_of_all });
        }
    }

    // From a directory that names no rung: the first of them all, with
    // WP12's warning, as before.
    const InDirectory cwd(root / "elsewhere");
    for (const std::string path : { "index.m3u8", "./index.m3u8" }) {
        INFO(path);
        const StreamMatch m = timeline::match_stream(t, scores, opened(path, kAdPlaylist, path));
        CHECK(m.by == StreamMatch::By::Variant);
        CHECK(m.opened_variant == 0);
        CHECK(m.warnings == std::vector<std::string>{ first_of_all });
    }
}

TEST_CASE("timeline::match_stream: rungs and a master named by absolute local paths, opened by paths relative "
          "to the current directory, matched by their addresses made absolute (D-125 (a))",
          "[timeline][timeline_match]")
{
    // D-069's shape as a document names it whose ladder was scored from its
    // master's absolute path: its master and its three rungs of one playlist
    // text by their absolute paths, <ladder>/master.m3u8 and
    // <ladder>/<rung>/index.m3u8, the report's entries resolved against the
    // master's path, as VMAFLadder resolves them. The ladder's directories
    // are made, with no file in them, and named by their canonical path, as
    // the current directory gives it back once set there, so that the open
    // path made absolute is its rung's: nothing is looked up on disk.
    const std::filesystem::path root = testtemp::path("q97-abs");
    std::filesystem::remove_all(root);
    for (const char* rung : kRungs) std::filesystem::create_directories(root / "ladder" / rung);
    const std::filesystem::path ladder = std::filesystem::canonical(root / "ladder");
    auto local = [&](int k) { return (ladder / kRungs[k] / "index.m3u8").string(); };
    const std::string sha256 = filehash::sha256_hex(kAdPlaylist);
    timeline::Timeline t;
    for (int k = 0; k < 3; ++k)
        t.variants.push_back({ .index = k, .pair = k - 1, .entries = { static_cast<std::size_t>(k) },
                               .url = local(k), .playlist_location = local(k), .playlist_sha256 = sha256 });
    const std::string            master = (ladder / "master.m3u8").string();
    const timeline::LadderScores scores = with_master(ad_report(), kMaster, master);
    REQUIRE(scores.entries.size() == 3);
    for (int k = 0; k < 3; ++k) REQUIRE(scores.entries[static_cast<std::size_t>(k)].url == local(k));
    const std::string edited = kAdPlaylist + "#EXT-X-INDEPENDENT-SEGMENTS\n";
    auto changed_since = [&](const std::string& path) {
        return std::format("{} has changed since the document was built: its SHA-256 is {}, the document's {}",
                           path, filehash::sha256_hex(edited), sha256);
    };

    for (int k = 0; k < 3; ++k) {
        INFO("rung " << kRungs[k]);
        const InDirectory cwd(ladder / kRungs[k]);
        REQUIRE(std::filesystem::current_path() == ladder / kRungs[k]);
        // index.m3u8 and ./index.m3u8, made absolute against the current
        // directory, are the rung's address: matched by it as by the
        // absolute path, with no warning, where WP12c matched them by their
        // bytes and their path, with D-124 (g) 3's warning; once the bytes
        // have changed since the build, with the warning that says so, the
        // open path named as it was given, where WP12c's address match
        // compared it as given and the document counted as another stream's
        // (D-125 (a), Q98 (b)).
        for (const std::string path : { "index.m3u8", "./index.m3u8" }) {
            INFO(path);
            StreamMatch m = timeline::match_stream(t, scores, opened(path, kAdPlaylist, path));
            INFO(warnings_of(m));
            CHECK(m.by == StreamMatch::By::Variant);
            CHECK(m.entries == StreamMatch::Entries::None);
            CHECK(m.opened_variant == k);
            CHECK(m.warnings.empty());
            m = timeline::match_stream(t, scores, opened(path, edited, path));
            CHECK(m.by == StreamMatch::By::Variant);
            CHECK(m.opened_variant == k);
            CHECK(m.warnings == std::vector<std::string>{ changed_since(path) });
        }
        // Another rung's through .., lexically, likewise.
        const int         other   = (k + 1) % 3;
        const std::string sibling = std::format("../{}/index.m3u8", kRungs[other]);
        StreamMatch m = timeline::match_stream(t, scores, opened(sibling, edited, sibling));
        CHECK(m.by == StreamMatch::By::Variant);
        CHECK(m.opened_variant == other);
        CHECK(m.warnings == std::vector<std::string>{ changed_since(sibling) });
        // By its absolute path: by its address, with no warning, and once
        // its bytes have changed, with the warning that says so.
        m = timeline::match_stream(t, scores, opened(local(k), kAdPlaylist, local(k)));
        CHECK(m.by == StreamMatch::By::Variant);
        CHECK(m.opened_variant == k);
        CHECK(m.warnings.empty());
        m = timeline::match_stream(t, scores, opened(local(k), edited, local(k)));
        CHECK(m.by == StreamMatch::By::Variant);
        CHECK(m.opened_variant == k);
        CHECK(m.warnings == std::vector<std::string>{ changed_since(local(k)) });
    }

    // The rungs named by a longer path that ends with the whole of the open
    // path made absolute, as a document built where the ladder lay under
    // another root names them: no address is the open path's, so the bytes
    // and the path pick the rung, the warning naming the open path whole,
    // from its root (D-124 (g) 3).
    timeline::Timeline moved = t;
    for (int k = 0; k < 3; ++k) {
        moved.variants[static_cast<std::size_t>(k)].url               = "/mnt" + local(k);
        moved.variants[static_cast<std::size_t>(k)].playlist_location = "/mnt" + local(k);
    }
    for (int k = 0; k < 3; ++k) {
        INFO("rung " << kRungs[k] << " under /mnt");
        const InDirectory cwd(ladder / kRungs[k]);
        REQUIRE(local(k).starts_with('/'));
        const StreamMatch m = timeline::match_stream(moved, scores, opened("index.m3u8", kAdPlaylist, "index.m3u8"));
        CHECK(m.by == StreamMatch::By::Variant);
        CHECK(m.opened_variant == k);
        CHECK(m.warnings == std::vector<std::string>{ std::format(
                                "the open playlist's bytes are those of 3 of the document's variants: variant {}, "
                                "whose URL ends with the open playlist's {}, is taken", k, local(k)) });
    }

    // The master likewise, changed since it was scored: by its absolute path
    // by its address, its entries by URL; by master.m3u8, ./master.m3u8, and
    // ../master.m3u8 from a rung's directory, made absolute, the same, the
    // open master's entries, which ManifestParser resolves against the
    // relative final URL, made absolute the same way before they are
    // matched to the report's (D-125 (a), Q98 (b)), where WP12c's address
    // match compared the relative path as given and the document counted as
    // another stream's.
    const std::string changed = kMaster + "#EXT-X-INDEPENDENT-SEGMENTS\n";
    auto master_changed = [&](const std::string& path) {
        return std::format("the master at {} has changed since it was scored: its SHA-256 is {}, the report's {}; "
                           "its entries are matched by URL", path, filehash::sha256_hex(changed), scores.master_sha256);
    };
    {
        const InDirectory cwd(ladder);
        StreamMatch m = timeline::match_stream(t, scores, opened(master, changed, master));
        CHECK(m.by == StreamMatch::By::Address);
        CHECK(m.entries == StreamMatch::Entries::Url);
        CHECK(m.variant_of == std::vector<int>{ 0, 1, 2 });
        CHECK(m.warnings == std::vector<std::string>{ master_changed(master) });
        for (const std::string path : { "master.m3u8", "./master.m3u8" }) {
            INFO(path);
            m = timeline::match_stream(t, scores, opened(path, changed, path));
            INFO(warnings_of(m));
            CHECK(m.by == StreamMatch::By::Address);
            CHECK(m.entries == StreamMatch::Entries::Url);
            CHECK(m.variant_of == std::vector<int>{ 0, 1, 2 });
            CHECK(m.warnings == std::vector<std::string>{ master_changed(path) });
        }
        // An entry the report doesn't have: the warning names its URL as it
        // was compared, made absolute.
        const std::string more = changed + "#EXT-X-STREAM-INF:BANDWIDTH=200000\nv144/index.m3u8\n";
        m = timeline::match_stream(t, scores, opened("master.m3u8", more, "master.m3u8"));
        CHECK(m.by == StreamMatch::By::Address);
        CHECK(m.variant_of == std::vector<int>{ 0, 1, 2, -1 });
        CHECK(any_warning(m, std::format("no entry of the report has the URL of the player's entry 3 ({}): "
                                         "VMAFLadder resolved the report's, ManifestParser the player's",
                                         (ladder / "v144" / "index.m3u8").string())));
        // Its bytes unchanged: by them first, its entries in order, however
        // its path is spelled.
        m = timeline::match_stream(t, scores, opened("master.m3u8", kMaster, "master.m3u8"));
        CHECK(m.by == StreamMatch::By::MasterBytes);
        CHECK(m.entries == StreamMatch::Entries::Index);
        CHECK(m.variant_of == std::vector<int>{ 0, 1, 2 });
        CHECK(m.warnings.empty());
    }
    const InDirectory cwd(ladder / "v270");
    const StreamMatch up = timeline::match_stream(t, scores, opened("../master.m3u8", changed, "../master.m3u8"));
    CHECK(up.by == StreamMatch::By::Address);
    CHECK(up.entries == StreamMatch::Entries::Url);
    CHECK(up.variant_of == std::vector<int>{ 0, 1, 2 });
    // From a directory where it names another file, as given and made
    // absolute: another stream's.
    const StreamMatch elsewhere = timeline::match_stream(t, scores, opened("master.m3u8", changed, "master.m3u8"));
    CHECK(elsewhere.by == StreamMatch::By::None);
    CHECK(elsewhere.variant_of == std::vector<int>{ -1, -1, -1 });
}

TEST_CASE("timeline::match_stream: rungs and a master that the document names by relative paths, opened by the "
          "same text, matched by their addresses as given (D-125 (a))",
          "[timeline][timeline_match]")
{
    // D-069's shape as a document names it whose ladder was scored from a
    // relative master path, ladder/master.m3u8: its master by that path and
    // its three rungs of one playlist text by ladder/<rung>/index.m3u8, the
    // report's entries resolved against the master's path, as VMAFLadder
    // resolves them. The player is given the same text, from a current
    // directory where, made absolute, it names other files than the ones
    // the ladder was scored from, which the document doesn't name: the
    // comparison as given, which D-125 (a) keeps for a document that names a
    // relative path, matches it by its address. Nothing is looked up on
    // disk.
    auto relative = [](int k) { return std::format("ladder/{}/index.m3u8", kRungs[k]); };
    const std::string sha256 = filehash::sha256_hex(kAdPlaylist);
    timeline::Timeline t;
    for (int k = 0; k < 3; ++k)
        t.variants.push_back({ .index = k, .pair = k - 1, .entries = { static_cast<std::size_t>(k) },
                               .url = relative(k), .playlist_location = relative(k), .playlist_sha256 = sha256 });
    const std::string            master = "ladder/master.m3u8";
    const timeline::LadderScores scores = with_master(ad_report(), kMaster, master);
    REQUIRE(scores.entries.size() == 3);
    for (int k = 0; k < 3; ++k) REQUIRE(scores.entries[static_cast<std::size_t>(k)].url == relative(k));
    const std::filesystem::path root = testtemp::path("q98-rel");
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "elsewhere");
    const InDirectory cwd(root / "elsewhere");

    // Each rung's playlist by the document's text: by its address, with no
    // warning, where by its address made absolute alone it would be matched
    // by its bytes, which the three rungs share, and its path, with
    // D-124 (g) 3's warning; and once its bytes have changed since the
    // build, by its address, with the warning that says so, where by its
    // address made absolute alone it would be another stream's, no variant
    // having those bytes.
    const std::string edited = kAdPlaylist + "#EXT-X-INDEPENDENT-SEGMENTS\n";
    for (int k = 0; k < 3; ++k) {
        INFO("rung " << kRungs[k]);
        const std::string path = relative(k);
        StreamMatch m = timeline::match_stream(t, scores, opened(path, kAdPlaylist, path));
        INFO(warnings_of(m));
        CHECK(m.by == StreamMatch::By::Variant);
        CHECK(m.entries == StreamMatch::Entries::None);
        CHECK(m.opened_variant == k);
        CHECK(m.warnings.empty());
        m = timeline::match_stream(t, scores, opened(path, edited, path));
        CHECK(m.by == StreamMatch::By::Variant);
        CHECK(m.opened_variant == k);
        CHECK(m.warnings == std::vector<std::string>{ std::format(
                                "{} has changed since the document was built: its SHA-256 is {}, the document's {}",
                                path, filehash::sha256_hex(edited), sha256) });
    }

    // The master, changed since it was scored, by the document's text: by
    // its address, its entries by their URLs as given, ManifestParser's and
    // VMAFLadder's, each resolved against the same relative path; by its
    // address made absolute alone it would be another stream's.
    const std::string          changed = kMaster + "#EXT-X-INDEPENDENT-SEGMENTS\n";
    const timeline::OpenStream open    = opened(master, changed, master);
    REQUIRE(open.variants.size() == 3);
    for (int k = 0; k < 3; ++k) REQUIRE(open.variants[static_cast<std::size_t>(k)].url == relative(k));
    const StreamMatch m = timeline::match_stream(t, scores, open);
    INFO(warnings_of(m));
    CHECK(m.by == StreamMatch::By::Address);
    CHECK(m.entries == StreamMatch::Entries::Url);
    CHECK(m.variant_of == std::vector<int>{ 0, 1, 2 });
    CHECK(m.warnings == std::vector<std::string>{ std::format(
                            "the master at {} has changed since it was scored: its SHA-256 is {}, the report's {}; "
                            "its entries are matched by URL", master, filehash::sha256_hex(changed),
                            scores.master_sha256) });
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
        // The main content from its start, its sequence told by a seek there:
        // at first sight its first 2 s, whose PTS the ad's frames repeat,
        // give no playhead (D-124 (c); the next test case).
        timeline::playhead_seeked(st, 0);
        CHECK(at(10.0).ref_index == 0);
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
        CHECK(other.unknown.empty());
    }
}

TEST_CASE("timeline::track_playhead: no playhead where the PTS on screen lies in two sequences that nothing "
          "has told apart, until a seek, a switch or the PTS in one sequence only tells it; at once at first "
          "sight in one sequence only (D-124 (c))",
          "[timeline][timeline_match]")
{
    const timeline::BuildInputs in = ad_inputs();
    const timeline::Timeline    t  = built(in);
    const timeline::SeekClock   seek{ .start = 10.0, .duration = 8.0, .dts_lead = 0.04 };
    const std::vector<int>      both{ 0, 1 };
    for (const int variant : { 0, 2 }) {   // the reference, and v180's distorted input
        INFO("variant " << variant);
        const timeline::VariantClock c = timeline::variant_clock(t, in.scores, variant);
        REQUIRE(c.frames.size() == 200);
        auto frame_at = [](const timeline::VariantClock& clock, timeline::PlayheadState& st, double pts) {
            const std::optional<timeline::PlayheadFrame> f = timeline::track_playhead(clock, st, pts);
            REQUIRE(f);
            CHECK(st.unknown.empty());
            return *f;
        };

        // First sight inside the ad, at its frame 87, PTS 10.48 s, which the
        // main content's frame 12 has too (Q93's case): no playhead, both
        // sequences named; and so frame by frame while the ad plays on.
        timeline::PlayheadState st;
        CHECK_FALSE(timeline::track_playhead(c, st, 10.48));
        CHECK(st.unknown == both);
        CHECK(st.sequence == -1);
        CHECK_FALSE(timeline::track_playhead(c, st, 10.52));
        CHECK(st.unknown == both);
        const timeline::PlayheadState inside = st;

        // A seek tells it. The panel's own, to a frame of the ad's sequence:
        // the ad's frames from there, then the main content after the ad in
        // the same sequence, the aligner's (D-046), as tracked before.
        timeline::PlayheadState s = inside;
        timeline::playhead_seeked(s, 1);
        CHECK(s.unknown.empty());
        CHECK(frame_at(c, s, 10.56).ref_index == 89);
        CHECK(frame_at(c, s, 11.96).ref_index == 124);
        CHECK(frame_at(c, s, 15.0).ref_index == 125);
        // Another seek, by where it lands: by its PTS, 10.48 s, the main
        // content's frame of that PTS (as decide_seek()'s case above has it),
        // sequence 0; the ad's step back after it is the next sequence's.
        s = inside;
        timeline::playhead_sought(s, c, seek, 10.48);
        CHECK(s.sequence == 0);
        CHECK(frame_at(c, s, 10.48).ref_index == 12);
        CHECK(frame_at(c, s, 12.96).ref_index == 74);
        CHECK(frame_at(c, s, 10.0).ref_index == 75);
        CHECK(s.sequence == 1);
        // A seek whose landing depends on the first packet's DTS lead, which
        // the Demuxer doesn't know, tells nothing. On libx264's segments, a
        // reorder delay of two frames (SeekClock::video_delay), a seek to the
        // main content's last frame before the ad, PTS 12.96 s, or to the one
        // before it, shows that frame with no lead, and with a lead of two
        // frames the first frame after the ad, PTS 15.0 s, of the ad's
        // sequence, as decide_seek() says, offering the segment's start
        // (D-121 (c)). The sequence isn't known after it, whatever it was
        // before, and the frame on screen then tells it, the main content's
        // last frames lying in sequence 0 only, the frame after the ad in
        // sequence 1 only. Two frames before the segment's end, at 12.88 s,
        // every lead shows that frame, and the seek tells sequence 0.
        const timeline::SeekClock x264{ .start = 10.0, .duration = 8.0, .video_delay = 2 };
        CHECK(contains(timeline::decide_seek(c, x264, 74).why,
                       "would show the frame of PTS 15.000 s in segment 5 instead"));
        for (const auto& [target, ref] : { std::pair<double, std::int64_t>{ 12.92, 73 },
                                           std::pair<double, std::int64_t>{ 12.96, 74 } }) {
            INFO("target " << target);
            CHECK(c.frames[timeline::simulate_seek(c, x264, target, 0.0).frame].ref_index == ref);
            CHECK(c.frames[timeline::simulate_seek(c, x264, target, 0.08).frame].ref_index == 125);
            // From the main content, the lead carrying the seek past the ad.
            timeline::PlayheadState carried{ .sequence = 0 };
            timeline::playhead_sought(carried, c, x264, target);
            CHECK(carried.sequence == -1);
            CHECK(frame_at(c, carried, 15.0).ref_index == 125);
            CHECK(frame_at(c, carried, 15.04).ref_index == 126);
            // From the ad, with no lead: the frame sought on screen.
            timeline::PlayheadState stayed{ .sequence = 1 };
            timeline::playhead_sought(stayed, c, x264, target);
            CHECK(stayed.sequence == -1);
            CHECK(frame_at(c, stayed, target).ref_index == ref);
        }
        s = inside;
        timeline::playhead_sought(s, c, x264, 12.88);
        CHECK(s.sequence == 0);
        // A seek whose landing can't be told, on a clock without segments,
        // tells nothing: still no playhead.
        timeline::VariantClock unmapped = c;
        unmapped.segments.clear();
        s = inside;
        timeline::playhead_sought(s, unmapped, seek, 10.48);
        CHECK(s.sequence == -1);
        CHECK_FALSE(timeline::track_playhead(unmapped, s, 10.48));
        CHECK(s.unknown == both);

        // A switch tells it: to the other rung, its first frame at 10.56 s.
        // The Demuxer's reposition by PTS lands in the main content
        // (D-124 (e)), its frame 14, sequence 0, which the player shows.
        const timeline::VariantClock other = timeline::variant_clock(t, in.scores, variant == 0 ? 2 : 0);
        REQUIRE(other.frames.size() == 200);
        s = inside;
        timeline::playhead_switched(s, other, seek, 10.56);
        CHECK(s.sequence == 0);
        CHECK(frame_at(other, s, 10.56).ref_index == 14);
        // One whose landing can't be told keeps the sequence the state held:
        // none, so still no playhead; a known one stays known.
        timeline::VariantClock other_unmapped = other;
        other_unmapped.segments.clear();
        s = inside;
        timeline::playhead_switched(s, other_unmapped, seek, 10.56);
        CHECK(s.sequence == -1);
        CHECK_FALSE(timeline::track_playhead(other_unmapped, s, 10.56));
        CHECK(s.unknown == both);
        timeline::PlayheadState known{ .sequence = 1 };
        timeline::playhead_switched(known, other_unmapped, seek, 10.56);
        CHECK(known.sequence == 1);
        CHECK(frame_at(other_unmapped, known, 10.56).ref_index == 89);

        // The PTS in one sequence only tells it: the ad played to its end,
        // then the main content after it from 15 s, sequence 1's alone, the
        // aligner's step of 3.04 s forward being no discontinuity (D-046);
        // then tracked as before.
        s = inside;
        CHECK_FALSE(timeline::track_playhead(c, s, 11.96));   // the ad's frame 124, the main content's 49
        CHECK(s.unknown == both);
        const timeline::PlayheadFrame after = frame_at(c, s, 15.0);
        CHECK(after.ref_index == 125);
        CHECK(after.sequence == 1);
        CHECK(s.sequence == 1);
        CHECK(frame_at(c, s, 15.04).ref_index == 126);
        CHECK(frame_at(c, s, 17.96).ref_index == 199);
        // First sight in the main content's first 2 s, whose PTS the ad
        // repeats: none until 12 s, sequence 0's alone; then the ad's step
        // back is the next sequence's, as before.
        timeline::PlayheadState start;
        CHECK_FALSE(timeline::track_playhead(c, start, 10.0));
        CHECK(start.unknown == both);
        CHECK_FALSE(timeline::track_playhead(c, start, 11.96));
        CHECK(frame_at(c, start, 12.0).ref_index == 50);
        CHECK(frame_at(c, start, 12.96).ref_index == 74);
        CHECK(frame_at(c, start, 10.0).ref_index == 75);
        CHECK(start.sequence == 1);

        // First sight at a PTS in one sequence only: the playhead at once.
        for (const auto& [pts, ref] : { std::pair{ 12.48, 62 }, std::pair{ 16.0, 150 } }) {
            INFO("PTS " << pts);
            timeline::PlayheadState fresh;
            const timeline::PlayheadFrame f = frame_at(c, fresh, pts);
            CHECK(f.ref_index == ref);
            CHECK(f.sequence == fresh.sequence);
        }

        // A PTS between frames lies in each sequence with a frame within
        // half a frame of it, half a frame itself included: first sight at
        // 10.50 s, half a frame from the main content's frame 12 and from the
        // ad's frame 87, lies in both, no playhead; at 12.02 s, half a frame
        // from the main content's frame 50, in sequence 0 alone, the ad's
        // nearest frame, at 11.96 s, being three half frames away.
        timeline::PlayheadState between;
        CHECK_FALSE(timeline::track_playhead(c, between, 10.50));
        CHECK(between.unknown == both);
        timeline::PlayheadState beside;
        CHECK(frame_at(c, beside, 12.02).ref_index == 50);

        // A PTS that lies in no sequence, further than half a frame from
        // every frame, as an unpaired frame's: the sequences whose nearest
        // frames are as near as the nearest of all, in ticks, are taken as
        // those it lies in, one telling it and two or more giving no
        // playhead, and while the playhead isn't known such a PTS tells
        // nothing, its PTS then the last (D-125 (b) 4, Q99 4 (b)). At first
        // sight at 14.0 s, between the main content's last frame before the
        // ad, at 12.96 s, 1.04 s away, and its first after it, at 15.0 s,
        // 1.0 s away: sequence 1 alone, its frame 125.
        timeline::PlayheadState gap;
        const timeline::PlayheadFrame g = frame_at(c, gap, 14.0);
        CHECK(g.ref_index == 125);
        CHECK(g.sequence == 1);
        // The report lacking the frames of PTS 10.52 s in both sequences, as
        // it lacks an unpaired frame. At first sight at 10.52 s the nearest
        // frames, at 10.48 s and 10.56 s in each sequence, are all a frame,
        // kFrame25 ticks, away: a tie of sequences 0 and 1, no playhead, both
        // named, where WP12 took the first of them, sequence 0's frame 12.
        // The clock's segments go with the frames, the tracker not using them.
        timeline::VariantClock holed = c;
        std::erase_if(holed.frames,
                      [](const timeline::ClockFrame& fr) { return fr.ref_index == 13 || fr.ref_index == 88; });
        holed.segments.clear();
        REQUIRE(holed.frames.size() == 198);
        timeline::PlayheadState hole_first;
        CHECK_FALSE(timeline::track_playhead(holed, hole_first, 10.52));
        CHECK(hole_first.unknown == both);
        CHECK(hole_first.sequence == -1);
        // No playhead at 10.48 s, which the ad and the main content share;
        // then 10.52 s, a PTS in no sequence while the playhead isn't known,
        // tells nothing, both still named, its PTS the last; and so 14.0 s,
        // though its nearest frame is sequence 1's alone, which at first
        // sight tells it. A jump after it is seen from it: back to 11.0 s,
        // in both, which leads sequences 0 and 1 to sequence 1 alone, its
        // frame 100, where from 10.52 s, 0.48 s before, no jump would be.
        timeline::PlayheadState hole;
        CHECK_FALSE(timeline::track_playhead(holed, hole, 10.48));
        CHECK(hole.unknown == both);
        CHECK_FALSE(timeline::track_playhead(holed, hole, 10.52));
        CHECK(hole.unknown == both);
        CHECK(hole.last_pts == std::optional<std::int64_t>{ 946800 });    // 10.52 s
        CHECK_FALSE(timeline::track_playhead(holed, hole, 14.0));
        CHECK(hole.unknown == both);
        CHECK(hole.sequence == -1);
        CHECK(hole.last_pts == std::optional<std::int64_t>{ 1260000 });   // 14.0 s
        const timeline::PlayheadFrame h = frame_at(holed, hole, 11.0);
        CHECK(h.ref_index == 100);
        CHECK(h.sequence == 1);
    }

    // A seek that shows no frame for one of the leads tells nothing either:
    // on the probe's shape of an ad whose timestamps restart at 1 s
    // (tests/test_hls_seek_behaviour.cpp), a seek to the stream's last frame
    // shows it with no lead, and with a lead of a frame nothing, the demuxer
    // dropping every packet to the stream's end, as decide_seek() says of it
    // below.
    constexpr double kReset[3] = { 10.0, 1.0, 12.0 };
    const timeline::VariantClock reset = probe_clock(kReset);
    const timeline::SeekClock    probe_seek{ .start = 10.0, .duration = 6.0, .dts_lead = 0.04 };
    CHECK(timeline::simulate_seek(reset, probe_seek, 13.96, 0.0).frame == 149);
    CHECK(timeline::simulate_seek(reset, probe_seek, 13.96, 0.04).kind == timeline::SeekLanding::Kind::Nothing);
    timeline::PlayheadState dropped{ .sequence = 2 };
    timeline::playhead_sought(dropped, reset, probe_seek, 13.96);
    CHECK(dropped.sequence == -1);
}

TEST_CASE("timeline::playhead_stepped_back: a step back told by where the scrub decoder's seek lands, the "
          "playback pipeline's next frame from its own last frame; a keyframe step by its keyframe's PTS "
          "(D-125 (b) 6)",
          "[timeline][timeline_match]")
{
    const timeline::BuildInputs in = ad_inputs();
    const timeline::Timeline    t  = built(in);
    // libx264's segments, as D-069's ladders are encoded: the decoder's
    // reorder delay of two frames, which the panel takes from the Demuxer.
    const timeline::SeekClock x264{ .start = 10.0, .duration = 8.0, .video_delay = 2 };
    // main.cpp's step back asks the scrub decoder for the frame one and a
    // half frames before the frame on screen.
    auto back_from = [](double pts) { return pts - 0.04 * 1.5; };
    for (const int variant : { 0, 2 }) {   // the reference, and v180's distorted input
        INFO("variant " << variant);
        const timeline::VariantClock c = timeline::variant_clock(t, in.scores, variant);
        REQUIRE(c.frames.size() == 200);
        auto frame_at = [&](timeline::PlayheadState& st, double pts) {
            const std::optional<timeline::PlayheadFrame> f = timeline::track_playhead(c, st, pts);
            REQUIRE(f);
            CHECK(st.unknown.empty());
            return f->ref_index;
        };

        // The playhead known in the ad: a seek to the segment before it, the
        // one decide_seek() offers for the ad's frames, then its frames to
        // the ad's first, where the PTS step back: sequence 1's frame 75.
        timeline::PlayheadState at_ad;
        timeline::playhead_sought(at_ad, c, x264, 12.0);
        CHECK(at_ad.sequence == 0);
        CHECK(frame_at(at_ad, 12.0) == 50);
        CHECK(frame_at(at_ad, 12.96) == 74);
        CHECK(frame_at(at_ad, 10.0) == 75);
        CHECK(at_ad.sequence == 1);
        CHECK_FALSE(at_ad.pipeline);

        // A step back from the ad's first frame: the scrub decoder is asked
        // for 9.94 s, before the stream's start, which the clamp makes the
        // start, so that it shows the main content's first frame, of the
        // same PTS: the step tells sequence 0, and the playhead marks frame
        // 0, where WP13's panel kept sequence 1 and marked the ad's frame 75.
        timeline::PlayheadState st = at_ad;
        timeline::playhead_stepped_back(st, c, x264, back_from(10.0));
        CHECK(st.sequence == 0);
        REQUIRE(st.pipeline);
        CHECK(st.pipeline->sequence == 1);
        CHECK(frame_at(st, 10.0) == 0);
        // A frame step forward then shows the playback pipeline's next
        // frame, which the step didn't move: the ad's second, PTS 10.04 s,
        // tracked from the ad's first frame, the pipeline's own last, as its
        // frame 76 of sequence 1, where tracked from the step's frame it
        // would be the main content's frame 1. Taken back once, the
        // pipeline's frames after it go on as before.
        timeline::playhead_resumed(st);
        CHECK_FALSE(st.pipeline);
        CHECK(frame_at(st, 10.04) == 76);
        CHECK(st.sequence == 1);
        timeline::playhead_resumed(st);
        CHECK(frame_at(st, 10.08) == 77);
        // With nothing held, nothing changes.
        timeline::PlayheadState plain = at_ad;
        timeline::playhead_resumed(plain);
        CHECK(plain.sequence == at_ad.sequence);
        CHECK(plain.last_pts == at_ad.last_pts);

        // Two steps back in a row from the ad's frame 80, at 10.2 s: each
        // asks for a PTS of the ad that the main content's first segment
        // holds on the demuxer's clock, so that each lands there, sequence
        // 0, the frames on screen the scrub decoder's first at or after its
        // request, 10.16 s and 10.12 s, the main content's frames 4 and 3;
        // the pipeline's state is held from before the first, so that its
        // next frame, the ad's 81, is sequence 1's.
        timeline::PlayheadState twice = at_ad;
        for (const double pts : { 10.04, 10.08, 10.12, 10.16 }) frame_at(twice, pts);
        CHECK(frame_at(twice, 10.2) == 80);
        timeline::playhead_stepped_back(twice, c, x264, back_from(10.2));
        CHECK(twice.sequence == 0);
        CHECK(frame_at(twice, 10.16) == 4);
        timeline::playhead_stepped_back(twice, c, x264, back_from(10.16));
        CHECK(twice.sequence == 0);
        CHECK(frame_at(twice, 10.12) == 3);
        timeline::playhead_resumed(twice);
        CHECK(frame_at(twice, 10.24) == 81);
        CHECK(twice.sequence == 1);

        // A seek after a step back moves the pipeline: what was held for it
        // is dropped, and the seek's frames follow from where it lands, a
        // seek to 11.0 s the main content's frame 25, where the held state
        // would make it the ad's frame 100; a switch and the panel's own
        // seek likewise.
        timeline::PlayheadState sought = at_ad;
        timeline::playhead_stepped_back(sought, c, x264, back_from(10.0));
        REQUIRE(sought.pipeline);
        timeline::playhead_sought(sought, c, x264, 11.0);
        CHECK_FALSE(sought.pipeline);
        timeline::playhead_resumed(sought);
        CHECK(frame_at(sought, 11.0) == 25);
        const timeline::VariantClock other = timeline::variant_clock(t, in.scores, variant == 0 ? 2 : 0);
        timeline::PlayheadState switched = at_ad;
        timeline::playhead_stepped_back(switched, c, x264, back_from(10.0));
        timeline::playhead_switched(switched, other, x264, 10.0);
        CHECK_FALSE(switched.pipeline);
        timeline::PlayheadState own = at_ad;
        timeline::playhead_stepped_back(own, c, x264, back_from(10.0));
        timeline::playhead_seeked(own, 1);
        CHECK_FALSE(own.pipeline);

        // A seek, then a step back's frame, in one iteration of main.cpp,
        // the step asked for before the seek: the panel takes the seek first,
        // by its own target (TimelinePanel::Inputs::seek_target), then the
        // step back, which holds where the seek lands for the playback
        // pipeline, whose next frames are the seek's. A seek to 16.0 s, the
        // main content after the ad, sequence 1, with the step back from the
        // ad's first frame: the step's frame the main content's first, then
        // the seek's the main content's frame 150, where with the step's PTS
        // taken for the seek's target, as main.cpp's last_seek_pos has it,
        // the pipeline's state would be sequence 0's, the seek's frame its
        // frame 74.
        timeline::PlayheadState both = at_ad;
        timeline::playhead_sought(both, c, x264, 16.0);
        CHECK(both.sequence == 1);
        timeline::playhead_stepped_back(both, c, x264, back_from(10.0));
        CHECK(both.sequence == 0);
        CHECK(frame_at(both, 10.0) == 0);
        timeline::playhead_resumed(both);
        CHECK(frame_at(both, 16.0) == 150);
        CHECK(both.sequence == 1);
        timeline::PlayheadState by_step = at_ad;
        timeline::playhead_sought(by_step, c, x264, 10.0);
        timeline::playhead_stepped_back(by_step, c, x264, back_from(10.0));
        CHECK(frame_at(by_step, 10.0) == 0);
        timeline::playhead_resumed(by_step);
        CHECK(frame_at(by_step, 16.0) == 74);
    }

    // A keyframe step seeks to its keyframe and shows from its PTS, which the
    // panel takes for its landing (TimelinePanel::Inputs::step_pts): on the
    // probe's shape of timestamps that continue, its three segments three
    // sequences, as discontinuity tags would make them (D-046), the second
    // segment's keyframe, PTS 12.0 s, lands in its segment for every lead of
    // libx264's two frames, and tells sequence 1; a simulated seek by PTS to
    // its DTS, two frames before, lands in the first segment with no lead,
    // and tells nothing.
    const timeline::SeekClock    probe_x264{ .start = 10.0, .duration = 6.0, .video_delay = 2 };
    constexpr double             kContinuous[3] = { 10.0, 12.0, 14.0 };
    const timeline::VariantClock cont = probe_clock(kContinuous);
    timeline::PlayheadState by_pts;
    timeline::playhead_sought(by_pts, cont, probe_x264, 12.0);
    CHECK(by_pts.sequence == 1);
    timeline::PlayheadState by_dts;
    timeline::playhead_sought(by_dts, cont, probe_x264, 11.92);
    CHECK(by_dts.sequence == -1);

    // A step back whose landing can't be told tells nothing: on the probe's
    // shape of an ad whose timestamps restart at 1 s
    // (tests/test_hls_seek_behaviour.cpp), from the third segment's first
    // frame, PTS 12.0 s, sequence 2, the scrub decoder is asked for 11.94 s,
    // which with no lead lands in the first segment, sequence 0, and with a
    // lead of 0.06 s, within libx264's two frames, in the ad's segment,
    // whose frames are all before it, and so on in the third, sequence 2.
    // The frame on screen then tells it where its PTS lies in one sequence
    // only, the first segment's last frame at 11.96 s, or the third
    // segment's first again at 12.0 s, as the lead has it; and the
    // pipeline's next frame, the third segment's second at 12.04 s, follows
    // from the pipeline's state, sequence 2's frame 101.
    constexpr double             kReset[3] = { 10.0, 1.0, 12.0 };
    const timeline::VariantClock reset = probe_clock(kReset);
    CHECK(reset.frames[timeline::simulate_seek(reset, probe_x264, 11.94, 0.0).frame].sequence == 0);
    CHECK(reset.frames[timeline::simulate_seek(reset, probe_x264, 11.94, 0.06).frame].sequence == 2);
    for (const auto& [shown, ref] : { std::pair{ 11.96, 49 }, std::pair{ 12.0, 100 } }) {
        INFO("the step's frame at " << shown << " s");
        timeline::PlayheadState probe{ .sequence = 2, .last_pts = 1080000 };   // at 12.0 s
        timeline::playhead_stepped_back(probe, reset, probe_x264, back_from(12.0));
        CHECK(probe.sequence == -1);
        REQUIRE(probe.pipeline);
        const std::optional<timeline::PlayheadFrame> f = timeline::track_playhead(reset, probe, shown);
        REQUIRE(f);
        CHECK(f->ref_index == ref);
        timeline::playhead_resumed(probe);
        const std::optional<timeline::PlayheadFrame> next = timeline::track_playhead(reset, probe, 12.04);
        REQUIRE(next);
        CHECK(next->ref_index == 101);
        CHECK(next->sequence == 2);
    }

    // The panel's first sight of a step back's frame holds the pipeline's
    // state as not known, an empty Held (src/TimelinePanel.cpp): the frame
    // on screen tells it where its PTS lies, and so does the pipeline's next
    // frame, as at first sight, not taken to follow the frame on screen. On
    // the probe's reset shape, the step's frame at 11.96 s, sequence 0's
    // alone, then the pipeline's at 12.04 s, sequence 2's alone, its frame
    // 101, which from 11.96 s, 0.08 s before, would be taken for sequence
    // 0's frame 49.
    timeline::PlayheadState unseen;
    unseen.pipeline = timeline::PlayheadState::Held{};
    const std::optional<timeline::PlayheadFrame> first = timeline::track_playhead(reset, unseen, 11.96);
    REQUIRE(first);
    CHECK(first->ref_index == 49);
    timeline::playhead_resumed(unseen);
    const std::optional<timeline::PlayheadFrame> after = timeline::track_playhead(reset, unseen, 12.04);
    REQUIRE(after);
    CHECK(after->ref_index == 101);
    CHECK(after->sequence == 2);
}

TEST_CASE("timeline::track_playhead: a jump of the PTS tells the sequence where it leads to one the PTS lies "
          "in (D-124 (c))",
          "[timeline][timeline_match]")
{
    // Five sequences of 50 frames at 25 fps, as the aligner splits them
    // (D-046): 0 from 10 s; 1 from 10 s again, a step back; 2 from 30 s, a
    // step forward of more than 10 s; 3 and 4 from 30 s again, steps back.
    timeline::VariantClock c;
    c.time_base  = kTs;
    c.frame_rate = { 25, 1 };
    for (int s = 0; s < 5; ++s)
        for (int i = 0; i < 50; ++i)
            c.frames.push_back({ .index = s * 50 + i, .ref_index = s * 50 + i,
                                 .pts = (s < 2 ? 10 : 30) * 90000 + i * kFrame25, .sequence = s });
    auto frame_at = [&](timeline::PlayheadState& st, double pts) {
        const std::optional<timeline::PlayheadFrame> f = timeline::track_playhead(c, st, pts);
        REQUIRE(f);
        CHECK(st.unknown.empty());
        return *f;
    };
    // First sight at 10.48 s, in 0 and 1; still both at 11.96 s, with no
    // jump in between.
    timeline::PlayheadState st;
    CHECK_FALSE(timeline::track_playhead(c, st, 10.48));
    CHECK_FALSE(timeline::track_playhead(c, st, 11.96));
    CHECK(st.unknown == std::vector<int>{ 0, 1 });
    const timeline::PlayheadState lying = st;
    // A step back to 10 s, which both have a frame at: it leads 0 to 1 and 1
    // to 2, and of those 1 alone has a frame there, its first.
    timeline::PlayheadState back = lying;
    const timeline::PlayheadFrame b = frame_at(back, 10.0);
    CHECK(b.ref_index == 50);
    CHECK(back.sequence == 1);
    // Then tracked as before: frame by frame, and at the next jump, forward
    // to 30 s, the next sequence.
    CHECK(frame_at(back, 10.04).ref_index == 51);
    CHECK(frame_at(back, 11.96).ref_index == 99);
    CHECK(frame_at(back, 30.0).ref_index == 100);
    CHECK(back.sequence == 2);
    // A step forward of more than 10 s to 30 s, in 2, 3 and 4: of the
    // sequences it leads 0 and 1 to, 1 and 2, the PTS lies in 2 alone.
    timeline::PlayheadState forward = lying;
    const timeline::PlayheadFrame f = frame_at(forward, 30.0);
    CHECK(f.ref_index == 100);
    CHECK(forward.sequence == 2);

    // First sight at 30.48 s, in 2, 3 and 4; a step back to 30 s leads them
    // to 3, 4 and 4, the last staying, and the PTS lies in both 3 and 4:
    // that jump tells nothing, and a seek then does.
    timeline::PlayheadState three;
    CHECK_FALSE(timeline::track_playhead(c, three, 30.48));
    CHECK_FALSE(timeline::track_playhead(c, three, 31.96));
    CHECK_FALSE(timeline::track_playhead(c, three, 30.0));
    CHECK(three.unknown == std::vector<int>{ 2, 3, 4 });
    CHECK(three.sequence == -1);
    timeline::playhead_seeked(three, 4);
    CHECK(frame_at(three, 30.0).ref_index == 200);

    // A jump that leads two of the sequences the last PTS lay in to one
    // tells that one: from a state naming 3 and 4 at 31.96 s, made by hand as
    // the first playhead case makes `other`, a step back to 30 s leads 3 to 4
    // and 4 to itself, the last staying, though the PTS lies in 2, 3 and 4.
    timeline::PlayheadState onto_one{ .last_pts = 2876400, .unknown = { 3, 4 } };   // 31.96 s
    const timeline::PlayheadFrame one = frame_at(onto_one, 30.0);
    CHECK(one.ref_index == 200);
    CHECK(onto_one.sequence == 4);

    // No frames, or no time base: no playhead, and nothing named.
    timeline::PlayheadState none = lying;
    CHECK_FALSE(timeline::track_playhead(timeline::VariantClock{}, none, 10.0));
    CHECK(none.unknown.empty());
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
