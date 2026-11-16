#pragma once

// The timeline panel's view math, ImGui-free: zoom, pan and fit, the curves' min/max envelopes
// and hit testing (eval/phase3-survey/design.md §10; DECISIONS.md D-114 (a)): written by WP12.
//
// eval/phase3-survey/ui.md §2.5's view math, which the panel (src/TimelinePanel.h,
// WP13) draws with and which mirage_tests tests without ImGui or GL (D-031):
//
// - The x axis. A position on it is the document's seconds of a reference
//   frame, the axis's (D-073: the reference's playlist time, or the frame grid
//   where the reference has no segment map), so that the panel's x and the
//   offsets the document and the CLI print are one clock; on an axis without
//   seconds, a frame's index (AxisScale), the panel drawing such an axis in
//   frames and labelling frame numbers, as built, which the owner kept
//   (D-121 (e), Q90 (a)). Positions go to frames as the axis turns seconds
//   into frames, the first frame in index order at or after them (D-081
//   (g)), which is monotonic: the frames between two positions are one run
//   of frames even where the playlist time steps back (src/TimelineAxis.h).
// - The view: the positions at the plot's two edges, fitting the extent until
//   the user zooms or pans; the wheel zooms about the cursor by 0.8 or 1.25,
//   the span clamped to [0.5, 1.5 x extent + 1], a drag pans, a double-click
//   fits, and a release that moved less than 3 px is a click: the Alignment
//   tab's interactions and limits (src/NetInspectorAlign.cpp's timeline),
//   whose span limits are seconds; on an axis without seconds the same
//   numbers are frames.
// - A point's span. A curve's point stands for the reference frames from its
//   own up to the next point's, the last point for its own frame, as a
//   drop's interval reads the points (D-081 (g)): on a coarser grid the
//   frames between two points are the earlier one's. Its span on x runs from
//   its frame's position to the next point's. A session's curve has no point
//   over a span that isn't scored (D-105 (b)), so the point before such a
//   span stands for that span's frames here too, as built: Q91, still
//   pending the owner, whose alternative is such a span outside the data,
//   which would need the session's spans here.
// - Envelopes. Each curve's scores as a min/max pyramid, built once when a
//   document is loaded (src/TimelineLoad.h; ui.md §4.2), so that a column's
//   minimum and maximum cost O(log n), and a redraw, each column's edges
//   found by binary search, O(columns x log n) per curve. A column holds the
//   points whose frames lie between its two edges, so that every point in
//   the view is in exactly one column: a one-frame dip is its column's
//   minimum at every zoom, where the player's two existing curves average a
//   column and flatten it (ui.md §1.8, F4). The first column also holds the
//   point whose span reaches into the view from its left edge, so that a
//   frame partly in view is drawn.
// - Hit testing: the lowest point among those whose spans meet the cursor
//   plus or minus a radius, the earliest of equal minima (ui.md §2.1), so
//   that a click anywhere over a point's span picks it at every zoom; markers
//   that lie closer than a spacing clustered with a count, and the cluster
//   nearest the cursor (ui.md §4.2). The radius and the spacing are the
//   caller's, in the panel's px, which the design doesn't give: no default
//   here.
// - nice_step() and axis_label(), the axis's ticks and their text: copies of
//   the Alignment tab's (src/NetInspectorAlign.cpp, in its anonymous
//   namespace), which stays unchanged (the design's P-AJ). Both are for
//   seconds: on an axis without seconds the panel chooses and labels its
//   ticks itself, as frame indices.
//
// Plain structs and functions; nothing here reads a clock, a file or the
// player's state. Unit-tested in tests/test_timeline_view.cpp.

#include "TimelineAxis.h"
#include "TimelineModel.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace timeline {

// ── The x axis ───────────────────────────────────────────────────────────────

// The axis's positions, for drawing: each reference frame's seconds on the
// document's axis (Axis::ticks over Axis::tick_den, D-073), or, on an axis
// without seconds (Axis::frame_duration 0/1, every frame at 0 s), the frame's
// index, so that the frames can still be told apart; such an axis is drawn
// in frames, the span limits counted in frames (kMinSpan), as built, which
// the owner kept (D-121 (e), Q90 (a)). The axis's ticks are copied with
// their running maximum, so that a position goes to its frame by a binary
// search.
struct AxisScale {
    bool seconds = false;                    // positions are seconds; false: frame indices
    std::int64_t frames = 0;                 // the reference's frames
    std::int64_t tick_den = 1;               // seconds are ticks / tick_den
    std::int64_t step = 0;                   // the frame duration in ticks, for past the frames
    std::vector<std::int64_t> ticks;         // per frame, as Axis::ticks
    std::vector<std::int64_t> running_max;   // per frame, the largest of ticks[0] to ticks[i]
    double lo = 0, hi = 1;                   // the extent: frame 0's position and the end of the
                                             // last frame, hi > lo

    // Reference frame `ref_index`'s position: its seconds as Axis::seconds()
    // gives them, past either end of the frames extrapolated by the frame
    // duration (a span's end, one past the last frame, included); or its index.
    double position(std::int64_t ref_index) const;
    // The first frame, in index order, whose position is at or after `p`, or
    // `frames` when none is (as Axis::frame_at_or_after(), D-081 (g)).
    std::int64_t frame_at_or_after(double p) const;
    // The first frame, in index order, whose position is after `p`, or `frames`.
    std::int64_t frame_after(double p) const;
    // The first frame, in index order, whose span ends after `p`, a frame's
    // span running to the next frame's position, the last frame's to
    // position(frames): the frame under `p`; frame 0 for a `p` before it, and
    // `frames` for one at or past the last frame's end.
    std::int64_t frame_ending_after(double p) const;
};

// The scale of `axis`, as above.
AxisScale axis_scale(const Axis& axis);

// ── The view ─────────────────────────────────────────────────────────────────

// The Alignment tab's interactions (src/NetInspectorAlign.cpp): the wheel's
// two factors, the narrowest span (seconds, or frames on an axis without
// seconds), and the widest as a multiple of the extent plus a margin.
inline constexpr double kZoomIn        = 0.8;
inline constexpr double kZoomOut       = 1.25;
inline constexpr double kMinSpan       = 0.5;
inline constexpr double kMaxSpanFactor = 1.5;
inline constexpr double kMaxSpanMargin = 1.0;
// A release whose drag stayed under 3 px, as ImGui's MouseDragMaxDistanceSqr
// measures it, is a click, not a pan (the Alignment tab's test).
inline constexpr float kClickDragSqPx = 9.0f;

// What the panel shows: the positions at the plot's left and right edges.
// While `fit` holds the view follows the extent; a zoom or a pan ends it,
// a double-click brings it back (fit_view()).
struct View {
    double t0 = 0, t1 = 1;
    bool   fit = true;

    double span() const { return t1 - t0; }
};

// The widest span of `scale`'s view: 1.5 times its extent, plus 1.
double max_span(const AxisScale& scale);

// The view fitted to `scale`'s extent, fit set: what a double-click does.
void fit_view(View& view, const AxisScale& scale);
// One wheel step at position `at` (wheel > 0: in, by kZoomIn; < 0: out, by
// kZoomOut; 0: nothing): the span scaled and clamped to [kMinSpan,
// max_span()], `at` kept where it was on the plot, fit cleared.
void zoom_at(View& view, const AxisScale& scale, double at, float wheel);
// A drag of `dx` px over a plot `width` px wide (width > 0): the view moves
// by -dx x span / width, the content following the cursor; fit cleared.
void pan_px(View& view, float dx, float width);
// The view kept on the data: its span clamped to [kMinSpan, max_span()]
// about its centre, then the view moved, its span kept, until its centre is
// within the extent, so that a pan or a zoom never leaves the plot empty.
// fit is left as it is.
void clamp_view(View& view, const AxisScale& scale);

// Whether a release whose drag reached `drag_sq` px squared is a click.
inline bool is_click(float drag_sq) { return drag_sq < kClickDragSqPx; }

// The position at x on a plot that starts at `x0` and is `width` px wide
// (width > 0), and the x of a position.
double position_at(const View& view, float x, float x0, float width);
float  x_at(const View& view, double position, float x0, float width);

// ── Envelopes ────────────────────────────────────────────────────────────────

// The minimum and the maximum of a run of a curve's points, counting only
// those with a score: a point whose score is NaN, null in the report, is
// skipped (src/LadderReportRead.h). `min_at` is the minimum's point, the
// earliest of equal ones; `points` how many points had a score, 0 for an
// empty run, whose other members mean nothing.
struct MinMax {
    double      min = 0, max = 0;
    std::size_t min_at = 0;
    std::size_t points = 0;

    bool any() const { return points > 0; }
};

// A curve's scores as a min/max pyramid (ui.md §2.5): level 0 holds each
// point, and node j of level k + 1 the points of level k's nodes 2j and
// 2j + 1, so that node j of level k holds points j x 2^k to
// (j + 1) x 2^k - 1, the last node of a level what is left.
struct Pyramid {
    std::vector<std::vector<MinMax>> levels;

    std::size_t size() const { return levels.empty() ? 0 : levels.front().size(); }
};

Pyramid build_pyramid(const std::vector<double>& scores);

// Points [first, last) of the pyramid's curve, from O(log n) of its nodes;
// empty (points 0) for an empty run, `last` clamped to the curve.
MinMax range_min_max(const Pyramid& pyramid, std::size_t first, std::size_t last);

// The points of `curve` whose reference frames are in [first_frame,
// end_frame), as places among its points, [first, last): the curve's points
// ascend by frame (src/TimelineModel.h).
struct PointRange {
    std::size_t first = 0, last = 0;
};
PointRange points_in(const Curve& curve, std::int64_t first_frame, std::int64_t end_frame);

// The points of `curve` whose spans meet the reference frames [first_frame,
// end_frame), a point's span running from its frame to the next point's,
// the last point's to its frame plus one (this file's head; D-081 (g)):
// points_in()'s run, and the point before it when that point's span reaches
// first_frame. Empty for an empty run of frames.
PointRange points_meeting(const Curve& curve, std::int64_t first_frame, std::int64_t end_frame);

// The envelope of `curve` over `columns` columns of the view: column c holds
// the frames from frame_at_or_after(t0 + c x span / columns) up to the next
// column's first, and so the points at those frames; the first column holds
// the frames from frame_ending_after(t0) instead, with the points whose
// spans meet them (points_meeting()), so that a frame partly in view at the
// left edge is drawn. The columns share out every point whose span meets
// the view, each in one of them. None for a view whose span isn't positive,
// or no column.
std::vector<MinMax> envelope(const Curve& curve, const Pyramid& pyramid, const AxisScale& scale,
                             const View& view, int columns);

// ── Hit testing ──────────────────────────────────────────────────────────────

// A point of a curve that the cursor picked.
struct PickedPoint {
    std::size_t  point = 0;       // its place among the curve's points
    std::int64_t ref_index = 0;   // its reference frame
    double       vmaf = 0;
};

// The lowest point of `curve` among those whose spans meet the positions
// within `radius` px of x, both ends included: the points that meet the
// frames from frame_ending_after(x - radius) to the last at or before x +
// radius (points_meeting()), so that a click anywhere over a point's span
// picks it, however wide the zoom draws it. The earliest of equal minima
// (ui.md §2.1); nullopt where no point with a score has its span there,
// outside the data among them.
std::optional<PickedPoint> pick_lowest(const Curve& curve, const Pyramid& pyramid,
                                       const AxisScale& scale, const View& view, float x,
                                       float x0, float width, float radius);

// Markers that lie closer than a spacing, as one with a count (ui.md §4.2):
// from ascending xs, greedily from the left, a cluster takes each marker
// less than `spacing` px after its own first. Each cluster names its markers
// by their places in the input, first to first + count - 1, and spans x0 to
// x1, the x of its first and its last marker; the counts add up to the
// markers.
struct MarkerCluster {
    std::size_t first = 0, count = 0;
    float       x0 = 0, x1 = 0;
};
std::vector<MarkerCluster> cluster_markers(const std::vector<float>& xs, float spacing);

// The cluster nearest x among those within `radius` px of it, a cluster's
// distance being 0 inside [x0, x1]; the first of equally near ones; nullopt
// when none is that near.
std::optional<std::size_t> nearest_cluster(const std::vector<MarkerCluster>& clusters, float x,
                                           float radius);

// ── The axis's ticks ─────────────────────────────────────────────────────────

// A tick step, in seconds, whose ticks lie at least `min_px` apart over
// `span` seconds drawn `width` px wide: the first of the Alignment tab's
// steps that does, 14400 past them. A copy of src/NetInspectorAlign.cpp's
// (P-AJ). Its steps are seconds, minutes and hours: on an axis without
// seconds (AxisScale::seconds false) the panel steps its frame indices
// itself.
double nice_step(double span, float width, float min_px);

// A tick's text: "12.34" for steps under 1 s, else "1:05", or "1:02:03" from
// an hour on. A copy of src/NetInspectorAlign.cpp's (P-AJ). Seconds only: on
// an axis without seconds the panel labels its frame indices itself, which
// this would print as minutes and seconds.
std::string axis_label(double t, double step);

} // namespace timeline
