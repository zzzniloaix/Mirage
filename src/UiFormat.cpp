#include "UiFormat.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace ui {

std::string format_clock(double t, bool with_hours)
{
    if (!(t > 0.0)) t = 0.0;   // also catches NaN
    const long long total_ms = std::llround(t * 1000.0);
    const long long ms  = total_ms % 1000;
    const long long sec = (total_ms / 1000) % 60;
    if (with_hours) {
        const long long min = (total_ms / 60000) % 60;
        const long long hr  = total_ms / 3600000;
        return std::format("{}:{:02d}:{:02d}.{:03d}", hr, min, sec, ms);
    }
    const long long min = total_ms / 60000;
    return std::format("{:02d}:{:02d}.{:03d}", min, sec, ms);
}

std::string format_timecode(double t, double dur, int mode, double fps)
{
    if (!(t > 0.0)) t = 0.0;
    if (!(dur > 0.0)) {
        // Seconds since 1970 (live CMAF timestamps such as Unified
        // Streaming's): the UTC time of day, as the stream's own clock.
        if (t >= 1e9) {
            const long long day_ms = std::llround(t * 1000.0) % 86'400'000;
            return std::format("LIVE {:02d}:{:02d}:{:02d}.{:03d} UTC", day_ms / 3'600'000, (day_ms / 60'000) % 60,
                               (day_ms / 1000) % 60, day_ms % 1000);
        }
        return "LIVE " + format_clock(t, t >= 3600.0);
    }

    const bool hours = dur >= 3600.0;
    if (mode == 1 && fps > 0.0) {
        return std::format("f {} / {}", std::llround(t * fps), std::llround(dur * fps));
    }
    if (mode == 2) {
        return "-" + format_clock(std::max(0.0, dur - t), hours) + " / " + format_clock(dur, hours);
    }
    return format_clock(t, hours) + " / " + format_clock(dur, hours);
}

std::vector<float> decimate_ticks(const std::vector<double>& kf, double dur,
                                  float width, float min_px)
{
    std::vector<float> out;
    if (kf.empty() || !(dur > 0.0) || width <= 0.0f) return out;

    auto to_x = [&](double p) {
        return std::clamp(static_cast<float>(p / dur) * width, 0.0f, width);
    };

    out.push_back(to_x(kf.front()));
    for (std::size_t i = 1; i < kf.size(); ++i) {
        const float x = to_x(kf[i]);
        if (x - out.back() >= min_px) out.push_back(x);
    }

    // Keep the last keyframe: replace the last kept tick if it collides
    // (unless that tick is the first one, which must also be kept).
    const float last = to_x(kf.back());
    if (out.back() != last) {
        if (out.size() > 1 && last - out[out.size() - 2] >= min_px)
            out.back() = last;
        else if (last - out.back() >= min_px)
            out.push_back(last);
    }
    return out;
}

int index_at_or_before(const std::vector<double>& pts, double t, double max_gap)
{
    if (pts.empty()) return -1;
    auto it = std::upper_bound(pts.begin(), pts.end(), t);
    if (it == pts.begin()) return -1;
    --it;
    if (t - *it > max_gap) return -1;
    return static_cast<int>(it - pts.begin());
}

} // namespace ui
