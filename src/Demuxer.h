#pragma once

#include "Queue.h"

#include <string>
#include <vector>
#include <stop_token>
#include <atomic>
#include <cstdint>
#include <mutex>

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

struct SubtitleTrackInfo {
    int         stream_idx = -1;
    std::string language;    // display string from "title" / "language" metadata, may be empty
    std::string codec_name;
    bool        is_text = true;   // false = bitmap (PGS/DVD/DVB)
    std::string lang;        // ISO "language" tag only (e.g. "eng"), may be empty
    std::string title;       // "title" metadata, may be empty
    bool        is_default = false;   // AV_DISPOSITION_DEFAULT
    bool        is_forced  = false;   // AV_DISPOSITION_FORCED
    bool        is_hi      = false;   // AV_DISPOSITION_HEARING_IMPAIRED
};

class Demuxer {
public:
    Demuxer() = default;
    ~Demuxer();

    Demuxer(const Demuxer&) = delete;
    Demuxer& operator=(const Demuxer&) = delete;

    [[nodiscard]] bool open(const std::string& url_or_path);
    void close();
    // Any thread: abort blocking network I/O (an open in progress fails fast,
    // a running read_loop sees errors). Not undone — for teardown / cancel.
    void abort_io() { abort_io_.store(true); }

    // Request a seek to `seconds` from any thread.
    void request_seek(double seconds);

    // Request switching to a different audio/video/subtitle stream.
    // Picked up by read_loop; triggers a flush sentinel + decoder reopen flag.
    // Subtitle switch with stream_idx == -1 disables subtitle routing.
    //
    // pos: the position (seconds, stream timeline) the viewer is at — the
    // presented video frame, or the audible audio for an audio switch; < 0 if
    // unknown. After changing the discard flags the demuxer repositions the
    // container there (av_seek_frame), so the new track starts at the segment
    // that contains it instead of wherever FFmpeg's HLS demuxer would resume it.
    // Streams that keep playing are not disturbed: their already-delivered
    // packets are dropped on the re-read.
    //
    // Video and audio: while playing (exact = false) the switch is seamless — packets
    // already queued from the old track still play, and the new track takes
    // over at the end of them (decoded frames before that are dropped by the
    // decode thread, see consume_video_resume). exact = true (paused) drops the
    // queued video and resumes the new track at exactly `pos`.
    //
    // keep_until (seamless only, seconds): queued packets of the old track
    // from this time on are dropped, so the new track takes over there instead
    // of after everything already buffered (seconds of video for local files).
    // < 0 keeps all of them.
    void request_audio_switch(int stream_idx, double pos = -1.0, bool exact = false, double keep_until = -1.0);
    void request_video_switch(int stream_idx, double pos = -1.0, bool exact = false, double keep_until = -1.0);
    void request_subtitle_switch(int stream_idx, double pos = -1.0);

    // Called by the decode threads after the flush sentinel caused by a stream switch.
    // Returns true (and clears the flag) if the decoder needs to be reopened.
    bool consume_audio_reopen();
    bool consume_video_reopen();
    bool consume_subtitle_reopen();
    // Video decode thread, right after reopening: seconds before which decoded
    // frames of the new track are dropped (< 0 = none). Clears the value.
    double consume_video_resume() { return video_resume_s_.exchange(-1.0); }
    // Video decode thread, on the switch sentinel: true if the switch is
    // seamless — drain the old decoder into the frame queue instead of
    // discarding what it holds. Clears the flag.
    bool consume_video_seamless() { return video_seamless_.exchange(false); }
    // Audio decode thread, on the switch sentinel: true if the switch is
    // seamless — keep the audio already queued for the device. Clears the flag.
    bool consume_audio_seamless() { return audio_seamless_.exchange(false); }

    // Track-switch progress for the loading indicator. switch_requested() is
    // bumped by every request_*_switch; switch_settled() reaches it once the
    // demuxer has handled the request and read its first packet afterwards
    // (for HLS: the new playlist's segment arrived).
    std::uint64_t switch_requested() const { return switch_req_.load(); }
    std::uint64_t switch_settled()   const { return switch_done_.load(); }
    // Wall seconds from the last audio/video switch being handled to the
    // first packet of the newly selected stream (-1 until measured). For a
    // network source this is the segment fetch the seamless take-over has
    // to cover.
    double last_switch_fetch_s()     const { return switch_fetch_s_.load(); }

    // read_loop reached the end of the media (it then idles and serves a
    // later seek or track switch) or stopped on an unrecoverable read error.
    bool ended() const { return ended_.load(); }
    // Ended, and nothing requested since is waiting to be served: no more
    // packets will arrive for any seek or switch made so far.
    bool exhausted() const
    {
        return ended_.load()
            && (exited_.load() || (seek_target_.load() < 0.0 && switch_done_.load() >= switch_req_.load()));
    }
    // Text of the error that stopped read_loop; empty for a clean EOF.
    std::string last_error() const;

    // Runs on a std::jthread — reads packets and routes to the right queue.
    // subq may be nullptr when no subtitle stream is selected; in that case
    // subtitle packets are dropped.
    void read_loop(std::stop_token st,
                   Queue<AVPacket*>& videoq,
                   Queue<AVPacket*>& audioq,
                   Queue<AVPacket*>* subq = nullptr);

    // Stream info — valid after open()
    int video_stream_index()    const { return video_idx_; }
    int audio_stream_index()    const { return audio_idx_; }
    int subtitle_stream_index() const { return subtitle_idx_.load(); }

    AVFormatContext* fmt_ctx() const { return fmt_ctx_; }

    AVCodecParameters* video_codecpar()    const;
    AVCodecParameters* audio_codecpar()    const;
    AVCodecParameters* subtitle_codecpar() const;

    AVRational video_time_base()    const;
    AVRational audio_time_base()    const;
    AVRational subtitle_time_base() const;

    // Highest end time (seconds, stream timeline) of video packets pushed to
    // videoq since the last seek, or -1 when unknown. Drives the seek bar's
    // buffered range. Written by read_loop, safe to read from any thread.
    double read_ahead_pts() const { return read_ahead_pts_.load(std::memory_order_relaxed); }

    // Total duration in seconds, or -1 if unknown (live streams).
    double duration() const;

    // Clockwise rotation of the video stream in degrees (0, 90, 180, 270).
    // Derived from the AV_PKT_DATA_DISPLAYMATRIX side-data attached to the stream.
    double video_rotation() const;

    // Stream-level SAR for the video stream ({0,1} = unspecified / square pixels).
    AVRational video_sar() const;

    // All audio/video/subtitle tracks in the container, populated at open().
    const std::vector<AudioTrackInfo>&    audio_tracks()    const { return audio_tracks_; }
    const std::vector<VideoTrackInfo>&    video_tracks()    const { return video_tracks_; }
    const std::vector<SubtitleTrackInfo>& subtitle_tracks() const { return subtitle_tracks_; }

private:
    AVFormatContext*    fmt_ctx_      = nullptr;
    std::atomic<int>    video_idx_{ -1 };   // written by read_loop (switch), read by main/decoders
    std::atomic<int>    audio_idx_{ -1 };
    bool                is_network_   = false;   // set by open(); enables read retries
    // Set when read_loop's stop is requested; FFmpeg's interrupt callback then
    // aborts any blocking network I/O so the demux thread joins promptly.
    std::atomic<bool>   abort_io_{ false };
    // Written by read_loop (track switch), read by the main and subtitle threads.
    std::atomic<int>    subtitle_idx_{ -1 };
    std::atomic<double> seek_target_{ -1.0 };
    std::atomic<double> read_ahead_pts_{ -1.0 };
    std::atomic<int>    pending_audio_switch_{ -1 };
    std::atomic<int>    pending_video_switch_{ -1 };
    std::atomic<int>    pending_subtitle_switch_{ -2 };  // -2=no request, -1=disable, >=0=switch
    std::atomic<bool>   audio_reopen_{ false };
    std::atomic<bool>   video_reopen_{ false };
    std::atomic<bool>   subtitle_reopen_{ false };
    std::atomic<double> switch_pos_{ -1.0 };
    std::atomic<bool>   video_switch_exact_{ false };
    std::atomic<double> video_resume_s_{ -1.0 };
    std::atomic<bool>   video_seamless_{ false };
    std::atomic<bool>   audio_seamless_{ false };
    std::atomic<bool>   audio_switch_exact_{ false };
    std::atomic<double> video_keep_until_{ -1.0 };
    std::atomic<double> audio_keep_until_{ -1.0 };
    std::atomic<double> switch_fetch_s_{ -1.0 };
    std::atomic<std::uint64_t> switch_req_{ 0 };
    std::atomic<std::uint64_t> switch_done_{ 0 };
    std::atomic<bool>   ended_{ false };
    std::atomic<bool>   exited_{ false };   // read_loop returned (error); requests are no longer served
    mutable std::mutex  err_mtx_;
    std::string         last_error_;
    // Trace: log the first packet / keyframe of a newly switched video stream.
    bool                trace_video_pkt_ = false;
    bool                trace_video_key_ = false;

    std::vector<AudioTrackInfo>    audio_tracks_;
    std::vector<VideoTrackInfo>    video_tracks_;
    std::vector<SubtitleTrackInfo> subtitle_tracks_;

    bool is_network_url(const std::string& url) const;
    void build_track_lists();
    void apply_stream_discard();
    void print_stream_info() const;
};
