#pragma once

// Decoded SCTE-35 messages as events, at their anchors (DECISIONS.md D-070,
// D-106, D-109, D-110; WP05 of eval/phase3-survey/design.md §14, §5.1, §5.5,
// and its follow-ups WP05b and WP05c, the owner's answers to what WP05 and
// WP05b left open). Every message that reaches it as a CueInput
// (src/PlaylistEvents.h), from any carrier, is decoded (src/Scte35.h),
// classed, placed on the reference's frames by its anchor's kind, merged
// into the plain cue it belongs to, and returned as events: the cues for
// WP04's pair_breaks(), which pairs every cue, plain or decoded (D-076,
// D-087), and the splice points, messages and cancellations to add to the
// timeline. Core code (design §3.1): plain structs in and out, nothing read
// or opened, libavutil at most. WP09c calls it from the build
// (src/TimelineBuild.h), before pair_breaks(), with --cue-types and
// --bad-crc (Q15, Q16) as MessageOptions. Unit-tested on sections the tests
// build (tests/Scte35TestSections.h, tests/test_scte35_events.cpp; D-107).
//
// The rules.
//
// - Text. A playlist tag's message is the text as written: hex for an
//   EXT-X-DATERANGE's SCTE35-CMD, SCTE35-OUT and SCTE35-IN (RFC 8216
//   §4.3.2.7), base64 with its padding for #EXT-OATCLS-SCTE35 and a cue
//   tag's SCTE35= (scte35.md §2), except a value that starts with 0x or 0X,
//   which is read as hex, as one vendor prints an OATCLS (scte35.md §2.4):
//   base64 of a section never starts so, since a section's first byte,
//   0xFC, makes its first character '/' (D-109 (e)). Any other carrier gives
//   the section's bytes.
// - Classes (D-106 (b), Q15 (a)). A splice_insert is a cue_out or a cue_in
//   by its out_of_network_indicator, a cancel when its
//   splice_event_cancel_indicator is set. A time_signal is a cue_out when
//   one of its segmentation descriptors that isn't cancelled has a Start
//   type of MessageOptions::cue_types, a cue_in when one has an End type of
//   them, and both when it has both: then the cue_in comes first, an end and
//   a start at one splice point being the end of one segment and the start
//   of the next (D-109 (e)). A segmentation descriptor whose cancel
//   indicator is set makes a cancel, in any command. Every other decoded
//   message is a `message`. The roles are Table 23's names: a type whose
//   name ends in Start or End (segmentation_role(), D-109 (e)); the default
//   cue_types are D-106 (b)'s, 0x22 to 0x23, 0x30 to 0x37 and 0x44 to 0x47.
//   The levels of SCTE 35's hierarchy of advertising segments count alike, a
//   Break above its Placement Opportunities and Ad Blocks, so that a Break
//   that declares a duration makes an ad_break around those of the levels
//   below it: an exception to D-087, by which no ad_break spans another
//   (D-109 (e)).
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
//   or it has no playlist (both D-109 (e)). A message whose own position
//   isn't found, by its tag or by its PTS, is still read: its cues are
//   returned unplaced, with their segment where it is known, and its splice
//   times are searched in its sequence where that is known. A period-time
//   anchor goes to the warnings: no DASH timeline is scored (design §5.5).
//   The message an EXT-X-CUE-OUT-CONT carries in its SCTE35= makes no
//   splice_point, and no warning is written of its anchors, which its record
//   still has (D-109 (c)): it repeats its break's message, whose splice time
//   the break's own message places, and a repeat's splice time lies before
//   its tag's segment by its nature.
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
//   8216 §4.3.2.7.1), of its own tag alone (D-109 (e)), since the tags of
//   one ID share START-DATE, the break's start: an SCTE35-IN's START-DATE is
//   recorded as the range's start, and set against nothing. A cue-in message
//   that its tag's cue_out takes (D-110 (b), below) writes none of these
//   warnings of its anchors, its in point and its splice times, which lie
//   outside the tag's segment by their nature where its break outlasts the
//   segment, nor that a date of its tag can't be set against the segment:
//   they are recorded all the same, in_segment among them, as a
//   CUE-OUT-CONT's message's are, and its splice times still make
//   splice_points (D-110 (d)). A splice time of such a message that isn't
//   placed still writes design §5.1's warning that its splice_point is left
//   out (below), one after the last frame of its tag's discontinuity
//   sequence among them, as an In Point's at the return to content is in a
//   stitched break: D-110 (d) names the check against the tag's segment,
//   and a CUE-OUT-CONT's message, which makes no splice_point, writes
//   neither (D-109 (c)). As built, pending the owner.
// - Merging (D-106 (d), Q44 (e)). A message a cue tag embeds (its SCTE35=
//   attribute: forms "CUE-OUT SCTE35", "CUE-OUT-CONT SCTE35" and "CUE-IN
//   SCTE35") is that tag's: it goes into the payload of the tag's plain cue,
//   whatever its class, and makes no cue of its own; so does a cue_out
//   message from any other carrier whose segment is known (an OATCLS, a
//   DATERANGE, a section read from the media with its segment) in the same
//   segment of the same variant as a plain EXT-X-CUE-OUT, which takes it
//   into its payload, the first such cue by line when a segment has two,
//   and declares the message's durations where it declares none of its
//   own.
// - One cue_out per signal (D-109 (a), Q63 option 2). Without a plain
//   EXT-X-CUE-OUT in their segment, the cue_out messages of one variant and
//   one segment that share their section's bytes, their splice_event_id (an
//   Out Point's) or a segmentation_event_id (of a descriptor that makes each
//   a cue_out, a Start type of the cue types) are one signal written more
//   than once: an OATCLS and a DATERANGE of one splice_insert, or the
//   DATERANGE tags RFC 8216 §4.3.2.7.1 writes for one section with several
//   segmentation types. The first of them in the messages' order makes the
//   one cue_out, which takes the others' records into its payload, and the
//   durations it lacks from them, as a plain cue takes a message; their
//   splice times make one splice_point each, the first message's that gives
//   it, so that a time the others repeat makes none and writes no warning.
//   Two cue_out messages that share none of the three, or that are of two
//   segments, are two cue_outs, and a message whose segment isn't known is
//   a signal of its own. The cue_out messages that a plain EXT-X-CUE-OUT
//   takes are signals in the same three ways, whose splice times make one
//   splice_point each too (D-110 (e)).
// - A tag's cue-out and cue-in messages (D-109 (b), D-110 (a), (b), Q64,
//   Q68 and Q69 option 2). An EXT-X-DATERANGE's cue-in message, of the class
//   cue_in alone, in the tag of a cue-out message, whichever of SCTE35-CMD,
//   SCTE35-OUT and SCTE35-IN carries each and in either order of the two,
//   makes no cue_in of its own: the cue_out that takes the tag's first
//   cue-out message, its own, its signal's or a plain one, takes the cue-in
//   message's record into its payload too, so that no ad_break of no frames
//   comes of the two. That cue_out holds the cue-in message only where the
//   tag itself declares DURATION or END-DATE, a duration finite and from 0,
//   and no plain EXT-X-CUE-OUT takes its messages: it then has the member
//   kHeldCueInLine (src/PlaylistEvents.h), by which pair_breaks() ends its
//   break at its declared duration, by D-076's order, the tag's DURATION or
//   END-DATE first, and no later cue_in ends it. Otherwise it pairs as
//   D-076 has it: the next cue_in ends it, and without one its declared
//   durations do, in D-076's order, so that a tag that declares no duration
//   ends at the cue_in after it, and a plain EXT-X-CUE-OUT that takes the
//   tag's messages at the next EXT-X-CUE-IN (D-110 (a)). A tag's duration
//   that no AVRational holds exactly, which pair_breaks() can't end a break
//   at, counts as declared all the same: its cue_out holds the cue-in
//   message, and pair_breaks() makes no ad_break of it, with its warning,
//   the cue_in after it ending nothing (declares(); as built, pending the
//   owner). A cue-in message whose tag has no cue-out message that makes a
//   cue_out (it doesn't decode, its CRC policy drops it, it isn't a
//   cue-out), or whose own classes aren't cue_in alone, is a cue_in of its
//   own, as one of a tag of its own (D-110 (f)).
// - One cue_in per signal (D-110 (e), Q72 option 2). The cue-in messages
//   that make a cue_in, every one but a cue tag's own and those their tags'
//   cue_outs take (above), whether a plain EXT-X-CUE-OUT takes their cue_out
//   part or not, are one signal where they are of one variant and one
//   segment and share their section's bytes, an In Point's splice_event_id
//   or a segmentation_event_id (of a descriptor that makes each a cue_in, an
//   End type of the cue types, not cancelled), and where they are cue-out
//   messages of one signal, whose cue_in is the first of its messages' that
//   ends a break (D-110 (f)). Each signal makes one cue_in, its first
//   message's in the messages' order, which takes the others' records into
//   `messages`, none of their durations; their splice times make one
//   splice_point each, the first message's that gives it. A cue-in message
//   in an EXT-X-CUE-IN's segment is still a cue_in of its own, since D-106
//   (d) names the cue-out only (D-109 (e)). A cue_in whose signal has a
//   message with the classes cue_in and cue_out, whose cue_out part an
//   EXT-X-CUE-OUT of its segment takes, or the cue_out of its signal of
//   cue-out messages, joins that cue_out, the first by line if there are
//   several: it ends the break before that cue_out's starts in either order
//   of their tags, MediaLive writing the message's tag before the
//   EXT-X-CUE-OUT and Wowza after it (scte35.md §2.4). It takes the
//   cue_out's line where its own is later or null (D-109 (e)), and the cues
//   go to pair_breaks() in the order cues_to_pair() gives, so that the
//   pairing takes it first.
// - Every other message is an event of its own class.
// - A malformed EXT-X-DATERANGE (D-106 (e), Q47 option 2):
//   parse_playlist_events() hands on each of its SCTE35-* attributes that is
//   a well-formed hexadecimal-sequence, and keeps the tag as a `daterange`
//   event with its error; such a message is decoded and placed by its
//   position with a warning, and none of the tag's other attributes, its
//   dates and durations, is read (D-109 (e)).
// - What can't be decoded or placed is a warning with its reason, never
//   dropped silently (design §5.1): text that isn't base64 or hex, a section
//   that doesn't decode or is encrypted, or whose table_id isn't 0xFC or
//   whose protocol_version isn't 0 (src/Scte35.h), which makes no event
//   (D-109 (e)), the messages of a variant whose demux pass was refused
//   (D-081 (c), D-086), one warning per variant, a tag after the last
//   segment's URI, a segment that isn't placed, a media PTS that isn't
//   placed, a splice time outside its sequence. A cue that isn't placed is
//   still returned, its ref_index kUnplaced, so that it takes its turn in
//   the pairing, as build() hands pair_breaks() the plain cues; a splice
//   point, a message or a cancel that isn't placed is left out.
//
// The owner's answers to what WP05 left open (D-109, Q63 to Q67), each in
// the rules above and where the code makes it: (a) one cue_out per signal,
// and one splice_point per splice time; (b) an EXT-X-DATERANGE's SCTE35-IN
// folded into the cue_out of the SCTE35-OUT of its tag, whose break ends at
// its declared duration; (c) no splice_point, and no warning of its anchors,
// from the message of an EXT-X-CUE-OUT-CONT; (d) each duration a message
// gives in 90 kHz ticks written in its ticks too, beside its seconds, which
// pair_breaks() reads first; (e) WP05's other choices as built, the levels
// of SCTE 35's hierarchy of advertising segments counted alike among them,
// an exception to D-087 (Classes, above), and of several segmentation
// descriptors of a cue's role, the first that gives a duration in the
// loop's order declaring segmentation_duration.
//
// The owner's answers to what WP05b left open (D-110, Q68 to Q73), each in
// the rules above and where the code makes it: (a) a cue_out holds its
// tag's cue-in message only where the tag declares DURATION or END-DATE and
// no plain EXT-X-CUE-OUT takes its messages, and otherwise pairs as D-076
// has it (fold(), and pair_breaks()), a duration that no AVRational holds
// exactly declared too, as built, pending the owner (declares()); (b) every
// cue-in message of an EXT-X-DATERANGE goes into the cue_out of the tag's
// cue-out message, whichever attribute carries each (link()); (c) an
// ad_break of no frames is kept, with a warning (pair_breaks()); (d) none of
// D-106 (a)'s warnings of the anchors of such a cue-in message, which are
// recorded, a splice time of it that isn't placed still writing design
// §5.1's warning, as built, pending the owner (turn(), splice_points()); (e)
// one splice_point per splice time of a signal that a plain EXT-X-CUE-OUT
// takes, and one cue_in per signal of cue-in messages (link(), in_cue());
// (f) WP05b's other choices as built: of D-109 (a)'s three ways, the
// segmentation_event_id of a descriptor that makes each message a cue_out,
// a Start type of the cue types, not cancelled; a signal's cue_out its
// first message's, taking the durations it has null from the others; a
// signal's cue_in the first of its messages' that ends a break, at the
// cue_out's line where its own is later; a cue-in message of a tag whose
// cue-out message makes no cue_out, or whose classes aren't cue_in alone, a
// cue_in of its own; a held break's ad_break with the tag's line as its
// cue_in_line and the duration's name as its end, its cue_out a member
// held_cue_in_line; a CUE-OUT-CONT's message with its anchors; pair_breaks()
// reading a ticks member beside every duration, a whole number from 0; and
// the ticks members, null beside a null duration of a cue of its own.
//
// The events, each of source scte35, of the carrying variant, with its
// segment's media sequence number and the media key of its frame, no id
// (build() gives every event its id). Payloads, in order, a value null
// where its source doesn't give it, their layout and names D-109 (e)'s:
//   cue_out, cue_in  line: the tag's, null for another carrier, and for a
//                    cue_in that joins a cue_out (One cue_in per signal,
//                    above) that cue's where its own is later or null;
//                    form: the CueInput's; raw: the tag's line as written,
//                    null for another carrier; the durations, in seconds, by
//                    kDurationNames: duration (an EXT-X-DATERANGE's
//                    DURATION), end_date_duration (its END-DATE less its
//                    START-DATE), planned_duration (its PLANNED-DURATION),
//                    break_duration (the splice_insert's), then
//                    break_duration_ticks, its 90 kHz ticks as a whole
//                    number (src/PlaylistEvents.h's kTicksSuffix; D-109
//                    (d)), segmentation_duration (that of the first
//                    segmentation descriptor of a cue type of the cue's role
//                    that gives one), then segmentation_duration_ticks; then
//                    the message's record: classes, crc_valid, message,
//                    anchors; a cue that takes more messages (D-109 (a),
//                    D-110 (b), (e)) has after them `messages` and, a
//                    cue_out holding its tag's cue-in message,
//                    held_cue_in_line, as below
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
// duration), with its ticks beside it (D-109 (d)). A signal's cue_out takes
// the records of its other messages in the same way, in the messages'
// order, and the durations it has null from the first that gives each
// (D-109 (a)). A cue_out that takes a cue-in message with its tag's cue-out
// message has its record in `messages` after the cue-out message's (first,
// for the cue_out of that cue-out message, whose own record is the
// cue_out's), and, holding it (D-110 (a)), gets once, after the members it
// has then, held_cue_in_line: the line of the first such tag (D-109 (b)).
// A signal's cue_in takes the records of its other messages in the same
// way, in the messages' order, and none of their durations (D-110 (e)).

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
// a signal's cue_out at its first message (D-109 (a)), and a signal's
// cue_in at its first message or, where that is earlier, at the first
// message of the signal whose cue_out it joins, before that cue_out (D-110
// (e)). `plain_cues` are the plain cue tags'
// events of every variant as the build holds them, placed or kUnplaced: the
// messages that belong to one of them are merged into its payload there. To
// pair every cue, the caller hands pair_breaks() cues_to_pair() of the
// returned events and `plain_cues`.
MessageEvents scte35_events(const std::vector<CueInput>& messages, std::vector<Event>& plain_cues,
                            const MessagePlacement& placement, const MessageOptions& options);

// Every cue for pair_breaks(): `events.cues`, then `plain_cues`.
// pair_breaks() takes cues of one segment and one line in the order it is
// given them (src/PlaylistEvents.cpp), so that a cue_in that joins an
// EXT-X-CUE-OUT, or its signal's cue_out, at that cue's line (Merging,
// above) comes before it.
std::vector<Event> cues_to_pair(const MessageEvents& events, const std::vector<Event>& plain_cues);

} // namespace timeline
