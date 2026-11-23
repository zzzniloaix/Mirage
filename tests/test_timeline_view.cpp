// Unit tests of src/TimelineView.h (eval/phase3-survey/design.md §9.1): written by WP12, a
// session's span that isn't scored by WP12c.
//
// eval/phase3-survey/ui.md §5.1's view math, without ImGui or GL (D-031): the
// axis's positions, the axis's own seconds and their frames, on a frame grid
// and where the playlist time steps back; x and positions round trip, a zoom
// keeps the position under the cursor and its span limits, a pan of dx px
// moves the view by dx x span / width, fit and clamping; nice_step() and
// axis_label() give the Alignment tab's results, its step table whole
// (P-AJ); the pyramid against brute force at every level, NaN scores
// skipped; each column's minimum and maximum against brute force on random
// curves, every point whose span meets the view in one column, empty
// columns, a frame partly in view at the left edge drawn; a one-frame dip
// that survives at every zoom (the regression test for ui.md §1.8, F4); the
// lowest point whose span meets the cursor's reach, against brute force on
// random views, the earliest of equal minima, nothing outside the data, a
// click inside a zoomed-in frame, every x of a fitted curve, and between two
// points of a coarser grid; markers clustered with their counts, and the
// nearest. A span outside the data, a session's span that isn't scored
// (D-124 (a)), on a curve shaped as a session's that plays the reference
// over D-069's ad: the point before it standing for the frames up to its
// start only, no point meeting its frames, its columns holding no score, the
// first column's among them, and no point picked in it, across it and at
// its two ends, with the overloads without the spans, a curve's with no
// such span, kept as they were; and each against brute force on random
// curves with random such spans, empty ones and adjacent ones among them.

#include <catch2/catch_test_macros.hpp>

#include "TimelineAxis.h"
#include "TimelineModel.h"
#include "TimelineView.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <random>
#include <vector>

using timeline::AxisScale;
using timeline::Curve;
using timeline::MinMax;
using timeline::View;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// An axis of `frames` frames at 25 fps, two ticks a frame (tick_den 50),
// each frame at its index times the frame duration.
timeline::Axis grid_axis(std::int64_t frames)
{
    timeline::Axis a;
    a.clock          = "frame-grid";
    a.checked        = "report-only";
    a.time_base      = { 1, 90000 };
    a.frame_duration = { 1, 25 };
    a.frames         = frames;
    a.tick_den       = 50;
    for (std::int64_t i = 0; i < frames; ++i) {
        a.ticks.push_back(2 * i);
        a.keys.push_back({ .sequence = 0, .pts = 3600 * i });
    }
    return a;
}

// A curve of `n` points on every `stride`-th reference frame from `first`.
Curve curve_of(const std::vector<double>& scores, std::int64_t first = 0, std::int64_t stride = 1)
{
    Curve c;
    c.variant = 1;
    for (std::size_t k = 0; k < scores.size(); ++k) {
        c.ref_index.push_back(first + static_cast<std::int64_t>(k) * stride);
        c.vmaf.push_back(scores[k]);
    }
    return c;
}

// The minimum and maximum of points [first, last) by brute force.
MinMax brute(const std::vector<double>& v, std::size_t first, std::size_t last)
{
    MinMax m;
    for (std::size_t i = first; i < last && i < v.size(); ++i) {
        if (!std::isfinite(v[i])) continue;
        if (m.points == 0 || v[i] < m.min) {
            m.min    = v[i];
            m.min_at = i;
        }
        if (m.points == 0 || v[i] > m.max) m.max = v[i];
        ++m.points;
    }
    return m;
}

bool same(const MinMax& a, const MinMax& b)
{
    if (a.points != b.points) return false;
    if (a.points == 0) return true;
    return a.min == b.min && a.max == b.max && a.min_at == b.min_at;
}

// Random scores between 20 and 100, about one in ten NaN.
std::vector<double> random_scores(std::mt19937& rng, std::size_t n)
{
    std::uniform_real_distribution<double> score(20.0, 100.0);
    std::uniform_int_distribution<int>     nan(0, 9);
    std::vector<double> v(n);
    for (double& s : v) s = nan(rng) == 0 ? kNaN : std::round(score(rng));   // rounded: ties happen
    return v;
}

// The frame a point's span ends at: the next point's, the last point's
// own plus one (D-081 (g)).
std::int64_t span_end(const Curve& c, std::size_t k)
{
    return k + 1 < c.ref_index.size() ? c.ref_index[k + 1] : c.ref_index[k] + 1;
}

// The first frame whose span, to the next frame's position, ends after `p`,
// by brute force.
std::int64_t first_ending_after(const AxisScale& s, double p)
{
    for (std::int64_t f = 0; f < s.frames; ++f)
        if (s.position(f + 1) > p) return f;
    return s.frames;
}

// The frame a point's span ends at where `unscored` holds spans outside the
// data (D-124 (a)): span_end(), or the start of the first of those spans,
// none empty, that starts after the point's frame and before that, by brute
// force over every frame.
std::int64_t span_end(const Curve& c, const std::vector<timeline::Interval>& unscored, std::size_t k)
{
    const std::int64_t end = span_end(c, k);
    for (std::int64_t f = c.ref_index[k] + 1; f < end; ++f)
        for (const timeline::Interval& u : unscored)
            if (u.ref_end > u.ref_index && u.ref_index == f) return f;
    return end;
}

// The lowest point whose span on the axis, from its frame's position to its
// span end's, meets [lo, hi], the earliest of equal minima, by brute force.
std::optional<std::size_t> brute_pick(const Curve& c, const AxisScale& s, double lo, double hi,
                                      const std::vector<timeline::Interval>& unscored = {})
{
    std::optional<std::size_t> best;
    for (std::size_t k = 0; k < c.ref_index.size(); ++k) {
        if (!(s.position(c.ref_index[k]) <= hi && s.position(span_end(c, unscored, k)) > lo)) continue;
        if (!std::isfinite(c.vmaf[k])) continue;
        if (!best || c.vmaf[k] < c.vmaf[*best]) best = k;
    }
    return best;
}

// A curve shaped as a session's whose middle span plays the reference over
// D-069's ad in top-rung mode, as tests/test_timeline_load.cpp's document
// has it: points at frames 0 to 74 and 125 to 199, 90 but for 60 at frame 74
// and 70 at frame 125, and none over [75, 125), its span that isn't scored.
const std::vector<timeline::Interval> kAdUnscored = { { .ref_index = 75, .ref_end = 125 } };

Curve ad_session_curve()
{
    Curve c;
    for (std::int64_t f = 0; f < 200; ++f) {
        if (f >= 75 && f < 125) continue;
        c.ref_index.push_back(f);
        c.vmaf.push_back(f == 74 ? 60.0 : f == 125 ? 70.0 : 90.0);
    }
    return c;
}

// Random spans outside the data on frames [0, frames): up to three, ordered,
// apart, adjacent or empty, and `c` without its points inside them, as
// curve_points() leaves a session's curve (src/TimelineModel.h).
std::vector<timeline::Interval> random_unscored(std::mt19937& rng, std::int64_t frames, Curve& c)
{
    std::uniform_int_distribution<int> count(0, 3), shape(0, 5);
    std::uniform_int_distribution<std::int64_t> at(0, frames - 1), length(1, 120);
    std::vector<timeline::Interval> out;
    std::int64_t from = at(rng) / 2;
    for (int n = count(rng); n > 0 && from < frames; --n) {
        const int k = shape(rng);
        const std::int64_t start = k == 0 && !out.empty() ? out.back().ref_end : from;   // adjacent
        const std::int64_t end   = k == 1 ? start : std::min(frames, start + length(rng));  // empty
        out.push_back({ .ref_index = start, .ref_end = end });
        from = end + at(rng) / 4;
    }
    Curve kept;
    kept.variant = c.variant;
    for (std::size_t k = 0; k < c.ref_index.size(); ++k) {
        const std::int64_t f = c.ref_index[k];
        if (std::any_of(out.begin(), out.end(),
                        [f](const timeline::Interval& u) { return f >= u.ref_index && f < u.ref_end; }))
            continue;
        kept.ref_index.push_back(f);
        kept.vmaf.push_back(c.vmaf[k]);
    }
    c = std::move(kept);
    return out;
}

} // namespace

// ── The x axis ───────────────────────────────────────────────────────────────

TEST_CASE("timeline::AxisScale: a frame's position is its seconds on the axis, and a position goes "
          "back to the first frame at or after it, where the playlist time steps back too",
          "[timeline][timeline_view]")
{
    timeline::Axis a = grid_axis(60);
    // Frames 30 on two frames early, as after an EXTINF two frames shorter
    // than its segment's frames (design §4.2): the playlist time steps back
    // there, frame 30 at 1.12 s after frame 29 at 1.16 s.
    for (std::int64_t i = 30; i < 60; ++i) a.ticks[static_cast<std::size_t>(i)] -= 4;
    const AxisScale s = timeline::axis_scale(a);
    REQUIRE(s.seconds);
    CHECK(s.frames == 60);
    CHECK(s.lo == 0.0);
    CHECK(s.hi == a.seconds(60));   // the last frame's end
    for (std::int64_t f = -3; f <= 63; ++f) {
        INFO(f);
        CHECK(s.position(f) == a.seconds(f));
    }
    for (int k = -10; k <= 260; ++k) {
        INFO(k);
        const double p = k / 100.0;
        CHECK(s.frame_at_or_after(p) == a.frame_at_or_after(AVRational{ k, 100 }));
        std::int64_t after = s.frames;   // the first frame after p, by brute force
        for (std::int64_t f = 0; f < s.frames && after == s.frames; ++f)
            if (s.position(f) > p) after = f;
        CHECK(s.frame_after(p) == after);
        CHECK(s.frame_ending_after(p) == first_ending_after(s, p));
    }
    // Frame 29's position, 1.16 s, is frame 31's too: the first frame in
    // index order at or after it is 29, the first after it 32, at 1.2 s.
    CHECK(s.position(31) == s.position(29));
    CHECK(s.frame_at_or_after(s.position(29)) == 29);
    CHECK(s.frame_after(s.position(29)) == 32);
    CHECK(s.frame_at_or_after(s.position(30)) == 28);   // 1.12 s: frame 28's
}

TEST_CASE("timeline::AxisScale: on an axis without seconds, positions are the frames' indices",
          "[timeline][timeline_view]")
{
    timeline::Axis a = grid_axis(10);
    a.frame_duration = { 0, 1 };   // as build_axis() leaves an axis it can't give seconds
    for (std::int64_t& t : a.ticks) t = 0;
    const AxisScale s = timeline::axis_scale(a);
    CHECK_FALSE(s.seconds);
    CHECK(s.lo == 0.0);
    CHECK(s.hi == 10.0);
    CHECK(s.position(4) == 4.0);
    CHECK(s.frame_at_or_after(2.5) == 3);
    CHECK(s.frame_at_or_after(3.0) == 3);
    CHECK(s.frame_at_or_after(-1.0) == 0);
    CHECK(s.frame_at_or_after(42.0) == 10);
    CHECK(s.frame_after(3.0) == 4);
    CHECK(s.frame_after(-0.5) == 0);
    // The frame under a position, frame 0 before the first, none past the last's end.
    CHECK(s.frame_ending_after(2.5) == 2);
    CHECK(s.frame_ending_after(3.0) == 3);
    CHECK(s.frame_ending_after(-1.0) == 0);
    CHECK(s.frame_ending_after(9.5) == 9);
    CHECK(s.frame_ending_after(10.0) == 10);
    CHECK(s.frame_ending_after(42.0) == 10);
}

// ── The view ─────────────────────────────────────────────────────────────────

TEST_CASE("timeline::View: x and positions round trip, a zoom keeps the position under the cursor "
          "within its span limits, and a pan of dx px moves the view by dx x span / width",
          "[timeline][timeline_view]")
{
    const AxisScale s = timeline::axis_scale(grid_axis(2500));   // 100 s
    REQUIRE(s.hi == 100.0);
    CHECK(timeline::max_span(s) == 151.0);   // 1.5 x the extent, plus 1
    std::mt19937 rng(12);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    for (int trial = 0; trial < 500; ++trial) {
        INFO(trial);
        View v;
        v.t0 = -20.0 + 120.0 * u(rng);
        v.t1 = v.t0 + 0.5 + 150.0 * u(rng);
        v.fit = false;
        const float x0 = static_cast<float>(10.0 + 50.0 * u(rng));
        const float w  = static_cast<float>(100.0 + 1900.0 * u(rng));
        const float x  = x0 + static_cast<float>(u(rng)) * w;

        // x to a position and back.
        const double p = timeline::position_at(v, x, x0, w);
        CHECK(std::abs(timeline::x_at(v, p, x0, w) - x) < 1e-2f);

        // A zoom about the cursor keeps the position there, and its span limits.
        View z = v;
        const float wheel = u(rng) < 0.5 ? 1.0f : -1.0f;
        timeline::zoom_at(z, s, p, wheel);
        CHECK_FALSE(z.fit);
        CHECK(std::abs(timeline::position_at(z, x, x0, w) - p) < 1e-6 * std::max(1.0, std::abs(p)));
        const double want = std::clamp(v.span() * (wheel > 0 ? timeline::kZoomIn : timeline::kZoomOut),
                                       timeline::kMinSpan, timeline::max_span(s));
        CHECK(std::abs(z.span() - want) < 1e-9);
        CHECK(z.span() >= timeline::kMinSpan);
        CHECK(z.span() <= timeline::max_span(s) + 1e-9);

        // A pan of dx px.
        View m = v;
        const float dx = static_cast<float>(-300.0 + 600.0 * u(rng));
        timeline::pan_px(m, dx, w);
        CHECK(std::abs((m.t0 - v.t0) - (-static_cast<double>(dx) * v.span() / w)) < 1e-9);
        CHECK(std::abs(m.span() - v.span()) < 1e-9);
    }
}

TEST_CASE("timeline::View: wheel steps stop at the span limits; fit, and clamping to the data",
          "[timeline][timeline_view]")
{
    const AxisScale s = timeline::axis_scale(grid_axis(250));   // 10 s
    View v;
    timeline::fit_view(v, s);
    CHECK(v.fit);
    CHECK(v.t0 == 0.0);
    CHECK(v.t1 == 10.0);
    for (int i = 0; i < 50; ++i) timeline::zoom_at(v, s, 5.0, 1.0f);
    CHECK(std::abs(v.span() - timeline::kMinSpan) < 1e-12);
    CHECK_FALSE(v.fit);
    for (int i = 0; i < 50; ++i) timeline::zoom_at(v, s, 5.0, -1.0f);
    CHECK(std::abs(v.span() - 16.0) < 1e-9);   // 1.5 x 10 + 1
    timeline::zoom_at(v, s, 5.0, 0.0f);        // no wheel: nothing
    CHECK(std::abs(v.span() - 16.0) < 1e-9);

    // A pan far off the data, then the clamp: the centre back on the extent's
    // end, the span kept.
    timeline::fit_view(v, s);
    timeline::zoom_at(v, s, 5.0, 1.0f);   // 8 s
    timeline::pan_px(v, -100000.0f, 800.0f);
    CHECK(v.t0 > 10.0);
    timeline::clamp_view(v, s);
    CHECK(std::abs(0.5 * (v.t0 + v.t1) - 10.0) < 1e-9);
    CHECK(std::abs(v.span() - 8.0) < 1e-9);
    timeline::pan_px(v, 100000.0f, 800.0f);
    timeline::clamp_view(v, s);
    CHECK(std::abs(0.5 * (v.t0 + v.t1) - 0.0) < 1e-9);
    // A view inside the data stays as it is; one too wide is narrowed about its centre.
    View in{ .t0 = 2.0, .t1 = 6.0, .fit = false };
    timeline::clamp_view(in, s);
    CHECK(in.t0 == 2.0);
    CHECK(in.t1 == 6.0);
    View wide{ .t0 = -50.0, .t1 = 60.0, .fit = false };
    timeline::clamp_view(wide, s);
    CHECK(std::abs(wide.span() - 16.0) < 1e-9);
    CHECK(std::abs(0.5 * (wide.t0 + wide.t1) - 5.0) < 1e-9);

    // A click is a release that moved less than 3 px.
    CHECK(timeline::is_click(0.0f));
    CHECK(timeline::is_click(8.99f));
    CHECK_FALSE(timeline::is_click(9.0f));
}

TEST_CASE("timeline::nice_step and axis_label: the Alignment tab's step table and labels (P-AJ)",
          "[timeline][timeline_view]")
{
    // Each step of the table is the one a span gets whose ticks of that step
    // lie exactly min_px apart; past the table, 14400.
    const double steps[] = { 0.1, 0.25, 0.5, 1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 900, 1800, 3600, 7200 };
    for (const double st : steps) {
        INFO(st);
        CHECK(timeline::nice_step(st * 10.0, 640.0f, 64.0f) == st);
    }
    CHECK(timeline::nice_step(7200.0 * 11.0, 640.0f, 64.0f) == 14400.0);
    CHECK(timeline::nice_step(10.0, 640.0f, 64.0f) == 1.0);
    CHECK(timeline::nice_step(10.0, 639.0f, 64.0f) == 2.0);
    CHECK(timeline::nice_step(0.5, 1000.0f, 64.0f) == 0.1);

    CHECK(timeline::axis_label(1.234, 0.5) == "1.23");
    CHECK(timeline::axis_label(65.4, 1.0) == "1:05");
    CHECK(timeline::axis_label(59.6, 2.0) == "1:00");
    CHECK(timeline::axis_label(3723.0, 5.0) == "1:02:03");
    CHECK(timeline::axis_label(0.0, 10.0) == "0:00");
}

// ── Envelopes ────────────────────────────────────────────────────────────────

TEST_CASE("timeline::build_pyramid and range_min_max: every level, and every run, against brute "
          "force; NaN scores skipped; the earliest of equal minima",
          "[timeline][timeline_view]")
{
    std::mt19937 rng(7);
    for (std::size_t n : { std::size_t{ 1 }, std::size_t{ 2 }, std::size_t{ 3 }, std::size_t{ 7 },
                           std::size_t{ 8 }, std::size_t{ 33 }, std::size_t{ 100 } }) {
        INFO(n);
        const std::vector<double> v = random_scores(rng, n);
        const timeline::Pyramid p = timeline::build_pyramid(v);
        REQUIRE(p.size() == n);
        REQUIRE(p.levels.back().size() == 1);
        for (std::size_t k = 0; k < p.levels.size(); ++k) {
            const std::size_t width = std::size_t{ 1 } << k;
            for (std::size_t j = 0; j < p.levels[k].size(); ++j) {
                INFO("level " << k << ", node " << j);
                CHECK(same(p.levels[k][j], brute(v, j * width, (j + 1) * width)));
            }
        }
        for (std::size_t a = 0; a <= n; ++a)
            for (std::size_t b = a; b <= n + 2; ++b) {
                INFO(a << " to " << b);
                CHECK(same(timeline::range_min_max(p, a, b), brute(v, a, b)));
            }
    }
    // All NaN: no point.
    const timeline::Pyramid none = timeline::build_pyramid({ kNaN, kNaN, kNaN });
    CHECK_FALSE(timeline::range_min_max(none, 0, 3).any());
    CHECK(timeline::build_pyramid({}).size() == 0);
    // Equal minima: the earliest.
    const timeline::Pyramid ties = timeline::build_pyramid({ 50, 20, 70, 20, 20, 90 });
    CHECK(timeline::range_min_max(ties, 0, 6).min_at == 1);
    CHECK(timeline::range_min_max(ties, 2, 6).min_at == 3);
    CHECK(timeline::range_min_max(ties, 2, 6).max == 90);
}

TEST_CASE("timeline::envelope: each column's minimum and maximum against brute force on random "
          "curves, every point whose span meets the view in one column, empty columns",
          "[timeline][timeline_view]")
{
    const timeline::Axis a = grid_axis(1000);
    const AxisScale      s = timeline::axis_scale(a);
    std::mt19937 rng(99);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    for (int trial = 0; trial < 200; ++trial) {
        INFO(trial);
        // Every frame, or every other one as on a coarser grid, from a random frame.
        const std::int64_t stride = 1 + static_cast<std::int64_t>(u(rng) * 2.0);
        const std::int64_t first  = static_cast<std::int64_t>(u(rng) * 50.0);
        const std::size_t  n      = static_cast<std::size_t>((1000 - first + stride - 1) / stride);
        const Curve c = curve_of(random_scores(rng, n), first, stride);
        const timeline::Pyramid p = timeline::build_pyramid(c.vmaf);
        View v;
        v.t0 = -5.0 + 45.0 * u(rng);
        v.t1 = v.t0 + 0.5 + 40.0 * u(rng);
        const int columns = 1 + static_cast<int>(u(rng) * 400.0);
        const std::vector<MinMax> env = timeline::envelope(c, p, s, v, columns);
        REQUIRE(env.size() == static_cast<std::size_t>(columns));
        // A point is in column 0 when its span meets the frames from the one
        // under t0 to column 1's first, and in a later column when its frame
        // is among that column's.
        const std::int64_t under = first_ending_after(s, v.t0);
        std::size_t points_seen = 0;
        for (int col = 0; col < columns; ++col) {
            const std::int64_t f0 = s.frame_at_or_after(v.t0 + v.span() * col / columns);
            const std::int64_t f1 = s.frame_at_or_after(col + 1 == columns ? v.t1 : v.t0 + v.span() * (col + 1) / columns);
            MinMax want;
            for (std::size_t k = 0; k < c.vmaf.size(); ++k) {
                const bool in = col == 0 ? under < f1 && c.ref_index[k] < f1 && span_end(c, k) > under
                                         : c.ref_index[k] >= f0 && c.ref_index[k] < f1;
                if (!in) continue;
                ++points_seen;
                const double x = c.vmaf[k];
                if (!std::isfinite(x)) continue;
                if (want.points == 0 || x < want.min) {
                    want.min    = x;
                    want.min_at = k;
                }
                if (want.points == 0 || x > want.max) want.max = x;
                ++want.points;
            }
            INFO("column " << col);
            CHECK(same(env[static_cast<std::size_t>(col)], want));
        }
        // Every point whose span meets the view, NaN ones included, is in one
        // column and in no other.
        const std::int64_t hi = s.frame_at_or_after(v.t1);
        std::size_t in_view = 0;
        for (std::size_t k = 0; k < c.ref_index.size(); ++k)
            in_view += under < hi && c.ref_index[k] < hi && span_end(c, k) > under;
        CHECK(points_seen == in_view);
    }
    // Columns narrower than a frame: most empty, the points each in one.
    const Curve c = curve_of({ 90, 80, 70 });
    const timeline::Pyramid p = timeline::build_pyramid(c.vmaf);
    const std::vector<MinMax> env = timeline::envelope(c, p, s, View{ .t0 = 0.0, .t1 = 0.12 }, 30);
    std::size_t filled = 0;
    for (const MinMax& m : env) filled += m.any();
    CHECK(filled == 3);
    CHECK(timeline::envelope(c, p, s, View{ .t0 = 1.0, .t1 = 1.0 }, 10).empty());
    CHECK(timeline::envelope(c, p, s, View{ .t0 = 0.0, .t1 = 1.0 }, 0).empty());
}

TEST_CASE("timeline::envelope: a frame partly in view at the left edge is drawn in the first column, "
          "on every frame and between two points of a coarser grid",
          "[timeline][timeline_view]")
{
    const AxisScale s = timeline::axis_scale(grid_axis(100));   // 4 s
    // A dip at frame 50, which spans 2.00 to 2.04 s; the view from 2.02 s, at
    // the narrowest span, over 1000 columns.
    std::vector<double> v(100, 95.0);
    v[50] = 20.0;
    const Curve c = curve_of(v);
    const timeline::Pyramid p = timeline::build_pyramid(c.vmaf);
    const View view{ .t0 = 2.02, .t1 = 2.52, .fit = false };
    std::vector<MinMax> env = timeline::envelope(c, p, s, view, 1000);
    REQUIRE(env.front().any());
    CHECK(env.front().min == 20.0);
    CHECK(env.front().min_at == 50);
    CHECK(env.front().points == 1);
    // A view from a frame's start: that frame, as before.
    env = timeline::envelope(c, p, s, View{ .t0 = 2.0, .t1 = 2.5, .fit = false }, 1000);
    CHECK(env.front().min_at == 50);
    CHECK(env.front().points == 1);

    // Points on every other frame, 20 at frame 50: from 2.06 s, in frame 51,
    // the point of frame 50 stands for it (D-081 (g)).
    std::vector<double> w(50, 95.0);
    w[25] = 20.0;
    const Curve coarse = curve_of(w, 0, 2);
    const timeline::Pyramid cp = timeline::build_pyramid(coarse.vmaf);
    env = timeline::envelope(coarse, cp, s, View{ .t0 = 2.06, .t1 = 2.56, .fit = false }, 1000);
    REQUIRE(env.front().any());
    CHECK(env.front().min == 20.0);
    CHECK(env.front().min_at == 25);
    // From 2.08 s, frame 52's own position: its point, not frame 50's.
    env = timeline::envelope(coarse, cp, s, View{ .t0 = 2.08, .t1 = 2.58, .fit = false }, 1000);
    CHECK(env.front().min_at == 26);
    CHECK(env.front().min == 95.0);
}

TEST_CASE("timeline::envelope: a one-frame dip is its column's minimum at every zoom (ui.md §1.8, F4)",
          "[timeline][timeline_view]")
{
    const timeline::Axis a = grid_axis(36000);   // 24 minutes at 25 fps
    const AxisScale      s = timeline::axis_scale(a);
    std::vector<double> flat(36000, 95.0);
    const std::size_t dip = 12345;
    flat[dip] = 20.0;
    const Curve c = curve_of(flat);
    const timeline::Pyramid p = timeline::build_pyramid(c.vmaf);
    View v;
    timeline::fit_view(v, s);
    const double at = s.position(static_cast<std::int64_t>(dip));
    // From the whole curve down to the narrowest span, about the dip and off it.
    for (int step = 0; step < 40; ++step) {
        for (const double off : { 0.0, 0.3, -0.45 }) {
            View z = v;
            z.t0 = at - z.span() * (0.5 + off);
            z.t1 = z.t0 + v.span();
            for (const int columns : { 37, 640, 1920 }) {
                INFO("span " << z.span() << ", offset " << off << ", columns " << columns);
                const std::vector<MinMax> env = timeline::envelope(c, p, s, z, columns);
                double lowest = 100.0;
                std::size_t where = 0;
                for (const MinMax& m : env)
                    if (m.any() && m.min < lowest) {
                        lowest = m.min;
                        where  = m.min_at;
                    }
                CHECK(lowest == 20.0);
                CHECK(where == dip);
            }
        }
        timeline::zoom_at(v, s, at, 1.0f);
    }
    CHECK(std::abs(v.span() - timeline::kMinSpan) < 1e-9);
}

// ── A span outside the data (D-124 (a)) ──────────────────────────────────────

TEST_CASE("timeline::point_end and points_meeting: a session's span that isn't scored ends the span of the "
          "point before it at its start, and meets no point; against brute force with random such spans "
          "(D-124 (a))",
          "[timeline][timeline_view]")
{
    const Curve c = ad_session_curve();
    REQUIRE(c.ref_index.size() == 150);
    REQUIRE(c.ref_index[74] == 74);
    REQUIRE(c.ref_index[75] == 125);
    // Frame 74's point, the one before the span, stands for its own frame
    // alone: its span ends at the span's start, where without the span it
    // would run to the next point's frame, 125.
    CHECK(timeline::point_end(c, kAdUnscored, 74) == 75);
    CHECK(timeline::point_end(c, {}, 74) == 125);
    CHECK(timeline::point_end(c, kAdUnscored, 73) == 74);
    CHECK(timeline::point_end(c, kAdUnscored, 75) == 126);    // frame 125's, the first after it
    CHECK(timeline::point_end(c, kAdUnscored, 149) == 200);   // the last point: its own frame
    CHECK(timeline::point_end(c, kAdUnscored, 150) == 0);     // a point the curve hasn't
    // An empty span cuts nothing, nor does one that holds the point's own
    // frame, which a session's span never does.
    CHECK(timeline::point_end(c, { { .ref_index = 100, .ref_end = 100 } }, 74) == 125);
    CHECK(timeline::point_end(c, { { .ref_index = 70, .ref_end = 80 } }, 74) == 125);

    // Each of the span's frames meets no point; as WP12's view had it, which
    // the overload without the spans keeps for WP13's panel, frame 74's.
    for (std::int64_t f = 75; f < 125; ++f) {
        INFO(f);
        const timeline::PointRange r = timeline::points_meeting(c, kAdUnscored, f, f + 1);
        CHECK(r.first == r.last);
        const timeline::PointRange before = timeline::points_meeting(c, f, f + 1);
        CHECK((before.first == 74 && before.last == 75));
    }
    // At its two ends, the points beside it; across it, the points on
    // either side; from inside it, those after it alone.
    timeline::PointRange r = timeline::points_meeting(c, kAdUnscored, 74, 75);
    CHECK((r.first == 74 && r.last == 75));
    r = timeline::points_meeting(c, kAdUnscored, 74, 76);
    CHECK((r.first == 74 && r.last == 75));
    r = timeline::points_meeting(c, kAdUnscored, 124, 126);
    CHECK((r.first == 75 && r.last == 76));
    r = timeline::points_meeting(c, kAdUnscored, 70, 130);
    CHECK((r.first == 70 && r.last == 80));
    r = timeline::points_meeting(c, kAdUnscored, 100, 130);
    CHECK((r.first == 75 && r.last == 80));
    r = timeline::points_meeting(c, 100, 130);
    CHECK((r.first == 74 && r.last == 80));

    // Random curves, every frame or a coarser grid, with random spans outside
    // their data: each point's end, and the points meeting random runs of
    // frames, against brute force.
    std::mt19937 rng(57);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    for (int trial = 0; trial < 500; ++trial) {
        INFO(trial);
        const std::int64_t stride = 1 + static_cast<std::int64_t>(u(rng) * 3.0);
        const std::int64_t first  = static_cast<std::int64_t>(u(rng) * 20.0);
        const std::size_t  n      = static_cast<std::size_t>((400 - first + stride - 1) / stride);
        Curve rc = curve_of(random_scores(rng, n), first, stride);
        const std::vector<timeline::Interval> gaps = random_unscored(rng, 400, rc);
        std::size_t wrong_ends = 0;
        for (std::size_t k = 0; k < rc.ref_index.size(); ++k)
            wrong_ends += timeline::point_end(rc, gaps, k) != span_end(rc, gaps, k);
        CHECK(wrong_ends == 0);
        for (int run = 0; run < 20; ++run) {
            const std::int64_t a = static_cast<std::int64_t>(u(rng) * 430.0) - 15;
            const std::int64_t b = a + static_cast<std::int64_t>(u(rng) * 80.0) - 5;
            INFO("frames " << a << " to " << b);
            std::size_t lo = 0, hi = 0, count = 0;
            for (std::size_t k = 0; k < rc.ref_index.size(); ++k) {
                if (!(a < b && rc.ref_index[k] < b && span_end(rc, gaps, k) > a)) continue;
                if (count++ == 0) lo = k;
                hi = k + 1;
            }
            const timeline::PointRange got = timeline::points_meeting(rc, gaps, a, b);
            if (count == 0) {
                CHECK(got.first == got.last);
            } else {
                CHECK(got.first == lo);
                CHECK(got.last == hi);
                CHECK(hi - lo == count);
            }
        }
    }
}

TEST_CASE("timeline::envelope: no score in the columns over a session's span that isn't scored, the first "
          "column's among them, and the points beside it at its two ends; against brute force with random "
          "such spans (D-124 (a))",
          "[timeline][timeline_view]")
{
    const AxisScale s = timeline::axis_scale(grid_axis(200));   // 8 s, frame f from 0.04 f s
    const Curve c = ad_session_curve();
    const timeline::Pyramid p = timeline::build_pyramid(c.vmaf);
    // A column a frame, each column's left edge half a frame before its
    // frame, so that column j holds frame j: the span's 50 columns hold no
    // score, and its two neighbours the points beside it.
    const View one{ .t0 = -0.02, .t1 = 7.98, .fit = false };
    const std::vector<MinMax> env = timeline::envelope(c, p, kAdUnscored, s, one, 200);
    REQUIRE(env.size() == 200);
    for (std::size_t j = 0; j < 200; ++j) {
        INFO("column " << j);
        if (j >= 75 && j < 125) {
            CHECK_FALSE(env[j].any());
            continue;
        }
        REQUIRE(env[j].any());
        CHECK(env[j].points == 1);
        CHECK(env[j].min_at == (j < 75 ? j : j - 50));
    }
    CHECK(env[74].min == 60.0);
    CHECK(env[125].min == 70.0);
    // Columns past the first are those of the frames they hold, which the
    // span has no point at: the overload without the spans gives the same.
    const std::vector<MinMax> as_before = timeline::envelope(c, p, s, one, 200);
    for (std::size_t j = 0; j < 200; ++j) CHECK(same(env[j], as_before[j]));

    // A view whose left edge lies inside the span, at frame 87's middle, a
    // column a frame: its first column, frames 87 and 88, and the next 36,
    // frames 89 to 124, hold no score, and column 37 frame 125's point; the
    // overload without the spans, as WP12's view had it, holds frame 74's
    // point in the first column.
    const View inside{ .t0 = 3.5, .t1 = 7.5, .fit = false };
    std::vector<MinMax> e = timeline::envelope(c, p, kAdUnscored, s, inside, 100);
    REQUIRE(e.size() == 100);
    for (std::size_t j = 0; j < 37; ++j) {
        INFO("column " << j);
        CHECK_FALSE(e[j].any());
    }
    REQUIRE(e[37].any());
    CHECK(e[37].min_at == 75);
    CHECK(e[37].min == 70.0);
    e = timeline::envelope(c, p, s, inside, 100);
    REQUIRE(e.front().any());
    CHECK(e.front().min_at == 74);
    CHECK(e.front().min == 60.0);
    // A view whose left edge lies inside frame 74, partly in view: its point,
    // as before, alone in the first column, frames 74 and 75; the next
    // column, frame 76, none.
    e = timeline::envelope(c, p, kAdUnscored, s, View{ .t0 = 2.98, .t1 = 4.98, .fit = false }, 50);
    REQUIRE(e.front().any());
    CHECK(e.front().min_at == 74);
    CHECK(e.front().points == 1);
    CHECK_FALSE(e[1].any());

    // Random curves with random spans outside their data, random views:
    // each column against brute force, the first column holding the points
    // whose spans, to their ends at a span's start, meet its frames.
    const AxisScale s2 = timeline::axis_scale(grid_axis(1000));
    std::mt19937 rng(64);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    for (int trial = 0; trial < 300; ++trial) {
        INFO(trial);
        const std::int64_t stride = 1 + static_cast<std::int64_t>(u(rng) * 2.0);
        const std::int64_t first  = static_cast<std::int64_t>(u(rng) * 50.0);
        const std::size_t  n      = static_cast<std::size_t>((1000 - first + stride - 1) / stride);
        Curve rc = curve_of(random_scores(rng, n), first, stride);
        const std::vector<timeline::Interval> gaps = random_unscored(rng, 1000, rc);
        const timeline::Pyramid rp = timeline::build_pyramid(rc.vmaf);
        View v;
        v.t0 = -5.0 + 45.0 * u(rng);
        v.t1 = v.t0 + 0.5 + 40.0 * u(rng);
        const int columns = 1 + static_cast<int>(u(rng) * 400.0);
        const std::vector<MinMax> got = timeline::envelope(rc, rp, gaps, s2, v, columns);
        REQUIRE(got.size() == static_cast<std::size_t>(columns));
        const std::int64_t under = first_ending_after(s2, v.t0);
        std::size_t wrong = 0;
        for (int col = 0; col < columns; ++col) {
            const std::int64_t f0 = s2.frame_at_or_after(v.t0 + v.span() * col / columns);
            const std::int64_t f1 =
                s2.frame_at_or_after(col + 1 == columns ? v.t1 : v.t0 + v.span() * (col + 1) / columns);
            MinMax want;
            for (std::size_t k = 0; k < rc.vmaf.size(); ++k) {
                const bool in = col == 0 ? under < f1 && rc.ref_index[k] < f1 && span_end(rc, gaps, k) > under
                                         : rc.ref_index[k] >= f0 && rc.ref_index[k] < f1;
                const double x = rc.vmaf[k];
                if (!in || !std::isfinite(x)) continue;
                if (want.points == 0 || x < want.min) {
                    want.min    = x;
                    want.min_at = k;
                }
                if (want.points == 0 || x > want.max) want.max = x;
                ++want.points;
            }
            wrong += !same(got[static_cast<std::size_t>(col)], want);
        }
        CHECK(wrong == 0);
    }
}

// ── Hit testing ──────────────────────────────────────────────────────────────

TEST_CASE("timeline::pick_lowest: the lowest point within the radius, the earliest of equal minima, "
          "nothing outside the data",
          "[timeline][timeline_view]")
{
    const AxisScale s = timeline::axis_scale(grid_axis(100));   // 4 s
    // Points on frames 10 to 59: 90 but for 40 at frames 30 and 34, 30 at 50.
    std::vector<double> v(50, 90.0);
    v[20] = 40.0;
    v[24] = 40.0;
    v[40] = 30.0;
    const Curve c = curve_of(v, 10);
    const timeline::Pyramid p = timeline::build_pyramid(c.vmaf);
    const View view{ .t0 = 0.0, .t1 = 4.0, .fit = false };   // 100 px a second over 400 px
    // The cursor at frame 32 (1.28 s, x 128), 8 px either side: frames 30 to
    // 34 (1.20 to 1.36 s), the two minima's earliest.
    std::optional<timeline::PickedPoint> got = timeline::pick_lowest(c, p, s, view, 128.0f, 0.0f, 400.0f, 8.0f);
    REQUIRE(got);
    CHECK(got->point == 20);
    CHECK(got->ref_index == 30);
    CHECK(got->vmaf == 40.0);
    // Wider, frame 50's lower score wins.
    got = timeline::pick_lowest(c, p, s, view, 128.0f, 0.0f, 400.0f, 80.0f);
    REQUIRE(got);
    CHECK(got->ref_index == 50);
    CHECK(got->vmaf == 30.0);
    // Both ends of the radius count: x 136 is frame 34's position, and
    // radius 0 at it picks that frame alone.
    got = timeline::pick_lowest(c, p, s, view, 136.0f, 0.0f, 400.0f, 0.0f);
    REQUIRE(got);
    CHECK(got->ref_index == 34);
    // Outside the data: before frame 10, after frame 59, past the axis.
    CHECK_FALSE(timeline::pick_lowest(c, p, s, view, 20.0f, 0.0f, 400.0f, 8.0f));
    CHECK_FALSE(timeline::pick_lowest(c, p, s, view, 300.0f, 0.0f, 400.0f, 8.0f));
    CHECK_FALSE(timeline::pick_lowest(c, p, s, view, 1000.0f, 0.0f, 400.0f, 8.0f));
    // A point without a score is never picked.
    std::vector<double> holes(50, kNaN);
    holes[45] = 70.0;
    const Curve h = curve_of(holes, 10);
    const timeline::Pyramid hp = timeline::build_pyramid(h.vmaf);
    CHECK_FALSE(timeline::pick_lowest(h, hp, s, view, 128.0f, 0.0f, 400.0f, 8.0f));
    got = timeline::pick_lowest(h, hp, s, view, 220.0f, 0.0f, 400.0f, 4.0f);
    REQUIRE(got);
    CHECK(got->ref_index == 55);
    // The last point stands for its own frame alone: x 239 is inside frame
    // 59 (2.36 to 2.40 s), x 240 at its end.
    got = timeline::pick_lowest(c, p, s, view, 239.0f, 0.0f, 400.0f, 0.0f);
    REQUIRE(got);
    CHECK(got->ref_index == 59);
    CHECK_FALSE(timeline::pick_lowest(c, p, s, view, 240.0f, 0.0f, 400.0f, 0.0f));
}

TEST_CASE("timeline::pick_lowest: a click anywhere over a point's span picks it, inside a zoomed-in "
          "frame, at every x of a fitted curve and between two points of a coarser grid; against brute "
          "force on random views",
          "[timeline][timeline_view]")
{
    // The narrowest span over 1000 px, 80 px a frame: frame 50, the dip, is
    // drawn from x 500 to 580, and a click anywhere in it picks it.
    const AxisScale s = timeline::axis_scale(grid_axis(100));   // 4 s
    std::vector<double> v(100, 95.0);
    v[50] = 20.0;
    const Curve c = curve_of(v);
    const timeline::Pyramid p = timeline::build_pyramid(c.vmaf);
    const View zoomed{ .t0 = 1.75, .t1 = 2.25, .fit = false };
    REQUIRE(std::abs(timeline::x_at(zoomed, s.position(50), 0.0f, 1000.0f) - 500.0f) < 1e-3f);
    REQUIRE(std::abs(timeline::x_at(zoomed, s.position(51), 0.0f, 1000.0f) - 580.0f) < 1e-3f);
    for (const float dx : { 0.0f, 10.0f, 40.0f, 70.0f, 79.0f })
        for (const float r : { 0.0f, 4.0f, 8.0f }) {
            INFO("x 500 + " << dx << ", radius " << r);
            const std::optional<timeline::PickedPoint> got =
                timeline::pick_lowest(c, p, s, zoomed, 500.0f + dx, 0.0f, 1000.0f, r);
            REQUIRE(got);
            CHECK(got->ref_index == 50);
            CHECK(got->vmaf == 20.0);
        }
    // Off the dip's span, its neighbours: frame 49 left of it, 51 right of it.
    CHECK(timeline::pick_lowest(c, p, s, zoomed, 490.0f, 0.0f, 1000.0f, 4.0f)->ref_index == 49);
    CHECK(timeline::pick_lowest(c, p, s, zoomed, 590.0f, 0.0f, 1000.0f, 4.0f)->ref_index == 51);

    // The fitted view of a 200-frame curve, the length of D-069's ladders,
    // over 1920 px: every x picks a point whose span is drawn within the
    // radius of it.
    const AxisScale s2 = timeline::axis_scale(grid_axis(200));   // 8 s
    const Curve flat = curve_of(std::vector<double>(200, 95.0));
    const timeline::Pyramid fp = timeline::build_pyramid(flat.vmaf);
    View fit;
    timeline::fit_view(fit, s2);
    std::size_t picked = 0;
    for (const float r : { 0.0f, 4.0f })
        for (int px = 0; px < 1920; ++px) {
            const float x = static_cast<float>(px);
            const std::optional<timeline::PickedPoint> got =
                timeline::pick_lowest(flat, fp, s2, fit, x, 0.0f, 1920.0f, r);
            if (!got) continue;
            ++picked;
            const float x_from = timeline::x_at(fit, s2.position(got->ref_index), 0.0f, 1920.0f);
            const float x_to   = timeline::x_at(fit, s2.position(got->ref_index + 1), 0.0f, 1920.0f);
            INFO("x " << px << ", radius " << r << ": frame " << got->ref_index);
            CHECK(x_from <= x + r + 1e-3f);
            CHECK(x_to > x - r - 1e-3f);
        }
    CHECK(picked == 2 * 1920);

    // Points on every other frame, as a 12.5 fps pair's on a 25 fps
    // reference: between two of them the earlier one's span, a click in
    // frame 51 picking frame 50's point, frame 52's its own.
    std::vector<double> w(50, 95.0);
    w[25] = 20.0;
    const Curve coarse = curve_of(w, 0, 2);
    const timeline::Pyramid cp = timeline::build_pyramid(coarse.vmaf);
    for (const float dx : { 80.0f, 120.0f, 159.0f }) {   // frame 51, 2.04 to 2.08 s
        INFO("x 500 + " << dx);
        const std::optional<timeline::PickedPoint> got =
            timeline::pick_lowest(coarse, cp, s, zoomed, 500.0f + dx, 0.0f, 1000.0f, 0.0f);
        REQUIRE(got);
        CHECK(got->point == 25);
        CHECK(got->ref_index == 50);
    }
    CHECK(timeline::pick_lowest(coarse, cp, s, zoomed, 670.0f, 0.0f, 1000.0f, 0.0f)->ref_index == 52);

    // Random views, cursors and radii on random curves, every frame or every
    // other one, NaN scores among them: the pick is the brute force's.
    const AxisScale s3 = timeline::axis_scale(grid_axis(1000));   // 40 s
    std::mt19937 rng(31);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    for (int trial = 0; trial < 2000; ++trial) {
        INFO(trial);
        const std::int64_t stride = 1 + static_cast<std::int64_t>(u(rng) * 2.0);
        const std::int64_t first  = static_cast<std::int64_t>(u(rng) * 50.0);
        const std::size_t  n      = static_cast<std::size_t>((900 - first + stride - 1) / stride);
        const Curve r = curve_of(random_scores(rng, n), first, stride);
        const timeline::Pyramid rp = timeline::build_pyramid(r.vmaf);
        View rv;
        rv.t0 = -2.0 + 40.0 * u(rng);
        rv.t1 = rv.t0 + 0.5 + 10.0 * u(rng);
        rv.fit = false;
        const float x0     = static_cast<float>(20.0 * u(rng));
        const float width  = static_cast<float>(200.0 + 1800.0 * u(rng));
        const float x      = x0 - 20.0f + static_cast<float>(u(rng)) * (width + 40.0f);
        const float radius = static_cast<float>(12.0 * u(rng));
        const double lo = timeline::position_at(rv, x - radius, x0, width);
        const double hi = timeline::position_at(rv, x + radius, x0, width);
        const std::optional<timeline::PickedPoint> got =
            timeline::pick_lowest(r, rp, s3, rv, x, x0, width, radius);
        const std::optional<std::size_t> want = brute_pick(r, s3, lo, hi);
        REQUIRE(got.has_value() == want.has_value());
        if (want) CHECK(got->point == *want);
    }
}

TEST_CASE("timeline::pick_lowest: no point picked in a session's span that isn't scored, across it and at "
          "its two ends; against brute force with random such spans (D-124 (a))",
          "[timeline][timeline_view]")
{
    // The fitted view over 800 px, 4 px a frame: frame f from x 4f to 4f + 4,
    // the span from x 300 to 500.
    const AxisScale s = timeline::axis_scale(grid_axis(200));   // 8 s
    const Curve c = ad_session_curve();
    const timeline::Pyramid p = timeline::build_pyramid(c.vmaf);
    View fit;
    timeline::fit_view(fit, s);
    const auto pick = [&](float x, float radius) {
        return timeline::pick_lowest(c, p, kAdUnscored, s, fit, x, 0.0f, 800.0f, radius);
    };

    // Across it: no point, at every x of its frames, nor with a reach of
    // 4 px that stays inside it; as WP12's view had it, which the overload
    // without the spans keeps for WP13's panel, frame 74's point.
    for (int px = 300; px < 500; ++px) {
        INFO("x " << px);
        const float x = static_cast<float>(px);
        CHECK_FALSE(pick(x, 0.0f));
        if (px >= 304 && px < 496) CHECK_FALSE(pick(x, 4.0f));
        const std::optional<timeline::PickedPoint> before = timeline::pick_lowest(c, p, s, fit, x, 0.0f, 800.0f, 0.0f);
        REQUIRE(before);
        CHECK(before->ref_index == 74);
    }
    // At its start: inside frame 74 its point, from frame 75's start on
    // nothing; a reach back over the start, the point before it.
    std::optional<timeline::PickedPoint> got = pick(298.0f, 0.0f);
    REQUIRE(got);
    CHECK(got->ref_index == 74);
    CHECK(got->vmaf == 60.0);
    got = pick(299.5f, 0.0f);
    REQUIRE(got);
    CHECK(got->ref_index == 74);
    CHECK_FALSE(pick(300.0f, 0.0f));
    got = pick(302.0f, 4.0f);
    REQUIRE(got);
    CHECK(got->ref_index == 74);
    // At its end: frame 124 nothing, frame 125's start its point; a reach
    // forward over the end, that point.
    CHECK_FALSE(pick(499.5f, 0.0f));
    got = pick(500.0f, 0.0f);
    REQUIRE(got);
    CHECK(got->point == 75);
    CHECK(got->ref_index == 125);
    CHECK(got->vmaf == 70.0);
    got = pick(498.0f, 4.0f);
    REQUIRE(got);
    CHECK(got->ref_index == 125);
    // A reach over the whole span and either side of it: the lower of the
    // points beside it.
    got = pick(400.0f, 120.0f);
    REQUIRE(got);
    CHECK(got->ref_index == 74);

    // Random curves with random spans outside their data, random views,
    // cursors and radii: the pick is the brute force's.
    const AxisScale s3 = timeline::axis_scale(grid_axis(1000));   // 40 s
    std::mt19937 rng(91);
    std::uniform_real_distribution<double> u(0.0, 1.0);
    for (int trial = 0; trial < 2000; ++trial) {
        INFO(trial);
        const std::int64_t stride = 1 + static_cast<std::int64_t>(u(rng) * 2.0);
        const std::int64_t first  = static_cast<std::int64_t>(u(rng) * 50.0);
        const std::size_t  n      = static_cast<std::size_t>((900 - first + stride - 1) / stride);
        Curve r = curve_of(random_scores(rng, n), first, stride);
        const std::vector<timeline::Interval> gaps = random_unscored(rng, 900, r);
        const timeline::Pyramid rp = timeline::build_pyramid(r.vmaf);
        View rv;
        rv.t0 = -2.0 + 40.0 * u(rng);
        rv.t1 = rv.t0 + 0.5 + 10.0 * u(rng);
        rv.fit = false;
        const float x0     = static_cast<float>(20.0 * u(rng));
        const float width  = static_cast<float>(200.0 + 1800.0 * u(rng));
        const float x      = x0 - 20.0f + static_cast<float>(u(rng)) * (width + 40.0f);
        const float radius = static_cast<float>(12.0 * u(rng));
        const double lo = timeline::position_at(rv, x - radius, x0, width);
        const double hi = timeline::position_at(rv, x + radius, x0, width);
        const std::optional<timeline::PickedPoint> picked =
            timeline::pick_lowest(r, rp, gaps, s3, rv, x, x0, width, radius);
        const std::optional<std::size_t> want = brute_pick(r, s3, lo, hi, gaps);
        REQUIRE(picked.has_value() == want.has_value());
        if (want) CHECK(picked->point == *want);
    }
}

TEST_CASE("timeline::cluster_markers and nearest_cluster: markers closer than the spacing as one with "
          "their count, and the nearest cluster",
          "[timeline][timeline_view]")
{
    const std::vector<float> xs = { 0.0f, 1.0f, 2.5f, 3.0f, 10.0f, 10.5f, 20.0f };
    const std::vector<timeline::MarkerCluster> c = timeline::cluster_markers(xs, 3.0f);
    REQUIRE(c.size() == 4);
    CHECK(c[0].first == 0);
    CHECK(c[0].count == 3);   // 0, 1 and 2.5: less than 3 px after 0
    CHECK(c[0].x0 == 0.0f);
    CHECK(c[0].x1 == 2.5f);
    CHECK(c[1].first == 3);   // 3 starts its own, 3 px after 0
    CHECK(c[1].count == 1);
    CHECK(c[2].first == 4);
    CHECK(c[2].count == 2);
    CHECK(c[2].x1 == 10.5f);
    CHECK(c[3].first == 6);
    CHECK(c[3].count == 1);
    std::size_t sum = 0;
    for (const timeline::MarkerCluster& m : c) sum += m.count;
    CHECK(sum == xs.size());
    // A spacing of 1 px: 2.5 and 3 together, and 10 and 10.5.
    CHECK(timeline::cluster_markers(xs, 1.0f).size() == 5);
    CHECK(timeline::cluster_markers({}, 3.0f).empty());

    // The nearest: inside a cluster's span its distance is 0.
    CHECK(timeline::nearest_cluster(c, 1.5f, 2.0f) == std::optional<std::size_t>{ 0 });
    CHECK(timeline::nearest_cluster(c, 7.0f, 4.0f) == std::optional<std::size_t>{ 2 });   // 3 px to 10, 4 to 3
    CHECK(timeline::nearest_cluster(c, 6.5f, 4.0f) == std::optional<std::size_t>{ 1 });   // 3.5 px to each: the first
    CHECK_FALSE(timeline::nearest_cluster(c, 6.5f, 3.0f));
    CHECK(timeline::nearest_cluster(c, 2.75f, 5.0f) == std::optional<std::size_t>{ 0 });  // 0.25 px to each
}
