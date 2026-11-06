// timeline::detect (src/TimelineDetect.h) on hand-built curves, with the
// tests' own numbers: the cases of eval/phase3-survey/design.md §14, WP02
// (a step drop; one-point dips at libvmaf context starts that N passes over;
// alternating dips as in a busy control; a drop inside the blind start of
// the warm-up, not found; gaps on a coarser grid; the baseline unchanged
// through a drop; the same drops twice; the shape of
// design/runs/detection-semantics-ad.txt, a context start at a drop's first
// frame), the rules D-077 and D-081 (g) give, the options and the curves it
// refuses, and the walk against the prototype's plain one on random curves.

#include <catch2/catch_test_macros.hpp>

#include "TimelineDetect.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <variant>
#include <vector>

using timeline::Curve;
using timeline::DetectOptions;
using timeline::Drop;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// The parameters the acceptance tests pin (T = 6, W = 50, N = 12: D-077),
// or others.
DetectOptions options(double t = 6.0, std::int64_t w = 50, std::int64_t n = 12)
{
    return DetectOptions{ .threshold = t, .window = w, .min_points = n };
}

// A pair's curve: its point k at reference frame first + k * step.
Curve curve_of(const std::vector<double>& scores, std::int64_t step = 1, std::int64_t first = 0)
{
    Curve c;
    for (std::size_t k = 0; k < scores.size(); ++k) {
        c.ref_index.push_back(first + static_cast<std::int64_t>(k) * step);
        c.vmaf.push_back(scores[k]);
    }
    return c;
}

// `n` scores of `v`.
std::vector<double> repeat(double v, int n)
{
    return std::vector<double>(static_cast<std::size_t>(n), v);
}

// `n` scores alternating between `lo` and `hi`, from `lo`: the median of an
// even number of them, in a row, is (lo + hi) / 2.
std::vector<double> alternating(double lo, double hi, int n)
{
    std::vector<double> out;
    for (int k = 0; k < n; ++k) out.push_back(k % 2 == 0 ? lo : hi);
    return out;
}

template <typename T>
std::vector<T> operator+(std::vector<T> a, const std::vector<T>& b)
{
    a.insert(a.end(), b.begin(), b.end());
    return a;
}

// The drops detect() finds, with no error.
std::vector<Drop> drops(const Curve& c, const DetectOptions& o)
{
    std::string err;
    std::vector<Drop> d = timeline::detect(c, o, err);
    REQUIRE(err.empty());
    return d;
}

bool same(const Drop& a, const Drop& b)
{
    return a.interval.ref_index == b.interval.ref_index && a.interval.ref_end == b.interval.ref_end
        && a.first_point == b.first_point && a.last_point == b.last_point && a.points == b.points
        && a.baseline == b.baseline && a.min == b.min && a.mean == b.mean
        && a.drop_magnitude == b.drop_magnitude && a.mean_drop == b.mean_drop;
}

bool same(const std::vector<Drop>& a, const std::vector<Drop>& b)
{
    return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                      [](const Drop& x, const Drop& y) { return same(x, y); });
}

// The prototype's walk written as plainly as it is
// (eval/phase3-survey/design/tools/detection_semantics.py:37-60): the
// points that weren't low sorted afresh at every point, the runs found
// afterwards, a run of more than N points a drop; points without a score
// skipped, and the interval as D-081 (g) gives it.
std::vector<Drop> plain_walk(const Curve& c, double t, std::int64_t w, std::int64_t n)
{
    std::vector<std::size_t> at;   // the scored points' positions
    std::vector<bool>        low;
    std::vector<double>      base, kept;
    for (std::size_t i = 0; i < c.vmaf.size(); ++i) {
        const double v = c.vmaf[i];
        if (!std::isfinite(v)) continue;
        const std::int64_t k    = static_cast<std::int64_t>(kept.size());
        const std::int64_t from = k > w ? k - w : 0;
        std::vector<double> pool(kept.begin() + from, kept.end());
        bool   is_low = false;
        double b      = 0.0;
        if (static_cast<std::int64_t>(pool.size()) >= std::max<std::int64_t>(1, w / 2)) {
            std::sort(pool.begin(), pool.end());
            const std::size_t h = pool.size() / 2;
            b      = pool.size() % 2 == 1 ? pool[h] : (pool[h - 1] + pool[h]) / 2;
            is_low = v < b - t;
        }
        at.push_back(i);
        low.push_back(is_low);
        base.push_back(b);
        if (!is_low) kept.push_back(v);
    }
    std::vector<Drop> out;
    for (std::size_t s = 0; s < at.size();) {
        if (!low[s]) {
            ++s;
            continue;
        }
        std::size_t e = s;
        while (e + 1 < at.size() && low[e + 1]) ++e;
        const std::int64_t points = static_cast<std::int64_t>(e - s + 1);
        if (points > n) {
            Drop d;
            d.first_point = at[s];
            d.last_point  = at[e];
            d.points      = points;
            d.baseline    = base[s];
            double sum = 0.0, min = c.vmaf[at[s]];
            for (std::size_t j = s; j <= e; ++j) {
                sum += c.vmaf[at[j]];
                min = std::min(min, c.vmaf[at[j]]);
            }
            d.min                = min;
            d.mean               = sum / static_cast<double>(points);
            d.drop_magnitude     = d.baseline - d.min;
            d.mean_drop          = d.baseline - d.mean;
            d.interval.ref_index = c.ref_index[at[s]];
            d.interval.ref_end   = e + 1 < at.size() ? c.ref_index[at[e + 1]]
                                                     : c.ref_index[at[e]] + 1;
            out.push_back(d);
        }
        s = e + 1;
    }
    return out;
}

} // namespace

TEST_CASE("timeline::detect: a step drop, its interval and its numbers",
          "[timeline][timeline_detect]")
{
    // A degraded segment's shape: eight segments of 25 frames, the fifth
    // (reference frames 100 to 124) far below the rest. The rest alternate
    // 94 and 96, so the median of the last 50 points before the drop is 95;
    // the drop's scores are 40 to 44, five of each.
    std::vector<double> segment;
    for (int k = 0; k < 25; ++k) segment.push_back(40.0 + k % 5);
    const Curve degraded = curve_of(alternating(94, 96, 100) + segment + alternating(94, 96, 75));
    const std::vector<Drop> d = drops(degraded, options());
    REQUIRE(d.size() == 1);
    CHECK(d[0].interval.ref_index == 100);
    CHECK(d[0].interval.ref_end == 125);   // the next point's frame (D-081 (g))
    CHECK(d[0].first_point == 100);
    CHECK(d[0].last_point == 124);
    CHECK(d[0].points == 25);
    CHECK(d[0].baseline == 95.0);
    CHECK(d[0].min == 40.0);
    CHECK(d[0].mean == 42.0);
    CHECK(d[0].drop_magnitude == 55.0);   // the baseline minus the minimum (D-077)
    CHECK(d[0].mean_drop == 53.0);        // the baseline minus the mean

    // The control rung, without the degraded segment: no drop.
    CHECK(drops(curve_of(alternating(94, 96, 200)), options()).empty());

    // A drop that runs to the curve's end ends one frame after its last
    // point.
    const std::vector<Drop> end = drops(curve_of(alternating(94, 96, 100) + segment), options());
    REQUIRE(end.size() == 1);
    CHECK(end[0].interval.ref_index == 100);
    CHECK(end[0].interval.ref_end == 125);
    CHECK(end[0].last_point == 124);
}

TEST_CASE("timeline::detect: N passes over the one-point dips at libvmaf context starts",
          "[timeline][timeline_detect]")
{
    // The first pair of each libvmaf context scores low (D-037): here the
    // pairs at reference frames 0 and 75, which start the curve's two
    // contexts, score 30 where the rest score 95.
    std::vector<double> scores = repeat(95, 200);
    scores[0]  = 30;
    scores[75] = 30;
    Curve c = curve_of(scores);
    c.context_first = { 0, 75 };

    // N = 12 passes over them.
    CHECK(drops(c, options()).empty());

    // They get no special case (D-077): with N = 0 the dip at frame 75 is a
    // drop of one point, as any other point's would be. The one at frame 0
    // is in the warm-up, where no point is examined.
    const std::vector<Drop> any = drops(c, options(6, 50, 0));
    REQUIRE(any.size() == 1);
    CHECK(any[0].interval.ref_index == 75);
    CHECK(any[0].interval.ref_end == 76);
    CHECK(any[0].points == 1);
    CHECK(any[0].baseline == 95.0);
    CHECK(any[0].min == 30.0);

    // Curve::context_first changes nothing.
    Curve unmarked = c;
    unmarked.context_first.clear();
    CHECK(same(drops(unmarked, options(6, 50, 0)), any));
    CHECK(drops(unmarked, options()).empty());
}

TEST_CASE("timeline::detect: a drop that starts at a context's first pair, on the ad's frames",
          "[timeline][timeline_detect]")
{
    // design/runs/detection-semantics-ad.txt's shape, with this test's own
    // numbers: three segments of 25 frames; the ad's two, reference frames
    // 75 to 124, with an EXT-X-CUE-OUT before its first frame and an
    // EXT-X-CUE-IN before frame 125; three more segments. Each pair's second
    // libvmaf context starts at the ad's first frame (D-037).
    constexpr std::int64_t kCueOut = 75, kCueIn = 125;

    // The low-quality rung: the ad scores 25 to 29, its first pair lower
    // still, 20; the main content alternates 91 and 93, a median of 92.
    std::vector<double> ad;
    for (int k = 0; k < 50; ++k) ad.push_back(25.0 + k % 5);
    ad[0] = 20;
    Curve low = curve_of(alternating(91, 93, 75) + ad + alternating(91, 93, 75));
    low.context_first = { 0, 75 };
    const std::vector<Drop> d = drops(low, options());
    REQUIRE(d.size() == 1);
    CHECK(d[0].interval.ref_index == 75);
    CHECK(d[0].interval.ref_end == 125);
    CHECK(d[0].points == 50);
    CHECK(d[0].baseline == 92.0);
    CHECK(d[0].min == 20.0);
    // 20, and 49 scores of 25 to 29 whose excess over 25 sums to 100.
    CHECK(d[0].mean == 1345.0 / 50);
    CHECK(d[0].drop_magnitude == 72.0);
    // The cue-out's frame lies in the drop's half-open interval, the
    // cue-in's at its end, outside it (D-078).
    CHECK((d[0].interval.ref_index <= kCueOut && kCueOut < d[0].interval.ref_end));
    CHECK(d[0].interval.ref_end == kCueIn);

    // The control rung scores the ad as well as the rest, but for the ad's
    // first pair, 80: one point, which N = 12 passes over.
    std::vector<double> control = repeat(98, 200);
    control[75] = 80;
    Curve ok = curve_of(control);
    ok.context_first = { 0, 75 };
    CHECK(drops(ok, options()).empty());

    // Had the contexts' first pairs been left out of the curve, which D-077
    // doesn't do, the drop would start at frame 76, after the cue-out's.
    Curve without = low;
    without.ref_index.erase(without.ref_index.begin() + 75);
    without.vmaf.erase(without.vmaf.begin() + 75);
    const std::vector<Drop> later = drops(without, options());
    REQUIRE(later.size() == 1);
    CHECK(later[0].interval.ref_index == 76);
    CHECK(later[0].interval.ref_end == 125);
    CHECK(later[0].points == 49);
}

TEST_CASE("timeline::detect: alternating one-point dips, as in a busy control",
          "[timeline][timeline_detect]")
{
    // A rung at 95 whose points dip to 80, more than 6 below, at reference
    // frames 75 and 76, at every other frame from 78 to 86, at 88, 89 and
    // 90, and at every other frame from 92 to 124: runs of 2, 1, 3 and 1
    // low points, 24 runs in all.
    std::vector<double> busy = repeat(95, 200);
    for (std::size_t f = 75; f <= 76; ++f) busy[f] = 80;
    for (std::size_t f = 78; f <= 86; f += 2) busy[f] = 80;
    for (std::size_t f = 88; f <= 90; ++f) busy[f] = 80;
    for (std::size_t f = 92; f <= 124; f += 2) busy[f] = 80;
    const Curve c = curve_of(busy);

    // N = 0: every run is a drop, each with the baseline 95, which the dips
    // don't enter.
    const std::vector<Drop> every = drops(c, options(6, 50, 0));
    REQUIRE(every.size() == 24);
    for (const Drop& d : every) CHECK(d.baseline == 95.0);

    // N = 1: the runs of 2 and 3 points.
    const std::vector<Drop> one = drops(c, options(6, 50, 1));
    REQUIRE(one.size() == 2);
    CHECK(one[0].interval.ref_index == 75);
    CHECK(one[0].interval.ref_end == 77);
    CHECK(one[0].points == 2);
    CHECK(one[1].interval.ref_index == 88);
    CHECK(one[1].interval.ref_end == 91);
    CHECK(one[1].points == 3);

    // N = 2: the run of 3; from N = 3, none.
    const std::vector<Drop> two = drops(c, options(6, 50, 2));
    REQUIRE(two.size() == 1);
    CHECK(two[0].interval.ref_index == 88);
    CHECK(drops(c, options(6, 50, 3)).empty());
    CHECK(drops(c, options()).empty());
}

TEST_CASE("timeline::detect: the blind start: no point of the first W/2 is examined",
          "[timeline][timeline_detect]")
{
    // W/2 rounded down, at least 1, as the prototype counts it.
    CHECK(timeline::warm_up(50) == 25);
    CHECK(timeline::warm_up(100) == 50);
    CHECK(timeline::warm_up(25) == 12);
    CHECK(timeline::warm_up(3) == 1);
    CHECK(timeline::warm_up(2) == 1);
    CHECK(timeline::warm_up(1) == 1);

    // A degraded first segment, frames 0 to 19 at 30 and the rest at 95,
    // lies in the first 25 points at W = 50 and isn't found (D-077); the
    // document's detection record states the warm-up (detection_record()).
    const Curve first = curve_of(repeat(30, 20) + repeat(95, 180));
    CHECK(drops(first, options()).empty());
    const timeline::Payload record = timeline::detection_record(options());
    CHECK(record[4].first == "warm_up");
    CHECK(std::get<std::int64_t>(record[4].second.v) == 25);

    // A drop at frames 20 to 39, which straddles the warm-up's end, is found
    // from the 26th point on: frames 20 to 24, examined by no baseline,
    // entered it.
    const Curve straddling = curve_of(repeat(95, 20) + repeat(30, 20) + repeat(95, 160));
    const std::vector<Drop> d = drops(straddling, options());
    REQUIRE(d.size() == 1);
    CHECK(d[0].interval.ref_index == 25);
    CHECK(d[0].interval.ref_end == 40);
    CHECK(d[0].points == 15);
    CHECK(d[0].baseline == 95.0);

    // With W = 10 the warm-up is 5 points, and the drop is found whole.
    const std::vector<Drop> w10 = drops(straddling, options(6, 10, 12));
    REQUIRE(w10.size() == 1);
    CHECK(w10[0].interval.ref_index == 20);
    CHECK(w10[0].points == 20);
}

TEST_CASE("timeline::detect: a coarser grid: W and N count points, a drop ends at the next point",
          "[timeline][timeline_detect]")
{
    // A 30 fps pair against a 60 fps reference has a point at every other
    // reference frame: points 0 to 199 at frames 0 to 398.
    std::vector<double> scores = alternating(94, 96, 200);
    for (std::size_t k = 100; k <= 124; ++k) scores[k] = 40;   // frames 200 to 248
    const std::vector<Drop> d = drops(curve_of(scores, 2), options());
    REQUIRE(d.size() == 1);
    CHECK(d[0].interval.ref_index == 200);
    // To the next point's frame, 250, which the last point stands for up to
    // (D-081 (g)), not to the last point's frame plus one.
    CHECK(d[0].interval.ref_end == 250);
    CHECK(d[0].first_point == 100);
    CHECK(d[0].last_point == 124);
    CHECK(d[0].points == 25);

    // N counts points (D-077): 7 low points over 14 reference frames aren't
    // more than N = 12, and 13 are.
    std::vector<double> runs = alternating(94, 96, 200);
    for (std::size_t k = 60; k < 67; ++k) runs[k] = 40;
    for (std::size_t k = 120; k < 133; ++k) runs[k] = 40;
    const std::vector<Drop> r = drops(curve_of(runs, 2), options());
    REQUIRE(r.size() == 1);
    CHECK(r[0].interval.ref_index == 240);
    CHECK(r[0].interval.ref_end == 266);
    CHECK(r[0].points == 13);

    // W counts points: the warm-up is the first 25 points, frames 0 to 48.
    // A dip at point 24 (frame 48) isn't examined, one at point 25 (frame
    // 50) is.
    std::vector<double> dips = repeat(95, 60);
    dips[24] = 40;
    dips[25] = 40;
    const std::vector<Drop> w = drops(curve_of(dips, 2), options(6, 50, 0));
    REQUIRE(w.size() == 1);
    CHECK(w[0].interval.ref_index == 50);
    CHECK(w[0].interval.ref_end == 52);

    // At the curve's end the drop ends at the last point's frame plus one.
    const std::vector<Drop> end = drops(curve_of(alternating(94, 96, 100) + repeat(40, 20), 2),
                                        options());
    REQUIRE(end.size() == 1);
    CHECK(end[0].interval.ref_index == 200);
    CHECK(end[0].interval.ref_end == 239);
}

TEST_CASE("timeline::detect: frames without a point, and points without a score, are skipped",
          "[timeline][timeline_detect]")
{
    std::vector<double> scores = alternating(94, 96, 200);
    for (std::size_t k = 100; k < 125; ++k) scores[k] = 40;

    // An unpaired reference frame inside the drop, frame 110, has no point:
    // the run goes on across it, and the interval holds it.
    Curve gap = curve_of(scores);
    gap.ref_index.erase(gap.ref_index.begin() + 110);
    gap.vmaf.erase(gap.vmaf.begin() + 110);
    std::vector<Drop> d = drops(gap, options());
    REQUIRE(d.size() == 1);
    CHECK(d[0].interval.ref_index == 100);
    CHECK(d[0].interval.ref_end == 125);
    CHECK(d[0].points == 24);

    // Frame 125 has no point: the drop ends at the next point's frame, 126.
    Curve after = curve_of(scores);
    after.ref_index.erase(after.ref_index.begin() + 125);
    after.vmaf.erase(after.vmaf.begin() + 125);
    d = drops(after, options());
    REQUIRE(d.size() == 1);
    CHECK(d[0].interval.ref_end == 126);

    // A score that isn't a finite number is no score: the point is skipped
    // in the same way, as if the curve didn't have it.
    std::vector<double> nan_inside = scores;
    nan_inside[110] = kNaN;
    d = drops(curve_of(nan_inside), options());
    REQUIRE(d.size() == 1);
    CHECK(d[0].interval.ref_index == 100);
    CHECK(d[0].interval.ref_end == 125);
    CHECK(d[0].points == 24);
    CHECK(d[0].mean == 40.0);

    std::vector<double> inf_first = scores;
    inf_first[100] = std::numeric_limits<double>::infinity();
    d = drops(curve_of(inf_first), options());
    REQUIRE(d.size() == 1);
    CHECK(d[0].interval.ref_index == 101);
    CHECK(d[0].first_point == 101);

    std::vector<double> nan_after = scores;
    nan_after[125] = kNaN;
    d = drops(curve_of(nan_after), options());
    REQUIRE(d.size() == 1);
    CHECK(d[0].interval.ref_end == 126);
    CHECK(d[0].last_point == 124);

    // The warm-up counts points with a score: with the first two points
    // without one, the 26th scored point, at frame 27, is the first
    // examined.
    std::vector<double> late = repeat(95, 60);
    late[0] = kNaN;
    late[1] = kNaN;
    late[26] = 40;
    late[27] = 40;
    d = drops(curve_of(late), options(6, 50, 0));
    REQUIRE(d.size() == 1);
    CHECK(d[0].interval.ref_index == 27);
    CHECK(d[0].points == 1);
}

TEST_CASE("timeline::detect: the baseline is the median of the last W points that weren't low",
          "[timeline][timeline_detect]")
{
    // W = 4, a warm-up of 2. At frame 5 the last four points that weren't
    // low are 90, 100, 110 and 120, 80 having left: the median of the four
    // is the mean of the middle two, 105.
    const Curve slide = curve_of({ 80, 90, 100, 110, 120, 30, 30, 30, 120 });
    const std::vector<Drop> d = drops(slide, options(6, 4, 2));
    REQUIRE(d.size() == 1);
    CHECK(d[0].interval.ref_index == 5);
    CHECK(d[0].interval.ref_end == 8);
    CHECK(d[0].baseline == 105.0);

    // An odd number of points: at frame 3 the median of 80, 90 and 100 is
    // the middle one.
    const std::vector<Drop> odd = drops(curve_of({ 80, 90, 100, 20, 100 }), options(6, 5, 0));
    REQUIRE(odd.size() == 1);
    CHECK(odd[0].interval.ref_index == 3);
    CHECK(odd[0].baseline == 90.0);

    // Low is more than T below: a score of exactly the baseline minus T,
    // 89 below a baseline of 95, isn't low; 88.75 is.
    std::vector<double> edge = repeat(95, 60);
    edge[30] = 89;
    edge[32] = 88.75;
    const std::vector<Drop> e = drops(curve_of(edge), options(6, 50, 0));
    REQUIRE(e.size() == 1);
    CHECK(e[0].interval.ref_index == 32);
    CHECK(e[0].min == 88.75);

    // T = 0: any score below the baseline is low.
    const std::vector<Drop> zero = drops(curve_of(edge), options(0, 50, 0));
    REQUIRE(zero.size() == 2);
    CHECK(zero[0].interval.ref_index == 30);
    CHECK(zero[1].interval.ref_index == 32);
}

TEST_CASE("timeline::detect: a low point never enters the baseline, the same through a drop",
          "[timeline][timeline_detect]")
{
    // W = 10, and a drop of 60 points, six windows long, rising from 20 to
    // 79, below 95 - 6 throughout. A baseline that took the drop's points
    // in would have followed it down within 10 points and ended it there,
    // as the median of the last W points low or not did in the design's
    // sweep (eval/phase3-survey/design-evaluation/runs/05_detection_sweep.txt);
    // this one stays at 95, and the drop is whole.
    std::vector<double> rising;
    for (int k = 0; k < 60; ++k) rising.push_back(20.0 + k);
    std::vector<double> scores = alternating(94, 96, 40) + rising + alternating(94, 96, 40)
                               + alternating(97, 99, 30) + repeat(50, 20) + alternating(97, 99, 10);
    const std::vector<Drop> d = drops(curve_of(scores), options(6, 10, 12));
    REQUIRE(d.size() == 2);
    CHECK(d[0].interval.ref_index == 40);
    CHECK(d[0].interval.ref_end == 100);
    CHECK(d[0].points == 60);
    CHECK(d[0].baseline == 95.0);
    CHECK(d[0].min == 20.0);
    CHECK(d[0].mean == 49.5);

    // The second drop's baseline is the median of the 10 points before it,
    // 97 and 99, none of the first drop's.
    CHECK(d[1].interval.ref_index == 170);
    CHECK(d[1].interval.ref_end == 190);
    CHECK(d[1].baseline == 98.0);
    CHECK(d[1].drop_magnitude == 48.0);

    // So a step down that doesn't come back is one drop to the curve's end:
    // no point after it is near enough the baseline to enter it.
    const std::vector<Drop> step = drops(curve_of(alternating(94, 96, 100) + repeat(85, 100)),
                                         options(6, 10, 12));
    REQUIRE(step.size() == 1);
    CHECK(step[0].interval.ref_index == 100);
    CHECK(step[0].interval.ref_end == 200);
    CHECK(step[0].baseline == 95.0);
}

TEST_CASE("timeline::detect: the same drops from the same input, twice",
          "[timeline][timeline_detect]")
{
    // A busy curve from a fixed seed, with steps, dips and noise.
    std::mt19937_64 rng(77);
    std::vector<double> scores;
    double level = 92;
    for (int k = 0; k < 2000; ++k) {
        if (rng() % 150 == 0) level = 60.0 + static_cast<double>(rng() % 40);
        double v = level + static_cast<double>(rng() % 17) * 0.25 - 2.0;
        if (rng() % 9 == 0) v -= 10.0 + static_cast<double>(rng() % 50);
        scores.push_back(v);
    }
    const Curve c = curve_of(scores);
    const std::vector<Drop> a = drops(c, options(6, 50, 3));
    const std::vector<Drop> b = drops(c, options(6, 50, 3));
    CHECK_FALSE(a.empty());
    CHECK(same(a, b));

    // Only the frames and the scores count: not the variant, nor the
    // context starts.
    Curve other = c;
    other.variant       = 7;
    other.context_first = { 0, 300, 1200 };
    CHECK(same(drops(other, options(6, 50, 3)), a));
}

TEST_CASE("timeline::detect: the walk against the prototype's plain one, on random curves",
          "[timeline][timeline_detect]")
{
    // Random curves with levels, steps, dips, ties (scores in halves),
    // frames without a point and points without a score, and random T, W
    // and N: the drops and every number of them as the prototype's plain
    // walk gives them.
    std::mt19937_64 rng(2026);
    const double       thresholds[] = { 0, 3, 6, 10, 25 };
    const std::int64_t windows[]    = { 1, 2, 3, 4, 5, 10, 25, 50, 1000 };
    const std::int64_t mins[]       = { 0, 1, 2, 5, 12 };
    std::size_t found = 0;
    for (int trial = 0; trial < 500; ++trial) {
        const std::size_t n = static_cast<std::size_t>(rng() % 301);
        Curve c;
        std::int64_t frame = static_cast<std::int64_t>(rng() % 3);
        double level = 60.0 + static_cast<double>(rng() % 40);
        for (std::size_t k = 0; k < n; ++k) {
            if (rng() % 40 == 0) level = 20.0 + static_cast<double>(rng() % 80);
            double v = level + (static_cast<double>(rng() % 9) - 4.0) * 0.5;
            if (rng() % 12 == 0) v -= 5.0 + static_cast<double>(rng() % 40);
            if (rng() % 50 == 0) v = kNaN;
            c.ref_index.push_back(frame);
            c.vmaf.push_back(v);
            frame += 1 + (rng() % 10 == 0 ? static_cast<std::int64_t>(rng() % 3) : 0);
        }
        const double       t = thresholds[rng() % std::size(thresholds)];
        const std::int64_t w = windows[rng() % std::size(windows)];
        const std::int64_t m = mins[rng() % std::size(mins)];
        const std::vector<Drop> d = drops(c, options(t, w, m));
        INFO("trial " << trial << ": " << n << " points, T = " << t << ", W = " << w
                      << ", N = " << m);
        CHECK(same(d, plain_walk(c, t, w, m)));
        found += d.size();
    }
    CHECK(found > 0);
}

TEST_CASE("timeline::detect: without W and N it finds nothing; its options",
          "[timeline][timeline_detect]")
{
    const Curve c = curve_of(alternating(94, 96, 100) + repeat(40, 25) + alternating(94, 96, 75));

    // T is 6 by default; W and N have no default (D-077).
    const DetectOptions none;
    CHECK(none.threshold == 6.0);
    CHECK(timeline::kDefaultThreshold == 6.0);
    CHECK_FALSE(none.window.has_value());
    CHECK_FALSE(none.min_points.has_value());
    CHECK_FALSE(none.given());
    CHECK(timeline::check_options(none).empty());

    // Without both, nothing is detected, and that is no error.
    for (const DetectOptions& o : { none, DetectOptions{ .window = 50 },
                                    DetectOptions{ .min_points = 12 } }) {
        std::string err = "not cleared";
        CHECK(timeline::detect(c, o, err).empty());
        CHECK(err.empty());
    }
    DetectOptions both;
    both.window     = 50;
    both.min_points = 12;
    CHECK(both.given());
    CHECK(drops(c, both).size() == 1);

    // Values detection can run with: T from 0, W from 1, N from 0.
    CHECK(timeline::check_options(options(0, 1, 0)).empty());
    CHECK(timeline::check_options(options(40, 1000000, 1000)).empty());
    const auto names = [](const DetectOptions& o, const char* what) {
        const std::string err = timeline::check_options(o);
        return !err.empty() && err.find(what) != std::string::npos;
    };
    CHECK(names(options(-1, 50, 12), "threshold"));
    CHECK(names(options(kNaN, 50, 12), "threshold"));
    CHECK(names(options(std::numeric_limits<double>::infinity(), 50, 12), "threshold"));
    CHECK(names(options(6, 0, 12), "window"));
    CHECK(names(options(6, -3, 12), "window"));
    CHECK(names(options(6, 50, -1), "minimum"));
    CHECK(names(DetectOptions{ .threshold = -1 }, "threshold"));

    // detect() with one: no drops, and the same error.
    std::string err;
    CHECK(timeline::detect(c, options(6, 0, 12), err).empty());
    CHECK(err == timeline::check_options(options(6, 0, 12)));
}

TEST_CASE("timeline::detect: curves it refuses, and curves too short to examine",
          "[timeline][timeline_detect]")
{
    // An empty curve and one no longer than the warm-up: no drops, no
    // error. At W = 100 the first 50 points aren't examined, whatever their
    // scores; the 51st is, against the median of the 50, 47.5.
    CHECK(drops(Curve{}, options()).empty());
    CHECK(drops(curve_of({ 10 }), options()).empty());
    CHECK(drops(curve_of(repeat(95, 25) + repeat(0, 25)), options(6, 100, 0)).empty());
    const std::vector<Drop> next = drops(curve_of(repeat(95, 25) + repeat(0, 26)),
                                         options(6, 100, 0));
    REQUIRE(next.size() == 1);
    CHECK(next[0].interval.ref_index == 50);
    CHECK(next[0].baseline == 47.5);

    const auto refused = [](const Curve& c, const char* what) {
        std::string err;
        const bool none = timeline::detect(c, options(), err).empty();
        return none && err.find(what) != std::string::npos;
    };
    Curve lengths = curve_of(repeat(95, 10));
    lengths.vmaf.pop_back();
    CHECK(refused(lengths, "10 reference frames for 9 scores"));

    Curve repeated = curve_of(repeat(95, 10));
    repeated.ref_index[5] = 4;
    CHECK(refused(repeated, "ascend"));
    Curve backward = curve_of(repeat(95, 10));
    backward.ref_index[5] = 2;
    CHECK(refused(backward, "ascend"));

    CHECK(refused(curve_of(repeat(95, 10), 1, -1), "positions from 0"));
    Curve last = curve_of(repeat(95, 3));
    last.ref_index[2] = std::numeric_limits<std::int64_t>::max();
    CHECK(refused(last, "no frame can follow"));
}

TEST_CASE("timeline::detection_record: every parameter detection runs with",
          "[timeline][timeline_detect]")
{
    using timeline::Value;
    const auto as_int = [](const Value& v) { return std::get<std::int64_t>(v.v); };
    const auto as_str = [](const Value& v) { return std::get<std::string>(v.v); };

    // The keys of the design's §8 detection record that detection decides,
    // in its order.
    const timeline::Payload r = timeline::detection_record(options());
    REQUIRE(r.size() == 6);
    CHECK(r[0].first == "threshold");
    CHECK(std::get<double>(r[0].second.v) == 6.0);
    CHECK(r[1].first == "window");
    CHECK(as_int(r[1].second) == 50);
    CHECK(r[2].first == "min_points");
    CHECK(as_int(r[2].second) == 12);
    CHECK(r[3].first == "baseline");
    CHECK(as_str(r[3].second) == "median-non-low");
    CHECK(r[4].first == "warm_up");   // the blind start, which the document states (D-077)
    CHECK(as_int(r[4].second) == 25);
    CHECK(r[5].first == "context_first");
    CHECK(as_str(r[5].second) == "no-special-case");

    CHECK(as_int(timeline::detection_record(options(4.5, 25, 0))[4].second) == 12);
    CHECK(std::get<double>(timeline::detection_record(options(4.5, 25, 0))[0].second.v) == 4.5);

    // Without W and N they are null, and so is the warm-up.
    const timeline::Payload none = timeline::detection_record(DetectOptions{});
    REQUIRE(none.size() == 6);
    CHECK(std::get<double>(none[0].second.v) == 6.0);
    CHECK(std::holds_alternative<std::monostate>(none[1].second.v));
    CHECK(std::holds_alternative<std::monostate>(none[2].second.v));
    CHECK(std::holds_alternative<std::monostate>(none[4].second.v));
}
