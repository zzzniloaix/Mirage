#include "ThumbPlan.h"

namespace thumbs {

double sample_stride(double duration, int max_thumbs)
{
    if (!(duration > 0.0) || max_thumbs < 2) return 0.0;
    // N samples span N-1 intervals; dividing by max-1 keeps the count <= max
    // when the keyframes lie within [0, duration].
    return duration / static_cast<double>(max_thumbs - 1);
}

std::vector<std::size_t> sample_keyframes(const std::vector<double>& kf,
                                          double stride, int max_thumbs)
{
    std::vector<std::size_t> out;
    if (kf.empty()) return out;
    out.push_back(0);
    double last = kf[0];
    for (std::size_t i = 1; i < kf.size(); ++i) {
        if (kf[i] - last >= stride) {
            out.push_back(i);
            last = kf[i];
        }
    }
    if (max_thumbs > 0 && out.size() > static_cast<std::size_t>(max_thumbs)) {
        const std::size_t n    = out.size();
        const std::size_t keep = static_cast<std::size_t>(max_thumbs);
        std::vector<std::size_t> thin;
        thin.reserve(keep);
        for (std::size_t k = 0; k < keep; ++k)
            thin.push_back(out[k * n / keep]);   // strictly increasing, starts at out[0]
        out = std::move(thin);
    }
    return out;
}

std::vector<std::size_t> coarse_to_fine(std::size_t n, std::size_t step)
{
    std::vector<std::size_t> order;
    order.reserve(n);
    if (step <= 1) {
        for (std::size_t i = 0; i < n; ++i) order.push_back(i);
        return order;
    }
    for (std::size_t i = 0; i < n; i += step) order.push_back(i);
    for (std::size_t i = 0; i < n; ++i)
        if (i % step != 0) order.push_back(i);
    return order;
}

} // namespace thumbs
