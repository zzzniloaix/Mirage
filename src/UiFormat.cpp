#include "UiFormat.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace ui {

double timeline_start(double video_start, double container_start, double duration)
{
    if (!(duration > 0.0)) return 0.0;   // live: the stream's own clock
    if (std::isfinite(video_start)) return video_start;
    if (std::isfinite(container_start)) return container_start;
    return 0.0;
}

double timeline_duration(double container_start, double duration, double start)
{
    if (!(duration > 0.0) || !std::isfinite(container_start)) return duration;
    const double span = container_start + duration - start;
    return span > 0.0 ? span : duration;
}

double position(double pts, double start)
{
    return pts - start;
}

double pts_at(double pos, double start)
{
    const double p = pos + start;
    return p > 0.0 ? p : 0.0;   // also catches NaN
}

long long frame_number(double pts, double start, double fps)
{
    const double f = position(pts, start) * fps;
    return f > 0.0 ? std::llround(f) : 0;   // also catches NaN
}

double bar_fraction(double pts, double start, double dur)
{
    if (!(dur > 0.0)) return 0.0;
    const double f = position(pts, start) / dur;
    return f > 0.0 ? std::min(f, 1.0) : 0.0;   // also catches NaN
}

double pts_at_fraction(double frac, double start, double dur)
{
    const double f = frac > 0.0 ? std::min(frac, 1.0) : 0.0;
    return pts_at(dur > 0.0 ? f * dur : 0.0, start);
}

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

std::string format_timecode(double pts, double start, double dur, int mode, double fps)
{
    if (!(dur > 0.0)) {
        const double t = pts > 0.0 ? pts : 0.0;
        // Seconds since 1970 (live CMAF timestamps such as Unified
        // Streaming's): the UTC time of day, as the stream's own clock.
        if (t >= 1e9) {
            const long long day_ms = std::llround(t * 1000.0) % 86'400'000;
            return std::format("LIVE {:02d}:{:02d}:{:02d}.{:03d} UTC", day_ms / 3'600'000, (day_ms / 60'000) % 60,
                               (day_ms / 1000) % 60, day_ms % 1000);
        }
        return "LIVE " + format_clock(t, t >= 3600.0);
    }

    if (mode == 3) {
        // Seconds, as ffprobe and the logs give PTS; negative ones as they are.
        const long long ms = std::isfinite(pts) ? std::llround(pts * 1000.0) : 0;
        const long long a  = ms < 0 ? -ms : ms;
        return std::format("PTS {}{}.{:03d}", ms < 0 ? "-" : "", a / 1000, a % 1000);
    }
    double t = position(pts, start);
    if (!(t > 0.0)) t = 0.0;
    const bool hours = dur >= 3600.0;
    if (mode == 1 && fps > 0.0) {
        return std::format("f {} / {}", frame_number(pts, start, fps), std::llround(dur * fps));
    }
    if (mode == 2) {
        return "-" + format_clock(std::max(0.0, dur - t), hours) + " / " + format_clock(dur, hours);
    }
    return format_clock(t, hours) + " / " + format_clock(dur, hours);
}

std::vector<float> decimate_ticks(const std::vector<double>& kf, double start, double dur,
                                  float width, float min_px)
{
    std::vector<float> out;
    if (kf.empty() || !(dur > 0.0) || width <= 0.0f) return out;

    auto to_x = [&](double p) {
        return std::clamp(static_cast<float>(position(p, start) / dur) * width, 0.0f, width);
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
