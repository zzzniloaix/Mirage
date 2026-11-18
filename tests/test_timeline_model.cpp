// The timeline's model (src/TimelineModel.h): the canonical order and the
// deterministic ids of eval/phase3-survey/design.md's P-I, whatever order
// the events come in; the overlap index of its P-J against a scan of every
// event, on random events and intervals; points and spans at an interval's
// exact boundaries (D-078); empty and reversed intervals, by D-078's rule as
// written; a pair's curve.

#include <catch2/catch_test_macros.hpp>

#include "TimelineModel.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <tuple>
#include <vector>

using timeline::Event;
using timeline::Interval;
using timeline::Source;

namespace {

Event point(Source source, std::string type, int variant, std::int64_t at)
{
    Event e;
    e.source    = source;
    e.type      = std::move(type);
    e.variant   = variant;
    e.ref_index = at;
    return e;
}

Event span(Source source, std::string type, int variant, std::int64_t from, std::int64_t to)
{
    Event e   = point(source, std::move(type), variant, from);
    e.ref_end = to;
    return e;
}

std::vector<std::string> ids_of(const std::vector<Event>& events)
{
    std::vector<std::string> out;
    for (const Event& e : events) out.push_back(e.id);
    return out;
}

// What an event is, without its id.
auto head(const Event& e)
{
    return std::make_tuple(e.ref_index, e.ref_end, e.source, e.type, e.variant);
}

// Twelve events around testgen's ad at reference frames 75 to 124, given in
// their canonical order, ids unset.
std::vector<Event> around_the_ad()
{
    return {
        span(Source::Playlist, "segment", 0, 50, 75),
        point(Source::Vmaf, "context_start", 2, 75),
        point(Source::Playlist, "discontinuity", 0, 75),
        point(Source::Playlist, "discontinuity", 2, 75),
        span(Source::Playlist, "segment", 1, 75, 75),   // an empty span ends where a point does
        point(Source::Playlist, "tag", 0, 75),
        point(Source::Scte35, "cue_out", 0, 75),
        point(Source::Player, "variant_switch", -1, 75),
        point(Source::Decoder, "keyframe", 0, 75),
        span(Source::Playlist, "segment", 0, 75, 100),
        span(Source::Playlist, "segment", 2, 75, 100),
        span(Source::Scte35, "ad_break", 0, 75, 125),
    };
}

} // namespace

TEST_CASE("timeline model: the sources' names, in section 3.1's order", "[timeline][timeline_model]")
{
    const std::vector<std::string> names = { "vmaf", "network", "playlist", "scte35", "player", "decoder" };
    for (std::size_t i = 0; i < timeline::kSourceCount; ++i) {
        const auto s = static_cast<Source>(i);
        CHECK(timeline::source_name(s) == names[i]);
        CHECK(timeline::source_from_name(names[i]) == s);
    }
    CHECK_FALSE(timeline::source_from_name("Vmaf").has_value());
    CHECK_FALSE(timeline::source_from_name("").has_value());
    CHECK(timeline::source_name(static_cast<Source>(6)).empty());
}

TEST_CASE("timeline model: the canonical order, whatever order the events come in",
          "[timeline][timeline_model]")
{
    std::vector<Event> expected = around_the_ad();
    timeline::assign_ids(expected);   // already in canonical order: the ids only
    const std::vector<std::string> order = ids_of(expected);
    CHECK(order == std::vector<std::string>{
                       "playlist/segment/0/0",          // starts first
                       "vmaf/context_start/2/0",        // then at frame 75, the points by source
                       "playlist/discontinuity/0/0",
                       "playlist/discontinuity/2/0",    // ... by variant
                       "playlist/segment/1/0",          // the empty span among them, by type
                       "playlist/tag/0/0",
                       "scte35/cue_out/0/0",
                       "player/variant_switch/-/0",     // no variant
                       "decoder/keyframe/0/0",
                       "playlist/segment/0/1",          // the spans, the shorter first
                       "playlist/segment/2/0",
                       "scte35/ad_break/0/0",
                   });

    std::mt19937_64 rng(20261103);
    for (int round = 0; round < 20; ++round) {
        std::vector<Event> shuffled = expected;
        std::shuffle(shuffled.begin(), shuffled.end(), rng);
        timeline::sort_events(shuffled);
        CHECK(ids_of(shuffled) == order);

        // Shuffled before their ids are given, the events get the same ids.
        std::vector<Event> fresh = around_the_ad();
        std::shuffle(fresh.begin(), fresh.end(), rng);
        timeline::assign_ids(fresh);
        CHECK(ids_of(fresh) == order);
        for (std::size_t i = 0; i < fresh.size(); ++i) CHECK(head(fresh[i]) == head(expected[i]));
    }

    for (std::size_t i = 0; i + 1 < expected.size(); ++i) {
        CHECK(timeline::canonical_before(expected[i], expected[i + 1]));
        CHECK_FALSE(timeline::canonical_before(expected[i + 1], expected[i]));
    }
    CHECK_FALSE(timeline::canonical_before(expected[3], expected[3]));
}

TEST_CASE("timeline model: ids number each source, type and variant, ties in their input order",
          "[timeline][timeline_model]")
{
    // Twelve tags of one segment, which only their payloads tell apart, and
    // a keyframe between them in the input: numbered as they come in.
    std::vector<Event> tags;
    for (std::int64_t k = 0; k < 12; ++k) {
        tags.push_back(point(Source::Playlist, "tag", 1, 100));
        tags.back().payload.emplace_back("line", timeline::Value{ 20 + k });
        if (k == 5) tags.push_back(point(Source::Decoder, "keyframe", 1, 100));
    }
    timeline::assign_ids(tags);
    REQUIRE(tags.size() == 13);
    for (std::int64_t k = 0; k < 12; ++k) {
        CHECK(tags[static_cast<std::size_t>(k)].id == "playlist/tag/1/" + std::to_string(k));
        CHECK(std::get<std::int64_t>(tags[static_cast<std::size_t>(k)].payload[0].second.v) == 20 + k);
    }
    CHECK(tags[12].id == "decoder/keyframe/1/0");

    // Sorted again, from any order, they keep their numbers' order:
    // "playlist/tag/1/10" after "playlist/tag/1/9".
    const std::vector<std::string> order = ids_of(tags);
    std::mt19937_64 rng(7);
    std::vector<Event> shuffled = tags;
    std::shuffle(shuffled.begin(), shuffled.end(), rng);
    timeline::sort_events(shuffled);
    CHECK(ids_of(shuffled) == order);

    // Identical inputs, identical ids.
    std::vector<Event> again = around_the_ad();
    std::vector<Event> other = around_the_ad();
    timeline::assign_ids(again);
    timeline::assign_ids(other);
    CHECK(ids_of(again) == ids_of(other));
}

TEST_CASE("timeline model: the overlap index against a scan of every event, on random events",
          "[timeline][timeline_model]")
{
    std::mt19937_64 rng(42);
    const auto draw = [&rng](std::int64_t n) { return static_cast<std::int64_t>(rng() % static_cast<std::uint64_t>(n)); };
    for (int round = 0; round < 20; ++round) {
        timeline::Timeline t;
        const std::int64_t n = 1 + draw(400);
        for (std::int64_t k = 0; k < n; ++k) {
            const std::int64_t at = draw(300);
            const auto source = static_cast<Source>(draw(6));
            switch (draw(4)) {
            case 0: t.events.push_back(point(source, "tag", static_cast<int>(draw(4)) - 1, at)); break;
            case 1: t.events.push_back(span(source, "segment", static_cast<int>(draw(4)) - 1, at, at + draw(30))); break;
            case 2: t.events.push_back(span(source, "ad_break", 0, at, at + draw(300))); break;   // long spans
            default: t.events.push_back(span(source, "segment", 1, at, at)); break;              // empty spans
            }
        }
        timeline::assign_ids(t.events);
        t.build_index();
        REQUIRE(t.max_end.size() == t.events.size());
        timeline::Timeline stale = t;
        stale.max_end.clear();

        for (int q = 0; q < 300; ++q) {
            const std::int64_t a = draw(340) - 20;
            const std::int64_t b = a + draw(90) - 10;   // empty and reversed intervals included
            std::vector<std::size_t> scan;
            for (std::size_t i = 0; i < t.events.size(); ++i)
                if (timeline::meets(t.events[i], Interval{ .ref_index = a, .ref_end = b })) scan.push_back(i);
            INFO("round " << round << ", interval [" << a << ", " << b << ")");
            CHECK(t.overlapping(a, b) == scan);
            CHECK(stale.overlapping(a, b) == scan);
        }
    }
}

TEST_CASE("timeline model: points and spans at an interval's exact boundaries",
          "[timeline][timeline_model]")
{
    // The ad's drop, [75, 125) (design §9.2).
    const Interval drop{ .ref_index = 75, .ref_end = 125 };
    CHECK(timeline::meets(point(Source::Scte35, "cue_out", 0, 75), drop));
    CHECK(timeline::meets(point(Source::Decoder, "keyframe", 0, 124), drop));
    CHECK_FALSE(timeline::meets(point(Source::Scte35, "cue_in", 0, 125), drop));
    CHECK_FALSE(timeline::meets(point(Source::Decoder, "corrupt_packet", 0, 74), drop));
    CHECK(timeline::meets(span(Source::Playlist, "segment", 0, 75, 100), drop));
    CHECK(timeline::meets(span(Source::Playlist, "segment", 0, 100, 125), drop));
    CHECK(timeline::meets(span(Source::Playlist, "segment", 0, 124, 125), drop));
    CHECK(timeline::meets(span(Source::Playlist, "segment", 0, 74, 76), drop));
    CHECK(timeline::meets(span(Source::Scte35, "ad_break", 0, 0, 200), drop));
    CHECK_FALSE(timeline::meets(span(Source::Playlist, "segment", 0, 50, 75), drop));
    CHECK_FALSE(timeline::meets(span(Source::Playlist, "segment", 0, 125, 150), drop));
    // An empty span [s, s) meets by the same rule: s < 125 and s > 75.
    CHECK(timeline::meets(span(Source::Playlist, "segment", 0, 100, 100), drop));
    CHECK_FALSE(timeline::meets(span(Source::Playlist, "segment", 0, 75, 75), drop));
    CHECK_FALSE(timeline::meets(span(Source::Playlist, "segment", 0, 125, 125), drop));

    timeline::Timeline t;
    t.events = {
        span(Source::Playlist, "segment", 0, 50, 75),
        point(Source::Decoder, "corrupt_packet", 0, 74),
        point(Source::Scte35, "cue_out", 0, 75),
        span(Source::Scte35, "ad_break", 0, 75, 125),
        span(Source::Playlist, "segment", 0, 100, 125),
        point(Source::Scte35, "cue_in", 0, 125),
        span(Source::Playlist, "segment", 0, 125, 150),
    };
    timeline::assign_ids(t.events);
    t.build_index();
    std::vector<std::string> met;
    for (std::size_t i : t.overlapping(75, 125)) met.push_back(t.events[i].id);
    CHECK(met == std::vector<std::string>{ "scte35/cue_out/0/0", "scte35/ad_break/0/0",
                                           "playlist/segment/0/1" });
    met.clear();
    for (std::size_t i : t.overlapping(74, 126)) met.push_back(t.events[i].id);
    CHECK(met == std::vector<std::string>{ "playlist/segment/0/0", "decoder/corrupt_packet/0/0",
                                           "scte35/cue_out/0/0", "scte35/ad_break/0/0",
                                           "playlist/segment/0/1", "scte35/cue_in/0/0",
                                           "playlist/segment/0/2" });
}

TEST_CASE("timeline model: an empty interval meets the spans that straddle it and no point",
          "[timeline][timeline_model]")
{
    // D-078's rule as written, for [100, 100): no point f has
    // 100 <= f < 100, and a span [s, e) meets it when s < 100 and e > 100.
    const Interval empty{ .ref_index = 100, .ref_end = 100 };
    const Event whole = span(Source::Scte35, "ad_break", 0, 0, 200);
    const Event over  = span(Source::Playlist, "segment", 0, 99, 101);
    const Event at    = point(Source::Decoder, "keyframe", 0, 100);
    const Event from  = span(Source::Playlist, "segment", 0, 100, 125);   // starts at frame 100
    const Event to    = span(Source::Playlist, "segment", 0, 75, 100);    // ends there
    const Event none  = span(Source::Playlist, "segment", 1, 100, 100);   // an empty span there
    CHECK(timeline::meets(whole, empty));
    CHECK(timeline::meets(over, empty));
    CHECK_FALSE(timeline::meets(at, empty));
    CHECK_FALSE(timeline::meets(from, empty));
    CHECK_FALSE(timeline::meets(to, empty));
    CHECK_FALSE(timeline::meets(none, empty));

    // A reversed interval, [120, 80), by the same rule: no point, and the
    // spans that start before frame 80 and end after frame 120.
    const Interval reversed{ .ref_index = 120, .ref_end = 80 };
    CHECK(timeline::meets(whole, reversed));
    CHECK_FALSE(timeline::meets(to, reversed));
    CHECK_FALSE(timeline::meets(over, reversed));
    CHECK_FALSE(timeline::meets(point(Source::Decoder, "keyframe", 0, 90), reversed));

    timeline::Timeline t;
    t.events = { whole, over, at, from, to, none };
    timeline::assign_ids(t.events);
    t.build_index();
    timeline::Timeline unindexed = t;
    unindexed.max_end.clear();
    const auto met = [](const timeline::Timeline& tl, std::int64_t a, std::int64_t b) {
        std::vector<std::string> out;
        for (std::size_t i : tl.overlapping(a, b)) out.push_back(tl.events[i].id);
        return out;
    };
    for (const timeline::Timeline* tl : { &t, &unindexed }) {
        CHECK(met(*tl, 100, 100) == std::vector<std::string>{ "scte35/ad_break/0/0", "playlist/segment/0/1" });
        CHECK(met(*tl, 120, 80) == std::vector<std::string>{ "scte35/ad_break/0/0" });
        CHECK(met(*tl, 100, 101) == std::vector<std::string>{ "scte35/ad_break/0/0", "playlist/segment/0/1",
                                                             "decoder/keyframe/0/0", "playlist/segment/0/2" });
    }
    CHECK(timeline::Timeline{}.overlapping(0, 10).empty());
    CHECK(timeline::Timeline{}.overlapping(10, 10).empty());
}

TEST_CASE("timeline model: a pair's curve is the one the timeline holds", "[timeline][timeline_model]")
{
    timeline::Timeline t;
    t.curves.resize(2);
    t.curves[1].variant       = 2;
    t.curves[1].ref_index     = { 0, 2, 4 };
    t.curves[1].vmaf          = { 91.5, 22.25, 97.0 };
    t.curves[1].context_first = { 0 };
    const timeline::Curve c = timeline::curve_points(t, timeline::CurveRef{ .index = 1 });
    CHECK(c.variant == 2);
    CHECK(c.ref_index == std::vector<std::int64_t>{ 0, 2, 4 });
    CHECK(c.vmaf == std::vector<double>{ 91.5, 22.25, 97.0 });
    CHECK(c.context_first == std::vector<std::int64_t>{ 0 });

    const timeline::Curve none = timeline::curve_points(t, timeline::CurveRef{ .index = 2 });
    CHECK(none.variant == -1);
    CHECK(none.ref_index.empty());
    CHECK(timeline::curve_points(t, timeline::CurveRef{ .index = -1 }).ref_index.empty());
}
