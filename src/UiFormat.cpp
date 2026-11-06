#include "UiFormat.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <string_view>

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

std::string ellipsize_middle(const std::string& s, float max_w,
                             const std::function<float(const std::string&)>& width)
{
    if (s.empty() || width(s) <= max_w) return s;
    std::vector<std::size_t> starts;   // each character's first byte
    for (std::size_t i = 0; i < s.size(); ++i)
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) starts.push_back(i);
    const std::size_t n = starts.size();
    if (n == 0) return "…";
    auto cut = [&](std::size_t keep) {   // keep / 2 characters of the head, the rest of the tail
        const std::size_t head = keep / 2, tail = keep - head;
        return s.substr(0, starts[head]) + "…" + (tail > 0 ? s.substr(starts[n - tail]) : std::string());
    };
    // Each character kept adds to the width, so the most that fit is found by halving.
    std::size_t lo = 0, hi = n - 1;   // cut(lo) is taken even when it does not fit
    while (lo < hi) {
        const std::size_t mid = (lo + hi + 1) / 2;
        if (width(cut(mid)) <= max_w) lo = mid;
        else hi = mid - 1;
    }
    return cut(lo);
}

std::vector<std::string> distinct_labels(const std::vector<std::string>& labels)
{
    std::vector<std::string> out = labels;
    const auto sep = [](char c) { return c == ' ' || c == '/' || c == '_' || c == '-' || c == '.'; };
    std::map<std::string, std::vector<std::size_t>> groups;   // head up to the first '/' → labels
    for (std::size_t i = 0; i < labels.size(); ++i) {
        const auto slash = labels[i].find('/');
        if (slash != std::string::npos) groups[labels[i].substr(0, slash + 1)].push_back(i);
    }
    for (const auto& [head, idx] : groups) {
        if (idx.size() < 2) continue;
        std::vector<std::string_view> names;   // what follows the head
        std::size_t shortest = std::string::npos;
        for (const std::size_t i : idx) {
            names.push_back(std::string_view(labels[i]).substr(head.size()));
            shortest = std::min(shortest, names.back().size());
        }
        if (shortest == 0) continue;
        const std::string_view n0 = names.front();
        const auto all_at = [&](auto&& pos_of) {   // every name has n0's byte at its pos_of(name)
            return std::all_of(names.begin(), names.end(),
                               [&](std::string_view n) { return n[pos_of(n)] == n0[pos_of(n0)]; });
        };
        // The shared head of the names, in bytes, then back to just after a separator. At least one
        // character of the shortest is left; the separators are ASCII, so a cut never splits UTF-8.
        std::size_t p = 0;
        while (p + 1 < shortest && all_at([&](std::string_view) { return p; })) ++p;
        while (p > 0 && !sep(n0[p - 1])) --p;
        // The shared tail likewise, back to just before a separator, and short of the head.
        std::size_t s = 0;
        while (p + s + 1 < shortest && all_at([&](std::string_view n) { return n.size() - 1 - s; })) ++s;
        while (s > 0 && !sep(n0[n0.size() - s])) --s;
        if (p == 0 && s == 0) continue;
        for (std::size_t k = 0; k < idx.size(); ++k)
            out[idx[k]] = head + (p > 0 ? "…" : "") + std::string(names[k].substr(p, names[k].size() - p - s))
                        + (s > 0 ? "…" : "");
    }
    return out;
}

} // namespace ui
