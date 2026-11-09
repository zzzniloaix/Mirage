#pragma once

// The events an interval of a curve meets, by source (DECISIONS.md D-078): written by WP01,
// a session's curve by WP07.
//
// attribute() answers eval/phase3-survey/design.md §6.1's query for a pair's
// curve or a session's: the events of the curve's own inputs (Q28 (a)),
// each rung's copy of a tag kept (D-081 (f)), that meet a half-open
// interval of reference frames by D-078's rule, an empty interval included,
// widened by margins given in seconds, none by default (Q29 (c)); in groups
// in section 3.1's order of sources, by offset within a group, with no
// ranking (Q30 (a)); each with its offsets in reference frames, exact, and
// in seconds on the axis (D-073).
//
// The scope rests on Event::variant and Event::session (src/TimelineModel.h).
// A pair's curve's inputs are its distorted variant and the reference: a
// pair's own events, its vmaf ones (context_start, unpaired_frame) among
// them, carry the index of its distorted variant, and the reference's
// events the reference variant's, the one whose pair is -1. A session's
// curve's inputs are the session's own events (its spans, switches and
// requests), the reference's, and a played variant's events in the part of
// the interval that a span playing it covers, so no other rung's copy of a
// tag (in_session_scope(), src/TimelineQuery.cpp). An event of a session is
// in that session's curve's scope alone, whatever its variant, and any other
// event of variant -1 in no curve's scope.

#include "TimelineModel.h"

extern "C" {
#include <libavutil/rational.h>
}

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace timeline {

// How an interval is widened before events are looked for (D-078).
struct QueryOptions {
    // Seconds on the axis before the interval's start and after its end;
    // zero, the default, leaves it as it is (Q29 (c)). A margin widens the
    // interval only: one of zero or less changes nothing.
    AVRational margin_before = { 0, 1 };
    AVRational margin_after  = { 0, 1 };
};

// One event an attribution lists, with its offsets from the interval's
// start (design §6.1).
struct Found {
    std::size_t  event = 0;                          // index into Timeline::events
    std::int64_t offset_frames = 0;                  // its ref_index minus the interval's
    std::optional<std::int64_t> offset_end_frames;   // a span's: its ref_end minus the interval's start
    double       offset_s = 0;                       // the same in seconds on the axis, for output
    std::optional<double> offset_end_s;
};

// What attribute() answers for an interval of a curve (design §6.1; D-077,
// D-078). The detection fields are a detected drop's, which the caller
// fills in from detect()'s drop (src/TimelineDetect.h); attribute() leaves
// them empty.
struct Attribution {
    CurveRef     curve;
    Interval     interval;                 // as asked: the drop's, or a user's
    Interval     searched;                 // the interval with its margins, in reference frames
    bool         detected = false;         // a detected drop, or a user's interval
    std::int64_t points = 0;               // the curve's points with a score in `interval`
    std::optional<double> baseline;        // the drop's (D-077)
    std::optional<double> min, mean;       // of those points' scores; nullopt without one
    std::optional<double> drop_magnitude, mean_drop;   // the drop's (D-077)
    std::array<std::vector<Found>, kSourceCount> by_source;   // indexed by Source
};

// `interval` widened by `options`' margins on `axis` (D-078): the start
// moves to the first frame, in index order, at or after its seconds minus
// margin_before, the end to the first frame at or after its seconds plus
// margin_after (D-081 (g); Axis::frame_at_or_after()), each only outwards.
// Every interval is widened so, an empty one included.
Interval widen(const Axis& axis, Interval interval, const QueryOptions& options);

// An interval given in seconds on the axis, [from, to), in reference
// frames: each end at the first frame at or after it (D-081 (g)).
Interval seconds_interval(const Axis& axis, AVRational from, AVRational to);

// The events of `curve`'s inputs that meet `interval` widened by `options`
// (see above), and the curve's points in `interval`. An empty interval,
// [a, a), has no point of the curve and meets the spans that straddle a,
// s < a < e, and no point event (D-078; meets()). For a curve the timeline
// doesn't have, nothing is found. Within a group, events of equal offset
// come in the order of Timeline::events, the canonical order once
// Timeline::build_index() has run, which also lets the query look at the
// events near the interval only; build_index() must run again after any
// change to the events (Timeline::max_end).
Attribution attribute(const Timeline& timeline, CurveRef curve, Interval interval,
                      const QueryOptions& options);

} // namespace timeline
