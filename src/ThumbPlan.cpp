#include "ThumbPlan.h"

#include <algorithm>
#include <cmath>

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

bool index_too_sparse(std::size_t n_keyframes, double duration)
{
    if (!(duration > 0.0)) return n_keyframes == 0;
    const std::size_t per_20s = static_cast<std::size_t>(std::min(16.0, std::floor(duration / 20.0)));
    return n_keyframes < std::max<std::size_t>(2, per_20s);
}

std::vector<double> sample_times(double duration, int max_samples, double min_spacing)
{
    std::vector<double> out;
    if (!(duration > 0.0) || max_samples < 1) return out;
    int n = max_samples;
    if (min_spacing > 0.0)
        n = static_cast<int>(std::min<double>(n, std::floor(duration / min_spacing) + 1.0));
    n = std::max(n, 1);
    out.reserve(static_cast<std::size_t>(n));
    for (int k = 0; k < n; ++k)
        out.push_back(duration * static_cast<double>(k) / static_cast<double>(n));
    return out;
}

int pick_thumb_stream(const std::vector<VideoCandidate>& cands, int current, int min_height)
{
    const auto cur = std::find_if(cands.begin(), cands.end(),
                                  [&](const VideoCandidate& c) { return c.index == current; });
    if (cur == cands.end() || cur->bit_rate <= 0) return current;
    const int need_h = cur->height > 0 ? std::min(min_height, cur->height) : min_height;
    const VideoCandidate* best = &*cur;
    for (const VideoCandidate& c : cands) {
        if (c.codec_id != cur->codec_id || c.bit_rate <= 0 || c.height < need_h) continue;
        if (c.bit_rate < best->bit_rate) best = &c;
    }
    return best->index;
}

} // namespace thumbs
