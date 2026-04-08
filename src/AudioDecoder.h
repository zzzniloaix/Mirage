#pragma once

#include "Decoder.h"
#include "AudioPlayer.h"

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

    // Tear down and reinitialise for a new audio stream (track switch).
    [[nodiscard]] bool reopen(AVCodecParameters* par, AudioPlayer& player,
                               AVRational time_base);

    // Rebuild the atempo filter graph for the given playback speed.
    // speed in [0.25, 4.0] — uses two chained atempo stages if needed.
    void set_speed(double speed);

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
    double           speed_        = 1.0;

    // Running media-time position for output frames (seconds).
    // Reset by flush(); re-seeded from the first decoded frame's PTS.
    double out_media_pts_  = 0.0;
    bool   pts_inited_     = false;

    // Build (or rebuild) a filter graph with the given speed.
    // speed == 1.0 → passthrough (no filter, graph is null)
    [[nodiscard]] bool build_filter_graph(double speed);
    void free_filter_graph();

    // Push a resampled AVFrame into the filter graph and drain output to player.
    void push_to_filter(AVFrame* frame, AudioPlayer& player);
};
