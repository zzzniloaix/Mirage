// timeline::build() and the attribution of drops (src/TimelineBuild.h): the
// merge of a ladder's sources into one timeline on the reference's frames
// (eval/phase3-survey/design.md §3.4, §5.1), on inputs the test builds by
// hand: a ladder report as timeline::read_ladder_report() gives one, each
// variant's media playlist parsed as the build parses it
// (timeline::parse_playlist_events()), and each demux pass as
// src/TimelineMedia.h returns one. Mostly in the shape of testgen's ad
// ladder (eval/phase3-survey/testgen.md §2.3), whose builder in
// tests/test_timeline_doc.cpp this copies: three rungs of eight 25-frame
// segments at 25 fps, the reference v360 and the pairs v270 and v180, the ad
// in segments 3 and 4 restarting its timestamps at the main content's
// origin, v180's ad scored low. Then a distorted input on a coarser grid
// with unpaired frames, a refused variant, a segment none of whose frames is
// paired, named with the tags it holds, tags no segment holds, an SCTE-35
// message, a cue_out that makes no break, libvmaf contexts that can't be
// placed, ad breaks ended by their declared duration, corrupt packets with
// their boundaries, the same as the adapter's next_boundary() gives, a
// mezzanine reference, the ranks of a master with shared and equal
// BANDWIDTHs, inputs that don't fit, the curves read back from a report,
// detection's attributions, and the same document from the same inputs
// twice.

#include <catch2/catch_test_macros.hpp>

#include "FileHash.h"
#include "LadderReportRead.h"
#include "PlaylistEvents.h"
#include "TimelineAxis.h"
#include "TimelineBuild.h"
#include "TimelineDetect.h"
#include "TimelineDoc.h"
#include "TimelineMedia.h"
#include "TimelineModel.h"
#include "TimelineQuery.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using timeline::Attribution;
using timeline::CurveRef;
using timeline::Event;
using timeline::Interval;
using timeline::Source;
using timeline::Value;

namespace {

bool contains(std::string_view s, std::string_view part) { return s.find(part) != std::string_view::npos; }

// ── testgen's ad ladder, as tests/test_timeline_doc.cpp builds it ────────────

constexpr AVRational   kTs      = { 1, 90000 };   // MPEG-TS's time base
constexpr std::int64_t kOrigin  = 900000;         // 10 s: where testgen's main content starts
constexpr std::int64_t kFrame25 = 3600;           // a frame at 25 fps, in 1/90000
constexpr std::int64_t kFrames  = 200;            // eight segments of 25 frames
constexpr const char*  kRungs[] = { "v360", "v270", "v180" };

// testgen's ad playlist (eval/phase3-survey/testgen.md §2.3), the same text
// in every rung: the ad in segments 3 and 4, between two EXT-X-DISCONTINUITY
// tags (lines 13 and 20), with the plain cue tags (lines 14, 17 and 21).
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

// A pair's score at frame i: v270 (pair 0) near 97, with the one-point dip
// of the first pair of the ad's libvmaf context (D-037); v180 (pair 1) near
// 91, its ad near 25.
double score(int pair, std::int64_t i)
{
    const double wobble = 0.25 * static_cast<double>(i % 4);
    if (pair == 0) return i == 75 ? 80.0 : 97.0 + wobble;
    return i >= 75 && i < 125 ? 25.0 + wobble : 91.0 + wobble;
}

// The ladder report as timeline::read_ladder_report() would give it: the
// master's three entries, the reference v360 and two complete pairs that pair
// every frame, the reference's sequences as the aligner finds them: a new
// one at the ad's step back, none at its 3.04 s step forward at frame 125.
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
    s.master_url              = "http://test.invalid/ladder/master.m3u8";
    s.master_location         = s.master_url;
    s.master_sha256           = filehash::sha256_hex("the master's bytes");
    const std::int64_t bandwidth[] = { 1000000, 600000, 350000 };
    const char* const  resolution[] = { "640x360", "480x270", "320x180" };
    for (int k = 0; k < 3; ++k) {
        timeline::MasterEntry e;
        e.index      = static_cast<std::size_t>(k);
        e.line       = static_cast<std::size_t>(3 + 2 * k);
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

// A demux pass of the ad ladder's shape: every frame in its 25-frame
// segment, a keyframe at each segment's first frame, the reference's PTS.
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

timeline::VariantRead read_of(const std::string& playlist, int variant, timeline::VariantMedia media)
{
    timeline::VariantRead r;
    r.playlist = timeline::parse_playlist_events(playlist, url_of(variant), variant);
    REQUIRE(r.playlist->error.empty());
    r.playlist_location = url_of(variant);
    r.playlist_sha256   = filehash::sha256_hex(playlist);
    r.media             = std::move(media);
    return r;
}

// The build's inputs for the ad ladder, every variant with `playlist`.
timeline::BuildInputs ad_inputs(const std::string& playlist = kAdPlaylist)
{
    timeline::BuildInputs in;
    in.scores = ad_report();
    for (int v = 0; v < 3; ++v) in.variants.push_back(read_of(playlist, v, ad_media()));
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

// The events of one type and variant, in the timeline's order.
std::vector<const Event*> events_of(const timeline::Timeline& t, std::string_view type, int variant)
{
    std::vector<const Event*> out;
    for (const Event& e : t.events)
        if (e.type == type && e.variant == variant) out.push_back(&e);
    return out;
}

const Value* member(const timeline::Payload& p, std::string_view name)
{
    for (const auto& [n, v] : p)
        if (n == name) return &v;
    return nullptr;
}

std::optional<std::int64_t> int_member(const timeline::Payload& p, std::string_view name)
{
    const Value* v = member(p, name);
    if (!v || !std::holds_alternative<std::int64_t>(v->v)) return std::nullopt;
    return std::get<std::int64_t>(v->v);
}

std::string text_member(const timeline::Payload& p, std::string_view name)
{
    const Value* v = member(p, name);
    return v && std::holds_alternative<std::string>(v->v) ? std::get<std::string>(v->v) : std::string();
}

bool any_warning(const timeline::Timeline& t, std::string_view part)
{
    return std::any_of(t.warnings.begin(), t.warnings.end(),
                       [part](const std::string& w) { return contains(w, part); });
}

// Every warning, a line each, for a failed check's message.
std::string warnings_text(const timeline::Timeline& t)
{
    std::string out;
    for (const std::string& w : t.warnings) out += w + "\n";
    return out;
}

// The ids of the events of one source that an attribution lists.
std::vector<std::string> listed(const timeline::Timeline& t, const Attribution& a, Source source)
{
    std::vector<std::string> out;
    for (const timeline::Found& f : a.by_source[static_cast<std::size_t>(source)])
        out.push_back(t.events[f.event].id);
    return out;
}

// What a document of `t` is written with, as the timeline subcommand fills it.
timeline::DocInfo doc_info(const timeline::LadderScores& scores, const timeline::Timeline& t)
{
    timeline::DocInfo info;
    info.ffmpeg_libraries     = { { "libavutil", "1.2.3" } };
    info.report               = timeline::report_info(scores);
    info.report.path          = "ad.json";
    info.report.sha256        = filehash::sha256_hex("the report's bytes");
    info.report.scores_sha256 = filehash::sha256_hex(timeline::scores_text(scores));
    std::string err;
    REQUIRE(timeline::reference_info(scores, t.axis, info.reference, err));
    return info;
}

} // namespace

// ── The ad ladder ────────────────────────────────────────────────────────────

TEST_CASE("timeline::build: the ad ladder's variants, axis, curves and events, each placed by "
          "design §5.1's rules", "[timeline][timeline_build]")
{
    const timeline::BuildInputs in = ad_inputs();
    const timeline::Timeline    t  = built(in);

    // The variants (design §4.5; D-075): the reference first, then each
    // pair's distorted playlist, by entry, URL and both ranks.
    REQUIRE(t.variants.size() == 3);
    const int top[] = { 1, 2, 3 }, bottom[] = { 3, 2, 1 };
    const std::int64_t bandwidth[] = { 1000000, 600000, 350000 };
    const char* const  resolution[] = { "640x360", "480x270", "320x180" };
    for (int v = 0; v < 3; ++v) {
        INFO("variant " << v);
        const timeline::Variant& var = t.variants[static_cast<std::size_t>(v)];
        CHECK(var.index == v);
        CHECK(var.pair == v - 1);
        CHECK(var.entries == std::vector<std::size_t>{ static_cast<std::size_t>(v) });
        CHECK(var.url == url_of(v));
        CHECK(var.playlist_location == url_of(v));
        CHECK(var.playlist_sha256 == filehash::sha256_hex(kAdPlaylist));
        CHECK(var.bandwidth == bandwidth[v]);
        CHECK(var.resolution == resolution[v]);
        CHECK(var.codecs == "avc1.64001e");
        CHECK(var.rank_from_top == top[v]);
        CHECK(var.rank_from_bottom == bottom[v]);
        CHECK(var.segment_map == "ok");
    }

    // The axis: the reference's playlist time on its segment map (D-073).
    CHECK(t.axis.frames == kFrames);
    CHECK(t.axis.clock == "playlist-time");
    CHECK(t.axis.warnings.empty());
    CHECK(t.warnings.empty());

    // The curves: each pair's scored frames, curve k on variant k + 1.
    REQUIRE(t.curves.size() == 2);
    for (int k = 0; k < 2; ++k) {
        CHECK(t.curves[static_cast<std::size_t>(k)].variant == k + 1);
        CHECK(t.curves[static_cast<std::size_t>(k)].ref_index.size() == static_cast<std::size_t>(kFrames));
        CHECK(t.curves[static_cast<std::size_t>(k)].context_first == std::vector<std::int64_t>{ 0, 75 });
    }

    // 22 events per variant (8 segments, 2 discontinuities, 3 cues, 1 break,
    // 8 keyframes) and one context_start per pair.
    CHECK(t.events.size() == 3 * 22 + 2);
    for (int v = 0; v < 3; ++v) {
        INFO("variant " << v);
        const auto segments = events_of(t, "segment", v);
        REQUIRE(segments.size() == 8);
        for (std::int64_t k = 0; k < 8; ++k) {
            const Event& e = *segments[static_cast<std::size_t>(k)];
            CHECK(e.source == Source::Playlist);
            CHECK(e.segment == k);
            CHECK(e.ref_index == 25 * k);
            CHECK(e.ref_end == 25 * k + 25);
            CHECK(int_member(e.payload, "bytes") == 12000 + k);
        }
        const auto disc = events_of(t, "discontinuity", v);
        REQUIRE(disc.size() == 2);
        CHECK(disc[0]->ref_index == 75);
        CHECK(disc[0]->segment == 3);
        CHECK(int_member(disc[0]->payload, "line") == 13);
        CHECK(disc[1]->ref_index == 125);
        CHECK(int_member(disc[1]->payload, "line") == 20);

        const auto out  = events_of(t, "cue_out", v);
        const auto cont = events_of(t, "cue_out_cont", v);
        const auto cin  = events_of(t, "cue_in", v);
        const auto brk  = events_of(t, "ad_break", v);
        REQUIRE(out.size() == 1);
        REQUIRE(cont.size() == 1);
        REQUIRE(cin.size() == 1);
        REQUIRE(brk.size() == 1);
        CHECK(out[0]->ref_index == 75);
        CHECK_FALSE(out[0]->ref_end);
        CHECK(cont[0]->ref_index == 100);
        CHECK(cin[0]->ref_index == 125);
        CHECK(brk[0]->ref_index == 75);
        CHECK(brk[0]->ref_end == 125);
        CHECK(brk[0]->segment == 3);
        CHECK(text_member(brk[0]->payload, "end") == "cue_in");

        const auto keys = events_of(t, "keyframe", v);
        REQUIRE(keys.size() == 8);
        for (std::int64_t k = 0; k < 8; ++k) {
            const Event& e = *keys[static_cast<std::size_t>(k)];
            CHECK(e.source == Source::Decoder);
            CHECK(e.ref_index == 25 * k);
            CHECK(e.segment == k);
            CHECK(int_member(e.payload, "index") == 25 * k);
        }
    }
    // The first pair of each libvmaf context but the first (D-037), on the
    // pair's distorted variant.
    for (int pair = 0; pair < 2; ++pair) {
        const auto starts = events_of(t, "context_start", pair + 1);
        REQUIRE(starts.size() == 1);
        CHECK(starts[0]->source == Source::Vmaf);
        CHECK(starts[0]->ref_index == 75);
        CHECK_FALSE(starts[0]->segment);
        CHECK(int_member(starts[0]->payload, "context") == 1);
        CHECK(int_member(starts[0]->payload, "first_index") == 75);
    }
    CHECK(events_of(t, "context_start", 0).empty());

    // Every event has the media key of its frame (D-073): the cue-out the
    // second report sequence's first, at the main content's origin again.
    for (const Event& e : t.events) {
        INFO(e.id);
        REQUIRE(e.ref_index >= 0);
        REQUIRE(e.ref_index < kFrames);
        CHECK(e.key.sequence == t.axis.keys[static_cast<std::size_t>(e.ref_index)].sequence);
        CHECK(e.key.pts == t.axis.keys[static_cast<std::size_t>(e.ref_index)].pts);
        CHECK_FALSE(e.wallclock_us);
        CHECK(e.session == -1);
    }
    const Event& cue = *events_of(t, "cue_out", 2)[0];
    CHECK(cue.key.sequence == 1);
    CHECK(cue.key.pts == kOrigin);
    CHECK(cue.id == "scte35/cue_out/2/0");

    // Canonical order with the index built (design §4.7).
    CHECK(std::is_sorted(t.events.begin(), t.events.end(), timeline::canonical_before));
    CHECK(t.max_end.size() == t.events.size());
}

TEST_CASE("timeline::build: the same document from the same inputs twice, and the reader reads it "
          "back", "[timeline][timeline_build]")
{
    const timeline::BuildInputs in = ad_inputs();
    const timeline::Timeline    a  = built(in);
    const timeline::Timeline    b  = built(in);
    const std::string text = timeline::to_json(a, {}, doc_info(in.scores, a));
    CHECK(timeline::to_json(b, {}, doc_info(in.scores, b)) == text);

    // The build's timeline meets the writer's preconditions
    // (src/TimelineDoc.h): the reader takes the document as it is written.
    timeline::Timeline       back;
    std::vector<Attribution> attributions;
    timeline::DocInfo        info;
    std::string              err;
    REQUIRE(timeline::from_json(text, back, attributions, info, err));
    CHECK(err.empty());
    REQUIRE(back.events.size() == a.events.size());
    for (std::size_t i = 0; i < a.events.size(); ++i) {
        CHECK(back.events[i].id == a.events[i].id);
        CHECK(back.events[i].ref_index == a.events[i].ref_index);
        CHECK(back.events[i].ref_end == a.events[i].ref_end);
    }
    CHECK(back.variants.size() == 3);
    CHECK(back.curves.empty());   // the caller's, from the report (attach_curves())
}

// ── Placement through a pair ─────────────────────────────────────────────────

TEST_CASE("timeline::build: a distorted input on a coarser grid, placed through its pair, its "
          "unpaired frames at the next paired frame", "[timeline][timeline_build]")
{
    // A reference of 20 frames at 25 fps in two segments of 10, and a
    // distorted input at 12.5 fps whose frame i pairs with reference frame
    // 2i, but for frame 3, which repeats the timestamp before it and fails,
    // so that reference frame 6 fails too, and frame 10, after the
    // reference's end. Its segments: frames 0 to 4 and 5 to 10.
    timeline::LadderScores s;
    s.status = "complete";
    s.reference_mode = "top-rung";
    for (int k = 0; k < 2; ++k) {
        timeline::MasterEntry e;
        e.index     = static_cast<std::size_t>(k);
        e.uri       = k == 0 ? "ref/index.m3u8" : "dis/index.m3u8";
        e.url       = "http://test.invalid/" + e.uri;
        e.bandwidth = k == 0 ? 2000000 : 1000000;
        e.video     = true;
        s.entries.push_back(e);
    }
    s.reference_url     = s.entries[0].url;
    s.reference_entries = { 0 };
    // The plan's pair 2, as if pairs 0 and 1 had failed: the build's
    // warnings name it by that number, the report's pairs[].index, as the
    // axis's do, though it is the first complete pair (variant 1, curve 0).
    timeline::PairScores p;
    p.index         = 2;
    p.entries       = { 1 };
    p.ref_url       = s.entries[0].url;
    p.dis_url       = s.entries[1].url;
    p.ref_time_base = kTs;
    p.dis_time_base = kTs;
    p.ref_rate      = { 25, 1 };
    p.dis_rate      = { 25, 2 };
    for (std::int64_t d : { 0, 1, 2, 4, 5, 6, 7, 8, 9 }) {
        p.dis_index.push_back(d);
        p.ref_index.push_back(2 * d);
        p.dis_pts.push_back(2 * d * kFrame25);
        p.ref_pts.push_back(2 * d * kFrame25);
        p.vmaf.push_back(90.0);
    }
    p.context_first = { 0, 3 };   // libvmaf starts again after the failed reference frame
    p.failures      = { { .input = "reference", .index = 6, .pts = 6 * kFrame25, .reason = "unmatched_reference" },
                        { .input = "distorted", .index = 3, .pts = 6 * kFrame25, .reason = "duplicate_distorted" },
                        { .input = "distorted", .index = 10, .pts = 20 * kFrame25, .reason = "extra_distorted" } };
    p.ref_sequences = { { .first_index = 0, .first_pts = 0, .frames = 20 } };
    p.dis_sequences = { { .first_index = 0, .first_pts = 0, .frames = 11 } };
    s.pairs.push_back(p);

    timeline::BuildInputs in;
    in.scores = s;
    timeline::VariantRead ref, dis;
    ref.playlist = timeline::parse_playlist_events(
        "#EXTM3U\n#EXT-X-TARGETDURATION:1\n#EXT-X-PLAYLIST-TYPE:VOD\n#EXTINF:0.4,\nr0.ts\n#EXTINF:0.4,\n"
        "r1.ts\n#EXT-X-ENDLIST\n", s.entries[0].url, 0);
    dis.playlist = timeline::parse_playlist_events(
        "#EXTM3U\n#EXT-X-TARGETDURATION:1\n#EXT-X-PLAYLIST-TYPE:VOD\n#EXTINF:0.4,\nd0.ts\n#EXTINF:0.48,\n"
        "d1.ts\n#EXT-X-ENDLIST\n", s.entries[1].url, 1);
    REQUIRE(ref.playlist->error.empty());
    REQUIRE(dis.playlist->error.empty());
    ref.media.segments  = { { .seq = 0, .first_index = 0, .frames = 10, .bytes = 1000 },
                            { .seq = 1, .first_index = 10, .frames = 10, .bytes = 1000 } };
    ref.media.keyframes = { 0, 10 };
    for (std::int64_t i = 0; i < 20; ++i) ref.media.pts.push_back(i * kFrame25);
    dis.media.segments  = { { .seq = 0, .first_index = 0, .frames = 5, .bytes = 500 },
                            { .seq = 1, .first_index = 5, .frames = 6, .bytes = 600 } };
    dis.media.keyframes = { 0, 3, 5, 10 };
    for (std::int64_t i = 0; i < 11; ++i) dis.media.pts.push_back(2 * i * kFrame25);
    in.variants = { ref, dis };

    const timeline::Timeline t = built(in);
    CHECK(t.axis.frames == 20);
    CHECK(t.axis.clock == "playlist-time");

    // The reference's segments over their own frames; the distorted input's
    // from its first paired frame's reference frame to the next paired
    // frame's, the last to one past its last paired frame's.
    const auto ref_segments = events_of(t, "segment", 0);
    REQUIRE(ref_segments.size() == 2);
    CHECK((ref_segments[0]->ref_index == 0 && ref_segments[0]->ref_end == 10));
    CHECK((ref_segments[1]->ref_index == 10 && ref_segments[1]->ref_end == 20));
    const auto dis_segments = events_of(t, "segment", 1);
    REQUIRE(dis_segments.size() == 2);
    CHECK((dis_segments[0]->ref_index == 0 && dis_segments[0]->ref_end == 10));
    CHECK((dis_segments[1]->ref_index == 10 && dis_segments[1]->ref_end == 19));

    // Keyframes at their own frames: the unpaired frame 3's at frame 4's
    // reference frame, 8; frame 10's, after the last paired frame, left out.
    const auto keys = events_of(t, "keyframe", 1);
    REQUIRE(keys.size() == 3);
    CHECK((keys[0]->ref_index == 0 && keys[0]->segment == 0 && int_member(keys[0]->payload, "index") == 0));
    CHECK((keys[1]->ref_index == 8 && keys[1]->segment == 0 && int_member(keys[1]->payload, "index") == 3));
    CHECK((keys[2]->ref_index == 10 && keys[2]->segment == 1 && int_member(keys[2]->payload, "index") == 5));
    CHECK(any_warning(t, "variant 1 (http://test.invalid/dis/index.m3u8): 1 keyframe after its input's "
                         "last paired frame, the first at frame 10 of its input, is left out (design §5.1)"));

    // The unpaired frames (D-081 (d)): the reference's at itself, the
    // distorted input's at the next paired frame, the last one left out.
    const auto unpaired = events_of(t, "unpaired_frame", 1);
    REQUIRE(unpaired.size() == 2);
    CHECK(unpaired[0]->ref_index == 6);
    CHECK(text_member(unpaired[0]->payload, "input") == "reference");
    CHECK(int_member(unpaired[0]->payload, "index") == 6);
    CHECK(text_member(unpaired[0]->payload, "reason") == "unmatched_reference");
    const Value* pts = member(unpaired[0]->payload, "pts");
    REQUIRE(pts);
    REQUIRE(std::holds_alternative<double>(pts->v));
    CHECK(std::get<double>(pts->v) == 0.24);
    CHECK(unpaired[1]->ref_index == 8);
    CHECK(text_member(unpaired[1]->payload, "input") == "distorted");
    CHECK(int_member(unpaired[1]->payload, "index") == 3);
    CHECK(any_warning(t, "pair 2: 1 unpaired frame of its distorted input after its last paired frame, "
                         "the first at frame 10, is left out (D-081 (d))"));
    CHECK(t.variants[1].pair == 0);   // its position among the complete pairs, until D-092 (b)'s package

    // The second libvmaf context starts at the pair whose frames are 4 and 8.
    const auto starts = events_of(t, "context_start", 1);
    REQUIRE(starts.size() == 1);
    CHECK(starts[0]->ref_index == 8);
    CHECK(int_member(starts[0]->payload, "first_index") == 3);
    CHECK(t.warnings.size() == 2);
}

// ── What can't be placed ─────────────────────────────────────────────────────

TEST_CASE("timeline::build: a refused variant keeps none of its segment-bound events, keyframes or "
          "corrupt packets (D-081 (c), D-086)", "[timeline][timeline_build]")
{
    timeline::BuildInputs in = ad_inputs();
    in.variants[2].media = timeline::VariantMedia{ .refused = "the PTS of frame 40 isn't the report's" };
    const timeline::Timeline t = built(in);

    CHECK(t.variants[2].segment_map == "refused: the PTS of frame 40 isn't the report's");
    for (const Event& e : t.events)
        if (e.variant == 2) CHECK(e.source == Source::Vmaf);
    CHECK(events_of(t, "context_start", 2).size() == 1);
    CHECK(events_of(t, "ad_break", 0).size() == 1);
    CHECK(events_of(t, "ad_break", 1).size() == 1);
    CHECK(events_of(t, "ad_break", 2).empty());
    REQUIRE(t.warnings.size() == 1);
    CHECK(t.warnings[0] == "variant 2 (" + url_of(2) +
                               "): no segment map, the PTS of frame 40 isn't the report's: its "
                               "segment-bound events, keyframes and corrupt packets are left out "
                               "(D-081 (c), D-086)");

    // A refused reference: the axis falls back to the frame grid (D-073), and
    // the reference's own events are left out the same way.
    timeline::BuildInputs r = ad_inputs();
    r.variants[0].media = timeline::VariantMedia{ .refused = "a scored frame the pass doesn't have" };
    const timeline::Timeline u = built(r);
    CHECK(u.axis.clock == "frame-grid");
    CHECK(events_of(u, "segment", 0).empty());
    CHECK(events_of(u, "segment", 1).size() == 8);
    CHECK(u.variants[0].segment_map == "refused: a scored frame the pass doesn't have");
}

TEST_CASE("timeline::build: a segment none of whose frames is paired, tags after the last URI, an "
          "SCTE-35 message, a cue_out without a break and a playlist's problems are warnings",
          "[timeline][timeline_build]")
{
    // Every rung's playlist: a PROGRAM-DATE-TIME that doesn't parse, a bare
    // cue-out with no cue-in, an OATCLS message, and a vendor tag after the
    // last URI.
    const std::string playlist =
        "#EXTM3U\n"
        "#EXT-X-VERSION:3\n"
        "#EXT-X-TARGETDURATION:1\n"
        "#EXT-X-MEDIA-SEQUENCE:0\n"
        "#EXT-X-PLAYLIST-TYPE:VOD\n"
        "#EXT-X-PROGRAM-DATE-TIME:not a date\n"   // line 6
        "#EXTINF:1.000000,\nmain_000.ts\n"
        "#EXTINF:1.000000,\nmain_001.ts\n"
        "#EXTINF:1.000000,\nmain_002.ts\n"
        "#EXT-X-DISCONTINUITY\n"                  // line 13
        "#EXT-X-CUE-OUT\n"                        // line 14
        "#EXT-OATCLS-SCTE35:/DAlAAAAAAAAAP/wFAUAAAABf+/+AAAAAH4AKTLgAAEAAAAAAAAAMQ8EAA==\n"   // line 15
        "#EXTINF:1.000000,\nad_000.ts\n"
        "#EXTINF:1.000000,\nad_001.ts\n"
        "#EXT-X-DISCONTINUITY\n"
        "#EXTINF:1.000000,\nmain_005.ts\n"
        "#EXTINF:1.000000,\nmain_006.ts\n"
        "#EXTINF:1.000000,\nmain_007.ts\n"
        "#EXT-X-VENDOR-NOTE:after the last URI\n"   // line 27
        "#EXT-X-ENDLIST\n";
    timeline::BuildInputs in = ad_inputs(playlist);

    // Pair 0 pairs none of v270's segment 1, frames 25 to 49, whose
    // reference frames no other variant of the pair holds; a frame of each
    // input there failed to pair.
    timeline::PairScores& p = in.scores.pairs[0];
    const auto drop_range = [](auto& v) { v.erase(v.begin() + 25, v.begin() + 50); };
    drop_range(p.ref_index);
    drop_range(p.dis_index);
    drop_range(p.ref_pts);
    drop_range(p.dis_pts);
    drop_range(p.vmaf);
    p.context_first = { 0, 50 };
    p.failures = { { .input = "reference", .index = 30, .pts = ref_pts(30), .reason = "unmatched_reference" },
                   { .input = "distorted", .index = 30, .pts = ref_pts(30), .reason = "extra_distorted" } };

    const timeline::Timeline t = built(in);
    INFO(warnings_text(t));

    // The segment that isn't placed, its keyframe at the next paired frame,
    // and the segment before it up to that frame (D-081 (g)).
    const auto v270 = events_of(t, "segment", 1);
    REQUIRE(v270.size() == 7);
    CHECK((v270[0]->segment == 0 && v270[0]->ref_index == 0 && v270[0]->ref_end == 50));
    CHECK((v270[1]->segment == 2 && v270[1]->ref_index == 50 && v270[1]->ref_end == 75));
    CHECK(any_warning(t, "variant 1 (" + url_of(1) + "): segment 1 (main_001.ts) isn't placed, none of "
                         "its frames is paired: it is left out, and it holds no tag (design §5.1)"));
    const auto keys = events_of(t, "keyframe", 1);
    REQUIRE(keys.size() == 8);
    CHECK((keys[1]->ref_index == 50 && keys[1]->segment == 1 && int_member(keys[1]->payload, "index") == 25));
    CHECK(keys[2]->ref_index == 50);
    // The unpaired frames: the reference's at itself, the distorted one's
    // at the next paired frame; the context after the gap where it starts.
    const auto unpaired = events_of(t, "unpaired_frame", 1);
    REQUIRE(unpaired.size() == 2);
    CHECK(unpaired[0]->ref_index == 30);
    CHECK(unpaired[1]->ref_index == 50);
    const auto starts = events_of(t, "context_start", 1);
    REQUIRE(starts.size() == 1);
    CHECK(starts[0]->ref_index == 75);
    CHECK(int_member(starts[0]->payload, "first_index") == 50);

    for (int v = 0; v < 3; ++v) {
        INFO("variant " << v);
        const std::string name = std::format("variant {} ({})", v, url_of(v));
        // The tag after the last URI (design §5.1).
        CHECK(any_warning(t, name + ": the tag at line 27 follows the last segment's URI, so no segment "
                                    "holds it: it is left out (design §5.1)"));
        // The undecoded message (design §5.5).
        CHECK(any_warning(t, name + ": its playlist carries 1 SCTE-35 message, the first at line 15 "
                                    "(OATCLS), which this build doesn't decode, so they make no event "
                                    "(design §5.5)"));
        // hls::parse_media_timeline's problem.
        CHECK(any_warning(t, name + ": its playlist: line 6: PROGRAM-DATE-TIME"));
        // The cue_out is placed; it makes no break (pair_breaks()).
        REQUIRE(events_of(t, "cue_out", v).size() == 1);
        CHECK(events_of(t, "cue_out", v)[0]->ref_index == 75);
        CHECK(events_of(t, "ad_break", v).empty());
        CHECK(any_warning(t, std::format("variant {}: no ad_break for the cue_out at line 14", v)));
        // The PDT that doesn't parse is still an event of its segment.
        REQUIRE(events_of(t, "program_date_time", v).size() == 1);
        CHECK(events_of(t, "program_date_time", v)[0]->ref_index == 0);
        CHECK(events_of(t, "tag", v).empty());
    }
}

TEST_CASE("timeline::build: a segment that isn't placed is named with each tag it holds, by type "
          "and line (design §5.1)", "[timeline][timeline_build]")
{
    // On the ad ladder, pair 1 pairs none of v180's segment 3, frames 75 to
    // 99, which holds the discontinuity at line 13 and the cue_out at line
    // 14, and pair 0 none of v270's segment 4, frames 100 to 124, which
    // holds the cue_out_cont at line 17.
    timeline::BuildInputs in = ad_inputs();
    const auto unpair = [](timeline::PairScores& p, std::ptrdiff_t from, std::ptrdiff_t to) {
        const auto cut = [&](auto& v) { v.erase(v.begin() + from, v.begin() + to); };
        cut(p.ref_index);
        cut(p.dis_index);
        cut(p.ref_pts);
        cut(p.dis_pts);
        cut(p.vmaf);
    };
    unpair(in.scores.pairs[1], 75, 100);
    unpair(in.scores.pairs[0], 100, 125);
    const timeline::Timeline t = built(in);
    INFO(warnings_text(t));

    CHECK(any_warning(t, "variant 2 (" + url_of(2) + "): segment 3 (ad_000.ts) isn't placed, none of its "
                         "frames is paired: it is left out, and so are the 2 tags it holds, the "
                         "discontinuity at line 13 and the cue_out at line 14 (design §5.1)"));
    CHECK(any_warning(t, "variant 1 (" + url_of(1) + "): segment 4 (ad_001.ts) isn't placed, none of its "
                         "frames is paired: it is left out, and so is the tag it holds, the cue_out_cont "
                         "at line 17 (design §5.1)"));
    // Its cue_out takes its turn in the pairing unplaced, and makes no break,
    // which pair_breaks() names too.
    CHECK(any_warning(t, "variant 2: no ad_break for the cue_out at line 14: it has no reference frame "
                         "(design §5.1)"));
    CHECK(events_of(t, "cue_out", 2).empty());
    CHECK(events_of(t, "ad_break", 2).empty());
    CHECK(events_of(t, "discontinuity", 2).size() == 1);
    // The segment before each spans the gap, to the next paired frame
    // (D-081 (g)).
    const auto v180 = events_of(t, "segment", 2);
    REQUIRE(v180.size() == 7);
    CHECK((v180[2]->segment == 2 && v180[2]->ref_index == 50 && v180[2]->ref_end == 100));
    const auto v270 = events_of(t, "segment", 1);
    REQUIRE(v270.size() == 7);
    CHECK((v270[3]->segment == 3 && v270[3]->ref_index == 75 && v270[3]->ref_end == 125));
    CHECK(t.warnings.size() == 3);
}

TEST_CASE("timeline::build: a libvmaf context that can't be placed is a warning, not left out "
          "silently (design §5.1)", "[timeline][timeline_build]")
{
    // Scores built by hand, as read_ladder_report() refuses them: pair 1's
    // third context starts past its 200 scored frames, and pair 0's last
    // scored frame, where its third context starts, sits at reference frame
    // 250, off the axis's 200, which build_axis() warns of. The plan's
    // numbers of the two pairs are 3 and 4: the warnings give those.
    timeline::BuildInputs in = ad_inputs();
    in.scores.pairs[0].index         = 3;
    in.scores.pairs[1].index         = 4;
    in.scores.pairs[0].ref_index.back() = 250;
    in.scores.pairs[0].context_first = { 0, 75, 199 };
    in.scores.pairs[1].context_first = { 0, 75, 200 };
    const timeline::Timeline t = built(in);
    INFO(warnings_text(t));

    CHECK(any_warning(t, "pair 4: its libvmaf context 2 starts at frame pair 200, which isn't one of its "
                         "200 scored frames: its context_start is left out (design §5.1)"));
    CHECK(any_warning(t, "pair 3: its libvmaf context 2 starts at frame pair 199, at reference frame 250, "
                         "which isn't one of the axis's 200 frames: its context_start is left out "
                         "(design §5.1)"));
    CHECK(std::any_of(t.axis.warnings.begin(), t.axis.warnings.end(),
                      [](const std::string& w) { return contains(w, "pair 3 has 1 scored frame outside"); }));
    // The contexts that can be placed still are, at the ad's first frame.
    for (int v = 1; v < 3; ++v) {
        const auto starts = events_of(t, "context_start", v);
        REQUIRE(starts.size() == 1);
        CHECK(starts[0]->ref_index == 75);
    }
}

TEST_CASE("timeline::build: a cue_out without a cue_in ends its ad_break at its declared duration on "
          "the axis (D-076, D-087)", "[timeline][timeline_build]")
{
    // testgen's ad playlist without its EXT-X-CUE-IN: the cue_out at
    // reference frame 75, 3 s on the reference's playlist time, declares
    // 2 s, which end at the first frame from it at or after 5 s, 125: the
    // build binds Axis::frame_after() to pair_breaks()'s FrameAfter.
    std::string playlist = kAdPlaylist;
    const std::string cue_in = "#EXT-X-CUE-IN\n";
    playlist.erase(playlist.find(cue_in), cue_in.size());
    const timeline::Timeline t = built(ad_inputs(playlist));
    CHECK(t.warnings.empty());
    for (int v = 0; v < 3; ++v) {
        INFO("variant " << v);
        CHECK(events_of(t, "cue_in", v).empty());
        const auto brk = events_of(t, "ad_break", v);
        REQUIRE(brk.size() == 1);
        CHECK(brk[0]->ref_index == 75);
        CHECK(brk[0]->ref_end == 125);
        CHECK(text_member(brk[0]->payload, "end") == "duration");
        const Value* in = member(brk[0]->payload, "cue_in_line");
        REQUIRE(in);
        CHECK(std::holds_alternative<std::monostate>(in->v));
        const Value* durations = member(brk[0]->payload, "durations");
        REQUIRE(durations);
        REQUIRE(std::holds_alternative<Value::Object>(durations->v));
        const Value* declared = member(std::get<Value::Object>(durations->v), "duration");
        REQUIRE(declared);
        CHECK(timeline::number_of(*declared) == 2.0);
    }

    // One that runs past the reference's end, 3 s + 10 s against its 8 s,
    // ends at the axis's end, its frame count.
    std::string longer = playlist;
    const std::string cue_out = "#EXT-X-CUE-OUT:2.000\n";
    longer.replace(longer.find(cue_out), cue_out.size(), "#EXT-X-CUE-OUT:10.000\n");
    const timeline::Timeline u = built(ad_inputs(longer));
    CHECK(u.warnings.empty());
    for (int v = 0; v < 3; ++v) {
        INFO("variant " << v);
        const auto brk = events_of(u, "ad_break", v);
        REQUIRE(brk.size() == 1);
        CHECK(brk[0]->ref_index == 75);
        CHECK(brk[0]->ref_end == kFrames);
        CHECK(text_member(brk[0]->payload, "end") == "duration");
    }
}

TEST_CASE("timeline::build: corrupt packets at their own frames, each with the boundary after it "
          "(D-081 (h)), and a reference that isn't a playlist", "[timeline][timeline_build]")
{
    // Unmarked continuity counters at the ad's splices (design §5.2): flags
    // a frame and three frames before the boundaries at 75 and 125, and one
    // in the last segment.
    timeline::BuildInputs in = ad_inputs();
    in.variants[2].media.corrupt = { 74, 122, 190 };
    const timeline::Timeline t = built(in);
    const auto corrupt = events_of(t, "corrupt_packet", 2);
    REQUIRE(corrupt.size() == 3);
    const auto boundary = [](const Event& e) -> const Value::Object* {
        const Value* b = member(e.payload, "next_boundary");
        return b && std::holds_alternative<Value::Object>(b->v) ? &std::get<Value::Object>(b->v) : nullptr;
    };
    const std::int64_t at[] = { 74, 122 }, next[] = { 3, 5 }, first[] = { 75, 125 }, segment[] = { 2, 4 };
    for (std::size_t k = 0; k < 2; ++k) {
        INFO("corrupt packet " << k);
        CHECK(corrupt[k]->source == Source::Decoder);
        CHECK(corrupt[k]->ref_index == at[k]);
        CHECK(corrupt[k]->segment == segment[k]);
        CHECK(int_member(corrupt[k]->payload, "index") == at[k]);
        const Value::Object* b = boundary(*corrupt[k]);
        REQUIRE(b);
        CHECK(int_member(*b, "media_sequence") == next[k]);
        CHECK(int_member(*b, "index") == first[k]);
        CHECK(int_member(*b, "ref_index") == first[k]);
        const Value* disc = member(*b, "discontinuity");
        REQUIRE(disc);
        CHECK(std::get<bool>(disc->v));
    }
    CHECK(corrupt[2]->ref_index == 190);
    const Value* last = member(corrupt[2]->payload, "next_boundary");
    REQUIRE(last);
    CHECK(std::holds_alternative<std::monostate>(last->v));

    // The build writes next_boundary()'s rule (src/TimelineMedia.h) again,
    // since the core can't call the adapter: at every frame of every
    // variant, a corrupt packet names the boundary next_boundary() gives,
    // so that the two can't part unnoticed until the rule has one place.
    timeline::BuildInputs every = ad_inputs();
    for (timeline::VariantRead& r : every.variants)
        for (std::int64_t i = 0; i < kFrames; ++i) r.media.corrupt.push_back(i);
    const timeline::Timeline flagged = built(every);
    std::size_t compared = 0;
    for (int v = 0; v < 3; ++v) {
        const timeline::VariantRead& r = every.variants[static_cast<std::size_t>(v)];
        for (const Event* c : events_of(flagged, "corrupt_packet", v)) {
            const std::optional<std::int64_t> frame = int_member(c->payload, "index");
            REQUIRE(frame);
            INFO("variant " << v << ", frame " << *frame);
            const std::optional<timeline::SegmentBoundary> want =
                timeline::next_boundary(r.media, r.playlist->timeline, *frame);
            const Value::Object* got = boundary(*c);
            REQUIRE(static_cast<bool>(got) == want.has_value());
            ++compared;
            if (!want) continue;
            CHECK(int_member(*got, "media_sequence") == want->seq);
            CHECK(int_member(*got, "index") == want->first_index);
            const Value* disc = member(*got, "discontinuity");
            REQUIRE(disc);
            CHECK(std::get<bool>(disc->v) == want->discontinuity);
        }
    }
    CHECK(compared == 3 * static_cast<std::size_t>(kFrames));

    // A mezzanine, D-002's alternative: no playlist, no segments, its
    // keyframes and corrupt packets at their frames, no boundary to name;
    // no entries, so no BANDWIDTH and no rank.
    timeline::BuildInputs m = ad_inputs();
    m.scores.reference_mode    = "mezzanine";
    m.scores.reference_url     = "http://test.invalid/mezzanine.ts";
    m.scores.reference_entries = {};
    for (timeline::PairScores& p : m.scores.pairs) p.ref_url = m.scores.reference_url;
    m.variants[0].playlist.reset();
    m.variants[0].playlist_location.clear();
    m.variants[0].playlist_sha256.clear();
    m.variants[0].media.segments.clear();
    m.variants[0].media.keyframes = { 0, 100 };
    m.variants[0].media.corrupt   = { 50 };
    const timeline::Timeline z = built(m);
    CHECK(z.axis.clock == "frame-grid");
    CHECK(z.variants[0].entries.empty());
    CHECK(z.variants[0].bandwidth == 0);
    CHECK(z.variants[0].rank_from_top == 0);
    CHECK(z.variants[0].rank_from_bottom == 0);
    CHECK(z.variants[0].url == "http://test.invalid/mezzanine.ts");
    CHECK(events_of(z, "segment", 0).empty());
    const auto keys = events_of(z, "keyframe", 0);
    REQUIRE(keys.size() == 2);
    CHECK((keys[0]->ref_index == 0 && !keys[0]->segment));
    CHECK((keys[1]->ref_index == 100 && !keys[1]->segment));
    const auto mezz_corrupt = events_of(z, "corrupt_packet", 0);
    REQUIRE(mezz_corrupt.size() == 1);
    const Value* none = member(mezz_corrupt[0]->payload, "next_boundary");
    REQUIRE(none);
    CHECK(std::holds_alternative<std::monostate>(none->v));
    // The rungs keep their ranks among the master's three.
    CHECK(z.variants[1].rank_from_top == 2);
    CHECK(z.variants[2].rank_from_bottom == 1);
}

// ── Variants ─────────────────────────────────────────────────────────────────

TEST_CASE("timeline::build: ranks among the master's video playlists, entries sharing a playlist, "
          "equal BANDWIDTHs (D-075, D-081 (e))", "[timeline][timeline_build]")
{
    // A master of seven entries: A the top rung; B named twice, its second
    // entry's BANDWIDTH the higher, with its own RESOLUTION and CODECS; C and
    // E of equal BANDWIDTH; F, a video playlist whose pair didn't complete,
    // so the report has no pair of it; and an entry without video.
    struct Row {
        const char*  url;
        std::int64_t bandwidth;
        const char*  resolution;
        const char*  codecs;
        bool         video;
    };
    const Row rows[] = {
        { "A", 3000000, "1280x720", "avc1.640020", true }, { "B", 2000000, "960x540", "avc1.4d401f", true },
        { "B", 2500000, "960x544", "avc1.640028", true },  { "C", 1000000, "640x360", "avc1.42c01e", true },
        { "D", 64000, nullptr, "mp4a.40.2", false },       { "E", 1000000, "640x360", "avc1.42c01e", true },
        { "F", 1500000, "800x450", "avc1.4d401f", true },
    };
    timeline::LadderScores s;
    s.status = "complete";
    s.reference_mode = "top-rung";
    for (std::size_t k = 0; k < std::size(rows); ++k) {
        timeline::MasterEntry e;
        e.index      = k;
        e.uri        = rows[k].url;
        e.url        = std::string("http://test.invalid/") + rows[k].url;
        e.bandwidth  = rows[k].bandwidth;
        if (rows[k].resolution) e.resolution = rows[k].resolution;
        e.codecs     = rows[k].codecs;
        e.video      = rows[k].video;
        s.entries.push_back(e);
    }
    s.reference_url     = s.entries[0].url;
    s.reference_entries = { 0 };
    const std::vector<std::vector<std::size_t>> pair_entries = { { 1, 2 }, { 3 }, { 5 } };
    for (std::size_t k = 0; k < pair_entries.size(); ++k) {
        timeline::PairScores p;
        p.index         = k;
        p.entries       = pair_entries[k];
        p.ref_url       = s.reference_url;
        p.dis_url       = s.entries[pair_entries[k][0]].url;
        p.ref_time_base = kTs;
        p.dis_time_base = kTs;
        p.ref_rate      = { 25, 1 };
        p.dis_rate      = { 25, 1 };
        for (std::int64_t i = 0; i < 2; ++i) {
            p.ref_index.push_back(i);
            p.dis_index.push_back(i);
            p.ref_pts.push_back(i * kFrame25);
            p.dis_pts.push_back(i * kFrame25);
            p.vmaf.push_back(95.0);
        }
        p.context_first = { 0 };
        p.ref_sequences = { { .first_index = 0, .first_pts = 0, .frames = 2 } };
        p.dis_sequences = p.ref_sequences;
        s.pairs.push_back(std::move(p));
    }
    timeline::BuildInputs in;
    in.scores = s;
    in.variants.resize(4);   // no playlists, nothing demuxed: only the variants are looked at
    const timeline::Timeline t = built(in);
    REQUIRE(t.variants.size() == 4);

    // The rungs are A, B, C, E and F: from the top A 1, B 2, F 3, C and E 4;
    // from the bottom C and E 1, F 3, B 4, A 5.
    struct Want {
        std::int64_t bandwidth;
        const char*  resolution;
        const char*  codecs;
        int          top, bottom;
    };
    const Want want[] = {
        { 3000000, "1280x720", "avc1.640020", 1, 5 },
        { 2500000, "960x544", "avc1.640028", 2, 4 },
        { 1000000, "640x360", "avc1.42c01e", 4, 1 },
        { 1000000, "640x360", "avc1.42c01e", 4, 1 },
    };
    for (std::size_t v = 0; v < 4; ++v) {
        INFO("variant " << v);
        const timeline::Variant& var = t.variants[v];
        CHECK(var.bandwidth == want[v].bandwidth);
        CHECK(var.resolution == want[v].resolution);
        CHECK(var.codecs == want[v].codecs);
        CHECK(var.rank_from_top == want[v].top);
        CHECK(var.rank_from_bottom == want[v].bottom);
    }
    CHECK(t.variants[1].entries == std::vector<std::size_t>{ 1, 2 });
    CHECK(t.variants[1].url == "http://test.invalid/B");
    CHECK(t.variants[3].pair == 2);
    // Read from no playlist: no location or SHA-256, which a document writes
    // as a playlist of null (src/TimelineDoc.h).
    CHECK(t.variants[0].playlist_location.empty());
    CHECK(t.variants[0].playlist_sha256.empty());
}

TEST_CASE("timeline::build: inputs that don't fit together", "[timeline][timeline_build]")
{
    std::string err;
    timeline::BuildInputs few = ad_inputs();
    few.variants.pop_back();
    CHECK(timeline::build(few, err).events.empty());
    CHECK(err == "the timeline build was given 2 variants for a report of 2 complete pairs: it takes "
                 "the reference and one per complete pair");

    timeline::BuildInputs broken = ad_inputs();
    broken.variants[1].playlist = timeline::parse_playlist_events("not a playlist", url_of(1), 1);
    CHECK(timeline::build(broken, err).variants.empty());
    CHECK(contains(err, "variant 1's playlist wasn't read: "));

    timeline::BuildInputs other = ad_inputs();
    other.variants[2].playlist = timeline::parse_playlist_events(kAdPlaylist, url_of(2), 1);
    CHECK(timeline::build(other, err).variants.empty());
    CHECK(err == "variant 2's playlist was read for variant 1");

    (void)timeline::build(ad_inputs(), err);
    CHECK(err.empty());
}

// ── Curves and attributions ──────────────────────────────────────────────────

TEST_CASE("timeline::pair_curves, attach_curves and pair_curve_refs: the curves of a report",
          "[timeline][timeline_build]")
{
    const timeline::LadderScores s = ad_report();
    const std::vector<timeline::Curve> curves = timeline::pair_curves(s);
    REQUIRE(curves.size() == 2);
    for (std::size_t k = 0; k < 2; ++k) {
        CHECK(curves[k].variant == static_cast<int>(k) + 1);
        CHECK(curves[k].ref_index == s.pairs[k].ref_index);
        CHECK(curves[k].vmaf == s.pairs[k].vmaf);
        CHECK(curves[k].context_first == std::vector<std::int64_t>{ 0, 75 });
    }

    // A timeline read back from its document has no curves until the
    // report's are attached, each on the variant that names its pair.
    timeline::Timeline t = built(ad_inputs());
    t.curves.clear();
    std::swap(t.variants[1].pair, t.variants[2].pair);   // as another writer might number them
    std::string err;
    REQUIRE(timeline::attach_curves(t, s, err));
    CHECK(err.empty());
    REQUIRE(t.curves.size() == 2);
    CHECK(t.curves[0].variant == 2);
    CHECK(t.curves[1].variant == 1);
    CHECK(t.curves[1].vmaf == s.pairs[1].vmaf);
    const std::vector<CurveRef> refs = timeline::pair_curve_refs(t);
    REQUIRE(refs.size() == 2);
    CHECK((refs[0].kind == CurveRef::Kind::Pair && refs[0].index == 0 && refs[1].index == 1));

    timeline::Timeline beyond = built(ad_inputs());
    beyond.variants[2].pair = 5;
    CHECK_FALSE(timeline::attach_curves(beyond, s, err));
    CHECK(err == "the document's variant 2 names pair 5, but the ladder report has 2 complete pairs");
    CHECK(beyond.curves.size() == 2);   // unchanged

    timeline::Timeline twice = built(ad_inputs());
    twice.variants[2].pair = 0;
    CHECK_FALSE(timeline::attach_curves(twice, s, err));
    CHECK(err == "the document's variants 1 and 2 both name pair 0");

    timeline::Timeline missing = built(ad_inputs());
    missing.variants.pop_back();
    CHECK_FALSE(timeline::attach_curves(missing, s, err));
    CHECK(err == "the ladder report's complete pair 1 (its pairs[].index 1) is the pair of none of the "
                 "document's variants");
}

TEST_CASE("timeline::attribute_drops: each pair's drops with the events of its curve's inputs "
          "(D-077, D-078)", "[timeline][timeline_build]")
{
    const timeline::Timeline t = built(ad_inputs());
    timeline::DetectOptions options;
    options.window     = 50;
    options.min_points = 12;
    std::string err;
    const std::vector<Attribution> drops =
        timeline::attribute_drops(t, timeline::pair_curve_refs(t), options, {}, err);
    CHECK(err.empty());

    // v180's ad is pair 1's one drop; v270's dip at frame 75 is one point,
    // which N passes over.
    REQUIRE(drops.size() == 1);
    const Attribution& a = drops[0];
    CHECK(a.curve.index == 1);
    CHECK(a.detected);
    CHECK((a.interval.ref_index == 75 && a.interval.ref_end == 125));
    std::string why;
    const std::vector<timeline::Drop> d = timeline::detect(t.curves[1], options, why);
    REQUIRE(d.size() == 1);
    CHECK(a.baseline == d[0].baseline);
    CHECK(a.drop_magnitude == d[0].drop_magnitude);
    CHECK(a.mean_drop == d[0].mean_drop);
    CHECK(a.points == d[0].points);
    CHECK(a.min == d[0].min);
    CHECK(a.mean == d[0].mean);

    // Its inputs' events only (Q28 (a)): v180's and v360's, each rung's copy
    // (D-081 (f)), in source groups (D-078).
    CHECK(listed(t, a, Source::Vmaf) == std::vector<std::string>{ "vmaf/context_start/2/0" });
    CHECK(listed(t, a, Source::Scte35) ==
          std::vector<std::string>{ "scte35/cue_out/0/0", "scte35/cue_out/2/0", "scte35/ad_break/0/0",
                                    "scte35/ad_break/2/0", "scte35/cue_out_cont/0/0",
                                    "scte35/cue_out_cont/2/0" });
    CHECK(listed(t, a, Source::Playlist).size() == 6);   // two discontinuities, four segments
    CHECK(listed(t, a, Source::Decoder).size() == 4);    // the ad's segments' keyframes
    CHECK(a.by_source[static_cast<std::size_t>(Source::Scte35)][0].offset_frames == 0);

    // A margin before the drop reaches segment 2 too (Q29 (c)).
    timeline::QueryOptions margins;
    margins.margin_before = { 1, 25 };
    const std::vector<Attribution> wide =
        timeline::attribute_drops(t, { CurveRef{ .index = 1 } }, options, margins, err);
    REQUIRE(wide.size() == 1);
    CHECK((wide[0].searched.ref_index == 74 && wide[0].interval.ref_index == 75));
    CHECK(listed(t, wide[0], Source::Playlist).size() == 8);

    // Without W and N, nothing, and no error (D-077).
    CHECK(timeline::attribute_drops(t, timeline::pair_curve_refs(t), {}, {}, err).empty());
    CHECK(err.empty());
    timeline::DetectOptions lone;
    lone.window = 50;
    CHECK(timeline::attribute_drops(t, timeline::pair_curve_refs(t), lone, {}, err).empty());
    CHECK(err.empty());

    // What it refuses.
    timeline::DetectOptions bad = options;
    bad.threshold = -1;
    CHECK(timeline::attribute_drops(t, timeline::pair_curve_refs(t), bad, {}, err).empty());
    CHECK(contains(err, "threshold"));
    CHECK(timeline::attribute_drops(t, { CurveRef{ .index = 5 } }, options, {}, err).empty());
    CHECK(err == "the timeline has no curve pair:5: it has 2 pairs' curves");
    timeline::Timeline odd = t;
    odd.curves[0].vmaf.pop_back();
    CHECK(timeline::attribute_drops(odd, timeline::pair_curve_refs(odd), options, {}, err).empty());
    CHECK(err.starts_with("curve pair:0: "));
}
