#include "VMAFPool.h"

#include <algorithm>
#include <cstdint>

// CMakeLists.txt compiles this file with -ffp-contract=off: a fused
// multiply-add in percentile() rounds once where pooling.py rounds the
// product and the sum each, and so can differ in the last bit.

namespace vmafpool {

double percentile(const std::vector<double>& sorted, unsigned p)
{
    const std::uint64_t h100 = static_cast<std::uint64_t>(sorted.size() - 1) * p;   // 100 * h
    const std::size_t   k    = static_cast<std::size_t>(h100 / 100);
    if (k + 1 >= sorted.size()) return sorted[k];
    const double fraction = static_cast<double>(h100 % 100) / 100.0;
    const double step     = fraction * (sorted[k + 1] - sorted[k]);
    return sorted[k] + step;
}

VMAFPooled pool(const std::vector<double>& scores)
{
    VMAFPooled out;
    if (scores.empty()) return out;

    // libvmaf's sums, in index order.
    double sum = 0.0, inverse_sum = 0.0;
    for (double s : scores) {
        sum += s;
        inverse_sum += 1.0 / (s + 1.0);
    }
    const double n = static_cast<double>(scores.size());
    out.mean          = sum / n;
    out.harmonic_mean = n / inverse_sum - 1.0;
    out.min           = *std::min_element(scores.begin(), scores.end());
    if (scores.size() > 1)
        out.min_without_first_frame = *std::min_element(scores.begin() + 1, scores.end());

    std::vector<double> sorted = scores;
    std::sort(sorted.begin(), sorted.end());
    out.p1  = percentile(sorted, 1);
    out.p5  = percentile(sorted, 5);
    out.p10 = percentile(sorted, 10);
    out.p25 = percentile(sorted, 25);
    out.p50 = percentile(sorted, 50);
    return out;
}

} // namespace vmafpool
