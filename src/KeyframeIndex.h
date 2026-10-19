#pragma once

// Keyframes of the playback video streams, for keyframe stepping (. / ,) and
// the seek bar's keyframe ticks where no container index lists them: HLS,
// and a video track switched to during playback (ThumbnailStrip indexes the
// stream playing at open only).
//
// Per stream, from two sources:
//  * observed — the keyframe packets read, with their exact pts and dts:
//    by the playback demuxer, and by the scrub decoder (its scrubs, and the
//    probes below). Complete for whatever has been played or buffered.
//  * predicted (HLS) — media segment starts from the stream's variant
//    playlist: segment i starts sum(EXTINF[0..i)) after the stream's first
//    frame (its start time — bipbop: 10.017 s, not 0), and an HLS segment
//    starts with a keyframe. Used only while the observed keyframes bear it
//    out: a segment start after the first was read where predicted, and
//    nothing contradicts it — a run of packets crossing a predicted start
//    without a keyframe there, or a read that began on a segment start (a
//    seek / switch landing) somewhere else. Every EXTINF must also be a whole
//    number of frames: rounded durations drift.
//
// What is known is not every keyframe: inside a segment not read yet only its
// start is. Each reader reports every packet it reads, in runs of
// consecutive packets (a run restarts with every seek), so the index knows
// which stretches were read through — every keyframe there is listed. A
// keyframe step (step()) is certain only when the stretch between the frame
// on screen and the neighbouring keyframe was read through; otherwise the
// caller reads that stretch first (Step::probe, ScrubDecoder::probe) and asks
// again. The seek bar draws predicted starts apart from keyframes read.
//
// Landing on a keyframe (Keyframe::seek_s / at_or_after): a backward HLS
// seek goes to the start of the segment holding the target (FFmpeg 8.1
// hls_read_seek), so an observed keyframe inside a segment is reached by a
// forward seek to its dts — the first keyframe at or after it — and a
// predicted segment start by a backward seek to its pts. Other inputs seek
// backward to the pts, as for container-index keyframes.
//
// Thread-safe: the demux and scrub threads observe, a fetch thread sets
// segments, the render thread reads.

#include <atomic>
#include <cstdint>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

class KeyframeIndex {
public:
    struct Keyframe {
        double pts         = -1.0;    // seconds, stream timeline
        double seek_s      = -1.0;    // the seek that lands on it (seconds)
        bool   at_or_after = false;   // seek_s is its dts: seek to the first keyframe at or after it
        bool   predicted   = false;   // a segment start from the playlist, not read yet
    };
    struct Status {
        std::size_t observed  = 0;    // keyframes read
        std::size_t predicted = 0;    // segment starts listed in addition (trusted only)
        std::size_t segments  = 0;    // segments in the playlist
        int  confirmed    = 0;        // segment starts after the first, read where predicted
        int  contradicted = 0;
        bool aligned      = false;    // every EXTINF a whole number of frames
        bool trusted      = false;
    };
    // Reading a stream to learn its keyframes around a step: a seek, then
    // every packet on to `until`, reported to observe() as one run.
    struct Probe {
        double seek_s        = -1.0;    // the seek reading starts with (seconds), as Keyframe::seek_s
        bool   at_or_after   = false;   //   and Keyframe::at_or_after
        bool   segment_start = false;   // it lands on an HLS segment start (a backward seek)
        bool   forward       = false;   // read up to the first keyframe after `until`; else to dts >= until
        double until         = -1.0;    // the frame stepped from (pts seconds)
        double frame_s       = 0.0;     // one frame (seconds, 0 = unknown)
    };
    // A keyframe step from the frame at t: the neighbouring keyframe among
    // those known, and whether it certainly is the neighbour — every packet
    // between t and it was read, so no keyframe in between went unseen (or,
    // stepping back, t is the stream's first frame). Not certain: read
    // `probe` first, then ask again.
    struct Step {
        std::optional<Keyframe> kf;
        bool                    certain = false;
        Probe                   probe;
    };

    // HLS input: observed keyframes are reached by forward seeks to their dts.
    void set_hls(bool hls);
    bool hls() const;
    // A video stream: its first frame (pts seconds, NaN if unknown) and one
    // frame's duration (seconds, 0 = unknown). Not `tracked`: observe()
    // ignores it (its keyframes come from elsewhere — a container index).
    void set_stream(int stream, double start_s, double frame_s, bool tracked = true);
    // The stream's media segments (EXTINF seconds, in order); empty = none.
    void set_segments(int stream, const std::vector<double>& durations);
    // A run id for observe(), unique across readers.
    std::uint32_t new_run() { return next_run_.fetch_add(1) + 1; }
    // A video packet read, in decode order: every packet of a run (`run`
    // changes whenever reading restarts elsewhere — seek, switch, read
    // error). segment_start: the run's first packet, read from an HLS
    // segment start (key or not).
    void observe(int stream, double pts, double dts, bool key, std::uint32_t run, bool segment_start);

    // Bumped by every change of any stream's list. Lock-free.
    std::uint64_t version() const { return version_.load(); }
    // Keyframes of `stream`, by pts.
    std::vector<Keyframe> list(int stream) const;
    // First keyframe after t / last one before t (1 ms tolerance, as
    // ThumbnailStrip's), nullopt if none.
    std::optional<Keyframe> next(int stream, double t) const;
    std::optional<Keyframe> prev(int stream, double t) const;
    // Last keyframe at or before t (1 ms tolerance): where decoding a frame
    // at t can start.
    std::optional<Keyframe> at_or_before(int stream, double t) const;
    // next(t) (forward) / prev(t), whether it is certain, and what to read
    // if not.
    Step step(int stream, double t, bool forward) const;
    Status status(int stream) const;

    // How far an observed keyframe may be from a predicted segment start
    // and still be that start (half a frame).
    static double tolerance(double frame_s);

private:
    struct Obs { double pts, dts; };
    struct Run {                                    // one reader's consecutive packets
        double first_key = std::numeric_limits<double>::quiet_NaN();   // pts of its first keyframe
        double last_key  = std::numeric_limits<double>::quiet_NaN();   // pts of its latest keyframe
        double end       = -std::numeric_limits<double>::infinity();   // read through here (dts; keyframes' pts)
    };
    struct Stream {
        double start_s = std::numeric_limits<double>::quiet_NaN();
        double frame_s = 0.0;
        bool   tracked = true;
        std::vector<Obs>    obs;                    // keyframes read, by pts
        std::vector<double> durations;              // EXTINF
        std::vector<double> bounds;                 // predicted segment starts (pts)
        std::vector<std::pair<double, double>> gaps;   // consecutive keyframes of one run, sorted
        std::vector<double> landings;               // first packets of segment-start reads (pts)
        int                 landings_nokey = 0;     // ... that were not keyframes
        std::map<std::uint32_t, Run> runs;
        // Oldest runs, folded into what they read through once there are
        // more than kMaxRuns (every served scrub request is a run).
        std::vector<std::pair<double, double>> folded;
        // Derived (rebuild)
        Status              st;
        std::vector<Keyframe> merged;
    };
    static constexpr std::size_t kMaxRuns = 1024;
    Stream& stream_locked(int stream);
    static void fold_runs(Stream& s);
    void rebuild(int id, Stream& s);   // derived state + merged list, mtx_ held
    // Every keyframe with pts in [a, b] has been read (mtx_ held).
    static bool covers(const Stream& s, double a, double b, double tol);
    // How far before t a known keyframe may be for a probe to start reading
    // there rather than where a backward seek lands (about one segment).
    static double probe_span(const Stream& s);

    mutable std::mutex     mtx_;
    bool                   hls_ = false;
    std::map<int, Stream>  streams_;
    std::atomic<std::uint64_t> version_{ 0 };   // bumped with mtx_ held
    std::atomic<std::uint32_t> next_run_{ 0 };
};
