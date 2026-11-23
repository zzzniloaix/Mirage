#pragma once

// Drops in a curve below its trailing median (DECISIONS.md D-077;
// eval/phase3-survey/design.md §6.3). detect() walks one curve's points
// once, as the design's prototype does
// (eval/phase3-survey/design/tools/detection_semantics.py:37-60): plain
// structs in and out and nothing but arithmetic, so that it is unit-tested
// on hand-built curves (tests/test_timeline_detect.cpp). Detection runs on
// each pair's curve; a session's drops are its pairs' drops clipped to the
// spans that played them (D-077; design §6.3), derived from these by the
// sessions' code.
//
// The rules (D-077, the owner's answers to the design's Q22 to Q27, and
// D-081 (g)):
// - Points. W and N count points of the curve: the pair's scored frames, at
//   the reference frames they are paired with. A reference frame without a
//   point, as between the points of a coarser grid or at an unpaired frame,
//   is skipped, and nothing is invented there. A score that isn't a finite
//   number is no score: its point is skipped in the same way, as if the
//   curve didn't have it.
// - The baseline at a point: the median of the last W points before it that
//   weren't low, once warm_up(W) such points exist, W/2 rounded down and at
//   least 1, as the prototype counts it (detection_semantics.py:42). Until
//   then a point has no baseline and can't be low, so the first warm_up(W)
//   points of a curve are never examined: at W = 50 the first 25, the whole
//   first segment of a D-069 ladder (design §6.3). The median of an even
//   number of scores is the mean of the middle two, (a + b) / 2, as the
//   prototype's statistics.median computes it.
// - Low: a point whose score is below its baseline minus T. A low point
//   never enters the baseline, so a drop doesn't pull its own baseline
//   down, and the baseline is the same at every point of a drop.
// - A drop: a run of more than N consecutive low points.
// - The first pair of each libvmaf context, which scores low (D-037), is a
//   point like any other: N of 1 or more passes over a dip of one point, and
//   a drop whose first point it is starts at it, as the ad's drop on
//   testgen's ad ladder starts at its cue-out's frame
//   (eval/phase3-survey/design/runs/detection-semantics-ad.txt).
// - A drop's interval, half-open on reference frames: from its first point's
//   frame to the frame of the curve's next point, or to its last point's
//   frame plus one where the curve ends (D-081 (g)), since on a coarser grid
//   a point stands for the reference frames up to the next.
// - Its numbers: the baseline at its first point, the minimum and the mean
//   of its points' scores, the mean summed in index order; drop_magnitude,
//   the baseline minus the minimum, and mean_drop, the baseline minus the
//   mean.
// - T is 6 by default. W and N have no default: detection runs only when
//   both are given, and without them detect() finds nothing, the timeline
//   document then saying that no detection ran.

#include "TimelineModel.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace timeline {

// T's default, in VMAF points (D-077).
inline constexpr double kDefaultThreshold = 6.0;

// What detection runs with. Each value changes the drops found, so the
// timeline document records them all (detection_record()).
struct DetectOptions {
    // T, in VMAF points: a point more than T below its baseline is low.
    double threshold = kDefaultThreshold;
    // W: the baseline is the median of the last W points that weren't low.
    std::optional<std::int64_t> window;
    // N: a drop is a run of more than N low points.
    std::optional<std::int64_t> min_points;

    // Whether W and N are both given: detection runs only then (D-077).
    bool given() const { return window.has_value() && min_points.has_value(); }
};

// What is wrong with `options`, or empty when detection can run with every
// value they give: T a finite number from 0, W a whole number of points
// from 1, N one from 0. W or N not given is no error: detection then
// doesn't run.
std::string check_options(const DetectOptions& options);

// The points that weren't low a baseline needs before any point is
// examined: W/2 rounded down, and at least 1.
std::int64_t warm_up(std::int64_t window);

// One drop of a curve.
struct Drop {
    Interval     interval;          // [its first point's frame, the next point's), D-081 (g)
    std::size_t  first_point = 0;   // its first low point: a position in the curve's vectors
    std::size_t  last_point  = 0;   // its last low point
    std::int64_t points      = 0;   // its low points, more than N; skipped points aren't counted
    double baseline       = 0.0;    // at its first point, and so at every point of it
    double min            = 0.0;    // of its points' scores
    double mean           = 0.0;
    double drop_magnitude = 0.0;    // baseline - min
    double mean_drop      = 0.0;    // baseline - mean
};

// The drops of `curve`, in its order. None, and no error, when W and N
// aren't both given (D-077). None, and `err` says why, when an option is
// invalid (check_options()) or `curve` isn't a curve: its ref_index and
// vmaf of different lengths, or its reference frames not positions from 0
// that ascend. Curve::context_first changes nothing (D-077).
std::vector<Drop> detect(const Curve& curve, const DetectOptions& options, std::string& err);

// The parameters detection ran with, as the timeline document records
// them: the keys of the design's §8 detection record that detection
// decides, in its order. "threshold", "window" and "min_points" (null when
// not given); "baseline", the statistic: "median-non-low", the median of
// the last W points that weren't low; "warm_up": warm_up(W), the points at
// a curve's start that can't be low (null without W); "context_first":
// "no-special-case", how a libvmaf context's first pair is taken (D-077).
// The caller adds which curves were detected.
Payload detection_record(const DetectOptions& options);

} // namespace timeline
