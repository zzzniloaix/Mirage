#include "UiFormat.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <set>
#include <string_view>

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

std::vector<std::string> row_labels(const std::vector<std::string>& labels)
{
    // Coarse tokens: a language tag ("cmn-TW") or a layout ("5.1") stays whole.
    const auto sep = [](char c) { return c == ' ' || c == '/' || c == '_'; };
    struct Tok { std::size_t b, e; bool first; };   // a token's bytes [b, e), its first time in the name
    std::vector<std::string> out(labels.size());
    std::map<std::string, std::vector<std::size_t>> groups;          // head up to the first '/' → labels
    for (std::size_t i = 0; i < labels.size(); ++i) {
        const auto slash = labels[i].find('/');
        if (slash == std::string::npos) out[i] = labels[i];
        else groups[labels[i].substr(0, slash + 1)].push_back(i);
    }
    const std::vector<std::string> ends = distinct_labels(labels);   // the fallback, with the head
    for (const auto& [head, idx] : groups) {
        const std::size_t n = idx.size();
        const auto fallback = [&] {
            for (const std::size_t i : idx) out[i] = ends[i].substr(head.size());
        };
        if (n < 2) {
            fallback();
            continue;
        }
        std::vector<std::string_view> names(n);                 // what follows the head
        std::vector<std::vector<Tok>> toks(n);
        std::map<std::string_view, std::vector<bool>> who;      // token → the names that have it
        for (std::size_t k = 0; k < n; ++k) {
            names[k] = std::string_view(labels[idx[k]]).substr(head.size());
            std::set<std::string_view> seen;
            for (std::size_t i = 0; i < names[k].size();) {
                while (i < names[k].size() && sep(names[k][i])) ++i;
                const std::size_t b = i;
                while (i < names[k].size() && !sep(names[k][i])) ++i;
                if (i == b) continue;
                const std::string_view w = names[k].substr(b, i - b);
                const bool first = seen.insert(w).second;
                toks[k].push_back({ b, i, first });
                if (first) {
                    auto& v = who[w];
                    v.resize(n);
                    v[k] = true;
                }
            }
        }
        std::set<std::string_view> dropped;
        const auto row = [&](std::size_t k) {   // the name's tokens not dropped, each once, joined by spaces
            std::string r;
            for (const Tok& t : toks[k]) {
                const std::string_view w = names[k].substr(t.b, t.e - t.b);
                if (!t.first || dropped.contains(w)) continue;
                if (!r.empty()) r += ' ';
                r += w;
            }
            return r;
        };
        const auto apart = [&] {   // every row non-empty and unlike the others
            std::set<std::string> rows;
            for (std::size_t k = 0; k < n; ++k) {
                const std::string r = row(k);
                if (r.empty() || !rows.insert(r).second) return false;
            }
            return true;
        };
        const auto having = [](const std::vector<bool>& v) {
            return static_cast<std::size_t>(std::count(v.begin(), v.end(), true));
        };
        // Tokens every name has.
        for (const auto& [w, v] : who)
            if (having(v) == n) dropped.insert(w);
        if (!apart()) {
            fallback();
            continue;
        }
        // Tokens at least 80% of the names have, the most common first, each only if the rows stay apart.
        std::vector<std::pair<std::size_t, std::string_view>> most;
        for (const auto& [w, v] : who)
            if (having(v) < n && having(v) * 5 >= n * 4) most.push_back({ having(v), w });
        std::stable_sort(most.begin(), most.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        for (const auto& m : most) {
            dropped.insert(m.second);
            if (!apart()) dropped.erase(m.second);
        }
        // Tokens that come in exactly the same names as one before them: the first of them stays.
        std::map<std::vector<bool>, std::string_view> first_of;
        for (std::size_t k = 0; k < n; ++k)
            for (const Tok& t : toks[k]) {
                const std::string_view w = names[k].substr(t.b, t.e - t.b);
                if (!t.first || dropped.contains(w)) continue;
                const auto [it, fresh] = first_of.emplace(who[w], w);
                if (!fresh && it->second != w) dropped.insert(w);
            }
        // Then the ends that every row still shares, as distinct_labels drops them.
        std::vector<std::string> rows;
        for (std::size_t k = 0; k < n; ++k) rows.push_back(head + row(k));
        rows = distinct_labels(rows);
        for (std::size_t k = 0; k < n; ++k) out[idx[k]] = rows[k].substr(head.size());
    }
    return out;
}
} // namespace ui
