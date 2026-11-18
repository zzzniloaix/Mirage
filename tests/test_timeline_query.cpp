// timeline::attribute() (src/TimelineQuery.h) for a pair's curve, on a
// timeline the test builds by hand in the shape of testgen's ad ladder
// (eval/phase3-survey/testgen.md §2.3): three rungs of eight 25-frame
// segments, the reference v360 and the pairs v270 and v180, each playlist
// with the ad's discontinuities and cue tags, the ad's drop at reference
// frames 75 to 124. The half-open overlap and the margins of D-078, for an
// empty interval too, the scope of a pair's curve (Q28 (a), each rung's copy
// kept, D-081 (f)), the groups in section 3.1's order (Q30 (a)), and offsets
// in frames and in seconds on the frame grid and on a playlist time that
// departs from it.

#include <catch2/catch_test_macros.hpp>

#include "TimelineModel.h"
#include "TimelineQuery.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

using timeline::Attribution;
using timeline::Event;
using timeline::Found;
using timeline::Interval;
using timeline::Source;

namespace {

constexpr Interval kAd{ .ref_index = 75, .ref_end = 125 };   // the ad's frames, and its drop's

// An axis of 200 frames at 25 fps: the frame grid, in ticks of 1/25 s.
timeline::Axis frame_grid()
{
    timeline::Axis a;
    a.clock          = "frame-grid";
    a.time_base      = { 1, 90000 };
    a.frame_duration = { 1, 25 };
    a.frames         = 200;
    a.keys.resize(200);
    a.tick_den = 25;
    for (std::int64_t i = 0; i < 200; ++i) a.ticks.push_back(i);
    return a;
}

// The playlist time of the ad shape with the ad's 50 frames at 24 fps, in
// ticks of 1/600 s: frames 0 to 74 1/25 s apart, the ad's 1/24 s apart from
// 3 s, the main content 1/25 s apart again from 3 s + 50/24 s.
timeline::Axis ad_at_24fps()
{
    timeline::Axis a = frame_grid();
    a.clock    = "playlist-time";
    a.tick_den = 600;
    for (std::int64_t i = 0; i < 200; ++i)
        a.ticks[static_cast<std::size_t>(i)] =
            i < 75 ? i * 24 : i < 125 ? 1800 + (i - 75) * 25 : 3050 + (i - 125) * 24;
    return a;
}

Event event(Source source, std::string type, int variant, std::int64_t at,
            std::optional<std::int64_t> end = std::nullopt)
{
    Event e;
    e.source    = source;
    e.type      = std::move(type);
    e.variant   = variant;
    e.ref_index = at;
    e.ref_end   = end;
    return e;
}

// The ad ladder's timeline on `axis`: variant 0 the reference v360, 1 and 2
// the pairs' v270 and v180, each playlist's segments, keyframes, cue tags and
// ad break; each pair's libvmaf context starting at the ad; a corrupt packet
// of v180 one frame before the ad; a session's request and switch, and an
// event of no variant, which no pair's curve lists.
timeline::Timeline ad_ladder(const timeline::Axis& axis)
{
    timeline::Timeline t;
    t.axis = axis;
    for (int v = 0; v < 3; ++v) {
        timeline::Variant var;
        var.index = v;
        var.pair  = v - 1;
        t.variants.push_back(var);
    }
    for (int pair = 0; pair < 2; ++pair) {
        timeline::Curve c;
        c.variant = pair + 1;
        for (std::int64_t i = 0; i < 200; ++i) {
            c.ref_index.push_back(i);
            c.vmaf.push_back(pair == 1 && i >= 75 && i < 125 ? 20.0 + 0.5 * static_cast<double>(i - 75) : 91.5);
        }
        c.context_first = { 0, 75 };
        t.curves.push_back(c);
    }
    for (int v = 0; v < 3; ++v) {
        for (std::int64_t k = 0; k < 8; ++k) {
            t.events.push_back(event(Source::Playlist, "segment", v, 25 * k, 25 * k + 25));
            t.events.back().segment = k;
            t.events.push_back(event(Source::Decoder, "keyframe", v, 25 * k));
        }
        t.events.push_back(event(Source::Playlist, "discontinuity", v, 75));
        t.events.push_back(event(Source::Playlist, "discontinuity", v, 125));
        t.events.push_back(event(Source::Scte35, "cue_out", v, 75));
        t.events.push_back(event(Source::Scte35, "cue_out_cont", v, 100));
        t.events.push_back(event(Source::Scte35, "cue_in", v, 125));
        t.events.push_back(event(Source::Scte35, "ad_break", v, 75, 125));
    }
    t.events.push_back(event(Source::Vmaf, "context_start", 1, 75));
    t.events.push_back(event(Source::Vmaf, "context_start", 2, 75));
    t.events.push_back(event(Source::Decoder, "corrupt_packet", 2, 74));
    t.events.push_back(event(Source::Network, "segment_request", 2, 75, 100));
    t.events.back().session = 0;
    t.events.push_back(event(Source::Player, "variant_switch", -1, 75));
    t.events.back().session = 0;
    t.events.push_back(event(Source::Playlist, "tag", -1, 80));
    timeline::assign_ids(t.events);
    t.build_index();
    return t;
}

std::vector<std::string> listed(const timeline::Timeline& t, const Attribution& a, Source source)
{
    std::vector<std::string> out;
    for (const Found& f : a.by_source[static_cast<std::size_t>(source)]) out.push_back(t.events[f.event].id);
    return out;
}

const Found* found(const timeline::Timeline& t, const Attribution& a, const std::string& id)
{
    for (const auto& group : a.by_source)
        for (const Found& f : group)
            if (t.events[f.event].id == id) return &f;
    return nullptr;
}

using Ids = std::vector<std::string>;

} // namespace

TEST_CASE("timeline query: a pair's curve lists its distorted variant's and the reference's events, "
          "by source, half-open",
          "[timeline][timeline_query]")
{
    const timeline::Timeline t = ad_ladder(frame_grid());
    const Attribution a = timeline::attribute(t, timeline::CurveRef{ .index = 1 }, kAd, {});
    CHECK(a.curve.index == 1);
    CHECK(a.interval.ref_index == 75);
    CHECK(a.interval.ref_end == 125);
    CHECK(a.searched.ref_index == 75);
    CHECK(a.searched.ref_end == 125);
    CHECK_FALSE(a.detected);

    // v180's own and v360's, none of v270's; neither the cue-in, the
    // discontinuity, the segment nor the keyframe at frame 125, where the
    // interval ends; nor the corrupt packet at frame 74.
    CHECK(listed(t, a, Source::Vmaf) == Ids{ "vmaf/context_start/2/0" });
    CHECK(listed(t, a, Source::Network).empty());
    CHECK(listed(t, a, Source::Playlist) ==
          Ids{ "playlist/discontinuity/0/0", "playlist/discontinuity/2/0", "playlist/segment/0/3",
               "playlist/segment/2/3", "playlist/segment/0/4", "playlist/segment/2/4" });
    CHECK(listed(t, a, Source::Scte35) ==
          Ids{ "scte35/cue_out/0/0", "scte35/cue_out/2/0", "scte35/ad_break/0/0", "scte35/ad_break/2/0",
               "scte35/cue_out_cont/0/0", "scte35/cue_out_cont/2/0" });
    CHECK(listed(t, a, Source::Player).empty());
    CHECK(listed(t, a, Source::Decoder) ==
          Ids{ "decoder/keyframe/0/3", "decoder/keyframe/2/3", "decoder/keyframe/0/4",
               "decoder/keyframe/2/4" });

    // The other pair's curve lists v270's copies instead of v180's.
    const Attribution b = timeline::attribute(t, timeline::CurveRef{ .index = 0 }, kAd, {});
    CHECK(listed(t, b, Source::Vmaf) == Ids{ "vmaf/context_start/1/0" });
    CHECK(listed(t, b, Source::Scte35) ==
          Ids{ "scte35/cue_out/0/0", "scte35/cue_out/1/0", "scte35/ad_break/0/0", "scte35/ad_break/1/0",
               "scte35/cue_out_cont/0/0", "scte35/cue_out_cont/1/0" });
    CHECK(listed(t, b, Source::Decoder) ==
          Ids{ "decoder/keyframe/0/3", "decoder/keyframe/1/3", "decoder/keyframe/0/4",
               "decoder/keyframe/1/4" });
}

TEST_CASE("timeline query: offsets in frames and in seconds on the axis", "[timeline][timeline_query]")
{
    const timeline::Timeline t = ad_ladder(frame_grid());
    const Attribution a = timeline::attribute(t, timeline::CurveRef{ .index = 1 }, kAd, {});
    const Found* cue = found(t, a, "scte35/cue_out/2/0");
    REQUIRE(cue);
    CHECK(cue->offset_frames == 0);
    CHECK(cue->offset_s == 0.0);
    CHECK_FALSE(cue->offset_end_frames.has_value());
    CHECK_FALSE(cue->offset_end_s.has_value());
    const Found* brk = found(t, a, "scte35/ad_break/2/0");
    REQUIRE(brk);
    CHECK(brk->offset_frames == 0);
    CHECK(brk->offset_end_frames == 50);
    CHECK(brk->offset_end_s == 2.0);
    const Found* cont = found(t, a, "scte35/cue_out_cont/2/0");
    REQUIRE(cont);
    CHECK(cont->offset_frames == 25);
    CHECK(cont->offset_s == 1.0);

    // On a playlist time with the ad at 24 fps, the offsets in frames stay
    // and those in seconds follow the ad's frames: 25/24 s to the segment
    // after the cue-out's, 50/24 s to the break's end.
    const timeline::Timeline slow = ad_ladder(ad_at_24fps());
    const Attribution s = timeline::attribute(slow, timeline::CurveRef{ .index = 1 }, kAd, {});
    const Found* seg = found(slow, s, "playlist/segment/2/4");
    REQUIRE(seg);
    CHECK(seg->offset_frames == 25);
    CHECK(seg->offset_s == 625.0 / 600.0);
    CHECK(seg->offset_end_frames == 50);
    CHECK(seg->offset_end_s == 1250.0 / 600.0);
    const Found* slow_break = found(slow, s, "scte35/ad_break/2/0");
    REQUIRE(slow_break);
    CHECK(slow_break->offset_end_s == 1250.0 / 600.0);
    // The same events as on the frame grid: only the seconds differ.
    for (std::size_t g = 0; g < timeline::kSourceCount; ++g)
        CHECK(listed(slow, s, static_cast<Source>(g)) == listed(t, a, static_cast<Source>(g)));

    // An interval given in seconds starts and ends at the first frames at or
    // after its ends: [3 s, 5 s) is frames 75 to 124 on the frame grid, 75 to
    // 122 where the ad runs at 24 fps.
    const Interval grid = timeline::seconds_interval(t.axis, AVRational{ 3, 1 }, AVRational{ 5, 1 });
    CHECK(grid.ref_index == 75);
    CHECK(grid.ref_end == 125);
    const Interval played = timeline::seconds_interval(slow.axis, AVRational{ 3, 1 }, AVRational{ 5, 1 });
    CHECK(played.ref_index == 75);
    CHECK(played.ref_end == 123);
}

TEST_CASE("timeline query: margins widen the interval on the axis, none by default",
          "[timeline][timeline_query]")
{
    const timeline::Timeline t = ad_ladder(frame_grid());
    const timeline::QueryOptions frame{ .margin_before = { 1, 25 }, .margin_after = { 1, 25 } };
    const Attribution a = timeline::attribute(t, timeline::CurveRef{ .index = 1 }, kAd, frame);
    CHECK(a.interval.ref_index == 75);
    CHECK(a.searched.ref_index == 74);
    CHECK(a.searched.ref_end == 126);

    // A frame before, the corrupt packet and the segment that ends at the
    // ad; at the end, the cue-in, the next segment and its discontinuity and
    // keyframe, each at its offset from the interval's start.
    CHECK(listed(t, a, Source::Decoder) ==
          Ids{ "decoder/corrupt_packet/2/0", "decoder/keyframe/0/3", "decoder/keyframe/2/3",
               "decoder/keyframe/0/4", "decoder/keyframe/2/4", "decoder/keyframe/0/5",
               "decoder/keyframe/2/5" });
    CHECK(listed(t, a, Source::Playlist) ==
          Ids{ "playlist/segment/0/2", "playlist/segment/2/2", "playlist/discontinuity/0/0",
               "playlist/discontinuity/2/0", "playlist/segment/0/3", "playlist/segment/2/3",
               "playlist/segment/0/4", "playlist/segment/2/4", "playlist/discontinuity/0/1",
               "playlist/discontinuity/2/1", "playlist/segment/0/5", "playlist/segment/2/5" });
    const Found* corrupt = found(t, a, "decoder/corrupt_packet/2/0");
    REQUIRE(corrupt);
    CHECK(corrupt->offset_frames == -1);
    CHECK(corrupt->offset_s == -0.04);
    const Found* before = found(t, a, "playlist/segment/2/2");
    REQUIRE(before);
    CHECK(before->offset_frames == -25);
    CHECK(before->offset_end_frames == 0);
    CHECK(before->offset_end_s == 0.0);
    const Found* cue_in = found(t, a, "scte35/cue_in/2/0");
    REQUIRE(cue_in);
    CHECK(cue_in->offset_frames == 50);
    CHECK(cue_in->offset_s == 2.0);

    // A margin smaller than the frame before the interval adds none of it;
    // any margin after it adds the frame at its end, whose time is inside
    // [3 s, 5 s + margin).
    const timeline::QueryOptions small{ .margin_before = { 3, 100 }, .margin_after = { 1, 100 } };
    const Interval w = timeline::widen(t.axis, kAd, small);
    CHECK(w.ref_index == 75);
    CHECK(w.ref_end == 126);
    // Zero and negative margins change nothing; an empty interval is widened
    // as any other, [90, 90) by a frame each way to [89, 91).
    const timeline::QueryOptions zero{ .margin_before = { 0, 1 }, .margin_after = { -1, 25 } };
    CHECK(timeline::widen(t.axis, kAd, zero).ref_index == 75);
    CHECK(timeline::widen(t.axis, kAd, zero).ref_end == 125);
    const Interval around = timeline::widen(t.axis, Interval{ .ref_index = 90, .ref_end = 90 }, frame);
    CHECK(around.ref_index == 89);
    CHECK(around.ref_end == 91);
    CHECK(timeline::widen(t.axis, Interval{ .ref_index = 90, .ref_end = 90 }, zero).ref_end == 90);
    // At the axis's ends a margin stops at its frames.
    const Interval all = timeline::widen(t.axis, Interval{ .ref_index = 0, .ref_end = 200 }, frame);
    CHECK(all.ref_index == 0);
    CHECK(all.ref_end == 200);
}

TEST_CASE("timeline query: the curve's points in the interval", "[timeline][timeline_query]")
{
    const timeline::Timeline t = ad_ladder(frame_grid());
    const Attribution a = timeline::attribute(t, timeline::CurveRef{ .index = 1 }, kAd, {});
    CHECK(a.points == 50);
    CHECK(a.min == 20.0);
    CHECK(a.mean == 32.25);   // 20 + 0.5 * 24.5
    CHECK_FALSE(a.baseline.has_value());
    CHECK_FALSE(a.drop_magnitude.has_value());
    CHECK_FALSE(a.mean_drop.has_value());

    // A point without a score is skipped (D-077); a curve on a coarser grid
    // has points at some frames only.
    timeline::Timeline coarse = t;
    coarse.curves[1].ref_index = { 74, 76, 78, 80, 124, 126 };
    coarse.curves[1].vmaf      = { 90.0, 30.0, std::numeric_limits<double>::quiet_NaN(), 20.0, 40.0, 90.0 };
    const Attribution c = timeline::attribute(coarse, timeline::CurveRef{ .index = 1 }, kAd, {});
    CHECK(c.points == 3);
    CHECK(c.min == 20.0);
    CHECK(c.mean == 30.0);
}

TEST_CASE("timeline query: an empty interval, unknown curves and a timeline without its index",
          "[timeline][timeline_query]")
{
    // [100, 100) holds none of the curve's points, and meets the spans that
    // straddle frame 100, as D-078's rule gives: the ad breaks of v180 and
    // v360, 25 frames before and after it. Not the points at frame 100, the
    // cue_out_cont and the keyframe, nor the segments that end or start there.
    const timeline::Timeline t = ad_ladder(frame_grid());
    const Interval at_100{ .ref_index = 100, .ref_end = 100 };
    const Attribution empty = timeline::attribute(t, timeline::CurveRef{ .index = 1 }, at_100, {});
    CHECK(empty.points == 0);
    CHECK_FALSE(empty.min.has_value());
    CHECK(empty.searched.ref_index == 100);
    CHECK(empty.searched.ref_end == 100);
    CHECK(listed(t, empty, Source::Scte35) == Ids{ "scte35/ad_break/0/0", "scte35/ad_break/2/0" });
    for (const Source source : { Source::Vmaf, Source::Network, Source::Playlist, Source::Player, Source::Decoder })
        CHECK(listed(t, empty, source).empty());
    CHECK_FALSE(found(t, empty, "scte35/cue_out_cont/2/0"));
    CHECK_FALSE(found(t, empty, "decoder/keyframe/2/4"));
    const Found* straddles = found(t, empty, "scte35/ad_break/2/0");
    REQUIRE(straddles);
    CHECK(straddles->offset_frames == -25);
    CHECK(straddles->offset_end_frames == 25);
    CHECK(straddles->offset_s == -1.0);
    CHECK(straddles->offset_end_s == 1.0);

    // Widened by a frame each way, it is [99, 101), which the points at
    // frame 100 and the segments that end or start there meet.
    const timeline::QueryOptions frame{ .margin_before = { 1, 25 }, .margin_after = { 1, 25 } };
    const Attribution widened = timeline::attribute(t, timeline::CurveRef{ .index = 1 }, at_100, frame);
    CHECK(widened.searched.ref_index == 99);
    CHECK(widened.searched.ref_end == 101);
    CHECK(widened.points == 0);
    CHECK(listed(t, widened, Source::Playlist) ==
          Ids{ "playlist/segment/0/3", "playlist/segment/2/3", "playlist/segment/0/4", "playlist/segment/2/4" });
    CHECK(listed(t, widened, Source::Scte35) ==
          Ids{ "scte35/ad_break/0/0", "scte35/ad_break/2/0", "scte35/cue_out_cont/0/0", "scte35/cue_out_cont/2/0" });
    CHECK(listed(t, widened, Source::Decoder) == Ids{ "decoder/keyframe/0/4", "decoder/keyframe/2/4" });

    const Attribution unknown = timeline::attribute(t, timeline::CurveRef{ .index = 2 }, kAd, {});
    CHECK(unknown.points == 0);
    CHECK(unknown.curve.index == 2);
    for (const auto& group : unknown.by_source) CHECK(group.empty());

    // The events in reverse and without the index: the same answer, by
    // offset within each group.
    timeline::Timeline raw = t;
    raw.max_end.clear();
    std::reverse(raw.events.begin(), raw.events.end());
    const Attribution a = timeline::attribute(t, timeline::CurveRef{ .index = 1 }, kAd, {});
    const Attribution r = timeline::attribute(raw, timeline::CurveRef{ .index = 1 }, kAd, {});
    for (std::size_t g = 0; g < timeline::kSourceCount; ++g) {
        const auto source = static_cast<Source>(g);
        const Ids ids = listed(raw, r, source);
        CHECK(ids.size() == listed(t, a, source).size());
        std::int64_t last = std::numeric_limits<std::int64_t>::min();
        for (const Found& f : r.by_source[g]) {
            CHECK(f.offset_frames >= last);
            last = f.offset_frames;
        }
    }
}
