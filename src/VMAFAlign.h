#pragma once

// Pairs a reference's and a distorted input's decoded frames by their
// timestamps (work order section 1.4; DECISIONS.md D-028, which approves
// docs/plans/frame-alignment.md with its recommended choices). The analyzer
// (src/VMAFAnalyzer.cpp) gives an Aligner each input's frames in the order its
// decoder outputs them and does what next() says: decode one more frame of an
// input, score the frames at the heads of its two queues as a pair, or drop
// the frame at the head of one queue. No FFmpeg here but AVRational and
// libavutil's rational arithmetic, so that it is unit-tested on timestamp
// sequences alone (tests/test_vmaf_align.cpp).
//
// The rules. Where D-028 and the plan leave a value open, the value here is
// the one named below, recorded for the owner's confirmation in
// DECISIONS.md P-043.
//
// - Times. A frame's timestamp is in its input's time base, measured from the
//   input's start, Clock::start: the container's start time, as `ffmpeg`
//   shifts each input without -copyts; the input's first timestamped frame
//   when the container has none. Times are compared exactly, as whole ticks
//   of a time base that every time base, frame duration and the tolerance
//   are whole multiples of.
// - Rule A, the reference's timeline. A pair is a reference frame and a
//   distorted frame whose times differ by at most the tolerance, half the
//   reference's frame duration, 1 / Clock::frame_rate.
// - The coarser grid. When the distorted input's frame rate is lower than the
//   reference's (compared exactly), its frames are the grid; otherwise the
//   reference's frames are. A distorted input without a frame rate is taken
//   to have the reference's.
// - Matching. In time order, each frame of the grid input is paired with the
//   nearest frame of the other input within the tolerance that is neither
//   paired nor dropped, the earlier of two equally near. A frame at or after
//   the grid frame's time is taken without looking further, so inputs whose
//   frames line up are paired without decoding ahead.
// - Frames that don't pair:
//   - a reference frame is an unmatched reference frame. On the distorted
//     input's coarser grid only the reference frames at the grid's points
//     count: the reference frame nearest to a point of the grid that no
//     distorted frame takes, if nearer to it than the tolerance, or one that
//     repeats the timestamp of the reference frame before it. The others
//     are off the grid, skipped and not failures;
//   - a distorted frame is a duplicate distorted frame when it repeats the
//     timestamp of the distorted frame before it, and otherwise an extra
//     distorted frame. When the distorted input's frame rate is higher than
//     the reference's, only the distorted frame nearest to a point of the
//     reference's grid that no reference frame takes, if nearer to it than
//     the tolerance, is extra; the others are off the grid.
//   The points of a grid that no frame takes: between two consecutive frames
//   a and b of the grid input, n = round((b - a) / d) - 1 points a + k d,
//   k = 1..n, d the grid input's frame duration; before its first frame
//   first - k d, after its last frame last + k d, k >= 1. Each point counts
//   at most one frame, and a frame for at most one point. A frame halfway
//   between two points, as every other frame of an input at twice the
//   other's rate is, counts for neither.
// - A frame without a timestamp can't be placed: it is a failure, dropped.
// - Discontinuities. A frame whose time is lower than that of its input's
//   previous timestamped frame, or higher by more than kForwardJump, is a
//   discontinuity of its input. At the first, in either input, pairing
//   stops: every frame of either input that isn't paired or counted yet
//   counts as failed, those still to come included (D-028, the plan's
//   option (a)).
// - libvmaf contexts. A pair before which a reference frame that counts as a
//   failure was dropped since the previous pair starts a new libvmaf
//   context (Step::new_context), so that no motion feature spans the gap.
//   Reference frames off the grid start none: on the distorted input's
//   coarser grid, the reference frames libvmaf gets are those of the grid.
// - The failure rate: failed / (paired + failed), the failures being the
//   unmatched reference frames, the extra and duplicate distorted frames,
//   the frames without a timestamp and those counted once pairing stopped.
//   It exceeds the threshold, kFailureThreshold, when it is higher.

extern "C" {
#include <libavutil/rational.h>
}

#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace vmafalign {

// A frame without a timestamp: FFmpeg's AV_NOPTS_VALUE.
inline constexpr int64_t kNoTimestamp = std::numeric_limits<int64_t>::min();

// The failure rate above which an analysis warns (D-028, #7: 1%).
inline constexpr double kFailureThreshold = 0.01;

// A forward jump of a timestamp larger than this, in seconds, is a
// discontinuity: the default of `ffmpeg`'s dts_delta_threshold, beyond which
// it takes a jump in a format whose timestamps may jump for a discontinuity
// (fftools/ffmpeg_opt.c and ffmpeg_demux.c of FFmpeg 8.1.1).
inline constexpr AVRational kForwardJump = { 10, 1 };

// The most ticks per second the common time base may have: up to 2^40, the
// times of 2^22 seconds (48 days) each side of an input's start fit in 64
// bits.
inline constexpr int64_t kMaxTicksPerSecond = int64_t{ 1 } << 40;

enum class Input { Reference, Distorted };

// One input's timing.
struct Clock {
    AVRational time_base  = { 0, 1 };   // of its frames' timestamps
    AVRational frame_rate = { 0, 1 };   // frames per second; num <= 0 when unknown
    int64_t    start      = kNoTimestamp;   // in time_base; kNoTimestamp: its first timestamped frame
};

// Why a frame counts as failed.
enum class Reason {
    UnmatchedReference,   // a reference frame no distorted frame paired with
    ExtraDistorted,       // a distorted frame that paired with no reference frame
    DuplicateDistorted,   // a distorted frame with the timestamp of the one before it
    NoTimestamp,          // a frame without a timestamp
    AfterDiscontinuity,   // a frame not paired or counted yet when pairing stopped
};

// "unmatched_reference", "extra_distorted", ... as the report names them.
const char* reason_name(Reason r);

struct Failure {
    Input   input;
    int64_t index;       // the frame's position in its input, from 0
    int64_t timestamp;   // as given, in its input's time base; kNoTimestamp
    Reason  reason;
};

struct Counts {
    int64_t paired              = 0;
    int64_t unmatched_reference = 0;
    int64_t extra_distorted     = 0;
    int64_t duplicate_distorted = 0;
    int64_t no_timestamp        = 0;
    int64_t after_discontinuity = 0;
    int64_t off_grid_reference  = 0;   // skipped by the coarser grid; not failures
    int64_t off_grid_distorted  = 0;
    int64_t discontinuities     = 0;   // every one found, also after pairing stopped

    int64_t failed() const;
    double  failure_rate() const;      // failed / (paired + failed); NaN when both are 0
    bool    exceeded() const;          // failure_rate() > kFailureThreshold
};

// What the caller does next.
struct Step {
    enum class Kind {
        NeedReference,    // decode the reference's next frame: push() it, or end() the input
        NeedDistorted,    // the same for the distorted input
        Pair,             // score the frames at the heads of both queues as a pair,
                          // and take them off the queues
        DropReference,    // take the reference frame at the head of its queue off it, unscored
        DropDistorted,    // the same for the distorted input's
        Done,             // both inputs ended, and every frame is paired or counted
    };
    Kind    kind        = Kind::Done;
    bool    new_context = false;   // Pair: score it in a new libvmaf context
    int64_t ref_index   = -1;      // Pair, DropReference: the frame's position in its input
    int64_t dis_index   = -1;      // Pair, DropDistorted
};

class Aligner {
public:
    // ok() is false, and error() says why, when the reference's frame rate
    // is unknown or a time base isn't positive, or when the times can't be
    // compared exactly within kMaxTicksPerSecond.
    Aligner(const Clock& reference, const Clock& distorted);

    bool               ok()    const { return error_.empty(); }
    const std::string& error() const { return error_; }

    // The next decoded frame of `in`, with its timestamp in its time base
    // (kNoTimestamp without one), after next() asked for it.
    void push(Input in, int64_t timestamp);
    // `in` has no more frames.
    void end(Input in);

    Step next();

    // The input whose frames are the grid (see above).
    Input      grid()      const { return grid_; }
    // Half the reference's frame duration, in seconds; {0, 1} when !ok().
    AVRational tolerance() const { return tolerance_; }
    // Where `in`'s times are measured from, in its time base: Clock::start,
    // or its first timestamped frame's; nullopt before that frame.
    std::optional<int64_t> start(Input in) const;
    bool                   start_from_clock(Input in) const;

    const Counts&               counts()   const { return counts_; }
    // In the order the frames were counted.
    const std::vector<Failure>& failures() const { return failures_; }

private:
    struct Entry {
        int64_t index;
        int64_t timestamp;   // as given
        int64_t t;           // ticks from the input's start; valid when timestamp != kNoTimestamp
        bool    repeats;     // the timestamp of the input's previous timestamped frame
    };
    struct Side {
        Clock             clock;
        std::deque<Entry> queue;
        bool              ended      = false;
        int64_t           next_index = 0;
        bool              has_start  = false;
        int64_t           start      = 0;      // in clock.time_base
        bool              has_last   = false;
        int64_t           last_t     = 0;      // ticks of its last timestamped frame
    };

    Side&       side(Input in) { return in == Input::Reference ? ref_ : dis_; }
    const Side& side(Input in) const { return in == Input::Reference ? ref_ : dis_; }
    Input       other(Input in) const
    {
        return in == Input::Reference ? Input::Distorted : Input::Reference;
    }
    int64_t ticks(const Side& s, int64_t timestamp) const;
    void    fail(Input in, const Entry& e, Reason r);
    void    unpaired_grid_frame(const Entry& g);
    void    close_interval(std::optional<int64_t> left, std::optional<int64_t> right);
    Step    drop(Input in);

    Side    ref_, dis_;
    Input   grid_        = Input::Reference;
    bool    rates_equal_ = true;      // the distorted input has the reference's frame rate
    AVRational tolerance_ = { 0, 1 };
    int64_t tick_den_    = 1;         // the common time base is 1 / tick_den_
    int64_t tol_         = 0;         // the tolerance, in ticks
    int64_t grid_dur_    = 0;         // the grid input's frame duration, in ticks
    int64_t jump_        = 0;         // kForwardJump, in ticks
    bool    stopped_     = false;     // a discontinuity was found
    bool    gap_         = false;     // a reference frame counted as failed since the last pair
    std::optional<int64_t> prev_grid_;    // the time of the grid input's last decided frame
    std::vector<Entry>     pending_;      // the other input's dropped frames since then
    Counts               counts_;
    std::vector<Failure> failures_;
    std::string          error_;
};

} // namespace vmafalign
