#include "TimelineBuild.h"

#include "TimelineAxis.h"
#include "TimelineDoc.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <limits>
#include <map>
#include <utility>

namespace timeline {

namespace {

Value int_value(std::int64_t v) { return Value{ v }; }
Value text_value(std::string s) { return Value{ std::move(s) }; }

// A PTS in seconds as the analyzer computes it (DecodeCtx::seconds in
// src/VMAFAnalyzer.cpp), so that an unpaired frame's is the report's own;
// null without a PTS or a positive time base.
Value pts_value(std::optional<std::int64_t> ticks, AVRational tb)
{
    if (!ticks || tb.num <= 0 || tb.den <= 0) return Value{};
    return Value{ static_cast<double>(*ticks) * tb.num / tb.den };
}

std::string plural(std::size_t n, std::string_view one, std::string_view many)
{
    return std::format("{} {}", n, n == 1 ? one : many);
}

// The report's complete pairs by their numbers (D-092 (b)), for a message:
// "the ladder report's complete pairs are 0 and 2".
std::string pairs_text(const LadderScores& s)
{
    if (s.pairs.empty()) return "the ladder report has no complete pair";
    std::string list;
    for (std::size_t k = 0; k < s.pairs.size(); ++k)
        list += std::format("{}{}", k == 0 ? "" : k + 1 == s.pairs.size() ? " and " : ", ",
                            s.pairs[k].index);
    return std::format("the ladder report's complete {} {}",
                       s.pairs.size() == 1 ? "pair is" : "pairs are", list);
}

// The line of a playlist event or cue, which its payload starts with
// (src/PlaylistEvents.h); nullopt without one.
std::optional<std::int64_t> line_of(const Event& e)
{
    for (const auto& [n, value] : e.payload)
        if (const auto* i = std::get_if<std::int64_t>(&value.v); i && n == "line") return *i;
    return std::nullopt;
}

// A playlist event or cue as a warning names it: "the cue_out at line 14".
std::string tag_text(const Event& e)
{
    const std::optional<std::int64_t> line = line_of(e);
    return line ? std::format("the {} at line {}", e.type, *line) : std::format("the {}", e.type);
}

// Where the frames of one input sit on the reference's frames (design
// §5.1): the reference's at themselves, a distorted input's through its
// pair's scored frames, whose positions both rise from frame to frame
// (src/LadderReportRead.h).
class Placement {
public:
    // The reference, of `frames` frames on the axis.
    static Placement reference(std::int64_t frames)
    {
        Placement p;
        p.frames_ = frames;
        return p;
    }
    // The distorted input of `pair`.
    static Placement distorted(const PairScores& pair, std::int64_t frames)
    {
        Placement p;
        p.frames_ = frames;
        p.dis_    = &pair.dis_index;
        p.ref_    = &pair.ref_index;
        return p;
    }

    // The reference frame where the input's frame `index` sits: its own, or
    // for a distorted frame the one it is paired with, or the next paired
    // frame's when it isn't paired (D-081 (d)). nullopt for a frame after the
    // input's last paired frame, or off the axis.
    std::optional<std::int64_t> at(std::int64_t index) const
    {
        if (!dis_) return on_axis(index);
        const std::size_t k = paired_from(index);
        return k < size() ? on_axis((*ref_)[k]) : std::nullopt;
    }

    // Where the input's frames [first, first + frames) sit, a segment's: from
    // its first paired frame's reference frame to the next paired frame's
    // after it, or to one past its own last paired frame's where none
    // follows. nullopt when none of them is paired.
    std::optional<Interval> span(std::int64_t first, std::int64_t frames) const
    {
        if (frames <= 0) return std::nullopt;
        const std::int64_t end = first + frames;
        if (!dis_) {
            if (first < 0 || first >= frames_) return std::nullopt;
            return Interval{ .ref_index = first, .ref_end = std::min(end, frames_) };
        }
        const std::size_t k = paired_from(first);
        if (k >= size() || (*dis_)[k] >= end) return std::nullopt;
        const std::size_t  next = paired_from(end);
        const std::int64_t to   = next < size() ? (*ref_)[next] : (*ref_)[next - 1] + 1;
        const auto start = on_axis((*ref_)[k]);
        if (!start || to <= *start || to > frames_) return std::nullopt;
        return Interval{ .ref_index = *start, .ref_end = to };
    }

private:
    std::int64_t                     frames_ = 0;
    const std::vector<std::int64_t>* dis_    = nullptr;   // null: the reference
    const std::vector<std::int64_t>* ref_    = nullptr;

    std::size_t size() const { return std::min(dis_->size(), ref_->size()); }
    // The first scored frame whose distorted position is `index` or later.
    std::size_t paired_from(std::int64_t index) const
    {
        const auto end = dis_->begin() + static_cast<std::ptrdiff_t>(size());
        return static_cast<std::size_t>(std::lower_bound(dis_->begin(), end, index) - dis_->begin());
    }
    std::optional<std::int64_t> on_axis(std::int64_t ref) const
    {
        if (ref < 0 || ref >= frames_) return std::nullopt;
        return ref;
    }
};

// The master's video playlists, each URL once at the highest BANDWIDTH of
// its video entries (D-081 (e)): the rungs a variant's ranks count.
std::vector<std::int64_t> rung_bandwidths(const LadderScores& s)
{
    std::map<std::string, std::int64_t> by_url;
    for (const MasterEntry& e : s.entries) {
        if (!e.video) continue;
        const auto [it, added] = by_url.emplace(e.url, e.bandwidth);
        if (!added) it->second = std::max(it->second, e.bandwidth);
    }
    std::vector<std::int64_t> out;
    out.reserve(by_url.size());
    for (const auto& [url, bandwidth] : by_url) out.push_back(bandwidth);
    return out;
}

Variant make_variant(int index, int pair, const std::vector<std::size_t>& entries, std::string url,
                     const LadderScores& s, const std::vector<std::int64_t>& rungs,
                     const VariantRead& read)
{
    Variant v;
    v.index             = index;
    v.pair              = pair;
    v.entries           = entries;
    v.url               = std::move(url);
    v.playlist_location = read.playlist_location;
    v.playlist_sha256   = read.playlist_sha256;
    // The entry with the highest BANDWIDTH, the first of equal ones in the
    // master's order, gives the BANDWIDTH (D-081 (e)), RESOLUTION and CODECS.
    const MasterEntry* best = nullptr;
    for (std::size_t e : entries) {
        if (e >= s.entries.size()) continue;
        const MasterEntry& m = s.entries[e];
        if (!best || m.bandwidth > best->bandwidth) best = &m;
    }
    if (best) {
        const std::int64_t b = best->bandwidth;
        v.bandwidth  = b;
        v.resolution = best->resolution;
        v.codecs     = best->codecs;
        const auto count = [&rungs](auto pred) {
            return static_cast<int>(std::count_if(rungs.begin(), rungs.end(), pred));
        };
        v.rank_from_top    = 1 + count([b](std::int64_t r) { return r > b; });
        v.rank_from_bottom = 1 + count([b](std::int64_t r) { return r < b; });
    }
    v.segment_map = read.media.refused.empty() ? std::string("ok") : "refused: " + read.media.refused;
    return v;
}

// The frames a placement couldn't place, of one kind and input: one warning
// for all of them.
struct Lost {
    std::size_t  count = 0;
    std::int64_t first = 0;   // the first one's position in its input

    void add(std::int64_t index)
    {
        if (count++ == 0) first = index;
    }
};

class Builder {
public:
    Builder(const BuildInputs& in, Timeline& t) : in_(in), s_(in.scores), t_(t) {}

    void run()
    {
        for (std::size_t v = 0; v < in_.variants.size(); ++v) variant_events(static_cast<int>(v));
        for (std::size_t k = 0; k < s_.pairs.size(); ++k) vmaf_events(k);
        const Breaks breaks = pair_breaks(cues_, [this](std::int64_t f, AVRational d) {
            return t_.axis.frame_after(f, d);
        });
        for (Event& c : cues_)
            if (c.ref_index != kUnplaced) events_.push_back(std::move(c));
        events_.insert(events_.end(), breaks.events.begin(), breaks.events.end());
        warnings_.insert(warnings_.end(), breaks.warnings.begin(), breaks.warnings.end());

        assign_ids(events_);
        t_.events   = std::move(events_);
        t_.warnings = std::move(warnings_);
        t_.build_index();
    }

private:
    const BuildInputs&       in_;
    const LadderScores&      s_;
    Timeline&                t_;
    std::vector<Event>       events_;
    std::vector<Event>       cues_;       // every cue to pair, placed or not
    std::vector<std::string> warnings_;

    Event placed(Event e, std::int64_t at, std::optional<std::int64_t> end = std::nullopt) const
    {
        e.ref_index = at;
        e.ref_end   = end;
        e.key       = t_.axis.keys[static_cast<std::size_t>(at)];
        return e;
    }

    Placement placement(int v) const
    {
        if (v == 0) return Placement::reference(t_.axis.frames);
        return Placement::distorted(s_.pairs[static_cast<std::size_t>(v - 1)], t_.axis.frames);
    }

    std::string name(int v) const
    {
        return std::format("variant {} ({})", v, t_.variants[static_cast<std::size_t>(v)].url);
    }

    void variant_events(int v)
    {
        const VariantRead&    read = in_.variants[static_cast<std::size_t>(v)];
        const VariantMedia&   m    = read.media;
        const PlaylistEvents* p    = read.playlist ? &*read.playlist : nullptr;
        if (p) {
            for (const std::string& problem : p->timeline.problems)
                warnings_.push_back(std::format("{}: its playlist: {}", name(v), problem));
            if (!p->messages.empty()) message_warning(v, *p);
        }
        if (!m.refused.empty()) {
            warnings_.push_back(std::format(
                "{}: no segment map, {}: its segment-bound events, keyframes and corrupt packets "
                "are left out (D-081 (c), D-086)", name(v), m.refused));
            return;
        }
        const Placement at = placement(v);

        // Each placed segment's span, by its media sequence number.
        std::map<std::int64_t, Interval> spans;
        if (p) segment_events(v, *p, m, at, spans);

        Lost lost_keys, lost_corrupt;
        for (std::int64_t frame : m.keyframes) {
            const std::optional<std::int64_t> f = at.at(frame);
            if (!f) {
                lost_keys.add(frame);
                continue;
            }
            Event e;
            e.source  = Source::Decoder;
            e.type    = kTypeKeyframe;
            e.variant = v;
            e.segment = segment_of(m, frame);
            e.payload.emplace_back("index", int_value(frame));
            events_.push_back(placed(std::move(e), *f));
        }
        for (std::int64_t frame : m.corrupt) {
            const std::optional<std::int64_t> f = at.at(frame);
            if (!f) {
                lost_corrupt.add(frame);
                continue;
            }
            Event e;
            e.source  = Source::Decoder;
            e.type    = kTypeCorruptPacket;
            e.variant = v;
            e.segment = segment_of(m, frame);
            e.payload.emplace_back("index", int_value(frame));
            e.payload.emplace_back("next_boundary", boundary(m, p, frame, spans));
            events_.push_back(placed(std::move(e), *f));
        }
        lost_warning(v, lost_keys, "keyframe", "keyframes");
        lost_warning(v, lost_corrupt, "corrupt packet", "corrupt packets");
    }

    void segment_events(int v, const PlaylistEvents& p, const VariantMedia& m, const Placement& at,
                        std::map<std::int64_t, Interval>& spans)
    {
        std::map<std::int64_t, const SegmentFrames*> frames;
        for (const SegmentFrames& f : m.segments) frames.emplace(f.seq, &f);

        // The playlist's events and cues each segment holds, in line order,
        // for the warning of a segment that isn't placed, which names each
        // of them, as after_last_uri() names a tag no segment holds (design
        // §5.1).
        std::map<std::int64_t, std::vector<const Event*>> held;
        for (const Event& e : p.events)
            if (e.segment) held[*e.segment].push_back(&e);
        for (const Event& e : p.cues)
            if (e.segment) held[*e.segment].push_back(&e);
        for (auto& [seq, tags] : held)
            std::stable_sort(tags.begin(), tags.end(),
                             [](const Event* a, const Event* b) { return line_of(*a) < line_of(*b); });

        for (std::size_t i = 0; i < p.timeline.segments.size(); ++i) {
            const hls::Segment& seg = p.timeline.segments[i];
            const auto f = frames.find(seg.seq);
            std::optional<Interval> span;
            std::string why;
            if (f == frames.end() || f->second->frames <= 0) {
                why = "the demux pass gave it no frame";
            } else {
                span = at.span(f->second->first_index, f->second->frames);
                if (!span) why = "none of its frames is paired";
            }
            if (!span) {
                const auto h = held.find(seg.seq);
                warnings_.push_back(std::format(
                    "{}: segment {} ({}) isn't placed, {}: it is left out, {} (design §5.1)", name(v),
                    seg.seq, i < p.uris.size() ? p.uris[i] : seg.uri, why,
                    held_text(h == held.end() ? std::vector<const Event*>{} : h->second)));
                continue;
            }
            spans.emplace(seg.seq, *span);
            events_.push_back(placed(segment_event(p, i, f->second->bytes), span->ref_index,
                                     span->ref_end));
        }

        // A tag at the start of the segment whose URI follows it (design §5.1).
        const auto start_of = [&spans](const Event& e) -> std::optional<std::int64_t> {
            if (!e.segment) return std::nullopt;
            const auto s = spans.find(*e.segment);
            if (s == spans.end()) return std::nullopt;
            return s->second.ref_index;
        };
        for (const Event& e : p.events) {
            if (const auto f = start_of(e)) events_.push_back(placed(e, *f));
            else if (!e.segment) after_last_uri(v, e);
        }
        for (const Event& c : p.cues) {
            if (const auto f = start_of(c)) {
                cues_.push_back(placed(c, *f));
            } else {
                if (!c.segment) after_last_uri(v, c);
                cues_.push_back(c);   // unplaced, it still takes its turn in the pairing
            }
        }
    }

    // The tags a segment that isn't placed holds, as its warning lists them:
    // "and so are the 2 tags it holds, the discontinuity at line 13 and the
    // cue_out at line 14". Its cues are named again by pair_breaks(), where
    // they make no ad break.
    static std::string held_text(const std::vector<const Event*>& tags)
    {
        if (tags.empty()) return "and it holds no tag";
        std::string out = tags.size() == 1
                              ? std::string("and so is the tag it holds, ")
                              : std::format("and so are the {} tags it holds, ", tags.size());
        for (std::size_t i = 0; i < tags.size(); ++i)
            out += std::format("{}{}", i == 0 ? "" : i + 1 == tags.size() ? " and " : ", ",
                               tag_text(*tags[i]));
        return out;
    }

    void after_last_uri(int v, const Event& e)
    {
        warnings_.push_back(std::format("{}: {} follows the last segment's URI, so no segment holds "
                                        "it: it is left out (design §5.1)",
                                        name(v), tag_text(e)));
    }

    void message_warning(int v, const PlaylistEvents& p)
    {
        const CueInput& first = p.messages.front();
        std::string where;
        if (const auto* a = std::get_if<PlaylistAnchor>(&first.anchor))
            where = std::format(", the first at line {} ({})", a->line, first.form);
        warnings_.push_back(std::format(
            "{}: its playlist carries {}{}, which this build doesn't decode, so they make no event "
            "(design §5.5)", name(v), plural(p.messages.size(), "SCTE-35 message", "SCTE-35 messages"),
            where));
    }

    void lost_warning(int v, const Lost& lost, std::string_view one, std::string_view many)
    {
        if (lost.count == 0) return;
        warnings_.push_back(std::format(
            "{}: {} after its input's last paired frame, the first at frame {} of its input, {} "
            "left out (design §5.1)", name(v), plural(lost.count, one, many), lost.first,
            lost.count == 1 ? "is" : "are"));
    }

    // The media sequence number of the segment holding the input's frame
    // `index`; nullopt for an input without segments or a frame in none.
    static std::optional<std::int64_t> segment_of(const VariantMedia& m, std::int64_t index)
    {
        for (const SegmentFrames& s : m.segments)
            if (index >= s.first_index && index < s.first_index + s.frames) return s.seq;
        return std::nullopt;
    }

    // A corrupt packet's next segment boundary (D-081 (h)), by
    // next_boundary()'s rule (src/TimelineMedia.h), which is the adapter's
    // and so not called from here: the player compiles the core without the
    // adapter from round 3 on (design §3.3). The segment after the one that
    // holds the frame, none in the last; with where its segment event
    // starts. The rule is written twice until the lead moves it into the
    // core (the design's P-AG); tests/test_timeline_build.cpp checks that
    // the two give the same boundary at every frame of its inputs.
    static Value boundary(const VariantMedia& m, const PlaylistEvents* p, std::int64_t frame,
                          const std::map<std::int64_t, Interval>& spans)
    {
        if (!p) return Value{};
        const std::vector<SegmentFrames>& segs = m.segments;
        for (std::size_t k = 0; k < segs.size(); ++k) {
            if (frame < segs[k].first_index || frame >= segs[k].first_index + segs[k].frames) continue;
            if (k + 1 == segs.size()) return Value{};
            const SegmentFrames& next   = segs[k + 1];
            const int            listed = p->timeline.index_of(next.seq);
            const bool disc =
                listed >= 0 && p->timeline.segments[static_cast<std::size_t>(listed)].discontinuity;
            const auto span = spans.find(next.seq);
            return Value{ Value::Object{
                { "media_sequence", int_value(next.seq) },
                { "index", int_value(next.first_index) },
                { "ref_index", span == spans.end() ? Value{} : int_value(span->second.ref_index) },
                { "discontinuity", Value{ disc } },
            } };
        }
        return Value{};
    }

    // A pair's warnings name it by its number, the report's pairs[].index,
    // the plan's, as the axis's do (src/TimelineAxis.cpp) and as
    // Variant::pair holds it (D-092 (b)).
    void vmaf_events(std::size_t k)
    {
        const PairScores& p = s_.pairs[k];
        const int         v = static_cast<int>(k) + 1;
        // Each libvmaf context's first pair but the first context's (D-037;
        // design §5.3). One that starts at no scored frame of the pair, or
        // at a reference frame off the axis, which no report that
        // read_ladder_report() accepts has, can't be placed: a warning.
        for (std::size_t c = 1; c < p.context_first.size(); ++c) {
            const std::int64_t first = p.context_first[c];
            if (first < 0 || static_cast<std::size_t>(first) >= p.ref_index.size()) {
                warnings_.push_back(std::format(
                    "pair {}: its libvmaf context {} starts at frame pair {}, which isn't one of its "
                    "{}: its context_start is left out (design §5.1)", p.index, c, first,
                    plural(p.ref_index.size(), "scored frame", "scored frames")));
                continue;
            }
            const std::int64_t at = p.ref_index[static_cast<std::size_t>(first)];
            if (at < 0 || at >= t_.axis.frames) {
                warnings_.push_back(std::format(
                    "pair {}: its libvmaf context {} starts at frame pair {}, at reference frame {}, "
                    "which isn't one of the axis's {}: its context_start is left out (design §5.1)",
                    p.index, c, first, at,
                    plural(static_cast<std::size_t>(t_.axis.frames), "frame", "frames")));
                continue;
            }
            Event e;
            e.source  = Source::Vmaf;
            e.type    = kTypeContextStart;
            e.variant = v;
            e.payload.emplace_back("context", int_value(static_cast<std::int64_t>(c)));
            e.payload.emplace_back("first_index", int_value(first));
            events_.push_back(placed(std::move(e), at));
        }
        // The aligner's unpaired frames (D-081 (d)).
        const Placement reference = Placement::reference(t_.axis.frames);
        const Placement distorted = Placement::distorted(p, t_.axis.frames);
        Lost lost_ref, lost_dis;
        for (const Failure& f : p.failures) {
            const bool ref = f.input == "reference";
            const std::optional<std::int64_t> at = ref ? reference.at(f.index) : distorted.at(f.index);
            if (!at) {
                (ref ? lost_ref : lost_dis).add(f.index);
                continue;
            }
            Event e;
            e.source  = Source::Vmaf;
            e.type    = kTypeUnpairedFrame;
            e.variant = v;
            e.payload.emplace_back("input", text_value(f.input));
            e.payload.emplace_back("index", int_value(f.index));
            e.payload.emplace_back("pts", pts_value(f.pts, ref ? p.ref_time_base : p.dis_time_base));
            e.payload.emplace_back("reason", text_value(f.reason));
            events_.push_back(placed(std::move(e), *at));
        }
        const auto warn = [&](const Lost& lost, std::string_view input, std::string_view where) {
            if (lost.count == 0) return;
            warnings_.push_back(std::format(
                "pair {}: {} of its {} input {}, the first at frame {}, {} left out (D-081 (d))",
                p.index, plural(lost.count, "unpaired frame", "unpaired frames"), input, where,
                lost.first, lost.count == 1 ? "is" : "are"));
        };
        warn(lost_ref, "reference", "outside the axis's frames");
        warn(lost_dis, "distorted", "after its last paired frame");
    }
};

} // namespace

Timeline build(const BuildInputs& in, std::string& err)
{
    err.clear();
    const LadderScores& s = in.scores;
    if (in.variants.size() != s.pairs.size() + 1) {
        err = std::format("the timeline build was given {} for a report of {}: it takes the "
                          "reference and one per complete pair",
                          plural(in.variants.size(), "variant", "variants"),
                          plural(s.pairs.size(), "complete pair", "complete pairs"));
        return {};
    }
    for (std::size_t v = 0; v < in.variants.size(); ++v) {
        const std::optional<PlaylistEvents>& p = in.variants[v].playlist;
        if (!p) continue;
        if (!p->error.empty()) {
            err = std::format("variant {}'s playlist wasn't read: {}", v, p->error);
            return {};
        }
        if (p->variant != static_cast<int>(v)) {
            err = std::format("variant {}'s playlist was read for variant {}", v, p->variant);
            return {};
        }
    }
    // Each pair's number, its pairs[].index, rising in the plan's order, as
    // read_ladder_report() checks it, so that curve k, the k-th complete
    // pair's, is also the k-th in the order of the pairs' numbers, where a
    // document read back places a curve (src/TimelineDoc.h, curve_of_pair()).
    for (std::size_t k = 0; k < s.pairs.size(); ++k) {
        const std::size_t index = s.pairs[k].index;
        if (index > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            err = std::format("the ladder report's pairs[{}].index is {}, beyond a pair's number "
                              "(D-092 (b))", k, index);
            return {};
        }
        if (k > 0 && index <= s.pairs[k - 1].index) {
            err = std::format("the ladder report's pairs[{}].index is {}, not after the pair before it "
                              "({}): pairs come in the plan's order, which the timeline numbers them "
                              "by (D-092 (b))", k, index, s.pairs[k - 1].index);
            return {};
        }
    }

    Timeline t;
    const std::vector<std::int64_t> rungs = rung_bandwidths(s);
    t.variants.push_back(
        make_variant(0, -1, s.reference_entries, s.reference_url, s, rungs, in.variants[0]));
    // Variant::pair is the pair's number, its pairs[].index, the plan's,
    // which skips the number of a pair that failed (D-092 (b)): the document
    // names a curve by it, and so do --curve and the build's warnings
    // (vmaf_events()). Curve k, on variant k + 1, keeps its place among the
    // complete pairs (pair_curves()).
    for (std::size_t k = 0; k < s.pairs.size(); ++k)
        t.variants.push_back(make_variant(static_cast<int>(k) + 1, static_cast<int>(s.pairs[k].index),
                                          s.pairs[k].entries, s.pairs[k].dis_url, s, rungs,
                                          in.variants[k + 1]));

    const std::optional<PlaylistEvents>& reference = in.variants[0].playlist;
    t.axis   = build_axis(s, &in.variants[0].media, reference ? &reference->timeline : nullptr);
    t.curves = pair_curves(s);
    Builder(in, t).run();
    return t;
}

std::vector<Curve> pair_curves(const LadderScores& scores)
{
    std::vector<Curve> out;
    out.reserve(scores.pairs.size());
    for (std::size_t k = 0; k < scores.pairs.size(); ++k) {
        const PairScores& p = scores.pairs[k];
        Curve c;
        c.variant   = static_cast<int>(k) + 1;
        c.ref_index = p.ref_index;
        c.vmaf      = p.vmaf;
        // A context that starts at no scored frame, which read_ladder_report()
        // refuses and build() warns of, has no reference frame to give.
        for (std::int64_t first : p.context_first)
            if (first >= 0 && static_cast<std::size_t>(first) < p.ref_index.size())
                c.context_first.push_back(p.ref_index[static_cast<std::size_t>(first)]);
        out.push_back(std::move(c));
    }
    return out;
}

bool attach_curves(Timeline& timeline, const LadderScores& scores, std::string& err)
{
    err.clear();
    // Each complete pair by its number, its pairs[].index (D-092 (b)), in
    // the plan's order, as read_ladder_report() checks it: then curve k is
    // the k-th in the order of the pairs' numbers too, the place from_json()
    // gives a curve reference of its pair (src/TimelineDoc.h).
    std::map<std::size_t, std::size_t> place;   // a pair's number -> its place among the complete pairs
    for (std::size_t k = 0; k < scores.pairs.size(); ++k) {
        const std::size_t index = scores.pairs[k].index;
        if (k > 0 && index <= scores.pairs[k - 1].index) {
            err = std::format("the ladder report's pairs[{}].index is {}, not after the pair before it "
                              "({}): pairs come in the plan's order, which a document's curves follow "
                              "(D-092 (b))", k, index, scores.pairs[k - 1].index);
            return false;
        }
        place.emplace(index, k);
    }
    std::vector<int> variant_of(scores.pairs.size(), -1);
    for (std::size_t i = 0; i < timeline.variants.size(); ++i) {
        const int pair = timeline.variants[i].pair;
        if (pair < 0) continue;
        const auto it = place.find(static_cast<std::size_t>(pair));
        if (it == place.end()) {
            err = std::format("the document's variant {} names pair {}, but {}", i, pair,
                              pairs_text(scores));
            return false;
        }
        int& v = variant_of[it->second];
        if (v >= 0) {
            err = std::format("the document's variants {} and {} both name pair {}", v, i, pair);
            return false;
        }
        v = static_cast<int>(i);
    }
    for (std::size_t k = 0; k < variant_of.size(); ++k)
        if (variant_of[k] < 0) {
            err = std::format("the ladder report's complete pair {} is the pair of none of the "
                              "document's variants", scores.pairs[k].index);
            return false;
        }
    std::vector<Curve> curves = pair_curves(scores);
    for (std::size_t k = 0; k < curves.size(); ++k) curves[k].variant = variant_of[k];
    timeline.curves = std::move(curves);
    return true;
}

std::vector<CurveRef> pair_curve_refs(const Timeline& timeline)
{
    std::vector<CurveRef> out;
    out.reserve(timeline.curves.size());
    for (std::size_t k = 0; k < timeline.curves.size(); ++k)
        out.push_back({ .kind = CurveRef::Kind::Pair, .index = static_cast<int>(k) });
    return out;
}

std::vector<Attribution> attribute_drops(const Timeline& timeline, const std::vector<CurveRef>& curves,
                                         const DetectOptions& detect, const QueryOptions& query,
                                         std::string& err)
{
    err.clear();
    if (std::string why = check_options(detect); !why.empty()) {
        err = std::move(why);
        return {};
    }
    std::vector<Attribution> out;
    if (!detect.given()) return out;
    for (const CurveRef c : curves) {
        // A curve is its place in Timeline::curves; a message names it by its
        // pair's number, the report's pairs[].index (D-092 (b)).
        if (c.kind != CurveRef::Kind::Pair || c.index < 0 ||
            static_cast<std::size_t>(c.index) >= timeline.curves.size()) {
            err = std::format("the timeline has no curve at place {}: it has {}", c.index,
                              plural(timeline.curves.size(), "pair's curve", "pairs' curves"));
            return {};
        }
        std::string why;
        const std::vector<Drop> drops = timeline::detect(curve_points(timeline, c), detect, why);
        if (!why.empty()) {
            const std::optional<int> pair = pair_number(timeline, c);
            err = pair ? std::format("curve pair:{}: {}", *pair, why)
                       : std::format("the curve at place {}, whose variant is no pair's: {}", c.index, why);
            return {};
        }
        for (const Drop& d : drops) {
            Attribution a    = attribute(timeline, c, d.interval, query);
            a.detected       = true;
            a.baseline       = d.baseline;
            a.drop_magnitude = d.drop_magnitude;
            a.mean_drop      = d.mean_drop;
            out.push_back(std::move(a));
        }
    }
    return out;
}

} // namespace timeline
