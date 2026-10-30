#pragma once

// The pooled values of a pair's per-frame VMAF scores (work order section
// 1.5, DECISIONS.md D-027). They are computed from the per-frame scores, so
// that a pair whose scores come from several libvmaf contexts pools as one
// run: the mean, harmonic mean and minimum with libvmaf's formulas, summed
// frame by frame in index order as libvmaf's pooling sums them, so that for
// the scores of one context they are the values vmaf_score_pooled() gives,
// to the last bit (tests/test_vmaf_pool.cpp); the percentiles by linear
// interpolation, as eval/pooling/pooling.py computes them. No FFmpeg /
// libvmaf here so it can be unit-tested in mirage_tests.

#include <cstddef>
#include <limits>
#include <vector>

// NaN where a value is undefined: every value of no scores, the minimum
// without the first frame of one, and the minimum without the contexts'
// first frames when every pair starts a context.
struct VMAFPooled {
    double mean                    = std::numeric_limits<double>::quiet_NaN();
    double harmonic_mean           = std::numeric_limits<double>::quiet_NaN();
    double min                     = std::numeric_limits<double>::quiet_NaN();
    double min_without_first_frame = std::numeric_limits<double>::quiet_NaN();
    double min_without_context_first_frames = std::numeric_limits<double>::quiet_NaN();
    double p1  = std::numeric_limits<double>::quiet_NaN();
    double p5  = std::numeric_limits<double>::quiet_NaN();
    double p10 = std::numeric_limits<double>::quiet_NaN();
    double p25 = std::numeric_limits<double>::quiet_NaN();
    double p50 = std::numeric_limits<double>::quiet_NaN();
};

namespace vmafpool {

// The p-th percentile (0 <= p <= 100) of `sorted`, ascending and not empty,
// by linear interpolation between the closest ranks, Hyndman and Fan's
// definition 7: with h = (n - 1) * p / 100 and k = floor(h),
// sorted[k] + (h - k) * (sorted[k + 1] - sorted[k]), or sorted[k] when k is
// the last rank. h - k is the double nearest ((n - 1) * p mod 100) / 100,
// and the difference, the product and the sum are each rounded on their
// own, as in pooling.py's "linear" definition.
double percentile(const std::vector<double>& sorted, unsigned p);

// The pooled values of `scores`, given in frame order, of a pair scored in
// libvmaf contexts whose first pairs are at `context_starts` (pair 0 always
// starts one, so empty means a single context):
//   mean           sum / n                       (VMAF_POOL_METHOD_MEAN)
//   harmonic_mean  n / sum(1 / (x + 1)) - 1      (VMAF_POOL_METHOD_HARMONIC_MEAN)
//   min            the smallest score            (VMAF_POOL_METHOD_MIN)
//   min_without_first_frame   the smallest score of frames 1 to n - 1
//   min_without_context_first_frames   the smallest score of the pairs that
//                             don't start a context, which have motion to a
//                             frame before them (DECISIONS.md D-037); for a
//                             pair scored in one context, the value above
//   p1, p5, p10, p25, p50     percentile()
VMAFPooled pool(const std::vector<double>& scores,
                const std::vector<std::size_t>& context_starts = {});

} // namespace vmafpool
