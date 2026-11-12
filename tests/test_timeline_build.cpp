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
// with unpaired frames, a refused variant, its messages too, a segment none
// of whose frames is paired, named with the tags it holds, tags no segment
// holds, an SCTE-35 message that doesn't decode, a cue_out that makes no
// break, libvmaf contexts that can't be placed, ad breaks ended by their
// declared duration, corrupt packets with their boundaries, the same as the
// adapter's next_boundary() gives, a mezzanine reference, the ranks of a
// master with shared and equal BANDWIDTHs, inputs that don't fit, the
// curves read back from a report, a report whose pairs' numbers skip a
// failed pair (D-092 (b)), detection's attributions, and the same document
// from the same inputs twice. Then the SCTE-35 messages the playlists' tags
// carry, decoded in the build (design §5.5; D-106, D-109), in sections that
// tests/Scte35TestSections.h builds from field values (D-107): a message
// merged into its plain cue_out, a message-only cue_out and cue_in paired,
// splice points, a message and a cancel, their attribution and their
// document read back; a message whose CRC_32 fails kept or dropped; the cue
// types; a plain cue numbered before a decoded one of its frame and the
// messages' warnings between the variants' and the pairing's, as built while
// they wait for the owner; and a ladder without messages, the same whatever
// the options. Then a scripted session merged into the ad ladder's timeline
// (add_session(), add_requests(); D-068, D-104, D-105): its spans and its
// player and network events, the ladder's ids kept, a fetch without a UTC
// anchor, what it refuses, and its drops, its pairs' clipped to its spans
// (D-077).

#include <catch2/catch_test_macros.hpp>

#include "FileHash.h"
#include "LadderReportRead.h"
#include "PlaylistEvents.h"
#include "Scte35.h"
#include "Scte35Events.h"
#include "Scte35TestSections.h"
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

// The same, each variant with its own of `playlists`, [0] the reference's.
timeline::BuildInputs ad_inputs_of(const std::vector<std::string>& playlists)
{
    REQUIRE(playlists.size() == 3);
    timeline::BuildInputs in;
    in.scores = ad_report();
    for (int v = 0; v < 3; ++v)
        in.variants.push_back(read_of(playlists[static_cast<std::size_t>(v)], v, ad_media()));
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
    // warnings and its variant name it by that number, the report's
    // pairs[].index (D-092 (b)), as the axis's do, though it is the first
    // complete pair (variant 1, curve 0).
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
    CHECK(t.variants[1].pair == 2);   // the plan's number (D-092 (b))
    REQUIRE(t.curves.size() == 1);
    CHECK(t.curves[0].variant == 1);
    CHECK(timeline::pair_number(t, CurveRef{ .index = 0 }) == 2);

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

    // A refused variant's SCTE-35 messages make no event either, which one
    // warning of the decoder's says after the build's own (src/Scte35Events.h):
    // v180's splice_insert out after its plain EXT-X-CUE-OUT.
    const std::string carrying = scte35test::ad_playlist(
        {}, { { 3, scte35test::oatcls(scte35test::splice_insert({ .event_id = 1, .pts = ref_pts(75) })) } });
    timeline::BuildInputs m = ad_inputs_of({ kAdPlaylist, kAdPlaylist, carrying });
    m.variants[2].media = timeline::VariantMedia{ .refused = "the PTS of frame 40 isn't the report's" };
    const timeline::Timeline v = built(m);
    for (const Event& e : v.events)
        if (e.variant == 2) CHECK(e.source == Source::Vmaf);
    REQUIRE(v.warnings.size() == 2);
    CHECK(v.warnings[0] == t.warnings[0]);
    CHECK(v.warnings[1] == "variant 2 (" + url_of(2) +
                               "): no segment map, the PTS of frame 40 isn't the report's: its 1 SCTE-35 "
                               "message is left out (D-081 (c), D-086)");
}

TEST_CASE("timeline::build: a segment none of whose frames is paired, tags after the last URI, an "
          "SCTE-35 message that doesn't decode, a cue_out without a break and a playlist's problems are "
          "warnings", "[timeline][timeline_build]")
{
    // Every rung's playlist: a PROGRAM-DATE-TIME that doesn't parse, a bare
    // cue-out with no cue-in, an OATCLS message that doesn't decode, a
    // splice_insert's section cut after its first 12 bytes, and a vendor tag
    // after the last URI.
    scte35test::Bytes cut = scte35test::section(scte35test::splice_insert({ .event_id = 1, .pts = ref_pts(75) }));
    cut.resize(12);
    const std::string playlist =
        std::string("#EXTM3U\n"
                    "#EXT-X-VERSION:3\n"
                    "#EXT-X-TARGETDURATION:1\n"
                    "#EXT-X-MEDIA-SEQUENCE:0\n"
                    "#EXT-X-PLAYLIST-TYPE:VOD\n"
                    "#EXT-X-PROGRAM-DATE-TIME:not a date\n"   // line 6
                    "#EXTINF:1.000000,\nmain_000.ts\n"
                    "#EXTINF:1.000000,\nmain_001.ts\n"
                    "#EXTINF:1.000000,\nmain_002.ts\n"
                    "#EXT-X-DISCONTINUITY\n"                  // line 13
                    "#EXT-X-CUE-OUT\n") +                     // line 14
        "#EXT-OATCLS-SCTE35:" + scte35test::base64(cut) + "\n" +   // line 15
        "#EXTINF:1.000000,\nad_000.ts\n"
        "#EXTINF:1.000000,\nad_001.ts\n"
        "#EXT-X-DISCONTINUITY\n"
        "#EXTINF:1.000000,\nmain_005.ts\n"
        "#EXTINF:1.000000,\nmain_006.ts\n"
        "#EXTINF:1.000000,\nmain_007.ts\n"
        "#EXT-X-VENDOR-NOTE:after the last URI\n"   // line 27
        "#EXT-X-ENDLIST\n";
    const std::string why = scte35::decode(cut).error;
    REQUIRE_FALSE(why.empty());
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
        // The message that doesn't decode makes no event, and a warning says
        // why (design §5.1, §5.5).
        CHECK(any_warning(t, name + ": the OATCLS at line 15: it doesn't decode, " + why +
                                 ": it makes no event (design §5.5)"));
        CHECK(events_of(t, "splice_point", v).empty());
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

// ── Decoded SCTE-35 messages (design §5.5; D-106, D-109) ─────────────────────

namespace {

// A message's record in a plain cue's `messages` (src/Scte35Events.h), by
// its place there.
const timeline::Payload& message_record(const Event& cue, std::size_t k)
{
    const Value* list = member(cue.payload, "messages");
    REQUIRE(list);
    REQUIRE(std::holds_alternative<Value::List>(list->v));
    const Value::List& records = std::get<Value::List>(list->v);
    REQUIRE(k < records.size());
    REQUIRE(std::holds_alternative<Value::Object>(records[k].v));
    return std::get<Value::Object>(records[k].v);
}

std::optional<bool> flag_member(const timeline::Payload& p, std::string_view name)
{
    const Value* v = member(p, name);
    if (!v || !std::holds_alternative<bool>(v->v)) return std::nullopt;
    return std::get<bool>(v->v);
}

// The frames of a variant's events of one type, in the timeline's order.
std::vector<std::int64_t> frames_of(const timeline::Timeline& t, std::string_view type, int variant)
{
    std::vector<std::int64_t> out;
    for (const Event* e : events_of(t, type, variant)) out.push_back(e->ref_index);
    return out;
}

} // namespace

TEST_CASE("timeline::build: decoded SCTE-35 messages, one merged into its plain cue_out, a cue_out and a cue_in "
          "of messages alone paired, splice points, a message and a cancel (D-106)",
          "[timeline][timeline_build]")
{
    using scte35test::Segmentation;
    // The splice times of the ad's first frame, 75, and of the main
    // content's return, 125, the same PTS in every rung, and 2 s of breaks.
    const std::int64_t out_pts = ref_pts(75), in_pts = ref_pts(125), two_s = 180000;
    // v270: an OATCLS of a splice_insert out after the plain EXT-X-CUE-OUT of
    // segment 3, that cue's message (D-106 (d)).
    const scte35test::Section merged =
        scte35test::splice_insert({ .event_id = 7, .pts = out_pts, .duration = std::pair{ true, two_s } });
    const std::string v270 = scte35test::ad_playlist({}, { { 3, scte35test::oatcls(merged) } });
    // v180, without plain cue tags: an OATCLS of a splice_insert out before
    // segment 3 and one of its splice_insert in before segment 5.
    const scte35test::Section out =
        scte35test::splice_insert({ .event_id = 8, .pts = out_pts, .duration = std::pair{ true, two_s } });
    const scte35test::Section in = scte35test::splice_insert({ .event_id = 8, .out = false, .pts = in_pts });
    const std::string v180 = scte35test::ad_playlist(
        { { 3, scte35test::oatcls(out) }, { 5, scte35test::oatcls(in) } }, {}, false);
    // The reference, v360: a time_signal of a Program Start, which no cue
    // type is (D-106 (b)), before segment 1, and the cancellation of
    // splice_event_id 9 before segment 6.
    const scte35test::Section program =
        scte35test::time_signal(ref_pts(25), { Segmentation{ .event_id = 5, .type = 0x10 } });
    const scte35test::Section cancel = scte35test::splice_insert({ .event_id = 9, .cancel = true });
    const std::string v360 = scte35test::ad_playlist(
        { { 1, scte35test::oatcls(program) }, { 6, scte35test::oatcls(cancel) } });
    const timeline::BuildInputs inputs = ad_inputs_of({ v360, v270, v180 });
    const timeline::Timeline    t      = built(inputs);
    INFO(warnings_text(t));
    CHECK(t.warnings.empty());   // every message decodes, and every anchor is in its tag's segment

    // v270: one cue_out, the plain one at line 14, its message's record in
    // its payload, and the message's break_duration beside the tag's 2 s, in
    // seconds and in 90 kHz ticks (D-109 (d)); the plain pairing unchanged.
    const auto v270_out = events_of(t, "cue_out", 1);
    REQUIRE(v270_out.size() == 1);
    const Event& plain = *v270_out[0];
    CHECK(plain.ref_index == 75);
    CHECK(int_member(plain.payload, "line") == 14);
    CHECK(text_member(plain.payload, "form") == "CUE-OUT");
    const timeline::Payload& record = message_record(plain, 0);
    CHECK(int_member(record, "line") == scte35test::line_with(v270, "#EXT-OATCLS-SCTE35:"));
    CHECK(text_member(record, "form") == "OATCLS");
    CHECK(flag_member(record, "crc_valid") == true);
    REQUIRE(member(plain.payload, "break_duration"));
    CHECK(timeline::number_of(*member(plain.payload, "break_duration")) == 2.0);
    CHECK(int_member(plain.payload, "break_duration_ticks") == two_s);
    const auto v270_break = events_of(t, "ad_break", 1);
    REQUIRE(v270_break.size() == 1);
    CHECK((v270_break[0]->ref_index == 75 && v270_break[0]->ref_end == 125));
    CHECK(text_member(v270_break[0]->payload, "end") == "cue_in");

    // v180: a cue_out and a cue_in of the messages alone, paired by
    // pair_breaks() into an ad_break, ended by its cue_in (D-076).
    const auto v180_out = events_of(t, "cue_out", 2), v180_in = events_of(t, "cue_in", 2);
    REQUIRE(v180_out.size() == 1);
    REQUIRE(v180_in.size() == 1);
    const std::int64_t out_line = scte35test::line_with(v180, scte35test::base64(scte35test::section(out)));
    const std::int64_t in_line  = scte35test::line_with(v180, scte35test::base64(scte35test::section(in)));
    CHECK((v180_out[0]->ref_index == 75 && v180_out[0]->segment == 3));
    CHECK(int_member(v180_out[0]->payload, "line") == out_line);
    CHECK(text_member(v180_out[0]->payload, "form") == "OATCLS");
    CHECK(flag_member(v180_out[0]->payload, "crc_valid") == true);
    CHECK(int_member(v180_out[0]->payload, "break_duration_ticks") == two_s);
    CHECK((v180_in[0]->ref_index == 125 && v180_in[0]->segment == 5));
    CHECK(int_member(v180_in[0]->payload, "line") == in_line);
    const auto v180_break = events_of(t, "ad_break", 2);
    REQUIRE(v180_break.size() == 1);
    CHECK((v180_break[0]->ref_index == 75 && v180_break[0]->ref_end == 125));
    CHECK(text_member(v180_break[0]->payload, "end") == "cue_in");
    CHECK(int_member(v180_break[0]->payload, "cue_out_line") == out_line);
    CHECK(int_member(v180_break[0]->payload, "cue_in_line") == in_line);
    CHECK(events_of(t, "cue_out_cont", 2).empty());

    // Each splice time a splice_point on its variant's own frames (D-106
    // (a)), in its tag's segment; none of the cancel, which has none.
    CHECK(frames_of(t, "splice_point", 0) == std::vector<std::int64_t>{ 25 });
    CHECK(frames_of(t, "splice_point", 1) == std::vector<std::int64_t>{ 75 });
    CHECK(frames_of(t, "splice_point", 2) == std::vector<std::int64_t>{ 75, 125 });
    const Event& point = *events_of(t, "splice_point", 1)[0];
    CHECK(point.segment == 3);
    CHECK(int_member(point.payload, "line") == int_member(record, "line"));
    CHECK(text_member(point.payload, "command") == "splice_insert");
    CHECK(int_member(point.payload, "pts_adjusted") == out_pts);
    CHECK(int_member(point.payload, "index") == 75);
    CHECK(flag_member(point.payload, "in_segment") == true);

    // The reference's message and cancel, at the segments their tags precede.
    const auto message = events_of(t, "message", 0);
    REQUIRE(message.size() == 1);
    CHECK((message[0]->ref_index == 25 && message[0]->segment == 1));
    CHECK(text_member(message[0]->payload, "command") == "time_signal");
    const Value* types = member(message[0]->payload, "segmentation_types");
    REQUIRE(types);
    REQUIRE(std::holds_alternative<Value::List>(types->v));
    REQUIRE(std::get<Value::List>(types->v).size() == 1);
    CHECK(std::get<std::int64_t>(std::get<Value::List>(types->v)[0].v) == 0x10);
    const auto cancelled = events_of(t, "cancel", 0);
    REQUIRE(cancelled.size() == 1);
    CHECK((cancelled[0]->ref_index == 150 && cancelled[0]->segment == 6));
    CHECK(int_member(cancelled[0]->payload, "splice_event_id") == 9);
    for (int v = 1; v < 3; ++v) {
        CHECK(events_of(t, "message", v).empty());
        CHECK(events_of(t, "cancel", v).empty());
    }

    // Each decoded message's event of the scte35 source, with the media key
    // of its frame.
    for (const Event& e : t.events) {
        if (e.type != "splice_point" && e.type != "message" && e.type != "cancel") continue;
        INFO(e.id);
        CHECK(e.source == Source::Scte35);
        CHECK(e.key.sequence == t.axis.keys[static_cast<std::size_t>(e.ref_index)].sequence);
        CHECK(e.key.pts == t.axis.keys[static_cast<std::size_t>(e.ref_index)].pts);
    }
    // The ladder's 22 events a variant and its two context_starts, as before,
    // but for v180's two decoded cues where its plain tags made three; then a
    // message, a cancel and four splice points.
    CHECK(t.events.size() == 3 * 22 + 2 - 1 + 2 + 4);
    CHECK(std::is_sorted(t.events.begin(), t.events.end(), timeline::canonical_before));

    // v180's drop lists its decoded cue_out, its splice_point and its
    // ad_break with the reference's plain cues (D-078), its cue_in at the
    // drop's end not (half-open).
    timeline::DetectOptions options;
    options.window     = 50;
    options.min_points = 12;
    std::string err;
    const std::vector<Attribution> drops =
        timeline::attribute_drops(t, timeline::pair_curve_refs(t), options, {}, err);
    INFO(err);
    REQUIRE(drops.size() == 1);
    CHECK((drops[0].interval.ref_index == 75 && drops[0].interval.ref_end == 125));
    CHECK(listed(t, drops[0], Source::Scte35) ==
          std::vector<std::string>{ "scte35/cue_out/0/0", "scte35/cue_out/2/0", "scte35/splice_point/2/0",
                                    "scte35/ad_break/0/0", "scte35/ad_break/2/0", "scte35/cue_out_cont/0/0" });

    // The document holds them as any other events, their payloads free
    // objects (design §8): read back, and written again to the same bytes.
    timeline::DocInfo info = doc_info(inputs.scores, t);
    info.detection = timeline::Detection{ .options = options, .curves = timeline::pair_curve_refs(t) };
    const std::string text = timeline::to_json(t, drops, info);
    timeline::Timeline       back;
    std::vector<Attribution> attributions;
    timeline::DocInfo        back_info;
    REQUIRE(timeline::from_json(text, back, attributions, back_info, err));
    REQUIRE(back.events.size() == t.events.size());
    CHECK(timeline::to_json(back, attributions, back_info) == text);
}

TEST_CASE("timeline::build: a message whose CRC_32 fails is kept, marked crc_valid false, with a warning, or "
          "dropped by BadCrc::Drop with a warning (D-106 (c))", "[timeline][timeline_build]")
{
    using scte35test::Segmentation;
    // v270: a splice_insert out whose CRC_32 fails, after the plain
    // EXT-X-CUE-OUT of segment 3; v360: a time_signal of a Program Start
    // whose CRC_32 fails, before segment 1; v180: one whose CRC_32 holds,
    // before segment 6.
    scte35test::Section bad_out = scte35test::splice_insert({ .event_id = 7, .pts = ref_pts(75) });
    bad_out.bad_crc             = true;
    scte35test::Section bad_message =
        scte35test::time_signal(ref_pts(25), { Segmentation{ .event_id = 5, .type = 0x10 } });
    bad_message.bad_crc = true;
    const scte35test::Section good_message =
        scte35test::time_signal(ref_pts(150), { Segmentation{ .event_id = 6, .type = 0x10 } });
    const std::string v360 = scte35test::ad_playlist({ { 1, scte35test::oatcls(bad_message) } });
    const std::string v270 = scte35test::ad_playlist({}, { { 3, scte35test::oatcls(bad_out) } });
    const std::string v180 = scte35test::ad_playlist({ { 6, scte35test::oatcls(good_message) } });
    timeline::BuildInputs inputs = ad_inputs_of({ v360, v270, v180 });

    // Each warning names the message, its CRC_32 and the one its bytes give.
    const auto crc_warning = [](int v, const std::string& playlist, const scte35test::Section& s,
                                std::string_view fate) {
        const scte35::SpliceInfo info = scte35::decode(scte35test::section(s));
        return std::format("variant {} ({}): the OATCLS at line {}: its CRC_32 is 0x{:08X} where its bytes "
                           "give 0x{:08X}: {} (D-106 (c))", v, url_of(v),
                           scte35test::line_with(playlist, "#EXT-OATCLS-SCTE35:"), info.crc_32,
                           info.crc_computed, fate);
    };

    // Kept, by default: their events, crc_valid false, each with a warning.
    const timeline::Timeline kept = built(inputs);
    INFO(warnings_text(kept));
    CHECK(kept.warnings == std::vector<std::string>{
                               crc_warning(0, v360, bad_message, "it is kept, marked crc_valid false"),
                               crc_warning(1, v270, bad_out, "it is kept, marked crc_valid false") });
    const auto kept_message = events_of(kept, "message", 0);
    REQUIRE(kept_message.size() == 1);
    CHECK(kept_message[0]->ref_index == 25);
    CHECK(flag_member(kept_message[0]->payload, "crc_valid") == false);
    const auto kept_out = events_of(kept, "cue_out", 1);
    REQUIRE(kept_out.size() == 1);
    CHECK(flag_member(message_record(*kept_out[0], 0), "crc_valid") == false);
    CHECK(frames_of(kept, "splice_point", 0) == std::vector<std::int64_t>{ 25 });
    CHECK(frames_of(kept, "splice_point", 1) == std::vector<std::int64_t>{ 75 });
    CHECK(flag_member(events_of(kept, "splice_point", 1)[0]->payload, "crc_valid") == false);
    const auto good = events_of(kept, "message", 2);
    REQUIRE(good.size() == 1);
    CHECK(good[0]->ref_index == 150);
    CHECK(flag_member(good[0]->payload, "crc_valid") == true);

    // Dropped (--bad-crc drop): no event of either, each with a warning;
    // the plain cue_out without the message; the good message as it was.
    inputs.messages.bad_crc = timeline::BadCrc::Drop;
    const timeline::Timeline dropped = built(inputs);
    INFO(warnings_text(dropped));
    CHECK(dropped.warnings == std::vector<std::string>{
                                  crc_warning(0, v360, bad_message, "it is dropped, as the bad-CRC policy drop says"),
                                  crc_warning(1, v270, bad_out, "it is dropped, as the bad-CRC policy drop says") });
    CHECK(events_of(dropped, "message", 0).empty());
    CHECK(events_of(dropped, "splice_point", 0).empty());
    CHECK(events_of(dropped, "splice_point", 1).empty());
    const auto dropped_out = events_of(dropped, "cue_out", 1);
    REQUIRE(dropped_out.size() == 1);
    CHECK_FALSE(member(dropped_out[0]->payload, "messages"));
    const auto names = [](const timeline::Payload& p) {
        std::vector<std::string> out;
        for (const auto& [n, v] : p) out.push_back(n);
        return out;
    };
    const timeline::Timeline plain = built(ad_inputs());
    CHECK(names(dropped_out[0]->payload) == names(events_of(plain, "cue_out", 1)[0]->payload));
    CHECK(frames_of(dropped, "message", 2) == std::vector<std::int64_t>{ 150 });
    // The breaks are the plain tags' in both.
    for (const timeline::Timeline* t : { &kept, &dropped })
        for (int v = 0; v < 3; ++v) CHECK(frames_of(*t, "ad_break", v) == std::vector<std::int64_t>{ 75 });
}

TEST_CASE("timeline::build: the cue types decide which time_signals are cues, the others messages, and types "
          "that are neither Start nor End types are refused (D-106 (b))", "[timeline][timeline_build]")
{
    using scte35test::Segmentation;
    // v180 without plain cue tags: a time_signal of a Provider Placement
    // Opportunity Start (0x34) before segment 3, and its End (0x35) before
    // segment 5.
    const std::string v180 = scte35test::ad_playlist(
        { { 3, scte35test::oatcls(scte35test::time_signal(ref_pts(75), { Segmentation{ .event_id = 11, .type = 0x34 } })) },
          { 5, scte35test::oatcls(scte35test::time_signal(ref_pts(125), { Segmentation{ .event_id = 11, .type = 0x35 } })) } },
        {}, false);
    timeline::BuildInputs inputs = ad_inputs_of({ kAdPlaylist, kAdPlaylist, v180 });

    // D-106 (b)'s types hold both: a cue_out and a cue_in, paired.
    const timeline::Timeline t = built(inputs);
    INFO(warnings_text(t));
    CHECK(t.warnings.empty());
    CHECK(frames_of(t, "cue_out", 2) == std::vector<std::int64_t>{ 75 });
    CHECK(frames_of(t, "cue_in", 2) == std::vector<std::int64_t>{ 125 });
    const auto brk = events_of(t, "ad_break", 2);
    REQUIRE(brk.size() == 1);
    CHECK((brk[0]->ref_index == 75 && brk[0]->ref_end == 125));
    CHECK(events_of(t, "message", 2).empty());

    // Breaks alone (--cue-types 0x22-0x23): both are messages, and no break
    // is made; their splice points stay.
    inputs.messages.cue_types = { 0x22, 0x23 };
    const timeline::Timeline u = built(inputs);
    INFO(warnings_text(u));
    CHECK(u.warnings.empty());
    CHECK(events_of(u, "cue_out", 2).empty());
    CHECK(events_of(u, "cue_in", 2).empty());
    CHECK(events_of(u, "ad_break", 2).empty());
    const auto messages = events_of(u, "message", 2);
    REQUIRE(messages.size() == 2);
    CHECK(messages[0]->ref_index == 75);
    CHECK(messages[1]->ref_index == 125);
    CHECK(frames_of(t, "splice_point", 2) == std::vector<std::int64_t>{ 75, 125 });
    CHECK(frames_of(u, "splice_point", 2) == std::vector<std::int64_t>{ 75, 125 });
    // The other rungs' plain tags, as before.
    CHECK(events_of(u, "ad_break", 1).size() == 1);

    // A type that is neither a Start nor an End type makes no cue: the
    // build refuses it, as check_message_options() says.
    inputs.messages.cue_types = { 0x22, 0x28 };
    std::string err;
    CHECK(timeline::build(inputs, err).events.empty());
    CHECK(err == timeline::check_message_options(inputs.messages));
    CHECK(contains(err, "0x28"));
}

TEST_CASE("timeline::build: a plain cue numbered before a decoded one of its frame, type and variant, and the "
          "messages' warnings between the variants' and the pairing's, as built", "[timeline][timeline_build]")
{
    // Two choices of the build's that set a document's bytes where a
    // playlist carries a message, pending the owner (Q77;
    // src/TimelineBuild.h, docs/TIMELINE_FORMAT.md), as they are built.
    //
    // v180's EXT-X-CUE-IN and, before it in segment 5, an OATCLS of a
    // splice_insert in, which makes a cue_in of its own at the same frame
    // (D-109 (e)): the plain one is numbered first, whatever their lines.
    const std::string v180 = scte35test::ad_playlist(
        { { 5, scte35test::oatcls(scte35test::splice_insert({ .event_id = 8, .out = false, .pts = ref_pts(125) })) } });
    const timeline::Timeline t = built(ad_inputs_of({ kAdPlaylist, kAdPlaylist, v180 }));
    const std::int64_t plain_line   = scte35test::line_with(v180, "#EXT-X-CUE-IN");
    const std::int64_t decoded_line = scte35test::line_with(v180, "#EXT-OATCLS-SCTE35:");
    REQUIRE(decoded_line < plain_line);
    const auto ins = events_of(t, "cue_in", 2);
    REQUIRE(ins.size() == 2);
    const auto id_at = [&ins](std::int64_t line) {
        for (const Event* e : ins)
            if (int_member(e->payload, "line") == line) return e->id;
        return std::string();
    };
    CHECK(id_at(plain_line) == "scte35/cue_in/2/0");
    CHECK(id_at(decoded_line) == "scte35/cue_in/2/1");

    // Every rung's playlist without its plain cue tags: an OATCLS of a
    // Program Start whose CRC_32 fails before segment 1 (D-106 (c)), a bare
    // EXT-X-CUE-OUT before segment 3 that nothing ends, and a vendor tag
    // after the last URI. The variants' warnings of every rung come first
    // (the tag no segment holds), then the messages' (the CRC_32), then the
    // pairing's (the cue_out without an ad_break).
    scte35test::Section bad =
        scte35test::time_signal(ref_pts(25), { scte35test::Segmentation{ .event_id = 5, .type = 0x10 } });
    bad.bad_crc = true;
    const std::string warned = scte35test::ad_playlist({ { 1, scte35test::oatcls(bad) }, { 3, "#EXT-X-CUE-OUT\n" } },
                                                       {}, false, "", "#EXT-X-VENDOR-NOTE:after the last URI\n");
    const timeline::Timeline w = built(ad_inputs(warned));
    INFO(warnings_text(w));
    const auto at = [&w](std::string_view part) {
        std::vector<std::size_t> out;
        for (std::size_t i = 0; i < w.warnings.size(); ++i)
            if (contains(w.warnings[i], part)) out.push_back(i);
        return out;
    };
    const std::vector<std::size_t> variants = at("follows the last segment's URI");
    const std::vector<std::size_t> messages = at("it is kept, marked crc_valid false (D-106 (c))");
    const std::vector<std::size_t> pairing  = at("no ad_break for the cue_out");
    REQUIRE(variants.size() == 3);
    REQUIRE(messages.size() == 3);
    REQUIRE(pairing.size() == 3);
    CHECK(variants.back() < messages.front());
    CHECK(messages.back() < pairing.front());
}

TEST_CASE("timeline::build: a ladder whose playlists carry no message, as D-069's carry plain cue tags only, has "
          "the same events, warnings and document whatever the message options", "[timeline][timeline_build]")
{
    timeline::BuildInputs inputs = ad_inputs();
    const timeline::Timeline a = built(inputs);
    inputs.messages.cue_types = { 0x10, 0x11 };
    inputs.messages.bad_crc   = timeline::BadCrc::Drop;
    const timeline::Timeline b = built(inputs);
    CHECK(timeline::to_json(b, {}, doc_info(inputs.scores, b)) == timeline::to_json(a, {}, doc_info(inputs.scores, a)));
    CHECK(a.warnings.empty());
    CHECK(b.warnings.empty());
    for (const Event& e : a.events) {
        INFO(e.id);
        CHECK((e.type != "splice_point" && e.type != "message" && e.type != "cancel"));
        CHECK_FALSE(member(e.payload, "messages"));
    }
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
    CHECK(err == "the document's variant 2 names pair 5, but the ladder report's complete pairs are 0 and 1");
    CHECK(beyond.curves.size() == 2);   // unchanged

    timeline::Timeline twice = built(ad_inputs());
    twice.variants[2].pair = 0;
    CHECK_FALSE(timeline::attach_curves(twice, s, err));
    CHECK(err == "the document's variants 1 and 2 both name pair 0");

    timeline::Timeline missing = built(ad_inputs());
    missing.variants.pop_back();
    CHECK_FALSE(timeline::attach_curves(missing, s, err));
    CHECK(err == "the ladder report's complete pair 1 is the pair of none of the document's variants");
}

TEST_CASE("timeline::build, the document and attach_curves: a report whose pairs' numbers skip a failed "
          "pair (D-092 (b))", "[timeline][timeline_build]")
{
    // The ad ladder as if the plan's pair 1 had failed and v180 were its
    // pair 2: the report's complete pairs are numbered 0 and 2.
    timeline::BuildInputs in = ad_inputs();
    in.scores.pairs[1].index = 2;
    const timeline::Timeline t = built(in);

    // The variants hold the report's numbers; the curves keep their places,
    // v180's the second, on variant 2.
    REQUIRE(t.variants.size() == 3);
    CHECK(t.variants[0].pair == -1);
    CHECK(t.variants[1].pair == 0);
    CHECK(t.variants[2].pair == 2);
    REQUIRE(t.curves.size() == 2);
    CHECK(t.curves[1].variant == 2);
    CHECK(timeline::pair_number(t, CurveRef{ .index = 1 }) == 2);
    CHECK(timeline::curve_of_pair(t, 2)->index == 1);
    CHECK_FALSE(timeline::curve_of_pair(t, 1));

    // Detection finds v180's ad on the curve at place 1, pair 2's.
    timeline::DetectOptions options;
    options.window     = 50;
    options.min_points = 12;
    std::string err;
    const std::vector<Attribution> drops =
        timeline::attribute_drops(t, timeline::pair_curve_refs(t), options, {}, err);
    INFO(err);
    REQUIRE(drops.size() == 1);
    CHECK(drops[0].curve.index == 1);
    CHECK((drops[0].interval.ref_index == 75 && drops[0].interval.ref_end == 125));

    // The document names the pair by its number everywhere.
    timeline::DocInfo info = doc_info(in.scores, t);
    info.detection = timeline::Detection{ .options = options, .curves = timeline::pair_curve_refs(t) };
    const std::string text = timeline::to_json(t, drops, info);
    CHECK(contains(text, R"("id": 2, "role": "distorted", "pair": 2,)"));
    CHECK(contains(text, R"("curves": [{"kind": "pair", "index": 0}, {"kind": "pair", "index": 2}])"));
    CHECK(contains(text, R"("curve": {"kind": "pair", "index": 2})"));
    CHECK_FALSE(contains(text, R"({"kind": "pair", "index": 1})"));

    // Read back, the curve at its place again; the report's curves attached
    // each on the variant whose pair is its pairs[].index, and the document
    // written again to the same bytes.
    timeline::Timeline       back;
    std::vector<Attribution> attributions;
    timeline::DocInfo        back_info;
    REQUIRE(timeline::from_json(text, back, attributions, back_info, err));
    REQUIRE(attributions.size() == 1);
    CHECK(attributions[0].curve.index == 1);
    REQUIRE(timeline::attach_curves(back, in.scores, err));
    REQUIRE(back.curves.size() == 2);
    CHECK(back.curves[0].variant == 1);
    CHECK(back.curves[1].variant == 2);
    CHECK(back.curves[1].vmaf == in.scores.pairs[1].vmaf);
    CHECK(timeline::to_json(back, attributions, back_info) == text);

    // attach_curves() names a pair by its number: the failed pair's, which no
    // complete pair has; a complete pair no variant names; pairs out of the
    // plan's order, which the curves' places couldn't follow.
    timeline::Timeline failed = back;
    failed.variants[2].pair = 1;
    CHECK_FALSE(timeline::attach_curves(failed, in.scores, err));
    CHECK(err == "the document's variant 2 names pair 1, but the ladder report's complete pairs are 0 and 2");
    timeline::Timeline missing = back;
    missing.variants.pop_back();
    CHECK_FALSE(timeline::attach_curves(missing, in.scores, err));
    CHECK(err == "the ladder report's complete pair 2 is the pair of none of the document's variants");
    timeline::LadderScores none = in.scores;
    none.pairs.clear();
    CHECK_FALSE(timeline::attach_curves(failed, none, err));
    CHECK(err == "the document's variant 1 names pair 0, but the ladder report has no complete pair");
    timeline::LadderScores reversed = in.scores;
    std::swap(reversed.pairs[0], reversed.pairs[1]);
    CHECK_FALSE(timeline::attach_curves(failed, reversed, err));
    CHECK(err == "the ladder report's pairs[1].index is 0, not after the pair before it (2): pairs come in "
                 "the plan's order, which a document's curves follow (D-092 (b))");

    // build() takes the pairs as read_ladder_report() gives them: their
    // numbers rising, each within an int.
    timeline::BuildInputs out_of_order = in;
    std::swap(out_of_order.scores.pairs[0], out_of_order.scores.pairs[1]);
    CHECK(timeline::build(out_of_order, err).variants.empty());
    CHECK(err == "the ladder report's pairs[1].index is 0, not after the pair before it (2): pairs come in "
                 "the plan's order, which the timeline numbers them by (D-092 (b))");
    timeline::BuildInputs huge = in;
    huge.scores.pairs[1].index = std::size_t{ 1 } << 40;
    CHECK(timeline::build(huge, err).variants.empty());
    CHECK(err == "the ladder report's pairs[1].index is 1099511627776, beyond a pair's number (D-092 (b))");
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
    CHECK(err == "the timeline has no curve at place 5: it has 2 pairs' curves");
    timeline::Timeline odd = t;
    odd.curves[0].vmaf.pop_back();
    CHECK(timeline::attribute_drops(odd, timeline::pair_curve_refs(odd), options, {}, err).empty());
    CHECK(err.starts_with("curve pair:0: "));
}

// ── A scripted session (design §3.4, step 6) ─────────────────────────────────

namespace {

// The plan of design §9.3's shape on the three rungs: entry 1 (v270) for
// segments 0 to 2, entry 2 (v180) for 3 and 4, entry 1 for 5 to 7.
timeline::SessionPlan down_at_the_ad()
{
    return timeline::SessionPlan{ .name  = "down at the ad",
                                  .spans = { { .entry = 1, .first_seq = 0, .last_seq = 2 },
                                             { .entry = 2, .first_seq = 3, .last_seq = 4 },
                                             { .entry = 1, .first_seq = 5, .last_seq = 7 } } };
}

// `files` as fetch_files() lists them, each given one row of an HTTP 200 by
// hand, one after another from 1 ms on the session's clock, 3 ms each, as
// fetch_session() would return them (src/SessionFetch.h).
std::vector<timeline::Request> fetched(std::vector<timeline::Request> files)
{
    std::int64_t at = 1000;
    for (timeline::Request& r : files) {
        r.chain = { timeline::RequestRow{ .url = r.url, .t_start = at, .t_sent = at + 100, .t_first_byte = at + 2500,
                                          .t_end = at + 3000, .bytes = 12032, .status = 200, .status_text = "OK",
                                          .content_length = 12032, .initiator = "playback" } };
        at += 3000;
    }
    return files;
}

// The ids of `t`'s events of one source, in their order.
std::vector<std::string> ids_of(const timeline::Timeline& t, Source source)
{
    std::vector<std::string> out;
    for (const Event& e : t.events)
        if (e.source == source) out.push_back(e.id);
    return out;
}

} // namespace

TEST_CASE("timeline::add_session and add_requests: a session merged into a ladder's timeline, its spans and "
          "its player and network events placed, the ladder's ids kept", "[timeline][timeline_build]")
{
    const timeline::BuildInputs in    = ad_inputs();
    const timeline::Timeline    plain = built(in);
    timeline::Timeline          t     = plain;
    std::string err;
    REQUIRE(timeline::add_session(t, in.scores.entries, down_at_the_ad(), err));
    CHECK(err.empty());

    // The session: its id its place as text, "0", by which --curve session:0
    // names it (design §7; a form D-111 (a) keeps); its plan's name; no
    // anchor before its fetch; its spans on the reference's frames through
    // the segments build() placed (design §5.1), each starting where the one
    // before it ended (D-105 (a)).
    REQUIRE(t.sessions.size() == 1);
    const timeline::Session& s = t.sessions[0];
    CHECK(s.id == "0");
    CHECK(s.name == "down at the ad");
    CHECK_FALSE(s.wallclock_anchor_utc);
    REQUIRE(s.spans.size() == 3);
    const int          variant[] = { 1, 2, 1 };
    const std::int64_t from[] = { 0, 75, 125 }, to[] = { 75, 125, 200 };
    for (std::size_t k = 0; k < 3; ++k) {
        INFO("span " << k);
        CHECK(s.spans[k].variant == variant[k]);
        CHECK(s.spans[k].ref_index == from[k]);
        CHECK(s.spans[k].ref_end == to[k]);
        CHECK(s.spans[k].scored);
    }

    // Its player events, a variant_span per span and a switch at each
    // boundary where the entry changes (design §5.6, D-108 (b)), each of the
    // session; the ladder's events keep their ids and places, since the
    // session's are of a source of their own (assign_ids()).
    CHECK(ids_of(t, Source::Player) ==
          std::vector<std::string>{ "player/variant_span/1/0", "player/variant_switch/2/0", "player/variant_span/2/0",
                                    "player/variant_switch/1/0", "player/variant_span/1/1" });
    for (const Event& e : t.events)
        if (e.source == Source::Player) CHECK(e.session == 0);
    REQUIRE(t.events.size() == plain.events.size() + 5);
    for (const Event& e : plain.events) {
        INFO(e.id);
        const auto same = std::find_if(t.events.begin(), t.events.end(), [&e](const Event& x) { return x.id == e.id; });
        REQUIRE(same != t.events.end());
        CHECK(same->ref_index == e.ref_index);
        CHECK(same->ref_end == e.ref_end);
        CHECK(same->type == e.type);
        CHECK(same->session == -1);
    }
    CHECK(t.max_end.size() == t.events.size());   // the index built again

    // Its fetch: the files fetch_files() lists in play order (D-104 (a)),
    // each with a row, added as network events of the session, each on the
    // media axis (D-105 (c)) with its wall clock, its first row's start
    // (D-073), the anchor the session's.
    const std::vector<timeline::Request> files =
        fetched(timeline::fetch_files(t, 0, in.scores.master_url, "", err));
    REQUIRE(err.empty());
    REQUIRE(files.size() == 11);
    timeline::add_requests(t, 0, files, "2026-11-10T09:30:00.000250Z");
    CHECK(t.sessions[0].wallclock_anchor_utc == "2026-11-10T09:30:00.000250Z");
    CHECK(t.warnings == plain.warnings);
    const std::vector<std::string> network = ids_of(t, Source::Network);
    CHECK(network.size() == 11);
    const auto request = [&t](std::string_view id) -> const Event& {
        const auto e = std::find_if(t.events.begin(), t.events.end(), [id](const Event& x) { return x.id == id; });
        REQUIRE(e != t.events.end());
        return *e;
    };
    const Event& master = request("network/playlist_request/-/0");
    CHECK((master.ref_index == 0 && !master.ref_end && master.variant == -1 && master.session == 0));
    CHECK(master.wallclock_us == 1000);
    const Event& v180 = request("network/playlist_request/2/0");
    CHECK(v180.ref_index == 75);   // the first frame of the first span that plays it
    const Event& ad = request("network/segment_request/2/0");
    CHECK((ad.ref_index == 75 && ad.ref_end == 100 && ad.segment == 3));
    CHECK(ad.wallclock_us == 1000 + 6 * 3000);   // the seventh file fetched
    CHECK(ad.key.sequence == 1);
    // Every id once.
    std::vector<std::string> all;
    for (const Event& e : t.events) all.push_back(e.id);
    std::sort(all.begin(), all.end());
    CHECK(std::adjacent_find(all.begin(), all.end()) == all.end());

    // The timeline with its session writes a document that reads back,
    // the session part filled (src/TimelineDoc.h).
    timeline::DocInfo info = doc_info(in.scores, t);
    info.session_plan = timeline::FileRef{ .path = "plan.json", .sha256 = filehash::sha256_hex("a plan") };
    const std::string text = timeline::to_json(t, {}, info);
    timeline::Timeline       back;
    std::vector<Attribution> none;
    timeline::DocInfo        back_info;
    REQUIRE(timeline::from_json(text, back, none, back_info, err));
    REQUIRE(back.sessions.size() == 1);
    CHECK(back.sessions[0].spans.size() == 3);
    CHECK(back.events.size() == t.events.size());
    CHECK(timeline::to_json(back, {}, back_info) == text);

    // A file without a row makes no event and a warning; a session the
    // timeline doesn't have, a warning alone.
    timeline::Timeline u = plain;
    REQUIRE(timeline::add_session(u, in.scores.entries, down_at_the_ad(), err));
    std::vector<timeline::Request> some = fetched(timeline::fetch_files(u, 0, in.scores.master_url, "", err));
    some[1].chain.clear();
    timeline::add_requests(u, 0, some, "2026-11-10T09:30:00.000250Z");
    CHECK(ids_of(u, Source::Network).size() == 10);
    REQUIRE(u.warnings.size() == plain.warnings.size() + 1);
    CHECK(u.warnings.back() == "the session fetch's request for variant 1's media playlist "
                               "(http://test.invalid/ladder/v270/index.m3u8) has no row of NetworkLogger's: it "
                               "makes no event (design §5.1)");
    // An empty anchor gives the session none, and then no request makes an
    // event, since its wall clock would count from no anchor: one warning
    // says so, and the timeline's document reads back (src/TimelineBuild.h).
    timeline::Timeline v = plain;
    REQUIRE(timeline::add_session(v, in.scores.entries, down_at_the_ad(), err));
    timeline::add_requests(v, 0, some, "");
    CHECK_FALSE(v.sessions[0].wallclock_anchor_utc);
    CHECK(ids_of(v, Source::Network).empty());
    REQUIRE(v.warnings.size() == plain.warnings.size() + 2);
    CHECK(v.warnings.back() == "the session fetch has no UTC anchor for its requests' wall clocks to count from: 10 "
                               "requests with rows of NetworkLogger's make no event (D-073)");
    timeline::DocInfo v_info = doc_info(in.scores, v);
    v_info.session_plan      = info.session_plan;
    timeline::Timeline v_back;
    timeline::DocInfo  v_back_info;
    const bool v_read = timeline::from_json(timeline::to_json(v, {}, v_info), v_back, none, v_back_info, err);
    INFO(err);
    REQUIRE(v_read);
    REQUIRE(v_back.sessions.size() == 1);
    CHECK_FALSE(v_back.sessions[0].wallclock_anchor_utc);
    CHECK(v_back.events.size() == plain.events.size() + 5);   // the ladder's and the session's player events
    timeline::Timeline w = plain;
    timeline::add_requests(w, 0, files, "2026-11-10T09:30:00.000250Z");
    CHECK(w.events.size() == plain.events.size());
    REQUIRE(w.warnings.size() == plain.warnings.size() + 1);
    CHECK(contains(w.warnings.back(), "the timeline has no session 0: it has 0 sessions"));
}

TEST_CASE("timeline::add_session: what it refuses leaves the timeline as it was", "[timeline][timeline_build]")
{
    const timeline::BuildInputs in    = ad_inputs();
    const timeline::Timeline    plain = built(in);
    const auto refusal = [&](const timeline::SessionPlan& plan) {
        timeline::Timeline t = plain;
        std::string        err;
        CHECK_FALSE(timeline::add_session(t, in.scores.entries, plan, err));
        CHECK(t.sessions.empty());
        CHECK(t.events.size() == plain.events.size());
        return err;
    };
    // make_session()'s refusals (src/SessionPlan.h), each naming what it
    // refuses: an entry the master hasn't got, a gap between two spans
    // (D-105 (a)).
    CHECK(refusal(timeline::SessionPlan{ .spans = { { .entry = 7, .first_seq = 0, .last_seq = 7 } } }) ==
          "the session plan's spans[0] (entry 7, segments 0 to 7) plays entry 7, which isn't one of the master's "
          "3 entries");
    CHECK(contains(refusal(timeline::SessionPlan{ .spans = { { .entry = 1, .first_seq = 0, .last_seq = 2 },
                                                            { .entry = 2, .first_seq = 4, .last_seq = 7 } } }),
                   "they leave reference frames 75 to 99 in no span"));

    // One session at most (D-072).
    timeline::Timeline t = plain;
    std::string        err;
    REQUIRE(timeline::add_session(t, in.scores.entries, down_at_the_ad(), err));
    const std::size_t events = t.events.size();
    CHECK_FALSE(timeline::add_session(t, in.scores.entries, down_at_the_ad(), err));
    CHECK(err == "the timeline has 1 session already: a timeline holds one session at most (D-072)");
    CHECK(t.sessions.size() == 1);
    CHECK(t.events.size() == events);
}

TEST_CASE("timeline::attribute_drops: a session's drops are its pairs' drops clipped to its spans, each with "
          "the session's events (D-077)", "[timeline][timeline_build]")
{
    const timeline::BuildInputs in = ad_inputs();
    timeline::Timeline          t  = built(in);
    std::string err;
    REQUIRE(timeline::add_session(t, in.scores.entries, down_at_the_ad(), err));
    timeline::add_requests(t, 0, fetched(timeline::fetch_files(t, 0, in.scores.master_url, "", err)),
                           "2026-11-10T09:30:00.000250Z");
    timeline::DetectOptions options;
    options.window     = 50;
    options.min_points = 12;

    // Every pair's curve, then the session's.
    const std::vector<CurveRef> curves = timeline::curve_refs(t);
    REQUIRE(curves.size() == 3);
    CHECK((curves[0].kind == CurveRef::Kind::Pair && curves[0].index == 0));
    CHECK((curves[1].kind == CurveRef::Kind::Pair && curves[1].index == 1));
    CHECK((curves[2].kind == CurveRef::Kind::Session && curves[2].index == 0));

    // v180's ad, its pair's drop, and the session's, which plays v180 over
    // it: the same frames, the pair's baseline and magnitudes (design §6.3,
    // rule (iii)), the session's own events listed with the reference's and
    // v180's (D-078).
    const std::vector<Attribution> drops = timeline::attribute_drops(t, curves, options, {}, err);
    INFO(err);
    REQUIRE(err.empty());
    REQUIRE(drops.size() == 2);
    const Attribution& pair    = drops[0];
    const Attribution& session = drops[1];
    CHECK((pair.curve.kind == CurveRef::Kind::Pair && pair.curve.index == 1));
    CHECK((session.curve.kind == CurveRef::Kind::Session && session.curve.index == 0));
    CHECK(session.detected);
    CHECK((session.interval.ref_index == 75 && session.interval.ref_end == 125));
    CHECK(session.points == pair.points);
    CHECK(session.baseline == pair.baseline);
    CHECK(session.min == pair.min);
    CHECK(session.mean == pair.mean);
    CHECK(session.drop_magnitude == pair.drop_magnitude);
    CHECK(session.mean_drop == pair.mean_drop);
    std::string why;
    const std::vector<timeline::Drop> clipped = timeline::session_drops(t, 0, options, why);
    REQUIRE(clipped.size() == 1);
    CHECK(session.baseline == clipped[0].baseline);
    CHECK(listed(t, session, Source::Player) ==
          std::vector<std::string>{ "player/variant_switch/2/0", "player/variant_span/2/0" });
    CHECK(listed(t, session, Source::Network) ==
          std::vector<std::string>{ "network/playlist_request/2/0", "network/segment_request/2/0",
                                    "network/segment_request/2/1" });
    CHECK(listed(t, session, Source::Scte35) == listed(t, pair, Source::Scte35));
    CHECK(listed(t, session, Source::Vmaf) == std::vector<std::string>{ "vmaf/context_start/2/0" });

    // A session that plays v270 over the ad has no drop there; one that
    // plays the reference over it, in top-rung mode, has none in that
    // unscored span (D-105 (b)).
    for (const timeline::SessionPlan& plan :
         { timeline::SessionPlan{ .spans = { { .entry = 1, .first_seq = 0, .last_seq = 7 } } },
           timeline::SessionPlan{ .spans = { { .entry = 1, .first_seq = 0, .last_seq = 2 },
                                             { .entry = 0, .first_seq = 3, .last_seq = 4 },
                                             { .entry = 2, .first_seq = 5, .last_seq = 7 } } } }) {
        timeline::Timeline other = built(in);
        REQUIRE(timeline::add_session(other, in.scores.entries, plan, err));
        const std::vector<Attribution> found =
            timeline::attribute_drops(other, { CurveRef{ .kind = CurveRef::Kind::Session, .index = 0 } }, options,
                                      {}, err);
        CHECK(err.empty());
        CHECK(found.empty());
    }

    // Without W and N none, and no error (D-077); a session the timeline
    // doesn't have is refused, as a pair's curve it doesn't have is.
    CHECK(timeline::attribute_drops(t, curves, {}, {}, err).empty());
    CHECK(err.empty());
    CHECK(timeline::attribute_drops(t, { CurveRef{ .kind = CurveRef::Kind::Session, .index = 5 } }, options, {},
                                    err)
              .empty());
    CHECK(err == "the timeline has no session at place 5: it has 1 session");
    timeline::Timeline odd = t;
    odd.curves[1].vmaf.pop_back();
    CHECK(timeline::attribute_drops(odd, { CurveRef{ .kind = CurveRef::Kind::Session, .index = 0 } }, options, {},
                                    err)
              .empty());
    CHECK(err.starts_with("curve session:0: pair 1's curve: "));
}
