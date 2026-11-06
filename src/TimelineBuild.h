#pragma once

// The merge of every source's events into a Timeline: filled in by WP09a.
//
// build() is eval/phase3-survey/design.md §3.4's step 5 for a ladder without
// a session (DECISIONS.md D-072 to D-081): from the ladder report as the
// timeline reads it (src/LadderReportRead.h), each scored input's media
// playlist (src/PlaylistEvents.h) and its demux pass (VariantMedia, D-074),
// it makes the reference's axis (src/TimelineAxis.h), the variants, the
// pairs' curves and every source's events, each placed on the reference's
// frames by the design's §5.1 rules, and it lists in Timeline::warnings,
// with the reason, every anchor it can't place, so that nothing is left out
// silently. attribute_drops() is the same section's step 7: detection on the
// pairs' curves and the attribution of each drop (D-077, D-078). Core code
// (design §3.1): plain structs in and out, nothing read or opened here,
// libavutil's rationals at most. The adapter src/TimelineMedia.h reads the
// playlists and runs the demux pass, and src/TimelineCli.h, the subcommands,
// calls both. Unit-tested on inputs the tests build by hand
// (tests/test_timeline_build.cpp).
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
//   ad break (pair_breaks()'s warnings). This granularity is the owner's
//   D-099 (c). A warning about a pair names it by its number, the report's
//   pairs[].index, as the axis's warnings do. A variant the demux pass refused
//   keeps none of its segment-bound events, keyframes or corrupt packets
//   (D-081 (c), D-086): one warning says so, and its cues take no part in
//   the pairing. The SCTE-35 messages a variant's tags embed (CueInput) are
//   named in one warning per variant: no decoder runs in this build (design
//   §5.5, whose decoded messages WP05 and WP09c add), so none of them makes
//   an event yet. Each problem hls::parse_media_timeline found in a
//   playlist is a warning too, since it can change which segment a tag
//   belongs to.
// - Ad breaks: every cue of every variant with a segment map, placed or not
//   (kUnplaced still takes its turn), paired by pair_breaks(), the axis's
//   frame_after() ending a break at its declared duration (D-076, D-087).
// - Order: the events get their ids from assign_ids() and are put in
//   canonical order with the overlap index built (Timeline::build_index()),
//   so that identical inputs give identical timelines (design §4.7).
//
// The payloads of the events made here, in order (those of the playlist
// events, the plain cues and the ad breaks are src/PlaylistEvents.h's).
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
// input of scores.pairs[k]. A session's plan and its requests come in round
// 2 (design §14, WP09b).
struct BuildInputs {
    LadderScores             scores;
    std::vector<VariantRead> variants;
};

// The timeline of a ladder (see above): its axis, variants, curves and
// events, the events in canonical order with their ids and the overlap index
// built, and its warnings. Empty, with `err` saying why, when the inputs
// don't fit together: one VariantRead per variant and no other, each
// playlist one that parse_playlist_events() read, without an error, for the
// variant it is given for, and the pairs' numbers rising in the report's
// order within an int, as read_ladder_report() gives them (D-092 (b)).
// Otherwise `err` is empty, whatever the warnings.
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

// The drops detect() finds on each of `curves` with `detect`, each with the
// events attribute() finds for its interval with `query` (D-077, D-078):
// detected true, and the baseline and both magnitudes those of its drop
// (Q27 (c)), attribute() giving its points, minimum and mean. In the order
// of `curves`, and each curve's drops in its order. None when W and N aren't
// both given. Empty, with `err` saying why, when a curve isn't one the
// timeline has, or isn't one detect() takes, named then by its pair's
// number (pair_number(), src/TimelineDoc.h), or an option is one
// check_options() refuses; otherwise `err` is empty.
std::vector<Attribution> attribute_drops(const Timeline& timeline, const std::vector<CurveRef>& curves,
                                         const DetectOptions& detect, const QueryOptions& query,
                                         std::string& err);

} // namespace timeline
