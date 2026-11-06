#include "TimelineModel.h"

#include <algorithm>
#include <array>
#include <format>
#include <map>
#include <tuple>

namespace timeline {

namespace {

constexpr std::array<std::string_view, kSourceCount> kSourceNames = {
    "vmaf", "network", "playlist", "scte35", "player", "decoder",
};

// Where an event ends for the order and the index: a span at its end, a
// point at its frame for the order and one past it for the index, since a
// point at f meets [a, b) exactly when the span [f, f + 1) does.
std::int64_t order_end(const Event& e) { return e.ref_end.value_or(e.ref_index); }
std::int64_t index_end(const Event& e) { return e.ref_end ? *e.ref_end : e.ref_index + 1; }

// The canonical order without the id: negative, zero or positive.
int compare_head(const Event& a, const Event& b)
{
    if (a.ref_index != b.ref_index) return a.ref_index < b.ref_index ? -1 : 1;
    if (order_end(a) != order_end(b)) return order_end(a) < order_end(b) ? -1 : 1;
    if (a.source != b.source) return a.source < b.source ? -1 : 1;
    if (const int c = a.type.compare(b.type); c != 0) return c < 0 ? -1 : 1;
    if (a.variant != b.variant) return a.variant < b.variant ? -1 : 1;
    return 0;
}

bool head_before(const Event& a, const Event& b) { return compare_head(a, b) < 0; }

} // namespace

std::string_view source_name(Source source)
{
    const auto i = static_cast<std::size_t>(source);
    return i < kSourceNames.size() ? kSourceNames[i] : std::string_view();
}

std::optional<Source> source_from_name(std::string_view name)
{
    for (std::size_t i = 0; i < kSourceNames.size(); ++i)
        if (kSourceNames[i] == name) return static_cast<Source>(i);
    return std::nullopt;
}

bool meets(const Event& event, Interval interval)
{
    if (!event.ref_end)
        return interval.ref_index <= event.ref_index && event.ref_index < interval.ref_end;
    return event.ref_index < interval.ref_end && *event.ref_end > interval.ref_index;
}

bool canonical_before(const Event& a, const Event& b)
{
    if (const int c = compare_head(a, b); c != 0) return c < 0;
    if (a.id.size() != b.id.size()) return a.id.size() < b.id.size();
    return a.id < b.id;
}

void sort_events(std::vector<Event>& events)
{
    std::stable_sort(events.begin(), events.end(), canonical_before);
}

void assign_ids(std::vector<Event>& events)
{
    std::stable_sort(events.begin(), events.end(), head_before);
    std::map<std::tuple<Source, std::string, int>, std::int64_t> next;
    for (Event& e : events) {
        std::int64_t& n = next[{ e.source, e.type, e.variant }];
        e.id = e.variant < 0
                   ? std::format("{}/{}/-/{}", source_name(e.source), e.type, n)
                   : std::format("{}/{}/{}/{}", source_name(e.source), e.type, e.variant, n);
        ++n;
    }
}

void Timeline::build_index()
{
    sort_events(events);
    max_end.resize(events.size());
    for (std::size_t i = 0; i < events.size(); ++i) {
        const std::int64_t end = index_end(events[i]);
        max_end[i] = i == 0 ? end : std::max(max_end[i - 1], end);
    }
}

std::vector<std::size_t> Timeline::overlapping(std::int64_t a, std::int64_t b) const
{
    std::vector<std::size_t> out;
    const Interval interval{ .ref_index = a, .ref_end = b };
    if (max_end.size() != events.size()) {
        for (std::size_t i = 0; i < events.size(); ++i)
            if (meets(events[i], interval)) out.push_back(i);
        return out;
    }
    // An event that meets [a, b), an empty or reversed one included, starts
    // before b and ends after a, a point's end being its frame plus one: the
    // events that start before b, from the last back while some event up to
    // it ends after a, meets() deciding each.
    const auto starts_before = std::partition_point(
        events.begin(), events.end(), [b](const Event& e) { return e.ref_index < b; });
    for (auto k = static_cast<std::size_t>(starts_before - events.begin()); k-- > 0;) {
        if (max_end[k] <= a) break;
        if (meets(events[k], interval)) out.push_back(k);
    }
    std::reverse(out.begin(), out.end());
    return out;
}

Curve curve_points(const Timeline& timeline, CurveRef curve)
{
    if (curve.index < 0) return {};
    const auto index = static_cast<std::size_t>(curve.index);
    if (curve.kind == CurveRef::Kind::Pair)
        return index < timeline.curves.size() ? timeline.curves[index] : Curve{};
    if (curve.kind != CurveRef::Kind::Session || index >= timeline.sessions.size()) return {};

    // A session's curve (design §4.4; D-068): at each reference frame of a
    // span, the score of the variant the span plays there, which is the
    // point of the pair whose distorted variant it is. So over each span
    // that is scored, in play order, that pair's points in the span's
    // frames [ref_index, ref_end), the pair's own grid kept: a pair on a
    // coarser grid than the reference leaves its gaps, nothing being
    // invented there (D-077). None over a span that isn't scored, the
    // reference's in top-rung mode (D-105 (b)), nor over one whose variant
    // no pair's curve scores. A libvmaf context of the pair that starts in a
    // span's frames starts there on the session's curve too; a switch starts
    // none, since the scores after it come from the pair's own contexts. The
    // curve names no variant (-1): it is several variants' in turn.
    Curve out;
    for (const Span& span : timeline.sessions[index].spans) {
        if (!span.scored) continue;
        const auto pair = std::find_if(timeline.curves.begin(), timeline.curves.end(),
                                       [&span](const Curve& c) { return c.variant == span.variant; });
        if (pair == timeline.curves.end()) continue;
        const std::size_t points = std::min(pair->ref_index.size(), pair->vmaf.size());
        const auto first = pair->ref_index.begin();
        const auto last  = first + static_cast<std::ptrdiff_t>(points);
        const auto lo    = std::lower_bound(first, last, span.ref_index);
        const auto hi    = std::lower_bound(lo, last, span.ref_end);
        out.ref_index.insert(out.ref_index.end(), lo, hi);
        out.vmaf.insert(out.vmaf.end(), pair->vmaf.begin() + (lo - first), pair->vmaf.begin() + (hi - first));
        for (const std::int64_t f : pair->context_first)
            if (f >= span.ref_index && f < span.ref_end) out.context_first.push_back(f);
    }
    return out;
}

} // namespace timeline
