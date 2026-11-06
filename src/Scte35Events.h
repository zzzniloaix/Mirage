#pragma once

// Decoded SCTE-35 messages as events, at their anchors (DECISIONS.md D-070,
// D-106; WP05 of eval/phase3-survey/design.md §14, §5.1, §5.5). Every message
// that reaches it as a CueInput (src/PlaylistEvents.h), from any carrier, is
// decoded (src/Scte35.h), classed, placed on the reference's frames by its
// anchor's kind, merged into the plain cue it belongs to, and returned as
// events: the cues for WP04's pair_breaks(), which pairs every cue, plain or
// decoded (D-076, D-087), and the splice points, messages and cancellations
// to add to the timeline. Core code (design §3.1): plain structs in and out,
// nothing read or opened, libavutil at most. WP09c calls it from the build
// (src/TimelineBuild.h), before pair_breaks(), with --cue-types and
// --bad-crc (Q15, Q16) as MessageOptions. Unit-tested on sections the tests
// build (tests/test_scte35_events.cpp; D-107).
//
// The rules.
//
// - Text. A playlist tag's message is the text as written: hex for an
//   EXT-X-DATERANGE's SCTE35-CMD, SCTE35-OUT and SCTE35-IN (RFC 8216
//   §4.3.2.7), base64 for #EXT-OATCLS-SCTE35 and a cue tag's SCTE35=
//   (scte35.md §2), except a value that starts with 0x or 0X, which is read
//   as hex, as one vendor prints an OATCLS (scte35.md §2.4): base64 of a
//   section never starts so, since a section's first byte, 0xFC, makes its
//   first character '/'. Any other carrier gives the section's bytes.
// - Classes (D-106 (b), Q15 (a)). A splice_insert is a cue_out or a cue_in
//   by its out_of_network_indicator, a cancel when its
//   splice_event_cancel_indicator is set. A time_signal is a cue_out when
//   one of its segmentation descriptors that isn't cancelled has a Start
//   type of MessageOptions::cue_types, a cue_in when one has an End type of
//   them, and both when it has both: then the cue_in comes first, an end and
//   a start at one splice point being the end of one segment and the start
//   of the next. A segmentation descriptor whose cancel indicator is set
//   makes a cancel, in any command. Every other decoded message is a
//   `message`. The roles are Table 23's names: a type whose name ends in
//   Start or End (segmentation_role()); the default cue_types are D-106
//   (b)'s, 0x22 to 0x23, 0x30 to 0x37 and 0x44 to 0x47.
// - The CRC_32 (D-106 (c), Q16 (a)). A message whose CRC_32 fails is kept,
//   its payload saying crc_valid false, with a warning; under BadCrc::Drop,
//   --bad-crc drop, it makes no event, and a warning says so.
// - Placement (D-106 (a), Q14 (a); design §5.1). A playlist tag's message is
//   placed by its position, where the segment whose URI follows the tag
//   starts, as build() places that segment's event: its first paired
//   frame's reference frame. A message read from the media (a
//   MediaPtsAnchor) is placed at its PTS, as a splice time is. A splice time
//   its message gives, (pts_time + pts_adjustment) mod 2^33, is a
//   splice_point event of its own, placed on the carrying variant's own
//   frames: the first of them, in index order, within the carrying
//   playlist's discontinuity sequence of the anchor's segment, whose PTS is
//   at or after it, compared in 90 kHz ticks modulo 2^33 from the
//   sequence's first frame; then where that frame sits on the reference's
//   frames, its pair's or, unpaired, the next paired frame's (D-081 (d)). A
//   time before the sequence's first frame or after its last is outside it,
//   and isn't placed. Without a known segment, a media PTS is searched over
//   the whole input only when its playlist has one discontinuity sequence,
//   or it has no playlist. A message whose own position isn't found, by its
//   tag or by its PTS, is still read: its cues are returned unplaced, with
//   their segment where it is known, and its splice times are searched in
//   its sequence where that is known. A period-time anchor goes to the
//   warnings: no DASH timeline is scored (design §5.5).
// - Every anchor is recorded in the message's payload with its offset from
//   the position, and a warning is written for each that falls outside the
//   tag's segment (D-106 (a)): the splice time, by the segment of the
//   carrying variant's frame it lands on; an EXT-X-DATERANGE's date of its
//   message's point, by the segment's EXT-X-PROGRAM-DATE-TIME and EXTINF,
//   compared in whole microseconds, so that a date written to the
//   millisecond, as RFC 8216 §4.3.2.6 asks, that falls exactly on a
//   segment's end isn't moved across it by the rounding of the doubles
//   hls::parse_iso8601() gives. That date is START-DATE, the date of an
//   SCTE35-OUT's splice or an SCTE35-CMD's command; for an SCTE35-IN it is
//   the in point, END-DATE or, without one, START-DATE plus DURATION (RFC
//   8216 §4.3.2.7.1), since the tags of one ID share START-DATE, the
//   break's start: an SCTE35-IN's START-DATE is recorded as the range's
//   start, and set against nothing.
// - Merging (D-106 (d), Q44 (e)). A message a cue tag embeds (its SCTE35=
//   attribute: forms "CUE-OUT SCTE35", "CUE-OUT-CONT SCTE35" and "CUE-IN
//   SCTE35") is that tag's: it goes into the payload of the tag's plain cue,
//   whatever its class, and makes no cue of its own; so does a cue_out
//   message from any other carrier whose segment is known (an OATCLS, a
//   DATERANGE, a section read from the media with its segment) in the same
//   segment of the same variant as a plain EXT-X-CUE-OUT, which takes it
//   into its payload, the first such cue by line when a segment has two,
//   and declares the message's durations where it declares none of its
//   own. Every other message is an event of its own class, a cue_in in an
//   EXT-X-CUE-IN's segment included, since D-106 (d) names the cue-out
//   only. A message with the classes cue_in and cue_out in an
//   EXT-X-CUE-OUT's segment gives that cue its cue_out part and makes a
//   cue_in of its own, which ends the break before that cue's starts in
//   either order of their tags: MediaLive writes the message's tag before
//   the EXT-X-CUE-OUT and Wowza after it (scte35.md §2.4). That cue_in takes
//   the EXT-X-CUE-OUT's line where its own is later or null, and the cues
//   go to pair_breaks() in the order cues_to_pair() gives, so that the
//   pairing takes it first.
// - A malformed EXT-X-DATERANGE (D-106 (e), Q47 option 2):
//   parse_playlist_events() hands on each of its SCTE35-* attributes that is
//   a well-formed hexadecimal-sequence, and keeps the tag as a `daterange`
//   event with its error; such a message is decoded and placed by its
//   position with a warning, and none of the tag's other attributes, its
//   dates and durations, is read.
// - What can't be decoded or placed is a warning with its reason, never
//   dropped silently (design §5.1): text that isn't base64 or hex, a section
//   that doesn't decode or is encrypted, the messages of a variant whose
//   demux pass was refused (D-081 (c), D-086), one warning per variant, a
//   tag after the last segment's URI, a segment that isn't placed, a media
//   PTS that isn't placed, a splice time outside its sequence. A cue that
//   isn't placed is still returned, its ref_index kUnplaced, so that it
//   takes its turn in the pairing, as build() hands pair_breaks() the plain
//   cues; a splice point, a message or a cancel that isn't placed is left
//   out.
//
// Pending the owner, as built, the code saying so where it makes each:
// - Several messages carrying one signal in one segment without a plain
//   EXT-X-CUE-OUT, such as an OATCLS and a DATERANGE of one splice_insert,
//   or the DATERANGE tags RFC 8216 §4.3.2.7.1 writes for one section with
//   several segmentation types, are a cue_out each, of which pair_breaks()
//   makes an ad_break each (D-087).
// - An EXT-X-DATERANGE with both SCTE35-OUT and SCTE35-IN, SCTE35-OUT
//   first, gives a cue_out and a cue_in at one line, of which pair_breaks()
//   makes an ad_break of no frames.
// - A message that a vendor repeats in each EXT-X-CUE-OUT-CONT's SCTE35=
//   gives a splice_point at each repeat, at the same frame, each with the
//   warning of a splice time outside the tag's segment.
// - The levels of SCTE 35's hierarchy of advertising segments, a Break
//   above its Placement Opportunities and Ad Blocks, count alike, so that a
//   Break that declares a duration makes an ad_break around those of the
//   levels below it; and of several segmentation descriptors of a cue's
//   role, the first that gives a duration in the loop's order declares its
//   segmentation_duration.
//
// The events, each of source scte35, of the carrying variant, with its
// segment's media sequence number and the media key of its frame, no id
// (build() gives every event its id). Payloads, in order, a value null
// where its source doesn't give it:
//   cue_out, cue_in  line: the tag's, null for another carrier, and for a
//                    cue_in that joins an EXT-X-CUE-OUT (Merging, above) that
//                    cue's where its own is later or null; form: the
//                    CueInput's; raw: the tag's line as written, null for
//                    another carrier; the durations, in seconds, by
//                    kDurationNames: duration (an EXT-X-DATERANGE's
//                    DURATION), end_date_duration (its END-DATE less its
//                    START-DATE), planned_duration (its PLANNED-DURATION),
//                    break_duration (the splice_insert's) and
//                    segmentation_duration (that of the first segmentation
//                    descriptor of a cue type of the cue's role that gives
//                    one); then the message's record: classes, crc_valid,
//                    message, anchors
//   message          line; form; raw; command: its name (Table 7);
//                    segmentation_types: the segmentation_type_id of each
//                    of its segmentation descriptors that isn't cancelled;
//                    the record
//   cancel           line; form; raw; command; splice_event_id: a cancelled
//                    splice_insert's; segmentation_event_ids: those of the
//                    cancelled segmentation descriptors; the record
//   splice_point     line; form; command; pts_time: as written;
//                    pts_adjustment; pts_adjusted; component_tag: a
//                    splice_insert's in component mode; index: the carrying
//                    variant's frame it lands on; in_segment: whether that
//                    frame is in the tag's segment, or the media anchor's
//                    (null where that isn't known); crc_valid
// The record: classes, the list of the message's classes ("cue_in",
// "cue_out", "cancel", "message"); crc_valid; message: every field decode()
// reads, by the standard's names, and a few of this module's beside them
// (command, the descriptors' and segmentation types' names, pts_adjusted
// and seconds by the ticks, and the counts of bytes decode() keeps);
// anchors, each an object with "anchor" its kind:
//   position     segment, line, ref_index: where the tag's segment starts
//   media_pts    pts, time_base ("num/den"), segment, index, ref_index
//   splice_time  pts_time, pts_adjusted, component_tag, index, segment: the
//                carrying variant's frame and its segment, ref_index,
//                offset_frames and offset_seconds from the position, the
//                seconds the axis's, in_segment
//   start_date   date: as written, unix: its Unix seconds, pdt: the Unix
//                seconds of its segment's EXT-X-PROGRAM-DATE-TIME,
//                offset_seconds: START-DATE less that PDT, in_segment: null
//                for an SCTE35-IN's, which isn't set against the segment
//   end_date     an SCTE35-IN's in point: from: "END-DATE", or "START-DATE
//                + DURATION" without one; date: END-DATE as written, null
//                for the other; unix; pdt; offset_seconds: the in point less
//                that PDT; in_segment
// A plain cue that takes a message gets the member `messages`, a list of
// the records of every message it takes, each with line, form and raw
// before classes; a plain cue_out also gets, after it, each duration of
// kDurationNames it lacks or has null, from the first message that gives
// one (an EXT-X-DATERANGE's DURATION filling a bare EXT-X-CUE-OUT's null
// duration).

#include "LadderReportRead.h"
#include "PlaylistEvents.h"
#include "Scte35.h"
#include "TimelineAxis.h"
#include "TimelineModel.h"

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace timeline {

struct VariantMedia;   // src/TimelineBuild.h

// The event types this module makes besides src/PlaylistEvents.h's cue
// types, of design §5.9's vocabulary.
inline constexpr std::string_view kTypeSplicePoint = "splice_point";   // scte35
inline constexpr std::string_view kTypeMessage     = "message";        // scte35
inline constexpr std::string_view kTypeCancel      = "cancel";         // scte35

// What a segmentation_type_id marks by its name in Table 23: the start or
// the end of a segment; nullopt for a type whose name says neither (Not
// Indicated, Content Identification, Private, the Program types from Early
// Termination to Immediate Resumption but Overlap Start) and for a reserved
// one (scte35::segmentation_type_name()).
enum class SegmentationRole : std::uint8_t { Start, End };
std::optional<SegmentationRole> segmentation_role(std::uint8_t segmentation_type_id);

// The time_signal segmentation types that are cue-outs and cue-ins by
// default (D-106 (b)): Break, Provider and Distributor Advertisement,
// Provider and Distributor Placement Opportunity, and Provider and
// Distributor Ad Block, each Start and End (Table 23).
inline constexpr std::uint8_t kDefaultCueTypes[] = {
    0x22, 0x23, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x44, 0x45, 0x46, 0x47,
};

// What becomes of a message whose CRC_32 fails (D-106 (c)).
enum class BadCrc : std::uint8_t {
    Keep,   // its events, crc_valid false, and a warning (the default)
    Drop,   // no event, and a warning (--bad-crc drop)
};

// The choices D-106 makes settable, for WP09c's --cue-types and --bad-crc.
struct MessageOptions {
    // The segmentation_type_ids that make a time_signal a cue: each a Start
    // or an End type (segmentation_role()); duplicates count once.
    std::vector<std::uint8_t> cue_types{ std::begin(kDefaultCueTypes), std::end(kDefaultCueTypes) };
    BadCrc bad_crc = BadCrc::Keep;
};

// Why `options` can't be used, naming the first cue type that is neither a
// Start nor an End type; empty when it can.
std::string check_message_options(const MessageOptions& options);

// One variant as the build read it (src/TimelineBuild.h's VariantRead), by
// pointer: its media playlist, null for a reference that isn't a playlist,
// and its demux pass, null when none ran.
struct MessageVariant {
    const PlaylistEvents* playlist = nullptr;
    const VariantMedia*   media    = nullptr;
};

// What the messages are placed through: the ladder report as read, whose
// pairs place a distorted frame on the reference's, the reference's axis,
// whose frames bound the placement and give each event its media key, and
// every variant, in build()'s order: [0] the reference, [k + 1] the
// distorted input of scores->pairs[k]. scores and axis must be given.
struct MessagePlacement {
    const LadderScores*         scores = nullptr;
    const Axis*                 axis   = nullptr;
    std::vector<MessageVariant> variants;
};

// Every message the variants' playlists hand on (PlaylistEvents::messages),
// in variant order and, within a variant, in line order.
std::vector<CueInput> playlist_messages(const std::vector<MessageVariant>& variants);

// What scte35_events() makes of the messages.
struct MessageEvents {
    std::vector<Event>       cues;            // cue_out and cue_in, placed or kUnplaced, for pair_breaks()
    std::vector<Event>       splice_points;   // placed
    std::vector<Event>       others;          // message and cancel, placed
    std::vector<std::string> warnings;        // what couldn't be decoded or placed, and why
};

// The events of `messages` by the rules above, in the order of `messages`,
// a message's cue_in before its cue_out. `plain_cues` are the plain cue
// tags' events of every variant as the build holds them, placed or
// kUnplaced: the messages that belong to one of them are merged into its
// payload there. To pair every cue, the caller hands pair_breaks()
// cues_to_pair() of the returned events and `plain_cues`.
MessageEvents scte35_events(const std::vector<CueInput>& messages, std::vector<Event>& plain_cues,
                            const MessagePlacement& placement, const MessageOptions& options);

// Every cue for pair_breaks(): `events.cues`, then `plain_cues`.
// pair_breaks() takes cues of one segment and one line in the order it is
// given them (src/PlaylistEvents.cpp), so that a cue_in that joins an
// EXT-X-CUE-OUT at that cue's line (Merging, above) comes before it.
std::vector<Event> cues_to_pair(const MessageEvents& events, const std::vector<Event>& plain_cues);

} // namespace timeline
