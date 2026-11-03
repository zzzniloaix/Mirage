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
    if (curve.kind == CurveRef::Kind::Pair && curve.index >= 0 &&
        static_cast<std::size_t>(curve.index) < timeline.curves.size())
        return timeline.curves[static_cast<std::size_t>(curve.index)];
    return {};
}

} // namespace timeline
