#pragma once

// The timeline's events, variants and curves on the reference's frames: filled in by WP01.
//
// The model of eval/phase3-survey/design.md §4: every event sits at a
// reference frame or spans reference frames, and intervals are compared on
// those integers, half-open (D-078). Beside the types the round-1 packages
// share, this module gives the events their canonical order and their
// deterministic ids (the design's P-I), so that identical inputs give
// identical documents, and the overlap index (the design's P-J): the events
// sorted by their first frame with a running maximum of their ends, built
// once per timeline. No FFmpeg here but AVRational, through the axis.

#include "TimelineAxis.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace timeline {

// Where an event comes from: the sources of the work order's section 3.1,
// in its order, which is the order of an attribution's groups (DECISIONS.md
// D-078).
enum class Source : std::uint8_t { Vmaf, Network, Playlist, Scte35, Player, Decoder };
inline constexpr std::size_t kSourceCount = 6;

// A value in an event's payload: null, a boolean, an integer, a number, a
// string, a list, or an object whose members keep their order. The timeline
// document writes it as JSON.
struct Value {
    using List   = std::vector<Value>;
    using Object = std::vector<std::pair<std::string, Value>>;
    std::variant<std::monostate, bool, std::int64_t, double, std::string, List, Object> v;
};

// What a source says about an event: names and values, in order.
using Payload = std::vector<std::pair<std::string, Value>>;

// One thing that happened, or holds, on the reference's timeline
// (eval/phase3-survey/design.md §4.3): a point at a reference frame or a
// span of reference frames, placed by the design's §5.1 rules, with the
// media key of the frame it starts at (D-073).
struct Event {
    std::string  id;                          // deterministic within its document (design §4.7)
    Source       source = Source::Vmaf;
    std::string  type;                        // design §5.9's vocabulary: "segment", "cue_out", ...
    // Index into Timeline::variants; -1: none. It sets which curves'
    // attributions list the event (src/TimelineQuery.h): a pair's own
    // events, its vmaf ones (context_start, unpaired_frame) among them, carry
    // the index of the pair's distorted variant; the reference's events
    // carry the reference variant's, the one whose pair is -1; an event of
    // variant -1 is in no pair's curve's scope (a session's own events come
    // later, with WP07).
    int          variant = -1;
    int          session = -1;                // index of its session (D-068); -1: none
    std::optional<std::int64_t> segment;      // segment-bound events: the media sequence number
    std::int64_t ref_index = 0;               // the reference frame it sits or starts at
    std::optional<std::int64_t> ref_end;      // a span's end, exclusive; nullopt: a point
    MediaKey     key;                         // of the frame at ref_index
    std::optional<std::int64_t> wallclock_us; // observed events only: microseconds on a steady
                                              // clock from its session's start (D-073)
    Payload      payload;                     // what its source says, in order
};

// A half-open interval of reference frames, [ref_index, ref_end). A point at
// frame f meets it when ref_index <= f < ref_end, a span [s, e) when
// s < ref_end and e > ref_index (D-078), for every interval, an empty one
// included (see meets()).
struct Interval {
    std::int64_t ref_index = 0;
    std::int64_t ref_end   = 0;   // exclusive
};

// A scored playlist, the reference's or a pair's distorted one, with the
// master entries that name it (design §4.5). A rung is named by its master
// entry's index and its URI; its two BANDWIDTH ranks are labels (D-075).
struct Variant {
    int index = -1;                      // its position in Timeline::variants
    int pair  = -1;                      // the report's pairs[].index of the pair whose distorted
                                         // input it is (D-092 (b)); -1: the reference
    std::vector<std::size_t> entries;    // the master's entries naming it, in the master's order
    std::string url;                     // as the ladder report gives it
    std::string playlist_location;       // where the build read it from, after redirects
    std::string playlist_sha256;         // of its bytes as the build read them, lowercase hex
    std::int64_t bandwidth = 0;          // the highest of its entries' BANDWIDTHs (D-081 (e))
    std::optional<std::string> resolution, codecs;   // as the master gives them; nullopt: absent
    int rank_from_top    = 0;            // by BANDWIDTH, 1 the highest; 0: not a rung
    int rank_from_bottom = 0;            // by BANDWIDTH, 1 the lowest; 0: not a rung
    std::string segment_map = "ok";      // or "refused: <why>" (D-081 (c))
};

// A pair's curve: its scored frames' VMAF scores at the reference frames
// they are paired with, in ascending order, and the reference frames at
// which a libvmaf context starts (design §4.4; D-037).
struct Curve {
    int variant = -1;                            // the pair's distorted variant
    std::vector<std::int64_t> ref_index;         // ascending
    std::vector<double>       vmaf;              // parallel to ref_index
    std::vector<std::int64_t> context_first;     // the reference frame of each context's first pair
};

// Names a curve, for the query and detection (design §6.1): a pair's, which
// Timeline::curves holds. A session's, derived from its pairs' curves, is
// the kind sessions add (D-068).
struct CurveRef {
    enum class Kind : std::uint8_t { Pair } kind = Kind::Pair;
    int index = 0;                               // into Timeline::curves
};

// The timeline of one ladder report: the axis, the variants, the pairs'
// curves, every source's events, and what couldn't be placed. The curves are
// read from the report, which a timeline document names instead of copying
// its scores (D-072).
struct Timeline {
    Axis                     axis;
    std::vector<Variant>     variants;
    std::vector<Curve>       curves;      // one per pair of the report, in its order
    std::vector<Event>       events;      // in canonical order (design §4.7)
    std::vector<std::string> warnings;    // anchors that couldn't be placed, and why (design §5.1)

    // The overlap index (the design's P-J), which build_index() makes: for
    // each event in canonical order, the largest end of the events up to it,
    // a point's end being its frame plus one. build_index() must be called
    // again after any change to the events. Only a change in their number is
    // detected, and overlapping() then scans every event instead of using
    // the index; an event moved or replaced without one leaves the index
    // stale, and overlapping() can miss it.
    std::vector<std::int64_t> max_end;

    // Puts the events in canonical order and builds the overlap index.
    void build_index();
    // Indices into `events` of the events meeting [a, b) by meets()'s rule
    // (D-078), an empty or reversed interval included, in the order of
    // `events`: the canonical order after build_index(). The scan that
    // stands in for a missing or outdated index keeps the canonical order
    // only when the events are sorted. Margins are the caller's: the query
    // widens the interval on the axis first (src/TimelineQuery.h).
    std::vector<std::size_t> overlapping(std::int64_t a, std::int64_t b) const;
};

// A curve's points: a pair's as Timeline::curves holds them, a session's
// derived from the pairs' (design §4.4). Defined in src/TimelineModel.cpp.
Curve curve_points(const Timeline& timeline, CurveRef curve);

// "vmaf", "network", "playlist", "scte35", "player", "decoder": a source's
// name in the timeline document (design §5.9).
std::string_view source_name(Source source);
// The source a name names; nullopt for any other text.
std::optional<Source> source_from_name(std::string_view name);

// Whether `event` meets `interval` (D-078): a point at frame f when
// interval.ref_index <= f < interval.ref_end, a span [s, e) when
// s < interval.ref_end and e > interval.ref_index. The rule holds as written
// for every interval: an empty one, [a, a), meets no point and every span
// that straddles a, s < a < e; one with ref_end < ref_index meets no point
// and the spans that start before its end and end after its start.
bool meets(const Event& event, Interval interval);

// The canonical order (the design's P-I): by ref_index; then by the end, a
// point counting as ending at its frame, so that at one frame the points,
// and any empty span, come before the other spans, the shorter first; then
// by source in section 3.1's order, by type, by variant, and by id, shorter
// ids first so that the per-type indices assign_ids() writes compare as
// numbers.
bool canonical_before(const Event& a, const Event& b);

// Sorts `events` into canonical order, keeping the order of events that are
// equal in it (which ids from assign_ids() never are).
void sort_events(std::vector<Event>& events);

// Gives every event its id, "<source>/<type>/<variant>/<n>" ("-" for no
// variant), n numbering the events of one source, type and variant from 0
// in canonical order, and leaves the events in that order (the design's
// P-I). Events equal in canonical order but for their ids are numbered in
// the order they come in, so that the ids depend on the inputs alone.
void assign_ids(std::vector<Event>& events);

} // namespace timeline
