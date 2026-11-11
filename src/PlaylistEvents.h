#pragma once

// A media playlist's events, its cues and their ad breaks (DECISIONS.md
// D-076; eval/phase3-survey/design.md §5.4 and §5.5). It reads the text of
// one media playlist, as the timeline build fetched it, and pairs cues into
// ad breaks; it reads no file and decodes no SCTE-35 message, so it is
// unit-tested on playlists the tests write (tests/test_playlist_events.cpp).
// No FFmpeg here but AVRational and libavutil's rational arithmetic.
//
// The rules.
//
// - Segments. hls::parse_media_timeline's, unchanged (src/HlsAlign.h).
//   Lines are numbered as it numbers them: from 1, every line counted,
//   blank lines included. A line that starts with '#' is a tag when it
//   starts with "#EXT" and a comment otherwise (RFC 8216 §4.1); a tag's name
//   is its text up to the first ':', without the '#', and its value the
//   rest. Each tag belongs to the segment whose URI line follows it, a tag
//   between #EXTINF and its URI included (RFC 8216 §4.3.2; design §5.1); a
//   tag after the last URI belongs to none. A URI that no #EXTINF precedes
//   makes no segment, as for parse_media_timeline, so the tags before it
//   belong to the next segment.
// - A live playlist, one without EXT-X-ENDLIST whose EXT-X-PLAYLIST-TYPE
//   isn't VOD (hls::MediaTimeline::live()), isn't read (D-081 (a)).
// - The playlist source's events (design §5.4): a `discontinuity` for each
//   EXT-X-DISCONTINUITY, never an inferred ad boundary (D-076); a
//   `program_date_time` for each EXT-X-PROGRAM-DATE-TIME; a `daterange` for
//   each EXT-X-DATERANGE without SCTE35-CMD, SCTE35-OUT or SCTE35-IN, with
//   its attributes as RFC 8216 §4.3.2.7 types them, kept raw with the
//   reason when it doesn't parse; and a `tag` for every other tag, raw,
//   vendor tags and the signalings outside D-070's list (EXT-X-SCTE35,
//   EXT-X-SPLICEPOINT-SCTE35, Adobe's EXT-X-CUE) included (D-076). The tags
//   that make no event are those whose content the playlist's header holds
//   (EXTM3U, EXT-X-VERSION, -TARGETDURATION, -MEDIA-SEQUENCE,
//   -DISCONTINUITY-SEQUENCE, -PLAYLIST-TYPE, -ENDLIST,
//   -INDEPENDENT-SEGMENTS) or a segment's event does (EXTINF,
//   EXT-X-BYTERANGE, EXT-X-GAP; segment_event()). The type `period` is
//   reserved for a DASH period boundary, which no reader makes in Phase 3
//   (D-076).
// - The plain cue tags of D-070, as cues of the scte35 source: a `cue_out`
//   for each EXT-X-CUE-OUT (bare, with a duration, with DURATION= or with
//   further attributes), a `cue_out_cont` for each EXT-X-CUE-OUT-CONT
//   (elapsed/duration, or ElapsedTime=, Duration= and SCTE35= attributes)
//   and a `cue_in` for each EXT-X-CUE-IN (bare or with attributes), in the
//   forms eval/phase3-survey/scte35.md §2 describes. A tag's name is
//   compared whole, so an EXT-X-CUE-OUT-CONT is never a cue-out.
// - Every message a tag embeds is handed on, undecoded, as a CueInput for
//   the decoder (src/Scte35.h, src/Scte35Events.h; design §5.5): the value
//   of an #EXT-OATCLS-SCTE35 (base64), each of an EXT-X-DATERANGE's
//   SCTE35-CMD, SCTE35-OUT and SCTE35-IN (hex), and the SCTE35= attribute of
//   a plain cue tag. A malformed EXT-X-DATERANGE is a `daterange` event kept
//   raw, with the reason, and hands on each of its SCTE35-* attributes that
//   is itself a well-formed hexadecimal-sequence, whatever else in it is
//   malformed: the owner's answer to Q47 (D-106 (e)), whose warning the
//   decoder writes (src/Scte35Events.h). add_daterange() in
//   src/PlaylistEvents.cpp says which attribute errors make one malformed.
// - Ad breaks (pair_breaks()): every cue, from the plain tags and from
//   decoded messages, so that the pairing has one owner (design §5.5).
//   Within each variant, in play order, a cue_out starts a break and the
//   next cue_in ends it. Breaks don't nest, the owner's answer to Q46
//   (D-087): a cue_out that another cue_out follows before any cue_in has
//   no cue_in, and the second pairs with the next cue_in. A break without its
//   cue_in ends at its start plus the first duration its cue_out declares,
//   in the order D-076 gives: the tag's own, then PLANNED-DURATION, the
//   message's break_duration and its segmentation_duration (Q17 (a)). Every
//   declared value is kept in the break's payload. A cue_out that holds its
//   cue_in in its tag (kHeldCueInLine) ends at its declared duration in the
//   same order, the tag's DURATION or END-DATE first, and no later cue_in
//   ends it (D-109 (b)); one that declares no duration holds none, and
//   pairs as any cue_out, the next cue_in ending it (D-110 (a)). A break of
//   no frames, its cue_in at its cue_out's frame or its declared duration 0,
//   is kept, with a warning that says so, since such a span meets no
//   interval that starts at its frame (D-078, D-110 (c)).
//
// Events as parse_playlist_events() and segment_event() return them aren't
// placed yet: each names its variant and the media sequence number of its
// segment, its ref_index is kUnplaced, and the timeline build places it on
// the reference's frames by the design's §5.1 rules. Ids are the model's
// (design §4.7).

extern "C" {
#include <libavutil/rational.h>
}

#include "HlsAlign.h"
#include "TimelineModel.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace timeline {

// What carried an SCTE-35 message.
enum class CueCarrier : std::uint8_t {
    PlaylistTag,        // D-070's tags: OATCLS, DATERANGE's SCTE35-*, CUE-OUT-CONT's SCTE35=
    DashEventStream,    // an event of a DASH EventStream
    TsSection,          // a section of an MPEG-TS stream (stream type 0x86)
    Id3Priv,            // an ID3 PRIV frame in a segment
};

// A tag's place in its playlist: the segment whose URI follows it, and its line.
struct PlaylistAnchor {
    int          variant = -1;              // index into Timeline::variants
    std::int64_t segment = 0;               // media sequence number; -1: no segment follows the tag
    int          line    = 0;               // the tag's, from 1
};

// A media PTS on the carrying variant's own clock.
struct MediaPtsAnchor {
    int          variant   = -1;            // index into Timeline::variants
    std::int64_t pts       = 0;             // ticks of time_base
    AVRational   time_base = { 0, 1 };
    std::optional<std::int64_t> segment;    // media sequence number, where known
};

// A time in a DASH period.
struct PeriodAnchor {
    std::string  period;                    // the period's id
    std::int64_t presentation_time = 0;     // the event's presentationTime, in timescale units
    std::int64_t timescale = 1;
};

// An SCTE-35 message from any carrier, with the anchor its carrier gives:
// the interface of DECISIONS.md D-070 through which every message reaches
// the decoder (src/Scte35Events.h; eval/phase3-survey/design.md §5.5).
// Phase 3 reads playlist tags only; the other carriers are declared so that
// a reader of them needs no change of these types.
struct CueInput {
    CueCarrier carrier = CueCarrier::PlaylistTag;
    std::variant<PlaylistAnchor, MediaPtsAnchor, PeriodAnchor> anchor;
    std::string form;                       // the tag or attribute that carried it: "OATCLS",
                                            // "DATERANGE SCTE35-OUT", "CUE-OUT-CONT SCTE35", ...
    // The message as its carrier gives it, for the decoder to read: a tag's
    // value as written (base64 or hex), a section's bytes from the others.
    std::vector<std::uint8_t> bytes;
};

// ── Events of this module ────────────────────────────────────────────────────

// The ref_index of an event not placed on the reference's frames yet, or
// that the build couldn't place (design §5.1). No reference frame is
// negative.
inline constexpr std::int64_t kUnplaced = -1;

// The event types this module makes, of design §5.9's vocabulary.
inline constexpr std::string_view kTypeSegment         = "segment";             // playlist, a span
inline constexpr std::string_view kTypeDiscontinuity   = "discontinuity";       // playlist
inline constexpr std::string_view kTypeProgramDateTime = "program_date_time";   // playlist
inline constexpr std::string_view kTypeDaterange       = "daterange";           // playlist
inline constexpr std::string_view kTypeTag             = "tag";                 // playlist
inline constexpr std::string_view kTypePeriod          = "period";              // playlist, reserved
inline constexpr std::string_view kTypeCueOut          = "cue_out";             // scte35
inline constexpr std::string_view kTypeCueOutCont      = "cue_out_cont";        // scte35
inline constexpr std::string_view kTypeCueIn           = "cue_in";              // scte35
inline constexpr std::string_view kTypeAdBreak         = "ad_break";            // scte35, a span

// The payload members in which a cue_out declares its break's duration, in
// seconds, in the order in which pair_breaks() takes the first one given
// (D-076, Q17 (a)): the tag's own (EXT-X-CUE-OUT's value; EXT-X-DATERANGE's
// DURATION), END-DATE less START-DATE, PLANNED-DURATION, the message's
// break_duration and its segmentation_duration. A plain EXT-X-CUE-OUT gives
// the first only; the decoder's cues give the others (src/Scte35Events.h).
// Beside a duration, the member of its name and kTicksSuffix may give it in
// whole ticks of kDurationClock, which pair_breaks() takes before the
// seconds, as an exact rational of the ticks over kDurationClock: the
// decoder's cues give break_duration_ticks and segmentation_duration_ticks
// beside break_duration and segmentation_duration, which a message gives in
// ticks of SCTE 35's 90 kHz clock, so that a count that 9 doesn't divide,
// which no decimal of seconds holds, still ends its break; the seconds stay
// as they are, and a duration in seconds alone is taken as before (D-109
// (d)).
inline constexpr std::string_view kDurationNames[] = {
    "duration", "end_date_duration", "planned_duration", "break_duration",
    "segmentation_duration",
};

// The suffix of the member that gives a duration of kDurationNames in ticks
// ("break_duration_ticks"), and the clock of those ticks (D-109 (d)).
inline constexpr std::string_view kTicksSuffix   = "_ticks";
inline constexpr std::int64_t     kDurationClock = 90000;

// The payload member of a cue_out that holds its own cue_in, an
// EXT-X-DATERANGE's cue-out message with a cue-in message of the same tag,
// whichever of its SCTE35-* attributes carries each, where the tag declares
// DURATION or END-DATE and no plain EXT-X-CUE-OUT takes its messages
// (src/Scte35Events.h; D-110 (a), (b)): the line of that tag, whose cue-in
// message makes no cue_in of its own. pair_breaks() ends such a break at
// its declared duration, and no later cue_in ends it (D-109 (b)); a cue_out
// with the member that declares no duration pairs as any cue_out (D-110
// (a)).
inline constexpr std::string_view kHeldCueInLine = "held_cue_in_line";

// One tag of a media playlist. Its segment is an index, where an event's
// and a PlaylistAnchor's are media sequence numbers.
struct PlaylistTag {
    int segment_index = -1;              // index into PlaylistEvents::timeline.segments of the
                                         // segment whose URI follows it; -1: none follows
    int line = 0;                        // from 1
    std::string name;                    // without its '#': "EXT-X-CUE-OUT"
    std::optional<std::string> value;    // after its first ':', as written; nullopt: no ':'
};

// One attribute of an HLS attribute list (RFC 8216 §4.2), as written.
struct Attribute {
    std::string name;
    std::string value;                   // a quoted-string's characters, without its quotes
    bool        quoted = false;
};

// `list`'s attributes in their order, or none and `err` saying what doesn't
// parse: a name that isn't made of letters, digits, '-' and '_', a name
// without '=' or without a value, a quoted value without its closing quote
// or followed by more than a ',', an unquoted value with a quote or a
// blank in it, a name given twice (RFC 8216 §4.2), or a ',' at the end.
// An empty list has no attributes. Two departures from RFC 8216 §4.2's
// grammar are accepted, which the owner's answer to Q47 (D-106 (e)) leaves
// as they are: blanks (spaces and tabs) around a ',' and at either end of
// the list, where the RFC allows no whitespace; and lowercase letters and
// '_' in a name, which the RFC makes of [A..Z], [0..9] and '-' only.
std::vector<Attribute> parse_attributes(std::string_view list, std::string& err);

// A media playlist read for the timeline: its segments, its tags, the events
// they make and the messages they embed.
struct PlaylistEvents {
    int                      variant = -1;   // index into Timeline::variants
    hls::MediaTimeline       timeline;       // its segments and header (src/HlsAlign.h)
    std::vector<std::string> uris;           // each segment's URI as written, parallel to
                                             // timeline.segments
    std::vector<PlaylistTag> tags;           // every tag but the header line, in line order
    std::vector<Event>       events;         // the playlist source's, in line order:
                                             // discontinuity, program_date_time, daterange, tag
    std::vector<Event>       cues;           // the plain cue tags', in line order: cue_out,
                                             // cue_out_cont, cue_in
    std::vector<CueInput>    messages;       // every message the tags embed, in line order,
                                             // each with a PlaylistAnchor
    std::string              error;          // why it isn't read; empty: read
};

// `text` as the media playlist of variant `variant`, served from `base_url`
// (its segments' URIs made absolute against it). With `error` set (not a
// media playlist with segments, or a live one), only `timeline` is filled.
//
// Every event names `variant`, the media sequence number of its tag's
// segment in `segment` (nullopt for a tag after the last URI) and kUnplaced
// in ref_index; its payload starts with the tag's "line". A message's
// PlaylistAnchor names the same, its segment -1 after the last URI.
// Payloads, in order (a value is null where the tag doesn't give it):
//   discontinuity      line; discontinuity_sequence: its segment's, which
//                      it starts (hls::Segment::disc_seq)
//   program_date_time  line; date: as written; unix: as Unix seconds
//                      (hls::parse_iso8601), null if it doesn't parse
//   daterange          line; raw: the value as written; attributes: each in
//                      its order, a decimal-floating-point as a number, a
//                      quoted-string without its quotes, anything else as
//                      written; and only when it doesn't parse, or an
//                      attribute isn't of the type RFC 8216 §4.3.2.7 gives
//                      it, error, with attributes empty, its well-formed
//                      SCTE35-* handed on all the same (D-106 (e))
//   tag                line; name; value: as written
//   cue_out            line; form "CUE-OUT"; raw: the line as written;
//                      duration: its value's leading number, or its DURATION
//                      attribute; attributes: each as written; and only when
//                      its value, or a number in it, doesn't parse, error
//   cue_out_cont       line; form "CUE-OUT-CONT"; raw; elapsed and duration:
//                      of the elapsed/duration form, or of its ElapsedTime
//                      and Duration attributes; attributes; error, as above
//   cue_in             line; form "CUE-IN"; raw; attributes; error, as above
// The cue tags' attribute names DURATION, ElapsedTime, Duration and SCTE35
// are matched without regard to case, since no specification defines them
// (scte35.md §2).
PlaylistEvents parse_playlist_events(const std::string& text, const std::string& base_url,
                                     int variant);

// The `segment` event of segment `index` of `playlist` (index <
// playlist.timeline.segments.size()): a span the build places from the
// segment's first frame to the next segment's (design §5.1), with `bytes`
// as the demux pass read them (D-074; nullopt: not known). Its payload, in
// order: line (of its URI); uri: as written; url: made absolute; media_sequence;
// extinf; byte_range: {length, offset} or null; discontinuity_sequence; pdt:
// Unix seconds, its own or carried on from the segment before it, not across
// a discontinuity (null: none); pdt_explicit; playlist_time: the sum of the
// EXTINF of the segments before it; bytes; gap: EXT-X-GAP; map: an
// EXT-X-MAP applies. All of these but bytes are hls::Segment's.
Event segment_event(const PlaylistEvents& playlist, std::size_t index,
                    std::optional<std::int64_t> bytes);

// ── Ad breaks ───────────────────────────────────────────────────────────────

// The reference frame at which `duration` seconds after reference frame
// `ref_index` end on the axis (D-073; D-081 (g); design §4.2). Its contract:
// it is given a finite, positive, exact duration, and returns the first
// frame from `ref_index` on, never before it, whose seconds are at or after
// `ref_index`'s plus `duration`, or the reference's frame count when no
// frame is; or nullopt when it can't say. pair_breaks() asks it for no other
// duration: a zero duration ends at `ref_index` without it, and one that no
// AVRational holds exactly makes no break. The build binds it to the axis's
// duration form, which WP01 adds (src/TimelineAxis.h).
using FrameAfter =
    std::function<std::optional<std::int64_t>(std::int64_t ref_index, AVRational duration)>;

struct Breaks {
    std::vector<Event>       events;     // ad_break spans, placed, in play order within a variant
    std::vector<std::string> warnings;   // each cue_out that makes no break, and why, and each
                                         // break of no frames (D-110 (c))
};

// The ad breaks of `cues`: every cue_out, cue_out_cont and cue_in of every
// variant, placed by the build (ref_index kUnplaced for one it couldn't
// place), from the plain tags and from decoded messages, in any order;
// events of other types are passed over. Each variant's cues are paired in
// play order, by segment (a cue of none last) and within a segment by
// payload "line", by the rules above. An unplaced cue still takes its turn,
// so that a cue_out never pairs with a later break's cue_in; a break whose
// cue_out or cue_in is unplaced, or that has neither a cue_in nor a declared
// duration, is not made, and a warning says why. A declared duration is
// taken as the decimal its number's shortest text gives (29.988 as
// 7497/250), so that a break that ends exactly on a frame ends there; a
// break without a cue_in whose duration no AVRational holds exactly, its
// numerator or its denominator above INT_MAX once reduced, is not made
// either, and a warning says so. A declared duration whose member of its
// name and kTicksSuffix is a whole number from 0 is taken instead as those
// ticks over kDurationClock, exactly, before its seconds; one that no
// AVRational holds makes no break without a cue_in either, and a warning
// says so (D-109 (d)).
//
// Each break is a span from its cue_out's reference frame, with that cue's
// variant, segment and media key, to its cue_in's frame or to frame_after()
// of its declared duration. Its payload, in order: cue_out_line, cue_in_line
// (null without a cue_in); end: "cue_in", or the name of the duration that
// ended it, as kDurationNames gives it; durations: each duration its cue_out
// declares, by name, in that order; cue_out_cont_durations: the duration of
// each cue_out_cont between its cue_out and the cue that ends it, in play
// order (null for one that declares none). The durations keep each
// duration's ticks beside it, by the name of its member (D-109 (d)). A
// cue_out whose payload has kHeldCueInLine holds its cue_in (D-109 (b)): it
// ends at its declared duration, a cue_in after it ending nothing; its
// break's cue_in_line is that member's line, and its end the duration's
// name. One that declares no duration holds none: it pairs as any cue_out,
// the next cue_in ending it (D-110 (a)). A break of no frames, its end at
// its start, from a cue_in at its cue_out's frame or a declared duration of
// 0, is made, and a warning names its cue_out and says that it has no
// frames, so that a drop that starts at its frame doesn't list it (D-078,
// D-110 (c)).
Breaks pair_breaks(const std::vector<Event>& cues, const FrameAfter& frame_after);

} // namespace timeline
