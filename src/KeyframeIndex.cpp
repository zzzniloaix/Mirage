#include "KeyframeIndex.h"
#include "Logger.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <iterator>
#include <string>

namespace {

using Keyframe = KeyframeIndex::Keyframe;

// One frame where the stream declares no rate (a probe's back-off).
constexpr double kDefaultFrame = 1.0 / 30.0;
// probe_span without segment durations.
constexpr double kDefaultSpan = 10.0;

// next / prev / at_or_before on a list sorted by pts (1 ms tolerance).
std::optional<Keyframe> next_in(const std::vector<Keyframe>& m, double t)
{
    const auto k = std::upper_bound(m.begin(), m.end(), t + 0.001,
                                    [](double v, const Keyframe& kf) { return v < kf.pts; });
    if (k == m.end()) return std::nullopt;
    return *k;
}

std::optional<Keyframe> prev_in(const std::vector<Keyframe>& m, double t)
{
    const auto k = std::lower_bound(m.begin(), m.end(), t - 0.001,
                                    [](const Keyframe& kf, double v) { return kf.pts < v; });
    if (k == m.begin()) return std::nullopt;
    return *std::prev(k);
}

std::optional<Keyframe> at_or_before_in(const std::vector<Keyframe>& m, double t)
{
    const auto k = std::upper_bound(m.begin(), m.end(), t + 0.001,
                                    [](double v, const Keyframe& kf) { return v < kf.pts; });
    if (k == m.begin()) return std::nullopt;
    return *std::prev(k);
}

} // namespace

double KeyframeIndex::tolerance(double frame_s)
{
    return frame_s > 0.0 ? std::max(0.002, 0.5 * frame_s) : 0.010;
}

void KeyframeIndex::set_hls(bool hls)
{
    std::lock_guard lk(mtx_);
    if (hls_ == hls) return;
    hls_ = hls;
    for (auto& [id, s] : streams_) rebuild(id, s);
    ++version_;
}

bool KeyframeIndex::hls() const
{
    std::lock_guard lk(mtx_);
    return hls_;
}

KeyframeIndex::Stream& KeyframeIndex::stream_locked(int stream)
{
    return streams_[stream];
}

void KeyframeIndex::set_stream(int stream, double start_s, double frame_s, bool tracked)
{
    std::lock_guard lk(mtx_);
    Stream& s = stream_locked(stream);
    s.start_s = start_s;
    s.frame_s = frame_s > 0.0 ? frame_s : 0.0;
    s.tracked = tracked;
    rebuild(stream, s);
    ++version_;
}

void KeyframeIndex::set_segments(int stream, const std::vector<double>& durations)
{
    std::lock_guard lk(mtx_);
    Stream& s = stream_locked(stream);
    s.durations = durations;
    rebuild(stream, s);
    ++version_;
    if (!s.durations.empty() && !s.st.aligned)
        logger::info("Keyframes: stream {} — the playlist's {} segment durations are not whole frames "
                     "(or the start / frame rate is unknown): segment starts are not predicted",
                     stream, s.durations.size());
}

void KeyframeIndex::observe(int stream, double pts, double dts, bool key, std::uint32_t run, bool segment_start)
{
    if (!std::isfinite(pts)) return;
    if (!std::isfinite(dts)) dts = pts;
    std::lock_guard lk(mtx_);
    Stream& s = stream_locked(stream);
    if (!s.tracked) return;

    // The run has read every packet up to this one (decode order), so every
    // keyframe up to its dts — up to its pts, for a keyframe: keyframes are
    // decoded in display order.
    auto [rit, fresh] = s.runs.try_emplace(run);
    if (fresh && s.runs.size() > kMaxRuns) {
        fold_runs(s);
        rit = s.runs.find(run);
    }
    Run& r = rit->second;
    r.end = std::max(r.end, key ? std::max(pts, dts) : dts);
    if (!key && !segment_start) return;   // the list does not change
    bool changed = false;

    if (segment_start) {
        if (!key) {
            ++s.landings_nokey;
            changed = true;
        } else if (std::none_of(s.landings.begin(), s.landings.end(),
                                [&](double p) { return std::abs(p - pts) < 1e-6; })) {
            s.landings.push_back(pts);
            changed = true;
        }
    }
    if (key) {
        // Every packet between two keyframes of one run was read: a segment
        // start predicted strictly between them has no keyframe.
        if (std::isfinite(r.last_key) && pts > r.last_key) {
            const std::pair<double, double> g{ r.last_key, pts };
            const auto it = std::lower_bound(s.gaps.begin(), s.gaps.end(), g);
            if (it == s.gaps.end() || *it != g) {
                s.gaps.insert(it, g);
                changed = true;
            }
        }
        if (!std::isfinite(r.first_key)) r.first_key = pts;
        r.last_key = pts;

        const auto it = std::lower_bound(s.obs.begin(), s.obs.end(), pts,
                                         [](const Obs& o, double p) { return o.pts < p; });
        const bool dup = (it != s.obs.end() && std::abs(it->pts - pts) < 1e-6)
                      || (it != s.obs.begin() && std::abs(std::prev(it)->pts - pts) < 1e-6);
        if (!dup) {
            s.obs.insert(it, Obs{ pts, dts });
            changed = true;
        }
    }
    if (changed) {
        rebuild(stream, s);
        ++version_;
    }
}

void KeyframeIndex::rebuild(int id, Stream& s)
{
    const double tol = tolerance(s.frame_s);

    // Predicted segment starts.
    s.bounds.clear();
    bool aligned = false;
    if (std::isfinite(s.start_s) && !s.durations.empty()) {
        aligned = s.frame_s > 0.0;
        double t = s.start_s;
        for (const double d : s.durations) {
            s.bounds.push_back(t);
            if (aligned) {
                const double n = d / s.frame_s;
                if (std::abs(n - std::round(n)) > 0.02) aligned = false;
            }
            t += d;
        }
    }
    auto near_obs = [&](double p) {
        const auto it = std::lower_bound(s.obs.begin(), s.obs.end(), p - tol,
                                         [](const Obs& o, double v) { return o.pts < v; });
        return it != s.obs.end() && it->pts <= p + tol;
    };
    auto near_bound = [&](double p) {
        const auto it = std::lower_bound(s.bounds.begin(), s.bounds.end(), p - tol);
        return it != s.bounds.end() && *it <= p + tol;
    };

    int confirmed = 0, contradicted = 0;
    std::string why;   // the first contradiction, for the log
    if (!s.bounds.empty()) {
        // The first start is the stream's start time itself: not evidence.
        for (std::size_t i = 1; i < s.bounds.size(); ++i)
            if (near_obs(s.bounds[i])) ++confirmed;
        for (const auto& [a, b] : s.gaps) {
            const auto it = std::upper_bound(s.bounds.begin(), s.bounds.end(), a + tol);
            if (it != s.bounds.end() && *it < b - tol) {
                if (why.empty())
                    why = std::format("keyframes at {:.3f} and {:.3f} were read in one go, none at the start "
                                      "predicted at {:.3f}", a, b, *it);
                ++contradicted;
            }
        }
        for (const double p : s.landings) {
            if (near_bound(p)) continue;
            if (why.empty()) {
                const auto it = std::lower_bound(s.bounds.begin(), s.bounds.end(), p);
                double nearest = it != s.bounds.end() ? *it : s.bounds.back();
                if (it != s.bounds.begin() && (it == s.bounds.end() || p - *std::prev(it) < *it - p))
                    nearest = *std::prev(it);
                why = std::format("a read from a segment start began at {:.3f}, the nearest start predicted "
                                  "is {:.3f}", p, nearest);
            }
            ++contradicted;
        }
        if (s.landings_nokey > 0 && why.empty())
            why = "a read from a segment start began on a frame that is not a keyframe";
        contradicted += s.landings_nokey;
    }
    const bool trusted = aligned && confirmed >= 1 && contradicted == 0;

    // Keyframes read, and the segment starts none of them is — both by pts.
    std::vector<Keyframe> read, starts;
    read.reserve(s.obs.size());
    for (const Obs& o : s.obs)
        read.push_back({ o.pts, hls_ ? o.dts : o.pts, hls_, false });
    if (trusted)
        for (const double b : s.bounds)
            if (!near_obs(b)) starts.push_back({ b, b, false, true });
    const std::size_t predicted = starts.size();
    s.merged.clear();
    s.merged.reserve(read.size() + starts.size());
    std::merge(read.begin(), read.end(), starts.begin(), starts.end(), std::back_inserter(s.merged),
               [](const Keyframe& a, const Keyframe& b) { return a.pts < b.pts; });

    const Status prev = s.st;
    s.st = { s.obs.size(), predicted, s.durations.size(), confirmed, contradicted, aligned, trusted };
    if (trusted && !prev.trusted)
        logger::info("Keyframes: stream {} — {} playlist segment starts confirmed ({} read where predicted, "
                     "first at {:.3f} s): listed with the {} keyframes read",
                     id, s.bounds.size(), confirmed, s.bounds.empty() ? 0.0 : s.bounds.front(), s.obs.size());
    if (contradicted > 0 && prev.contradicted == 0 && !s.bounds.empty())
        logger::warn("Keyframes: stream {} — the playlist's segment starts don't match the keyframes read "
                     "({} contradiction{}: {}): only keyframes read are listed",
                     id, contradicted, contradicted == 1 ? "" : "s", why);
}

void KeyframeIndex::fold_runs(Stream& s)
{
    // The older half (run ids grow with new_run()). A reader still going on
    // one of them continues in a fresh entry from its next keyframe: what is
    // known only shrinks.
    std::size_t n = s.runs.size() / 2;
    for (auto it = s.runs.begin(); it != s.runs.end() && n > 0; --n) {
        if (std::isfinite(it->second.first_key)) s.folded.emplace_back(it->second.first_key, it->second.end);
        it = s.runs.erase(it);
    }
    std::sort(s.folded.begin(), s.folded.end());
    std::vector<std::pair<double, double>> merged;
    for (const auto& iv : s.folded) {
        if (!merged.empty() && iv.first <= merged.back().second + 1e-6)
            merged.back().second = std::max(merged.back().second, iv.second);
        else
            merged.push_back(iv);
    }
    s.folded = std::move(merged);
}

bool KeyframeIndex::covers(const Stream& s, double a, double b, double tol)
{
    // Each run read through [its first keyframe, end]; runs that overlap
    // (or meet at a keyframe both read) read through their union.
    std::vector<std::pair<double, double>> iv(s.folded);
    iv.reserve(s.folded.size() + s.runs.size());
    for (const auto& [id, r] : s.runs)
        if (std::isfinite(r.first_key)) iv.emplace_back(r.first_key, r.end);
    std::sort(iv.begin(), iv.end());
    bool   open = false;
    double lo = 0.0, hi = 0.0;
    for (const auto& [x, y] : iv) {
        if (!open || x > hi + 1e-6) {
            open = true;
            lo   = x;
            hi   = y;
        } else {
            hi = std::max(hi, y);
        }
        if (lo <= a + tol && hi >= b - tol) return true;
    }
    return false;
}

double KeyframeIndex::probe_span(const Stream& s)
{
    double seg = 0.0;
    for (const double d : s.durations) seg = std::max(seg, d);
    return seg > 0.0 ? 1.5 * seg : kDefaultSpan;
}

std::vector<KeyframeIndex::Keyframe> KeyframeIndex::list(int stream) const
{
    std::lock_guard lk(mtx_);
    const auto it = streams_.find(stream);
    return it == streams_.end() ? std::vector<Keyframe>{} : it->second.merged;
}

std::optional<KeyframeIndex::Keyframe> KeyframeIndex::next(int stream, double t) const
{
    std::lock_guard lk(mtx_);
    const auto it = streams_.find(stream);
    if (it == streams_.end()) return std::nullopt;
    return next_in(it->second.merged, t);
}

std::optional<KeyframeIndex::Keyframe> KeyframeIndex::prev(int stream, double t) const
{
    std::lock_guard lk(mtx_);
    const auto it = streams_.find(stream);
    if (it == streams_.end()) return std::nullopt;
    return prev_in(it->second.merged, t);
}

std::optional<KeyframeIndex::Keyframe> KeyframeIndex::at_or_before(int stream, double t) const
{
    std::lock_guard lk(mtx_);
    const auto it = streams_.find(stream);
    if (it == streams_.end()) return std::nullopt;
    return at_or_before_in(it->second.merged, t);
}

KeyframeIndex::Step KeyframeIndex::step(int stream, double t, bool forward) const
{
    std::lock_guard lk(mtx_);
    static const Stream kUnknown;
    const auto it = streams_.find(stream);
    const Stream& s = it != streams_.end() ? it->second : kUnknown;
    const double tol = tolerance(s.frame_s);

    Step out;
    out.probe.forward = forward;
    out.probe.until   = t;
    out.probe.frame_s = s.frame_s;
    // A probe starts reading at a keyframe known shortly before t, else
    // where a backward seek lands: the start of the HLS segment holding the
    // target, or the container index's keyframe at or before it.
    auto start_at = [&](const std::optional<Keyframe>& k, double fallback) {
        Probe& p = out.probe;
        if (k && t - k->pts <= probe_span(s)) {
            p.seek_s        = k->seek_s;
            p.at_or_after   = k->at_or_after;
            p.segment_start = hls_ && !k->at_or_after;
        } else {
            p.seek_s        = std::max(0.0, fallback);
            p.at_or_after   = false;
            p.segment_start = hls_;
        }
    };
    if (forward) {
        out.kf      = next_in(s.merged, t);
        out.certain = out.kf && !out.kf->predicted && covers(s, t, out.kf->pts, tol);
        start_at(at_or_before_in(s.merged, t), t);
    } else {
        out.kf      = prev_in(s.merged, t);
        out.certain = out.kf ? covers(s, out.kf->pts, t, tol)
                             : std::isfinite(s.start_s) && t <= s.start_s + tol;
        // A frame and a half back: the seek must land before the frame at t.
        start_at(out.kf, t - 1.5 * (s.frame_s > 0.0 ? s.frame_s : kDefaultFrame));
    }
    return out;
}

KeyframeIndex::Status KeyframeIndex::status(int stream) const
{
    std::lock_guard lk(mtx_);
    const auto it = streams_.find(stream);
    return it == streams_.end() ? Status{} : it->second.st;
}
