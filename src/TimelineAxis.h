#pragma once

// Media keys and seconds on the reference's frames (DECISIONS.md D-073): filled in by WP01.
//
// Events are placed and compared on the reference's frames, as integers; the
// seconds their offsets are printed in are the axis's
// (eval/phase3-survey/design.md §4.2). build_axis() makes the axis of a
// ladder report:
//
// - Media keys. Every reference frame's key is the report's reference
//   sequence it is in (D-057) and its PTS: the report's where a pair scored
//   it, the demux pass's where none did (design §5.2).
// - Seconds. Where the reference has a segment map (the demux pass's, D-074)
//   that fits its playlist, a frame is at its playlist time: the EXTINF of
//   the segments the playlist lists before its segment, plus its PTS's
//   distance from the PTS of its segment's first frame. Where it has none (a
//   reference that isn't a playlist, a map refused under D-081 (c)), a frame
//   is at its index times the frame duration, the frame grid.
// - The check (design §4.2). Where the map exists both clocks are computed,
//   and at every scored reference frame (1) the frame grid's seconds are
//   compared with the playlist time and (2) the frame's PTS with the frame
//   grid of its playlist discontinuity sequence, which starts at that
//   sequence's first scored frame. Without a map, (2) runs on the report's
//   sequences instead, each grid starting at its sequence's first frame.
//   Each run of consecutive scored frames more than half a frame off, the
//   aligner's tolerance (src/VMAFAlign.h), is an AxisRun: a warning, except
//   for a playlist without a map, whose runs are facts that claim nothing
//   about the seconds, since a forward jump under 10 s stays inside one
//   report sequence (vmafalign::kForwardJump) and may be one the playlist
//   signals.
// - Seconds to frames, in two forms. The ends of an interval given in
//   seconds, and of a margin, are the first frames, in index order, whose
//   seconds are at or after them (D-081 (g)), since an EXTINF shorter than
//   its frames makes the playlist time step back at the next segment
//   (design §4.2): a margin's can come before the frame it is measured
//   from. A duration from a frame, a cue_out's declared one, ends at the
//   first frame from that frame on whose seconds are at or after the
//   frame's plus the duration, never before it.
//
// Exact arithmetic (the design's P-A): the PTS, the frame duration, the
// EXTINF, read to the nanosecond, and the seconds are whole ticks of one time
// base that each of them is a multiple of, as the aligner compares times;
// they become doubles only for output. No FFmpeg here but libavutil's
// AVRational, its rational arithmetic and AV_NOPTS_VALUE, so that it is
// unit-tested on hand-made reports (tests/test_timeline_axis.cpp).

extern "C" {
#include <libavutil/rational.h>
}

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hls { struct MediaTimeline; }

namespace timeline {

struct LadderScores;   // src/LadderReportRead.h
struct VariantMedia;   // src/TimelineBuild.h

// A reference frame's media key: its discontinuity sequence in the ladder
// report (D-057) and its PTS. Together they name the frame where PTS alone
// repeat, as they do where an ad restarts its timestamps (D-073).
struct MediaKey {
    std::int32_t sequence = 0;            // the report's reference sequence, from 0
    std::optional<std::int64_t> pts;      // ticks of the reference's time base; nullopt: unknown
};

// A run of consecutive scored reference frames that one test of the check of
// design §4.2 found more than half a frame off.
struct AxisRun {
    enum class Test : std::uint8_t {
        Clocks,         // (1): the frame grid's seconds against the playlist time
        PlaylistGrid,   // (2): the PTS against the frame grid of its playlist discontinuity sequence
        ReportGrid,     // (2) without a segment map: against the frame grid of its report sequence
    };
    Test         test = Test::Clocks;
    std::int64_t sequence = 0;     // the grid tests: the playlist's discontinuity sequence number,
                                   // or the report sequence's index
    std::int64_t first = 0;        // its first and last reference frames
    std::int64_t last  = 0;
    std::int64_t frames = 0;       // the scored frames in it
    std::int64_t largest_at = 0;   // the frame of its largest difference, the first of equal ones
    double       largest = 0;      // that difference in seconds, signed: the frame grid's seconds
                                   // minus the playlist time, or the PTS minus the grid's
    bool         warning = true;   // false: a fact, claiming nothing about the seconds
};

// What events are placed on: the reference's frames, compared as integers,
// and the seconds offsets are printed in (D-073). The seconds are the
// reference's playlist time where it has a segment map, the frame index
// times the frame duration where it has none; where the map exists the two
// are compared at every scored frame, and each frame's PTS with the frame
// grid of its playlist discontinuity sequence, each run of frames beyond
// half a frame a warning (eval/phase3-survey/design.md §4.2).
struct Axis {
    std::string  kind = "reference-frames";   // what events are placed and compared on
    std::string  clock;                       // the seconds: "playlist-time" or "frame-grid"
    AVRational   time_base      = { 0, 1 };   // the reference's: MediaKey::pts is in it
    AVRational   frame_duration = { 0, 1 };   // the reference's, in seconds
    std::int64_t frames = 0;                  // the reference's frames
    std::vector<MediaKey> keys;               // one per reference frame
    std::string  checked;                     // "segment-map" or "report-only" (design §4.2)
    std::vector<std::string> warnings;        // the check's runs of frames (design §4.2)

    // The seconds, exactly: each reference frame's on `clock`, in ticks of
    // 1 / tick_den seconds, a time base of which the reference's time base,
    // its frame duration, half of it and every EXTINF read are multiples.
    std::int64_t tick_den = 1;
    std::vector<std::int64_t> ticks;          // one per reference frame
    // The check's runs, in the order of its tests and then of their frames:
    // each warning's text is also in `warnings`, after the warnings about the
    // inputs themselves; a fact's is not.
    std::vector<AxisRun> runs;

    // Reference frame `ref_index`'s seconds in ticks; past either end of the
    // frames, the nearest frame's plus its distance times the frame duration
    // (a span's end, one past the last frame, included).
    std::int64_t ticks_at(std::int64_t ref_index) const;
    // The same in seconds, for output only.
    double seconds(std::int64_t ref_index) const;
    // seconds(to) - seconds(from), rounded once: an offset (design §6.1).
    double seconds_between(std::int64_t from, std::int64_t to) const;

    // Seconds to frames. An axis without seconds is one without a frame
    // duration: build_axis() leaves it 0/1, with every frame at 0 s and a
    // warning that says why, when the reference's time base or frame rate is
    // unknown, when the two have no common time base within the aligner's
    // limit, or when the frame grid doesn't fit the ticks; a default Axis
    // has none either.
    //
    // The first reference frame, in index order, whose seconds are at or
    // after `seconds`; `frames` when none is (D-081 (g); design §4.2): the
    // ends of an interval given in seconds (src/TimelineQuery.h). A rational
    // with a zero denominator counts as +infinity when its numerator is
    // positive, as -infinity otherwise. On an axis without seconds: frame 0
    // for seconds at or before 0 s, `frames` for any later.
    std::int64_t frame_at_or_after(AVRational seconds) const;
    // The same, in index order, for the seconds of `ref_index` plus `offset`
    // (negative: before it): where a margin before or after a frame ends
    // (src/TimelineQuery.h's widen()). Where the playlist time steps back
    // the frame can come before `ref_index` even for a positive offset. On
    // an axis without seconds, frame 0 for an offset of zero or less and
    // `frames` for a positive one, so that a margin there reaches the
    // reference's first frame or its end.
    std::int64_t frame_at_or_after(std::int64_t ref_index, AVRational offset) const;
    // Where `duration` seconds from reference frame `ref_index` end: the
    // first frame from `ref_index` on whose seconds are at or after
    // `ref_index`'s plus `duration`, `frames` when none is; never a frame
    // before `ref_index`. Zero denominators count as above, so that
    // -infinity gives `ref_index` itself. nullopt when `ref_index` isn't one
    // of the axis's frames, and on an axis without seconds, where no
    // duration has an end. This is the form the timeline build binds to
    // WP04's FrameAfter (src/PlaylistEvents.h), which ends a cue_out's break
    // at its declared duration (D-076); margins and intervals in seconds
    // take frame_at_or_after(), in index order.
    std::optional<std::int64_t> frame_after(std::int64_t ref_index, AVRational duration) const;
};

// A run's text, as Axis::warnings gives a warning's: its test, its frames
// and its largest difference.
std::string run_text(const AxisRun& run);

// The axis of a ladder report's reference (design §4.2): `scores`, the
// complete pairs, give the reference's time base, frame rate, sequences and
// the PTS of every scored frame; `reference` its demux pass, when the build
// ran one (nullptr: none), whose PTS give the frames no pair scored and whose
// segments are the segment map, unless it was refused (D-081 (c)), when
// neither is used; `reference_playlist` its media playlist, nullptr for a
// reference that isn't a playlist. The reference's frames are the most that
// a pair read, the sum of its reference sequences' frames, or that the demux
// pass gives; a scored frame whose reference position is outside the frames
// its pair read isn't used, so that no position sizes the axis. Such frames,
// pairs that disagree on the reference, a map that doesn't fit the playlist,
// and seconds that can't be computed exactly are in Axis::warnings, with the
// fallback each leads to.
Axis build_axis(const LadderScores& scores, const VariantMedia* reference,
                const hls::MediaTimeline* reference_playlist);

} // namespace timeline
