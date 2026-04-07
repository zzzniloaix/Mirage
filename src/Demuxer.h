#pragma once

#include "Queue.h"

#include <string>
#include <stop_token>
#include <atomic>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

// A unique address used as a sentinel in packet queues to signal "flush decoder".
// Decode threads MUST NOT call av_packet_free() on this pointer.
inline AVPacket* flush_sentinel() {
    static uint8_t s_marker;
    return reinterpret_cast<AVPacket*>(&s_marker);
}

class Demuxer {
public:
    Demuxer() = default;
    ~Demuxer();

    Demuxer(const Demuxer&) = delete;
    Demuxer& operator=(const Demuxer&) = delete;

    [[nodiscard]] bool open(const std::string& url_or_path);
    void close();

    // Request a seek to `seconds` from any thread.
    // The actual seek runs inside read_loop on the demux thread.
    void request_seek(double seconds);

    // Runs on a std::jthread — reads packets and routes to the right queue.
    // Returns when EOF, read error, or stop is requested.
    void read_loop(std::stop_token st,
                   Queue<AVPacket*>& videoq,
                   Queue<AVPacket*>& audioq);

    // Stream info — valid after open()
    int video_stream_index() const { return video_idx_; }
    int audio_stream_index() const { return audio_idx_; }

    AVFormatContext* fmt_ctx() const { return fmt_ctx_; }

    AVCodecParameters* video_codecpar() const;
    AVCodecParameters* audio_codecpar() const;

    AVRational video_time_base() const;
    AVRational audio_time_base() const;

    // Total duration in seconds, or -1 if unknown (live streams).
    double duration() const;

private:
    AVFormatContext*    fmt_ctx_      = nullptr;
    int                 video_idx_    = -1;
    int                 audio_idx_    = -1;
    std::atomic<double> seek_target_{ -1.0 };  // seconds, negative = no seek pending

    bool is_network_url(const std::string& url) const;
    void print_stream_info() const;
};
