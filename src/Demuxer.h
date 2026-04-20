#pragma once

#include "Queue.h"

#include <string>
#include <vector>
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

struct AudioTrackInfo {
    int         stream_idx;
    std::string language;    // from "language" / "title" metadata, may be empty
    std::string codec_name;
    int         sample_rate = 0;
    int         channels    = 0;
    int64_t     bit_rate    = 0;
};

struct VideoTrackInfo {
    int         stream_idx;
    std::string codec_name;
    int         width = 0, height = 0;
    double      fps   = 0.0;
    int64_t     bit_rate = 0;
};

class Demuxer {
public:
    Demuxer() = default;
    ~Demuxer();

    Demuxer(const Demuxer&) = delete;
    Demuxer& operator=(const Demuxer&) = delete;

    [[nodiscard]] bool open(const std::string& url_or_path);
    void close();

    // Request a seek to `seconds` from any thread.
    void request_seek(double seconds);

    // Request switching to a different audio/video stream.
    // Picked up by read_loop; triggers a flush sentinel + decoder reopen flag.
    void request_audio_switch(int stream_idx);
    void request_video_switch(int stream_idx);

    // Called by the decode threads after the flush sentinel caused by a stream switch.
    // Returns true (and clears the flag) if the decoder needs to be reopened.
    bool consume_audio_reopen();
    bool consume_video_reopen();

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

    // Clockwise rotation of the video stream in degrees (0, 90, 180, 270).
    // Derived from the AV_PKT_DATA_DISPLAYMATRIX side-data attached to the stream.
    double video_rotation() const;

    // Stream-level SAR for the video stream ({0,1} = unspecified / square pixels).
    AVRational video_sar() const;

    // All audio/video tracks in the container, populated at open().
    const std::vector<AudioTrackInfo>& audio_tracks() const { return audio_tracks_; }
    const std::vector<VideoTrackInfo>& video_tracks() const { return video_tracks_; }

private:
    AVFormatContext*    fmt_ctx_      = nullptr;
    int                 video_idx_    = -1;
    int                 audio_idx_    = -1;
    std::atomic<double> seek_target_{ -1.0 };
    std::atomic<int>    pending_audio_switch_{ -1 };
    std::atomic<int>    pending_video_switch_{ -1 };
    std::atomic<bool>   audio_reopen_{ false };
    std::atomic<bool>   video_reopen_{ false };

    std::vector<AudioTrackInfo> audio_tracks_;
    std::vector<VideoTrackInfo> video_tracks_;

    bool is_network_url(const std::string& url) const;
    void build_track_lists();
    void print_stream_info() const;
};
