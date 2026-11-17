#pragma once

// The timeline panel's document matched to the open master, its playhead, its seek decision and
// the interval a click queries, ImGui-free (eval/phase3-survey/design.md §10; DECISIONS.md
// D-114 (b), (d)): written by WP12, the path among a playlist's equal bytes by WP12b (D-121 (d)),
// a relative open path made absolute there by WP12c (D-124 (g) 2), the playhead not known where
// its sequence isn't by WP13b (D-124 (c)).
//
// What the panel (src/TimelinePanel.h, WP13) needs between a loaded document
// (src/TimelineLoad.h) and the stream the player has open, as pure functions
// of what the player and the document give, and for the match of a relative
// open path the current directory, so that mirage_tests tests them (D-031):
//
// - The match (eval/phase3-survey/ui.md §2.3). The document's master is the
//   open one when the SHA-256 of the bytes the player's ManifestParser read
//   is the report's ladder.sha256. Its entries then correspond by their order
//   of EXT-X-STREAM-INF when both lists are as long and each entry agrees on
//   its BANDWIDTH and on its URL, the report's URI resolved by the player's
//   own parser against where the player read the master, so that the same
//   bytes served from elsewhere still correspond; else they correspond by
//   that URL, with a warning, the match staying one of bytes, so that the
//   same bytes served from elsewhere match there too. ManifestParser drops
//   an entry whose URI line doesn't follow its EXT-X-STREAM-INF at once
//   (src/ManifestParser.cpp), where VMAFLadder allows blank lines and
//   comments between them (src/VMAFLadder.h), so the order alone can mislead.
//   Otherwise the master is the open one by its address, the report's
//   ladder.location being where the player read it from or its ladder.url
//   what the player opened; it has changed since it was scored, which a
//   warning says, and the entries correspond by URL, each resolved by its own
//   parser, VMAFLadder's in the report and ManifestParser's in the player:
//   where the two resolve one URI apart, the entry matches nothing, and a
//   warning names it. Otherwise the stream may be one variant's own input
//   opened alone, as ui.md §2.3 matches a pair's report to a local file
//   (D-121 (d), Q89 (b)): the variant whose URL or playlist location is the
//   open path, else whose media playlist has the bytes the player read, by
//   the SHA-256 the document gives it. Where several have those bytes, as
//   D-069's rungs share one playlist text, the one is taken whose URL or
//   playlist location ends with the longest run of path components that the
//   open path or its final URL ends with, the first of the longest, so that
//   a rung's v270/index.m3u8 opened from another address matches the v270
//   rung; and where none shares more than its file name, the first of them
//   all, with WP12's warning. A URL's path components are its path's alone,
//   without its scheme, authority, query or fragment (RFC 3986 §3), a local
//   path's lexically normalized. The open path is the text the player was
//   given, and a local file's final URL the same text
//   (src/ManifestParser.cpp); where that is a local path relative to the
//   current directory, it is made absolute against the current directory,
//   lexically, before its components are compared, so that a rung's
//   playlist opened by a path relative to its own directory, index.m3u8 or
//   ./index.m3u8 given to a player started there, names its rung
//   (D-124 (g) 2, Q97 2 (b)). The current directory is all the match asks
//   of the system: nothing is looked up on disk, and a URL is compared as it
//   was given. The address matches above, the master's and the variant's,
//   compare the open path and its final URL as they were given, as WP12
//   built them: a reading pending the owner, whose alternative is a
//   relative open path made absolute against the current directory there
//   too. So a rung that the document names by an absolute local path, as it
//   does where the ladder was scored from its master's absolute path,
//   opened by a path relative to its own directory, is matched by its bytes
//   and its path, with the warning below, where by its absolute path it is
//   matched by its address without one; and once its bytes have changed
//   since the build, by neither, the document then counting as another
//   stream's, where by its absolute path it is matched by its address with
//   the warning that says so. Where the path picks the variant, a warning
//   says which and by what path, the whole of an absolute one from its
//   root, as one is given wherever several variants have those bytes, so
//   that the match carries a warning for the panel's chip even where the
//   path named the rung, a local file that the document names by a relative
//   path and the player by an absolute one, which the address match doesn't
//   take, among them, as built, which the owner kept
//   (D-124 (g) 3, Q97 3 (a)). Otherwise the document is another stream's,
//   which the panel shows only on the user's word.
// - A variant's clock (VariantClock): the frames of the variant on screen with
//   their PTS and reference frames, from the report (a pair's distorted
//   frames; the reference's own through the axis's media keys), and its
//   segments from the document's segment events, which carry every variant's
//   segments and discontinuities, so that the panel fetches nothing (design
//   §10).
// - The playhead (ui.md §2.4): the player's PTS of the frame on screen to the
//   variant's frame nearest it within the discontinuity sequence being
//   played, and that frame's reference frame on the axis. The sequence moves
//   to the next one when the PTS jumps as the aligner defines a
//   discontinuity, back, or forward by more than 10 s (src/VMAFAlign.h), and
//   is set from where a seek or a variant switch lands, a seek's where it
//   lands in one sequence for every lead of the first packet's DTS that the
//   seek decision allows (playhead_sought()). Where it isn't known, as when
//   the panel first sees the player's frames or after a seek whose landing
//   can't be told, and the PTS on screen lies in more than one sequence, as
//   inside an ad whose timestamps restart at the main content's origin, the
//   playhead isn't known either: none is given, and the sequences it may be
//   in are, until a seek, a switch or a jump of the PTS tells which, or the
//   PTS lies in one sequence only, so that the panel never marks a frame
//   other than the one on screen (D-124 (c), Q93 (c); track_playhead()).
// - The seek decision (ui.md §2.4; D-114 (b), Q37 (a)): whether the player's
//   seek to a frame's PTS shows that frame, by the seek as ui.md §1.6 reads
//   FFmpeg 8.1.1's hls.c and tests/test_hls_seek_behaviour.cpp pins it. The
//   player clamps the target into [start, start + duration - 0.5]
//   (src/Demuxer.cpp); the hls demuxer finds the segment by adding the
//   EXTINF to its first packet's DTS, whatever timestamps the segments carry,
//   moves a backward seek to that segment's start, and drops the packets
//   until a keyframe whose DTS is at or after it; the decode thread then
//   shows the first frame whose PTS is at or after the target, within half a
//   frame, or the last where every frame is before it (src/MediaSession.cpp's
//   show_from). On the test's segments that first DTS precedes the first PTS
//   by the decoder's reorder delay, a frame for MPEG-2 and two for libx264,
//   which carries a segment's last frame, or last two, into the next; the
//   decision takes the delay from the player (SeekClock::video_delay). The
//   segments are taken to start with their keyframes, and their DTS to lead
//   their PTS alike, one encoder having made them. A frame that isn't shown
//   so, an ad's whose timestamps restart among them, is "not reachable by a
//   seek", and the decision offers a seek to the start of the latest
//   segment, at or before the frame's, whose start a seek reaches, from which
//   playing reaches the frame: for an ad whose timestamps restart, the
//   segment before its discontinuity, as D-114 (b) has it; for a frame the
//   lead carries into the next segment, its own segment's start, D-114 (b)
//   extended beyond ads as built, which the owner kept (D-121 (c), Q88 (a);
//   decide_seek()).
//   The simulation takes each segment's first frame for its only keyframe.
//   Where a segment holds more, as bipbop's do (src/MediaSession.cpp), the
//   demuxer can start at a later keyframe of a segment whose first one it
//   drops, which the simulation doesn't see. Where the timestamps rise from
//   there to the frame, that can only turn a Seek into an Offer, a frame a
//   seek reaches being said not to be, never the reverse: where the found
//   segment's first keyframe passes, the two start there alike, and
//   otherwise the frames between that later keyframe and the simulation's
//   start lie before the target. Segments of several keyframes weren't
//   sought by the test: MEASUREMENT PENDING.
// - A click (design §10; D-114 (d), Q39 (b)): inside a detected drop of the
//   clicked curve, the document's attribution of the drop; elsewhere the
//   core's attribute() for the clicked frame's segment of the clicked curve's
//   variant, the frame alone where that variant has no segment map, so that
//   the panel and mirage_vmaf attribute answer from one query.
//
// What wasn't measured, and so rests on the reading above (ui.md §2.4, rule
// 4): a discontinuity whose timestamps jump forward, fMP4 segments, and a
// master with several variants, where the demuxer finds the segment in its
// default stream's playlist, taken here to line up with the playing
// variant's, as the rungs of D-069's ladders do: MEASUREMENT PENDING. So is
// whether the player's PTS through the master equal mirage_vmaf's through
// the media playlist, which the playhead and the seek take them to (the
// design's R16).
//
// Nothing here opens media, the network or a file, and nothing of ImGui, GL
// or GLFW (D-031); ManifestParser is called only to resolve a URI as the
// player does, and match_stream() asks for the current directory only to
// make a relative open path absolute (D-124 (g) 2). Unit-tested in
// tests/test_timeline_match.cpp.

#include "LadderReportRead.h"
#include "ManifestParser.h"
#include "TimelineLoad.h"
#include "TimelineModel.h"
#include "TimelineQuery.h"

extern "C" {
#include <libavutil/rational.h>
}

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace timeline {

// ── The document matched to the open stream (ui.md §2.3) ─────────────────────

// What the player has open, as the panel sees it.
struct OpenStream {
    // What the player was given to open: a URL or a local path, a relative
    // one being relative to the current directory, which the match takes it
    // from where it compares path components (D-124 (g) 2), and not where
    // it compares addresses, a reading pending the owner (this file's head).
    std::string path;
    // What its ManifestParser read (src/ManifestParser.h): where the master was
    // read from, after HTTP redirects, its bytes, and its EXT-X-STREAM-INF
    // entries in the parser's order, which MediaSession::playing_hls_variant()
    // indexes. All empty for an input that isn't a manifest.
    std::string                final_url;
    std::string                text;
    std::vector<VariantStream> variants;
};

// How the document was matched to the open stream, and which of its
// variants each of the player's entries plays.
struct StreamMatch {
    enum class By : std::uint8_t {
        MasterBytes,   // the master's bytes are the report's (ladder.sha256)
        Address,       // the master's address is the report's, its bytes not: changed since scored
        Variant,       // the open stream is one variant's own input: opened_variant
        None,          // another stream's document: the panel shows it only on the user's word
    };
    enum class Entries : std::uint8_t {
        Index,   // the player's entry i is the report's entry i
        Url,     // by their resolved URLs
        None,    // not matched: By::Variant or By::None
    };
    By      by      = By::None;
    Entries entries = Entries::None;
    // Per entry of OpenStream::variants: the index into Timeline::variants of
    // the variant it plays, -1 for one the document has no variant of (an
    // entry without video, or one that matched nothing).
    std::vector<int> variant_of;
    int opened_variant = -1;            // By::Variant: the variant opened alone
    std::vector<std::string> warnings;  // how the match fell short, for the panel's chip
};

// The match of the document (`timeline` and `scores`, its report) to `open`,
// as this file's head says.
StreamMatch match_stream(const Timeline& timeline, const LadderScores& scores, const OpenStream& open);
inline StreamMatch match_stream(const Snapshot& s, const OpenStream& open)
{
    return match_stream(s.timeline, s.scores, open);
}

// "master SHA-256", "master address", "variant", "none": how a match was made.
std::string_view match_name(StreamMatch::By by);

// ── A variant's clock ────────────────────────────────────────────────────────

// One frame of a variant with a PTS: a pair's scored distorted frame, or one
// of the reference's frames whose PTS the axis knows (D-073).
struct ClockFrame {
    std::int64_t index = 0;       // its position in the variant's input
    std::int64_t ref_index = 0;   // the reference frame it is paired with; the reference's: itself
    std::int64_t pts = 0;         // ticks of VariantClock::time_base
    int          sequence = 0;    // its discontinuity sequence in the report (D-057), from 0
};

// One segment of a variant, from its segment event of the document (the
// payload of src/PlaylistEvents.h's segment_event()).
struct ClockSegment {
    std::int64_t media_sequence = 0;
    std::int64_t discontinuity_sequence = 0;   // the playlist's
    double       playlist_time = 0;            // seconds: the EXTINF of the segments before it
    double       extinf = 0;                   // seconds
    std::int64_t ref_index = 0, ref_end = 0;   // its span on the reference's frames
    std::size_t  first = 0, last = 0;          // its frames, [first, last) in VariantClock::frames
};

// The frames and segments of one variant, for its playhead and its seeks.
struct VariantClock {
    int         variant = -1;                  // into Timeline::variants; -1: none
    int         pair = -1;                     // Variant::pair: -1 for the reference
    bool        playlist = false;              // it was read from a media playlist
    std::string segment_map = "ok";            // Variant::segment_map
    AVRational  time_base  = { 0, 1 };         // of the frames' PTS
    AVRational  frame_rate = { 0, 1 };         // as the report gives it; 0/1: unknown
    std::vector<ClockFrame>   frames;          // in the input's order, those with a PTS
    std::vector<ClockSegment> segments;        // in the playlist's order; none without a segment map

    // A frame's PTS in seconds, as the player's clock has it: pts x time base.
    double seconds(const ClockFrame& f) const;
};

// The clock of variant `variant` of `timeline`, `scores` being its report: a
// distorted variant's frames from its pair's scored frames, the reference's
// from the axis's media keys; empty for a variant that isn't there.
VariantClock variant_clock(const Timeline& timeline, const LadderScores& scores, int variant);
inline VariantClock variant_clock(const Snapshot& s, int variant)
{
    return variant_clock(s.timeline, s.scores, variant);
}

// ── The playhead (ui.md §2.4) ────────────────────────────────────────────────

struct SeekClock;   // the stream as the player's seek sees it (below, the seek decision)

// The playhead's state between frames: the sequence being played and the
// last PTS tracked; while the sequence isn't known, the sequences the last
// PTS tracked lay in, which nothing has told apart (D-124 (c)).
struct PlayheadState {
    int sequence = -1;                       // -1: not known
    std::optional<std::int64_t> last_pts;    // ticks of the clock's time base
    // The playhead not known (D-124 (c), Q93 (c)): the two or more sequences
    // with a frame at the last PTS tracked, ascending, which is why
    // track_playhead() gave no frame for it; empty otherwise.
    std::vector<int> unknown;
};

// Where the playhead is: a frame of the variant's clock and its reference frame.
struct PlayheadFrame {
    std::size_t  frame = 0;        // into VariantClock::frames
    std::int64_t index = 0;        // the frame's position in its input
    std::int64_t ref_index = 0;    // its reference frame: the playhead's place on the axis
    int          sequence = 0;
    double       distance = 0;     // seconds from the PTS asked to the frame's, signed
};

// The frame of `clock` nearest the player's `pts` (seconds) within the
// sequence `state` holds, the earlier of two as near, and `state` moved on.
// The sequence moves to the next one when `pts` is lower than the last PTS
// tracked, or higher by more than vmafalign::kForwardJump (D-046), and
// stays at the last one. Where it isn't known, as at first sight (a state
// as made) and after a seek or a switch whose landing isn't known
// (playhead_seeked() with -1), `pts` tells it where it lies (D-124 (c),
// Q93 (c)): it lies in each sequence with a frame within half a frame of
// it, and where that is one sequence only, that one; where it is none, the
// nearest frame's, as WP12 built it, which ends a playhead not known too,
// though D-124 (c) names no such teller: a reading pending the owner
// (src/TimelineMatch.cpp); and where it is two or more, the playhead isn't
// known: nullopt, `state.unknown` naming them, until a seek or a switch
// (playhead_seeked(), playhead_sought(), playhead_switched()), a jump of
// the PTS or a PTS that lies in one sequence only tells it. A jump tells it
// where, of the sequences that it leads those the last PTS lay in to, each
// to its next one as above, exactly one has a frame within half a frame of
// `pts`. nullopt too for a clock without frames or without a time base,
// `state.unknown` then empty.
std::optional<PlayheadFrame> track_playhead(const VariantClock& clock, PlayheadState& state, double pts);

// A seek the player made, to a frame of sequence `sequence` (as the seek
// decision's target or a landing gives it; -1: not known): the playhead
// follows from there, and where the sequence isn't known, track_playhead()
// is told it as it is at first sight (D-124 (c)).
void playhead_seeked(PlayheadState& state, int sequence);

// A seek the player made to `target` (PTS seconds) on the variant of
// `clock`, one the panel didn't ask for (the seek bar's, a key's, a
// keyframe step's): the playhead follows from the sequence of the frames
// simulate_seek() lands `target` on, which tells the panel the sequence
// (D-124 (c)), where they are of one sequence for every lead of the first
// packet's DTS that decide_seek() allows, from 0 to the largest of one
// frame, `seek.video_delay` frames and `seek.dts_lead`. Where the
// simulation shows no frame for one of them, or frames of two sequences
// for two, the seek tells nothing, the Demuxer not knowing the lead: the
// playhead follows from a sequence not known, as at first sight, so that
// the frame on screen tells it where its PTS lies in one sequence only
// (track_playhead()). So a seek to one of a libx264 segment's last two
// frames before an ad whose timestamps restart at the main content's
// origin, which with no lead shows that frame and with libx264's lead of
// two frames the first frame after the ad (D-121 (c)), tells nothing.
void playhead_sought(PlayheadState& state, const VariantClock& clock, const SeekClock& seek, double target);

// A variant switch that put the variant of `clock` on screen, its first
// frame at `pts` (seconds). The Demuxer repositions a switch with a seek by
// PTS on the position playing (av_seek_frame, src/Demuxer.cpp), so that
// the new variant's frames go on from where that seek lands: the playhead
// follows from the sequence of the frame simulate_seek() lands `pts` on,
// with no lead of the first packet's DTS, which tells the panel the
// sequence (D-124 (c)). No other lead is needed, unlike for a seek's
// target: `pts` is the PTS of the frame the switch put on screen, and a
// lead that lands the simulation elsewhere carries it on to a later
// segment's first frame, whose PTS is later, not the frame on screen.
// Where the simulation shows no frame, the playhead follows from the
// sequence `state` holds, which isn't known where it holds none. Inside an
// ad whose timestamps restart at the main content's origin, as D-069's
// does, that seek lands in the main content, which the player then plays
// again from there, and the playhead follows the main content's frame on
// screen: the Demuxer's doing, reported in Phase 3's PR, the Demuxer
// unchanged, which the owner chose (D-124 (e), Q95 (a)).
void playhead_switched(PlayheadState& state, const VariantClock& clock, const SeekClock& seek, double pts);

// ── The seek decision (ui.md §2.4; D-114 (b)) ────────────────────────────────

// The stream the player plays, as its seek sees it: what the panel fills
// from the player's Demuxer.
struct SeekClock {
    double start = 0;      // AVFormatContext::start_time in seconds, the Demuxer's media start
    double duration = 0;   // the Demuxer's duration() in seconds; <= 0: unknown
    // The frames the video's decoder reorders by: AVCodecParameters::
    // video_delay of the stream on screen, as Demuxer::video_codecpar() gives
    // it once avformat_find_stream_info() has run. The panel must fill it.
    // The hls demuxer counts the EXTINF from the first packet's DTS, which
    // the Demuxer doesn't know (ui.md §2.4); on
    // tests/test_hls_seek_behaviour.cpp's segments that DTS precedes the
    // first PTS by this many frames, one for MPEG-2 and two for libx264 as
    // D-069's ladders are encoded (D-080). decide_seek() allows a lead of at
    // least one frame, ui.md §2.4's minimum, so that left 0 it allows one,
    // and then seeks a libx264 segment's second-to-last frame by its PTS,
    // which shows the next segment's first frame instead.
    int video_delay = 0;
    // A lead of the first packet's DTS, in seconds, beyond video_delay's,
    // where the caller knows of one; 0: none.
    double dts_lead = 0;
};

// Where the player's seek to `target` (seconds) lands, for a first packet's
// DTS `lead` seconds before the start, by the reading of this file's head.
struct SeekLanding {
    enum class Kind : std::uint8_t {
        Shown,     // `frame` is the first frame shown
        Nothing,   // the demuxer drops every packet to the stream's end
        Failed,    // the demuxer refuses the target: the player reads on from where it was
        Unknown,   // a segment on the way has no frame with a PTS in the report
    };
    Kind        kind    = Kind::Unknown;
    std::size_t segment = 0;   // the segment the demuxer starts from (into VariantClock::segments)
    std::size_t frame   = 0;   // Shown: into VariantClock::frames
    bool        clamped = false;   // the player moved the target into the stream first
};
SeekLanding simulate_seek(const VariantClock& clock, const SeekClock& seek, double target, double lead);

// What the panel does to take the player to a frame.
struct SeekDecision {
    enum class Kind : std::uint8_t {
        Seek,    // s_seek_to(target) shows the frame, for every lead the decision allows
        Offer,   // the frame isn't reachable by a seek: s_seek_to(target) reaches the start of
                 // `segment`, from which playing reaches the frame (D-114 (b))
        None,    // neither: `why` says why
    };
    Kind        kind = Kind::None;
    double      target = 0;                 // Seek, Offer: the PTS in seconds to seek to
    std::optional<std::size_t> frame;       // the frame asked for, into VariantClock::frames
    std::optional<std::size_t> segment;     // Offer: the segment it seeks to, into ::segments
    std::string why;                        // Offer, None: why a seek to the frame doesn't reach it
};

// How the panel takes the player to reference frame `ref_index` on the
// variant of `clock`, the one on screen: its frame there is the last of its
// frames at or before `ref_index` (a point stands for the frames up to the
// next, D-081 (g)), or its first. A variant that isn't a playlist, a
// mezzanine opened alone, is sought as a file, by the frame's PTS; one whose
// segment map was refused gives None, since where its seek lands can't be
// told; a playlist's frame is sought by its PTS when simulate_seek() shows it
// for every lead of the first packet's DTS from 0 to the largest of one
// frame of the variant, `seek.video_delay` frames and `seek.dts_lead` (a
// frame as the report's frame rate for the variant gives it;
// `seek.dts_lead` alone where the report gives none), and otherwise the
// decision offers the latest segment start, at or before the frame's
// segment, that a seek reaches so: the segment before the discontinuity for
// an ad whose timestamps restart (D-114 (b)), and the frame's own segment's
// start for a frame the lead carries into the next segment, a segment's last
// frame, or its last two on libx264's segments, D-114 (b) extended beyond
// ads as built, which the owner kept (D-121 (c), Q88 (a)). A first DTS
// further ahead than that, which the test's encoders don't write, can make a
// Seek miss.
SeekDecision decide_seek(const VariantClock& clock, const SeekClock& seek, std::int64_t ref_index);

// ── A click (design §10; D-114 (d)) ──────────────────────────────────────────

// The interval a click at reference frame `ref_index` on `curve` queries
// outside a detected drop (D-114 (d), Q39 (b)): the span of the segment event
// that holds the frame, of the clicked curve's variant, a session's being
// the variant its span plays there; the frame alone, [ref_index,
// ref_index + 1), where that variant has no segment map or no segment of it
// holds the frame.
Interval click_interval(const Timeline& timeline, CurveRef curve, std::int64_t ref_index);

// What a click shows.
struct ClickAnswer {
    // The document's detected drop of `curve` that holds the frame, an index
    // into Snapshot::attributions; nullopt: none, the click's own query.
    std::optional<std::size_t> drop;
    Attribution attribution;
};

// Inside a detected drop of `curve`, the document's attribution of it, as
// the document gives it; elsewhere attribute() for click_interval(), with
// the margins the document's attributions were made with (DocInfo::overlap),
// so that it agrees with the document's drops, as built, which the owner
// kept (D-121 (b), Q87 (a)). mirage_vmaf attribute --interval queries with
// none without --margin-before or --margin-after (Q29's default): the two
// differ only for a document built with margins.
ClickAnswer click_answer(const Snapshot& snapshot, CurveRef curve, std::int64_t ref_index);

} // namespace timeline
