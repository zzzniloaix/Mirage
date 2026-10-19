#pragma once

#include "FfOpen.h"
#include "KeyframeIndex.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#include <string>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/rational.h>
}

// Dedicated single-frame decoder for scrub bar interaction.
//
// Keeps its own AVFormatContext open for the lifetime of the player.
// The main thread calls request() on every mouse-move event during scrubbing;
// the background thread seeks to the latest requested position, decodes one
// frame, and posts it.  poll_frame() retrieves the result with no blocking.
//
// This keeps scrubbing completely decoupled from the main demux / decode
// pipeline, eliminating the flush→seek→decode round-trip through three threads
// that caused choppy behaviour when using demuxer.request_seek() during a drag.
//
// In-flight cancellation: if a new request arrives while the background thread
// is decoding, it aborts the current decode and starts the new one immediately.
//
// It decodes the playing video stream: after a video track / HLS variant
// switch, retarget() makes it follow the new one. Its context also serves
// keyframe probes (probe()): reading a stretch of that stream so the
// keyframe index knows every keyframe in it before a keyframe step.
class ScrubDecoder {
public:
    ScrubDecoder()  = default;
    ~ScrubDecoder();
    ScrubDecoder(const ScrubDecoder&)            = delete;
    ScrubDecoder& operator=(const ScrubDecoder&) = delete;

    // Open the file in a background thread.  Non-fatal: if the file cannot be
    // opened the decoder stays silent and poll_frame() always returns nullptr.
    // video_stream_idx / time_base: the playback stream to decode, as opened
    // from `url` (or, HLS, from the master copy `master` playback opened
    // instead — see ffopen::open_input); posted frames carry pts in
    // time_base. variant (HLS): that stream's own media playlist, opened
    // instead of either when it checks out.
    void open(const std::string& url, int video_stream_idx, AVRational time_base,
              const ffopen::VariantInput& variant = {}, const std::string& master = {});

    // Decode playback stream `video_stream_idx` from now on (time_base,
    // variant: as for open()) — a video track / variant switch completed.
    // Any thread; returns at once. The decode thread switches before it
    // serves anything else: in place when its context holds that stream
    // (opened like playback was), else by opening `variant`'s playlist
    // (failing that, the master). A request being decoded is abandoned and
    // decoded again from the new stream if its frame was not polled yet;
    // frames of the old stream still waiting are dropped.
    void retarget(int video_stream_idx, AVRational time_base, const ffopen::VariantInput& variant = {});

    // Before open(): keyframes of the playback streams (may be nullptr). A
    // request then decodes from the last keyframe read at or before its
    // target — on HLS a backward seek starts at the segment's first frame,
    // up to a segment of frames earlier. Every packet of the stream decoded
    // that the decoder reads is reported to it (KeyframeIndex::observe).
    void set_keyframes(KeyframeIndex* index) { keyframes_ = index; }

    // Stop the background thread and free all resources.
    void close();

    // Post a seek target (seconds). Thread-safe; cancels any in-flight decode
    // and supersedes any pending request that has not yet started. Returns
    // the request's serial (increasing, > 0), for poll_frame().
    std::uint64_t request(double target_pts);

    // Read playback stream `stream` as `p` says (KeyframeIndex::Step::probe)
    // so the keyframe index (set_keyframes) learns its keyframes there: no
    // decoding, every packet reported. Thread-safe; supersedes an earlier
    // probe. Served once no request() is waiting; a request() or retarget()
    // arriving meanwhile abandons it. Returns its serial (> 0).
    std::uint64_t probe(int stream, const KeyframeIndex::Probe& p);
    enum class ProbeState { Pending, Done, Failed };
    // Done: it read what it was asked to (or up to the end of the media).
    // Failed: abandoned, superseded, not the stream decoded, a seek or read
    // that failed, or no decoder thread (open failed / closed).
    ProbeState probe_state(std::uint64_t serial) const;
    // Drop the pending probe / abandon the one being read.
    void cancel_probe();

    // Retrieve the most-recently decoded frame from the main (GL) thread.
    // Returns a newly-allocated AVFrame* if a frame is ready, nullptr otherwise.
    // Frames answering requests older than `min_serial` are dropped. The
    // frame's pts is in its time_base (the playback stream's); *stream (if
    // given) is the playback stream it was decoded from.
    // The caller MUST av_frame_free() the returned pointer.
    [[nodiscard]] AVFrame* poll_frame(std::uint64_t min_serial = 0, int* stream = nullptr);

    // Playback stream being decoded (the latest open() / retarget()).
    int stream() const { return latest_stream_.load(); }

    // How requests started decoding so far: at a keyframe read
    // (set_keyframes), or at the decoder's own seek — `missed` of the former
    // fell back to it (the seek did not land on the keyframe).
    struct Counters { int from_key = 0, from_seek = 0, missed = 0; };
    Counters counters() const { return { n_from_key_.load(), n_from_seek_.load(), n_missed_.load() }; }

private:
    struct Target {
        int                  stream    = -1;          // playback stream index
        AVRational           time_base = { 1, 1 };    // its time base: posted pts are in it
        ffopen::VariantInput variant;                 // HLS: its own media playlist
    };
    void decode_loop(std::stop_token st, std::string url, std::string master);

    // Request channel (main thread → background)
    std::mutex              req_mtx_;
    std::condition_variable req_cv_;
    double                  req_pts_     = -1.0;
    bool                    req_pending_ = false;
    std::uint64_t           req_serial_  = 0;      // latest request
    Target                  target_;               // latest open() / retarget()
    std::uint64_t           target_gen_  = 0;      // bumped by each of them
    KeyframeIndex::Probe    probe_req_;            // latest probe() (req_mtx_)
    int                     probe_stream_  = -1;
    bool                    probe_pending_ = false;
    std::uint64_t           probe_serial_  = 0;    // latest probe() / cancel_probe()

    // Result channel (background → main thread)
    std::mutex    result_mtx_;
    AVFrame*      result_        = nullptr;
    std::uint64_t result_gen_    = 0;
    std::uint64_t result_serial_ = 0;
    int           result_stream_ = -1;

    KeyframeIndex*             keyframes_ = nullptr;
    std::atomic<std::uint64_t> latest_gen_{ 0 };       // = target_gen_, for poll_frame()
    std::atomic<std::uint64_t> consumed_serial_{ 0 };  // newest request whose frame was polled
    std::atomic<int>           latest_stream_{ -1 };
    std::atomic<int>           n_from_key_{ 0 }, n_from_seek_{ 0 }, n_missed_{ 0 };
    std::atomic<std::uint64_t> probe_result_{ 0 };     // last probe finished: serial << 1 | done
    std::atomic<bool>          stopped_{ true };       // no decode thread: before open(), or it returned

    std::jthread thread_;
};
