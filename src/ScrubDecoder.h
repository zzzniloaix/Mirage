#pragma once

#include <condition_variable>
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
class ScrubDecoder {
public:
    ScrubDecoder()  = default;
    ~ScrubDecoder();
    ScrubDecoder(const ScrubDecoder&)            = delete;
    ScrubDecoder& operator=(const ScrubDecoder&) = delete;

    // Open the file in a background thread.  Non-fatal: if the file cannot be
    // opened the decoder stays silent and poll_frame() always returns nullptr.
    void open(const std::string& url, int video_stream_idx, AVRational time_base);

    // Stop the background thread and free all resources.
    void close();

    // Post a seek target (seconds). Thread-safe; cancels any in-flight decode
    // and supersedes any pending request that has not yet started.
    void request(double target_pts);

    // Retrieve the most-recently decoded frame from the main (GL) thread.
    // Returns a newly-allocated AVFrame* if a frame is ready, nullptr otherwise.
    // The caller MUST av_frame_free() the returned pointer.
    [[nodiscard]] AVFrame* poll_frame();

private:
    void decode_loop(std::stop_token st, std::string url,
                     int video_stream_idx, AVRational time_base);

    // Request channel (main thread → background)
    std::mutex              req_mtx_;
    std::condition_variable req_cv_;
    double                  req_pts_     = -1.0;
    bool                    req_pending_ = false;

    // Result channel (background → main thread)
    std::mutex  result_mtx_;
    AVFrame*    result_ = nullptr;

    std::jthread thread_;
};
