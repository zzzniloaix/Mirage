#pragma once

#include "Decoder.h"
#include "AudioPlayer.h"

#include <atomic>

extern "C" {
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
}

// Wraps Decoder (audio codec) + SwrContext (resampling) + atempo filter graph.
// decode() pulls frames from the codec, resamples to float stereo, runs through
// the atempo filter (for speed != 1.0), and writes to the AudioPlayer ring buffer.
class AudioDecoder {
public:
    AudioDecoder() = default;
    ~AudioDecoder();

    AudioDecoder(const AudioDecoder&) = delete;
    AudioDecoder& operator=(const AudioDecoder&) = delete;

    // par: audio stream codecpar
    // player: where to push resampled samples
    // time_base: stream time_base used to convert frame->pts to seconds
    [[nodiscard]] bool open(AVCodecParameters* par, AudioPlayer& player, AVRational time_base);

    // Send one packet, pull all resulting frames, resample, push to player.
    void decode(AVPacket* pkt, AudioPlayer& player);

    // Flush codec + filter buffers after a seek.
    void flush();

    // The audio decoded from here on starts at `t` (seconds, stream
    // timeline): samples before it are dropped, to the sample — a track
    // switch's take-over point, a seek's target. The demuxer queues a few
    // packets before it, so the decoder is warmed up when it gets there
    // (a decoder started on the packet holding `t` lost up to a packet's
    // worth there and clicked). < 0: none. Kept by flush() / reopen(),
    // cleared once a frame reaches it.
    void start_at(double t) { start_at_ = t; }

    // Tear down and reinitialise for a new audio stream (track switch).
    [[nodiscard]] bool reopen(AVCodecParameters* par, AudioPlayer& player,
                               AVRational time_base);

    // Request a new playback speed. Safe to call from any thread: the audio
    // decode thread applies it (drains the old atempo graph, rebuilds) at the
    // start of its next decode() or flush().
    // speed in [0.25, 4.0] — uses two chained atempo stages if needed.
    void set_speed(double speed) { requested_speed_.store(speed); }

private:
    Decoder     decoder_;
    SwrContext* swr_    = nullptr;
    AVFrame*    frame_  = nullptr;   // raw decoded frame (resampled input)
    AVFrame*    swr_frame_ = nullptr; // resampled float stereo frame

    // Output spec (matches AudioPlayer)
    int             out_rate_    = 48000;
    AVChannelLayout out_layout_  = {};
    AVRational      time_base_   = {1, 1};

    // atempo filter graph
    AVFilterGraph*   filter_graph_ = nullptr;
    AVFilterContext* filt_src_     = nullptr;  // abuffer
    AVFilterContext* filt_sink_    = nullptr;  // abuffersink
    double           speed_        = 1.0;   // speed the current graph was built for
    double           tempo_        = 1.0;   // actual atempo product (speed_ after clamping)
    std::atomic<double> requested_speed_{ 1.0 };

    // Running media-time position for output frames (seconds).
    // Reset by flush(); re-seeded from the first decoded frame's PTS.
    double out_media_pts_  = 0.0;
    bool   pts_inited_     = false;
    double start_at_       = -1.0;   // start_at()

    // Build (or rebuild) a filter graph with the given speed.
    // speed == 1.0 → passthrough (no filter, graph is null)
    [[nodiscard]] bool build_filter_graph(double speed);
    void free_filter_graph();

    // Push a resampled AVFrame into the filter graph and drain output to player.
    void push_to_filter(AVFrame* frame, AudioPlayer& player);

    // Pull every frame currently available from the sink and push it to player.
    void pull_filter_output(AudioPlayer& player);

    // Audio thread only: if set_speed() requested a new speed, flush the old
    // graph's buffered tail to player at the old tempo, then rebuild.
    void apply_pending_speed(AudioPlayer& player);
};
