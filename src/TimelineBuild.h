#pragma once

// The merge of every source's events into a Timeline: filled in by WP09a, a session's by WP09b,
// the decoded SCTE-35 messages by WP09c.
//
// build() is eval/phase3-survey/design.md §3.4's step 5 for a ladder without
// a session (DECISIONS.md D-072 to D-081): from the ladder report as the
// timeline reads it (src/LadderReportRead.h), each scored input's media
// playlist (src/PlaylistEvents.h) and its demux pass (VariantMedia, D-074),
// it makes the reference's axis (src/TimelineAxis.h), the variants, the
// pairs' curves and every source's events, each placed on the reference's
// frames by the design's §5.1 rules, the SCTE-35 messages the playlists'
// tags carry decoded and placed among them (src/Scte35Events.h; design §5.5;
// D-106, D-109), and it lists in Timeline::warnings, with the reason, every
// anchor it can't place, so that nothing is left out silently.
// add_session() and add_requests() are the section's step 6, a
// scripted session's (D-068, D-104, D-105), on the timeline build() made:
// the session's spans and its player events from its plan, then, once the
// session's files are fetched, its network events. attribute_drops() is
// step 7: detection on the pairs' curves, a session's drops being its
// pairs' drops clipped to its spans, and the attribution of each drop
// (D-077, D-078). Core code (design §3.1): plain structs in and out, nothing
// read or opened here, libavutil's rationals at most. The adapters
// src/TimelineMedia.h and src/SessionFetch.h read the playlists, run the
// demux pass and fetch a session's files, and src/TimelineCli.h, the
// subcommands, calls them all. Unit-tested on inputs the tests build by
// hand (tests/test_timeline_build.cpp).
//
// The rules.
//
// - Variants (design §4.5; D-075). Variant 0 is the reference; variant k + 1
//   is the distorted input of the report's k-th complete pair, which curve k
//   scores. A variant's pair is that pair's number, the report's own
//   pairs[].index, the plan's, which skips the number of a pair that failed
//   (D-092 (b)), and by which the document, the warnings and --curve name
//   the pair, while curve k keeps its place (src/TimelineDoc.h,
//   pair_number()). The pairs come in the plan's order, as
//   read_ladder_report() gives them, so that the curves' places follow the
//   pairs' numbers. A variant's entries are those the report gives it, none
//   for a mezzanine; its BANDWIDTH is the highest of theirs (D-081 (e)), and
//   its RESOLUTION and CODECS are those of the entry with that BANDWIDTH,
//   the first of equal ones in the master's order. Its two ranks are
//   counted among the master's video playlists, each URL once at the
//   highest BANDWIDTH of its video entries, whether or not the run scored
//   it: from the top, 1 plus the playlists of a higher BANDWIDTH; from the
//   bottom, 1 plus those of a lower one; so playlists of equal BANDWIDTH
//   share a rank. A variant without entries has no rank (0). Which entry
//   gives RESOLUTION and CODECS, and the set and the ties of the ranks,
//   D-075 and D-081 (e) left open; the owner's D-099 (c) keeps them as
//   written here.
// - Placement (design §5.1). A reference frame sits at itself. A distorted
//   frame sits at the reference frame it is paired with, and one that isn't
//   paired at the next paired frame of its input, by index (D-081 (d)); a
//   frame after its input's last paired frame can't be placed. A segment
//   spans from where its first paired frame sits (the reference's: its first
//   frame) to where the input's next paired frame after it sits, the start
//   of the next segment that is placed, as a point of a curve stands for
//   the reference frames up to the next point (D-081 (g)); where no paired
//   frame follows, to one past where its own last paired frame sits, as a
//   drop's interval ends at its curve's end. The design names no end for a
//   last segment, nor for one before a segment that isn't placed: D-099 (c)
//   keeps both as written here. A playlist tag, and a plain cue tag, sits
//   where the segment whose URI follows it starts. A keyframe and a corrupt
//   packet sit at their own frames (D-081 (h)), an unpaired frame of the
//   reference at itself and one of the distorted input at the next paired
//   frame (D-081 (d)), and a libvmaf context's start at its first pair's
//   reference frame.
// - What can't be placed goes to Timeline::warnings (design §5.1): a
//   segment none of whose frames is paired, or that the demux pass gave no
//   frame, with its tags and cues, one warning for the segment that names
//   each of them by its type and line; a tag after the last URI, which no
//   segment holds, one warning each; the keyframes, corrupt packets and
//   unpaired frames after their input's last paired frame, one warning for
//   each kind and input; a libvmaf context that starts at no scored frame of
//   its pair or off the axis, one warning each; and a cue_out that makes no
//   ad break, and an ad_break of no frames, which is kept (D-110 (c)), each
//   with a warning of pair_breaks()'s. This granularity is the owner's
//   D-099 (c). A warning about a pair names it by its number, the report's
//   pairs[].index, as the axis's warnings do. A variant the demux pass refused
//   keeps none of its segment-bound events, keyframes or corrupt packets
//   (D-081 (c), D-086): one warning says so, and its cues take no part in
//   the pairing; its SCTE-35 messages make no event either, and one warning
//   of scte35_events() says how many there are. Each problem
//   hls::parse_media_timeline found in a playlist is a warning too, since it
//   can change which segment a tag belongs to.
// - Decoded messages (design §5.5; src/Scte35Events.h). Once every variant's
//   plain cues are made, placed or not, every message the playlists' tags
//   embed (PlaylistEvents::messages), in variant order and within a variant
//   in line order (playlist_messages()), is decoded, classed and placed by
//   scte35_events() under BuildInputs::messages, --cue-types and --bad-crc
//   (D-106 (b), (c)), through the variants as build() reads them: [0] the
//   reference, [k + 1] the distorted input of the report's k-th complete
//   pair, each with its playlist and its demux pass. A message that belongs
//   to a plain cue is merged into that cue's payload (D-106 (d)), and the
//   others make cues of their own, one cue_out per signal (D-109 (a)), and
//   splice_point, message and cancel events, each of the carrying variant;
//   what can't be decoded or placed is a warning of scte35_events(). Its
//   warnings, of every variant, come after the variants' and the pairs',
//   and before pair_breaks()': a choice of this package's that sets a
//   document's bytes, pending the owner, Q77 (docs/TIMELINE_FORMAT.md); or
//   each variant's after its own warnings. A ladder whose playlists carry no
//   message, as D-069's carry plain cue tags only
//   (eval/phase3-survey/testgen.md §2.3), has the events and the warnings it
//   had before messages were decoded, whatever options
//   check_message_options() takes.
// - Ad breaks: every cue, plain and decoded, of every variant with a segment
//   map, placed or not (kUnplaced still takes its turn), paired by
//   pair_breaks() in the order cues_to_pair() gives, the decoded first, so
//   that a cue_in that joins a plain cue_out at its line ends the break
//   before it, the axis's frame_after() ending a break at its declared
//   duration (D-076, D-087, D-109 (b), (d), (e)). Of the cues, those placed
//   are events, one that isn't placed only taking its turn: the plain ones
//   first, then the decoded ones, so that where a plain cue and a decoded
//   one share a frame, a type and a variant, as an EXT-X-CUE-IN and a cue-in
//   message of its segment do (D-109 (e)), assign_ids() numbers the plain
//   one first, whatever their lines: a choice of this package's that sets a
//   document's ids, pending the owner, Q77 (docs/TIMELINE_FORMAT.md); or the
//   decoded one first, or the one of the earlier line.
// - Order: the events get their ids from assign_ids() and are put in
//   canonical order with the overlap index built (Timeline::build_index()),
//   so that identical inputs give identical timelines (design §4.7), the
//   fetch's measured times and wall clock apart.
// - A session (add_session(), add_requests(); src/SessionPlan.h): its spans
//   on the reference's frames through the segments build() placed (design
//   §5.1), its player events and, after its fetch, its network events, each
//   carrying the session's index (Event::session), placed by D-105 (c); a
//   session adds events of the player and network sources alone, which
//   assign_ids() numbers apart from the ladder's, so that the ladder's
//   events keep their ids. Its fetch comes after build(), from the segments
//   build() placed (fetch_files()), so a session isn't one of BuildInputs.
//
// The payloads of the events made here, in order (those of the playlist
// events, the plain cues and the ad breaks are src/PlaylistEvents.h's, those
// of the decoded messages' events, and the records they add to a plain cue,
// src/Scte35Events.h's, D-109 (e)).
// Their names are this package's, since the design gives the vmaf and
// decoder events none, and the owner's D-099 (c) keeps them; the vmaf ones
// take the ladder report's own names (alignment.vmaf_contexts,
// alignment.failures):
//   context_start   context: the libvmaf context's position among its
//                   pair's, from 0; the first context starts every pair and
//                   makes no event (D-037), so the first event's is 1;
//                   first_index: the index of its first frame pair in the
//                   report's frames
//   unpaired_frame  input: "reference" or "distorted"; index: its position
//                   in that input; pts: its PTS in seconds, as the report
//                   gives it, null without one; reason: as the report names
//                   it
//   keyframe        index: its frame's position in its input
//   corrupt_packet  index: as a keyframe's; next_boundary: the segment that
//                   follows the one holding it, {media_sequence, index: its
//                   first frame's position in the input, ref_index: where
//                   its segment event starts, null when it isn't placed,
//                   discontinuity: whether an EXT-X-DISCONTINUITY precedes
//                   it}, or null in the last segment and in an input that
//                   isn't a playlist (D-081 (h))
// Each vmaf event of pair k carries pair k's distorted variant, so that only
// that pair's curve lists it (src/TimelineQuery.h), and no segment. A
// keyframe and a corrupt packet carry the media sequence number of the
// segment that holds their frame, where their input has segments.

#include "LadderReportRead.h"
#include "PlaylistEvents.h"
#include "Scte35Events.h"
#include "SessionPlan.h"
#include "TimelineDetect.h"
#include "TimelineModel.h"
#include "TimelineQuery.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace timeline {

// One segment of a variant as the timeline build's demux pass read it
// (DECISIONS.md D-074): its frames, by their positions in the input, and
// its bytes.
struct SegmentFrames {
    std::int64_t seq         = 0;   // media sequence number
    std::int64_t first_index = 0;   // its first frame's position in the input, from 0
    std::int64_t frames      = 0;
    std::int64_t bytes       = 0;   // as read
};

// What the demux pass gives of one variant (D-074): its segments' frames,
// the frames of its key and corrupt packets, and every frame's PTS. The
// adapter src/TimelineMedia.h returns it; a test can build one by hand.
struct VariantMedia {
    std::vector<SegmentFrames> segments;     // in the playlist's order
    std::vector<std::int64_t>  keyframes;    // positions in the input of the frames of key packets
    std::vector<std::int64_t>  corrupt;      // the same for corrupt packets (AV_PKT_FLAG_CORRUPT)
    std::vector<std::int64_t>  pts;          // per frame, ticks of the input's time base
    std::string                refused;      // why it gets no segment map (D-081 (c)), or empty
};

// The event types made here, of design §5.9's vocabulary.
inline constexpr std::string_view kTypeContextStart  = "context_start";    // vmaf
inline constexpr std::string_view kTypeUnpairedFrame = "unpaired_frame";   // vmaf
inline constexpr std::string_view kTypeKeyframe      = "keyframe";         // decoder
inline constexpr std::string_view kTypeCorruptPacket = "corrupt_packet";   // decoder

// One scored input as the timeline build read it.
struct VariantRead {
    // Its media playlist, read again (P-D) and parsed by
    // parse_playlist_events() with this variant's index; nullopt for a
    // reference that isn't a playlist, a mezzanine (design §5.2).
    std::optional<PlaylistEvents> playlist;
    std::string playlist_location;   // where the playlist was read from, after redirects (P-D)
    std::string playlist_sha256;     // of its bytes as read; both empty without a playlist
    VariantMedia media;              // its demux pass (D-074)
};

// What build() merges: the ladder report as read, and one VariantRead per
// variant in build()'s order, [0] the reference and [k + 1] the distorted
// input of scores.pairs[k]; and what becomes of the SCTE-35 messages their
// playlists carry. A session's plan and its requests come after build(), to
// add_session() and add_requests(): its fetch lists the segments build()
// placed (fetch_files(), src/SessionPlan.h).
struct BuildInputs {
    LadderScores             scores;
    std::vector<VariantRead> variants;
    // Which time_signals are cues, and what becomes of a message whose
    // CRC_32 fails: --cue-types and --bad-crc (D-106 (b), (c)), D-106's
    // defaults unless they are given.
    MessageOptions           messages;
};

// The timeline of a ladder (see above): its axis, variants, curves and
// events, the events in canonical order with their ids and the overlap index
// built, and its warnings. Empty, with `err` saying why, when the inputs
// don't fit together: one VariantRead per variant and no other, each
// playlist one that parse_playlist_events() read, without an error, for the
// variant it is given for, the pairs' numbers rising in the report's order
// within an int, as read_ladder_report() gives them (D-092 (b)), and message
// options that check_message_options() takes (src/Scte35Events.h), its text
// then `err`. Otherwise `err` is empty, whatever the warnings.
//
// It places through the pairs as read_ladder_report() checks them: each
// pair's reference and distorted positions rise from frame to frame, each
// reference position is one of the frames its pair read, and so of the
// axis's, and each libvmaf context starts at one of the pair's scored
// frames. Scores built by hand need not hold that: a context that starts at
// no scored frame, or at a reference frame off the axis, is then a warning
// (above), and a scored frame off the axis, which build_axis() warns of,
// places nothing, so that what would sit at it, or a segment that would end
// there, is left out with a warning.
Timeline build(const BuildInputs& in, std::string& err);

// Each complete pair's curve, in the report's order: curve k on variant
// k + 1, its scored frames' reference positions and scores, and the
// reference frame of each libvmaf context's first pair, the first context's
// included (design §4.4); a context that starts at no scored frame, which
// read_ladder_report() refuses, has none and is left out.
std::vector<Curve> pair_curves(const LadderScores& scores);

// Gives `timeline`, read back from a document (src/TimelineDoc.h, which
// leaves the curves to the caller), the curves of `scores`, the ladder report
// it names: curve k from the report's k-th complete pair, scores.pairs[k],
// on the variant whose pair is that pair's number, pairs[k].index (D-092
// (b)). As the report lists its complete pairs in the plan's order, curve k
// is the k-th of the document's pairs in the order of their numbers, the
// place from_json() gives a curve reference of that pair. Returns false,
// with `err` saying why, naming each pair by its number, and `timeline`
// unchanged, when the document's variants and the report's complete pairs
// don't name each other one for one, or the report's pairs don't come in
// the plan's order.
bool attach_curves(Timeline& timeline, const LadderScores& scores, std::string& err);

// Every pair's curve of `timeline`, in order: what detection runs on for a
// ladder (D-077).
std::vector<CurveRef> pair_curve_refs(const Timeline& timeline);

// Every curve of `timeline`: each pair's, in order (pair_curve_refs()), then
// each session's, by its place in Timeline::sessions: what mirage_vmaf
// timeline runs detection on, and attribute answers without --curve, for a
// ladder with a session, as the owner kept them (D-111 (f), (g)).
std::vector<CurveRef> curve_refs(const Timeline& timeline);

// The drops of each of `curves`, each with the events attribute() finds for
// its interval with `query` (D-077, D-078): a pair's curve's, those detect()
// finds on it with `detect`; a session's, its pairs' drops clipped to the
// spans that played them under design §6.3's rules (session_drops(),
// src/SessionPlan.h; D-077), none over a span that isn't scored (D-105 (b)),
// listed for the session's curve, whose scope is the session's own events,
// the reference's and the played variant's in each span (D-078). Each with
// detected true, and the baseline and both magnitudes those of its drop
// (Q27 (c)), attribute() giving its points, minimum and mean, which for a
// session's drop are those of its pair's low points in its interval. In the
// order of `curves`, and each curve's drops in its order. None when W and N
// aren't both given. Empty, with `err` saying why, when a curve isn't one
// the timeline has, or isn't one detect() or session_drops() takes, named
// then by its pair's number (pair_number(), src/TimelineDoc.h) or by its
// session's id, or an option is one check_options() refuses; otherwise
// `err` is empty.
std::vector<Attribution> attribute_drops(const Timeline& timeline, const std::vector<CurveRef>& curves,
                                         const DetectOptions& detect, const QueryOptions& query,
                                         std::string& err);

// ── A scripted session (design §3.4, step 6; D-068) ──────────────────────────

// Adds the session of `plan` to `timeline`, a ladder's timeline as build()
// gives it, the master's entries being `entries` (LadderScores::entries):
// its spans on the reference's frames (make_session(), src/SessionPlan.h;
// D-105 (a), (b)); its id, its place among the timeline's sessions as text,
// "0", by which --curve session:<id> names it (design §7), while an event's
// session and the document's curve references count sessions by place
// (src/TimelineDoc.h): design §8 gives the id no form, so this one is
// WP09b's, which the owner kept (D-111 (a)); no wall clock anchor until
// add_requests() gives it one; and its player events, a variant_span over
// each span and a variant_switch where the entry played changes
// (session_events(); design §5.6, D-108 (b)). Then every event gets its id
// again (assign_ids(), which leaves the ladder's events' ids as they were)
// and the index is built again. A timeline holds one session at most
// (D-072). Returns false, with `err` saying why and `timeline` unchanged,
// for a plan that make_session() refuses (an entry the ladder doesn't have
// or whose variant isn't scored or has no segment map, a segment the
// timeline doesn't have, a gap or an overlap, each named) or for a second
// session; otherwise `err` is empty.
// The session's files aren't fetched here: fetch_files() lists them,
// src/SessionFetch.h fetches them and add_requests() adds what it returns.
bool add_session(Timeline& timeline, const std::vector<MasterEntry>& entries, const SessionPlan& plan,
                 std::string& err);

// Adds to session `session` of `timeline` its fetch: `files` as
// fetch_session() returns them (src/SessionFetch.h), each with NetworkLogger's
// rows, and `anchor_utc`, the UTC time the fetch started, which becomes the
// session's wall clock anchor (Session::wallclock_anchor_utc; D-073), none
// when it is empty. Each file makes one network event (request_events(),
// src/SessionPlan.h; D-104 (a)), a segment_request over its segment's span
// or a playlist_request at the first frame of the first span that plays its
// variant, the master's at the session's first frame (D-105 (c)), with its
// wall clock, its first row's start; a file whose event can't be made is a
// warning in Timeline::warnings. With an empty `anchor_utc` no file makes an
// event, since a request's wall clock counts from its session's anchor and
// a document holds neither a request without a wall clock nor a wall clock
// in a session without an anchor (src/TimelineDoc.h): one warning then says
// how many requests had rows, and the session keeps no anchor. fetch_session()
// gives an empty anchor only with no file, its fetch not started, so a
// document of what this gives reads back either way. Then every event gets
// its id again and the index is built again, as add_session() does. For a
// session the timeline doesn't have, nothing but request_events()'s warning.
void add_requests(Timeline& timeline, int session, const std::vector<Request>& files,
                  const std::string& anchor_utc);

} // namespace timeline
