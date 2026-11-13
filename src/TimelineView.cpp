// The timeline panel's view math (eval/phase3-survey/design.md §10): written by WP12.

#include "TimelineView.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace timeline {

namespace {

using i128 = __int128;

// Ticks in seconds as Axis::seconds() computes them (src/TimelineAxis.cpp):
// exactly where both terms fit a double's 53 bits, else as quotient and
// remainder, so that a frame's position is the seconds the document gives it.
double seconds_of(i128 t, std::int64_t den)
{
    if (den <= 0) return 0.0;
    constexpr i128 kExact = i128{ 1 } << 53;
    if (t > -kExact && t < kExact && den < kExact)
        return static_cast<double>(static_cast<std::int64_t>(t)) / static_cast<double>(den);
    const i128 q = t / den, r = t % den;
    return static_cast<double>(q) + static_cast<double>(r) / static_cast<double>(den);
}

// Two runs of points as one, the earlier minimum kept on a tie.
MinMax combine(const MinMax& a, const MinMax& b)
{
    if (!a.any()) return b;
    if (!b.any()) return a;
    MinMax r;
    const bool a_low = a.min < b.min || (a.min == b.min && a.min_at <= b.min_at);
    r.min    = a_low ? a.min : b.min;
    r.min_at = a_low ? a.min_at : b.min_at;
    r.max    = std::max(a.max, b.max);
    r.points = a.points + b.points;
    return r;
}

} // namespace

// ── The x axis ───────────────────────────────────────────────────────────────

AxisScale axis_scale(const Axis& axis)
{
    AxisScale s;
    s.frames  = std::max<std::int64_t>(axis.frames, 0);
    s.seconds = axis.frame_duration.num > 0 && axis.frame_duration.den > 0 && axis.tick_den > 0;
    if (s.seconds) {
        s.tick_den = axis.tick_den;
        // A whole number of ticks: the axis's time base holds the frame
        // duration (src/TimelineAxis.h).
        s.step  = static_cast<std::int64_t>(static_cast<i128>(axis.frame_duration.num) * axis.tick_den /
                                            axis.frame_duration.den);
        s.ticks = axis.ticks;
        s.running_max.reserve(s.ticks.size());
        for (const std::int64_t t : s.ticks)
            s.running_max.push_back(s.running_max.empty() ? t : std::max(s.running_max.back(), t));
        s.frames = static_cast<std::int64_t>(s.ticks.size());
        s.lo     = s.position(0);
        s.hi     = s.position(s.frames);
        if (!s.running_max.empty()) s.hi = std::max(s.hi, seconds_of(s.running_max.back(), s.tick_den));
    } else {
        s.lo = 0.0;
        s.hi = static_cast<double>(s.frames);
    }
    // As the Alignment tab's extent: never empty.
    if (!(s.hi > s.lo)) s.hi = s.lo + 1.0;
    return s;
}

double AxisScale::position(std::int64_t ref_index) const
{
    if (!seconds) return static_cast<double>(ref_index);
    i128 t = 0;
    if (ticks.empty()) {
        t = static_cast<i128>(ref_index) * step;
    } else {
        const auto last = static_cast<std::int64_t>(ticks.size()) - 1;
        if (ref_index < 0)          t = ticks.front() + static_cast<i128>(ref_index) * step;
        else if (ref_index > last)  t = ticks.back() + static_cast<i128>(ref_index - last) * step;
        else                        t = ticks[static_cast<std::size_t>(ref_index)];
    }
    return seconds_of(t, tick_den);
}

std::int64_t AxisScale::frame_at_or_after(double p) const
{
    if (!seconds) {
        if (!(p > 0.0)) return 0;
        if (!(p < static_cast<double>(frames))) return frames;
        return std::min(frames, static_cast<std::int64_t>(std::ceil(p)));
    }
    // The first frame whose running maximum reaches p is the first whose own
    // position does, and the running maximum never falls.
    const auto at = std::partition_point(running_max.begin(), running_max.end(),
                                         [&](std::int64_t m) { return seconds_of(m, tick_den) < p; });
    return static_cast<std::int64_t>(at - running_max.begin());
}

std::int64_t AxisScale::frame_after(double p) const
{
    if (!seconds) {
        if (p < 0.0) return 0;
        if (!(p < static_cast<double>(frames))) return frames;
        return std::min(frames, static_cast<std::int64_t>(std::floor(p)) + 1);
    }
    const auto at = std::partition_point(running_max.begin(), running_max.end(),
                                         [&](std::int64_t m) { return seconds_of(m, tick_den) <= p; });
    return static_cast<std::int64_t>(at - running_max.begin());
}

std::int64_t AxisScale::frame_ending_after(double p) const
{
    // The frame before the first that starts after p: its span runs to that
    // one's position, after p, and every earlier frame's ends at or before
    // p, the running maximum never falling. Past the last frame's start, the
    // last frame while its end is after p.
    const std::int64_t f = frame_after(p);
    if (f == 0) return 0;
    if (f == frames && !(position(frames) > p)) return frames;
    return f - 1;
}

// ── The view ─────────────────────────────────────────────────────────────────

double max_span(const AxisScale& scale)
{
    return (scale.hi - scale.lo) * kMaxSpanFactor + kMaxSpanMargin;
}

void fit_view(View& view, const AxisScale& scale)
{
    view.t0  = scale.lo;
    view.t1  = scale.hi;
    view.fit = true;
}

void zoom_at(View& view, const AxisScale& scale, double at, float wheel)
{
    if (wheel == 0.0f) return;
    double span = view.span();
    if (!(span > 0.0)) span = scale.hi - scale.lo;
    const double f  = wheel > 0.0f ? kZoomIn : kZoomOut;
    const double ns = std::clamp(span * f, kMinSpan, std::max(kMinSpan, max_span(scale)));
    view.t0  = at - (at - view.t0) / span * ns;
    view.t1  = view.t0 + ns;
    view.fit = false;
}

void pan_px(View& view, float dx, float width)
{
    if (!(width > 0.0f)) return;
    const double dt = -static_cast<double>(dx) / static_cast<double>(width) * view.span();
    view.t0 += dt;
    view.t1 += dt;
    view.fit = false;
}

void clamp_view(View& view, const AxisScale& scale)
{
    const double span   = std::clamp(view.span(), kMinSpan, std::max(kMinSpan, max_span(scale)));
    const double centre = std::clamp(0.5 * (view.t0 + view.t1), scale.lo, scale.hi);
    view.t0 = centre - 0.5 * span;
    view.t1 = view.t0 + span;
}

double position_at(const View& view, float x, float x0, float width)
{
    return view.t0 + static_cast<double>(x - x0) / static_cast<double>(width) * view.span();
}

float x_at(const View& view, double position, float x0, float width)
{
    return x0 + static_cast<float>((position - view.t0) / view.span() * static_cast<double>(width));
}

// ── Envelopes ────────────────────────────────────────────────────────────────

Pyramid build_pyramid(const std::vector<double>& scores)
{
    Pyramid p;
    if (scores.empty()) return p;
    std::vector<MinMax> level(scores.size());
    for (std::size_t i = 0; i < scores.size(); ++i) {
        level[i].min_at = i;
        if (!std::isfinite(scores[i])) continue;
        level[i].min = level[i].max = scores[i];
        level[i].points = 1;
    }
    p.levels.push_back(std::move(level));
    while (p.levels.back().size() > 1) {
        const std::vector<MinMax>& below = p.levels.back();
        std::vector<MinMax> up((below.size() + 1) / 2);
        for (std::size_t j = 0; j < up.size(); ++j)
            up[j] = 2 * j + 1 < below.size() ? combine(below[2 * j], below[2 * j + 1]) : below[2 * j];
        p.levels.push_back(std::move(up));
    }
    return p;
}

MinMax range_min_max(const Pyramid& pyramid, std::size_t first, std::size_t last)
{
    MinMax acc;
    last = std::min(last, pyramid.size());
    if (first >= last) return acc;
    // Bottom up: a node at an odd place on either end is the only one of its
    // parent's two inside the run, so it is taken here and the run narrowed.
    std::size_t l = first, r = last;
    for (std::size_t k = 0; l < r && k < pyramid.levels.size(); ++k) {
        const std::vector<MinMax>& level = pyramid.levels[k];
        if (l & 1) acc = combine(acc, level[l++]);
        if (r & 1) acc = combine(acc, level[--r]);
        l >>= 1;
        r >>= 1;
    }
    return acc;
}

PointRange points_in(const Curve& curve, std::int64_t first_frame, std::int64_t end_frame)
{
    const std::size_t n = std::min(curve.ref_index.size(), curve.vmaf.size());
    const auto begin = curve.ref_index.begin();
    const auto end   = begin + static_cast<std::ptrdiff_t>(n);
    PointRange r;
    r.first = static_cast<std::size_t>(std::lower_bound(begin, end, first_frame) - begin);
    r.last  = std::max(r.first, static_cast<std::size_t>(std::lower_bound(begin, end, end_frame) - begin));
    return r;
}

PointRange points_meeting(const Curve& curve, std::int64_t first_frame, std::int64_t end_frame)
{
    if (first_frame >= end_frame) return {};
    PointRange r = points_in(curve, first_frame, end_frame);
    // The point before the run stands for the frames up to the run's first
    // point, or up to the next point after the frames where the run is
    // empty: it meets them when that point comes after first_frame. Past the
    // last point, which stands for its own frame alone, nothing does.
    const std::size_t n = std::min(curve.ref_index.size(), curve.vmaf.size());
    if (r.first > 0 && r.first < n && curve.ref_index[r.first] > first_frame) --r.first;
    return r;
}

std::vector<MinMax> envelope(const Curve& curve, const Pyramid& pyramid, const AxisScale& scale,
                             const View& view, int columns)
{
    std::vector<MinMax> out;
    if (columns <= 0 || !(view.span() > 0.0)) return out;
    // Each column's first frame, and the view's end; the edges are computed
    // from the column's number, not added up, so that no rounding builds up.
    std::vector<std::int64_t> edge(static_cast<std::size_t>(columns) + 1);
    for (int c = 0; c <= columns; ++c)
        edge[static_cast<std::size_t>(c)] = scale.frame_at_or_after(
            c == columns ? view.t1 : view.t0 + view.span() * c / columns);
    out.resize(static_cast<std::size_t>(columns));
    for (std::size_t c = 0; c < out.size(); ++c) {
        // The first column from the frame under the view's left edge, with
        // the point whose span reaches it, so that a frame partly in view is
        // drawn; frame_ending_after(t0) is at or before edge[0].
        const PointRange pr =
            c == 0 ? points_meeting(curve, std::min(scale.frame_ending_after(view.t0), edge[0]),
                                    std::max(edge[0], edge[1]))
                   : points_in(curve, edge[c], std::max(edge[c], edge[c + 1]));
        out[c] = range_min_max(pyramid, pr.first, pr.last);
    }
    return out;
}

// ── Hit testing ──────────────────────────────────────────────────────────────

std::optional<PickedPoint> pick_lowest(const Curve& curve, const Pyramid& pyramid,
                                       const AxisScale& scale, const View& view, float x,
                                       float x0, float width, float radius)
{
    if (!(width > 0.0f) || !(view.span() > 0.0)) return std::nullopt;
    const double lo = position_at(view, x - std::max(radius, 0.0f), x0, width);
    const double hi = position_at(view, x + std::max(radius, 0.0f), x0, width);
    // The frames from the one under lo to the last that starts at or before
    // hi, and the points whose spans meet them.
    const PointRange pr = points_meeting(curve, scale.frame_ending_after(lo), scale.frame_after(hi));
    const MinMax m = range_min_max(pyramid, pr.first, pr.last);
    if (!m.any() || m.min_at >= curve.ref_index.size()) return std::nullopt;
    return PickedPoint{ .point = m.min_at, .ref_index = curve.ref_index[m.min_at], .vmaf = m.min };
}

std::vector<MarkerCluster> cluster_markers(const std::vector<float>& xs, float spacing)
{
    std::vector<MarkerCluster> out;
    for (std::size_t i = 0; i < xs.size(); ++i) {
        if (!out.empty() && xs[i] - xs[out.back().first] < spacing) {
            ++out.back().count;
            out.back().x1 = xs[i];
            continue;
        }
        out.push_back({ .first = i, .count = 1, .x0 = xs[i], .x1 = xs[i] });
    }
    return out;
}

std::optional<std::size_t> nearest_cluster(const std::vector<MarkerCluster>& clusters, float x,
                                           float radius)
{
    std::optional<std::size_t> best;
    float best_d = 0.0f;
    for (std::size_t i = 0; i < clusters.size(); ++i) {
        const MarkerCluster& c = clusters[i];
        const float d = x < c.x0 ? c.x0 - x : x > c.x1 ? x - c.x1 : 0.0f;
        if (d > radius) continue;
        if (!best || d < best_d) {
            best   = i;
            best_d = d;
        }
    }
    return best;
}

// ── The axis's ticks: copies of src/NetInspectorAlign.cpp's (P-AJ) ──────────

double nice_step(double span, float width, float min_px)
{
    static constexpr double kSteps[] = { 0.1, 0.25, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600, 7200 };
    for (double s : kSteps)
        if (span / s * min_px <= width) return s;
    return 14400;
}

std::string axis_label(double t, double step)
{
    if (step < 1.0) return std::format("{:.2f}", t);
    const auto s = static_cast<std::int64_t>(std::llround(t));
    if (s >= 3600) return std::format("{}:{:02}:{:02}", s / 3600, s / 60 % 60, s % 60);
    return std::format("{}:{:02}", s / 60, s % 60);
}

} // namespace timeline
