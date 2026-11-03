#include "TimelineQuery.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace timeline {

namespace {

bool positive(AVRational q) { return av_cmp_q(q, AVRational{ 0, 1 }) > 0; }

// Whether `e` is one of the pair's inputs' events (Q28 (a)): its distorted
// variant's, `variant`, or the reference's, the variant of no pair. It rests
// on Event::variant's rule (src/TimelineModel.h): a pair's own events carry
// its distorted variant's index, the reference's the reference variant's;
// an event of variant -1, and any event of a session whatever its variant,
// belong to no pair.
bool in_pair_scope(const Timeline& t, int variant, const Event& e)
{
    if (e.session >= 0 || e.variant < 0) return false;
    if (e.variant == variant) return true;
    const auto v = static_cast<std::size_t>(e.variant);
    return v < t.variants.size() && t.variants[v].pair == -1;
}

} // namespace

Interval widen(const Axis& axis, Interval interval, const QueryOptions& options)
{
    Interval out = interval;
    if (positive(options.margin_before)) {
        const AVRational before{ -options.margin_before.num, options.margin_before.den };
        out.ref_index = std::min(interval.ref_index, axis.frame_at_or_after(interval.ref_index, before));
    }
    if (positive(options.margin_after))
        out.ref_end = std::max(interval.ref_end,
                               axis.frame_at_or_after(interval.ref_end, options.margin_after));
    return out;
}

Interval seconds_interval(const Axis& axis, AVRational from, AVRational to)
{
    return { .ref_index = axis.frame_at_or_after(from), .ref_end = axis.frame_at_or_after(to) };
}

Attribution attribute(const Timeline& timeline, CurveRef curve, Interval interval,
                      const QueryOptions& options)
{
    Attribution a;
    a.curve    = curve;
    a.interval = interval;
    a.searched = interval;
    if (curve.kind != CurveRef::Kind::Pair || curve.index < 0 ||
        static_cast<std::size_t>(curve.index) >= timeline.curves.size())
        return a;
    const Curve& c = timeline.curves[static_cast<std::size_t>(curve.index)];
    a.searched = widen(timeline.axis, interval, options);

    // The curve's points in the interval, a point without a score skipped
    // (D-077, Q24 (a)).
    double lowest = std::numeric_limits<double>::infinity(), sum = 0.0;
    const std::size_t points = std::min(c.ref_index.size(), c.vmaf.size());
    for (std::size_t k = 0; k < points; ++k) {
        if (c.ref_index[k] < interval.ref_index || c.ref_index[k] >= interval.ref_end) continue;
        if (!std::isfinite(c.vmaf[k])) continue;
        ++a.points;
        lowest = std::min(lowest, c.vmaf[k]);
        sum += c.vmaf[k];
    }
    if (a.points > 0) {
        a.min  = lowest;
        a.mean = sum / static_cast<double>(a.points);
    }

    for (std::size_t i : timeline.overlapping(a.searched.ref_index, a.searched.ref_end)) {
        const Event& e = timeline.events[i];
        const auto source = static_cast<std::size_t>(e.source);
        if (source >= kSourceCount || !in_pair_scope(timeline, c.variant, e)) continue;
        Found f;
        f.event         = i;
        f.offset_frames = e.ref_index - interval.ref_index;
        f.offset_s      = timeline.axis.seconds_between(interval.ref_index, e.ref_index);
        if (e.ref_end) {
            f.offset_end_frames = *e.ref_end - interval.ref_index;
            f.offset_end_s      = timeline.axis.seconds_between(interval.ref_index, *e.ref_end);
        }
        a.by_source[source].push_back(f);
    }
    // By offset within a group (Q30 (a)), which the canonical order already
    // is; sorted again for a timeline whose events aren't in it.
    for (std::vector<Found>& group : a.by_source)
        std::stable_sort(group.begin(), group.end(), [](const Found& x, const Found& y) {
            return x.offset_frames < y.offset_frames;
        });
    return a;
}

} // namespace timeline
