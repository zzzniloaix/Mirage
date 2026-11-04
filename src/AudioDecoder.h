#pragma once

#include "Decoder.h"
#include "AudioPlayer.h"
#include "LoudnessTap.h"

#include <atomic>

extern "C" {
#include <libswresample/swresample.h>
#include <libavutil/channel_layout.h>
#include <libavutil/downmix_info.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
}

// Wraps Decoder (audio codec) + SwrContext (resampling) + atempo filter graph.
// decode() pulls frames from the codec, resamples to float stereo, runs through
// the atempo filter (for speed != 1.0), and writes to the AudioPlayer ring buffer.
//
// The resampler is made for the decoded frames — their layout, sample format
// and rate, and the downmix levels the stream carries (AC-3, E-AC-3) — and
// made again when those change. Without downmix levels FFmpeg's defaults
// apply: centre and surrounds at 0.707107 (-3 dB), the LFE left out.
class AudioDecoder {
public:
    // How the resampler mixes a surround stream down to stereo.
    struct MixLevels {
        bool   from_stream = false;   // the stream's AV_FRAME_DATA_DOWNMIX_INFO; else swr's defaults
        double center   = 0.0;        // linear factors, as swr's options take them
        double surround = 0.0;
        double lfe      = 0.0;        // 0: swr's default, the LFE left out
        int    matrix   = AV_MATRIX_ENCODING_NONE;
        bool operator==(const MixLevels&) const = default;
    };
    // The levels aresample takes from a stream's downmix info: the Lt/Rt
    // ones, with swr's matrix encoding, when the stream prefers an Lt/Rt or
    // Pro Logic II downmix, else the Lo/Ro ones; the LFE level only when the
    // stream gives one (AC-3's decoder gives 0 when it carries none).
    static MixLevels stream_mix_levels(const AVDownmixInfo& di);

    AudioDecoder() = default;
    ~AudioDecoder();

    AudioDecoder(const AudioDecoder&) = delete;
    AudioDecoder& operator=(const AudioDecoder&) = delete;

    // par: audio stream codecpar
    // player: where to push resampled samples
    // time_base: stream time_base used to convert frame->pts to seconds
    [[nodiscard]] bool open(AVCodecParameters* par, AudioPlayer& player, AVRational time_base);

    // Send one packet, pull all resulting frames, resample, push to player.
    // With no decoder open (a reopen that failed: no decoder for the
    // track), the packet's time is pushed as silence instead, so the audio
    // clock — the master clock video follows — keeps going.
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

    // The loudness meters' tap (LoudnessTap.h): every decoded frame, as
    // decoded, before the resampler; told of every flush / reopen. Set
    // before the decode thread starts; null = none.
    void set_tap(LoudnessTap* tap) { tap_ = tap; }

    // The audio codec's decode errors (Decoder::errors). Any thread.
    Decoder::Errors decode_errors() const { return decoder_.errors(); }

private:
    LoudnessTap* tap_ = nullptr;
    Decoder     decoder_;
    SwrContext* swr_    = nullptr;
    AVFrame*    frame_  = nullptr;   // raw decoded frame (resampled input)
    AVFrame*    swr_frame_ = nullptr; // resampled float stereo frame

    // What swr_ was made for (swr_built_; swr_ null: it could not be made).
    bool            swr_built_  = false;
    AVChannelLayout swr_layout_ = {};
    int             swr_format_ = -1;
    int             swr_rate_   = 0;
    MixLevels       swr_levels_;

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
    // push_silence(): where the silence pushed so far ends (media seconds);
    // reset by flush() / reopen().
    bool   silent_         = false;
    double silent_until_   = 0.0;

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

    // A resampler for frame `f`: the one there, or a new one when f's input
    // or downmix levels differ (the old one drained into player first).
    // False when none can be made for it (the frame is dropped).
    [[nodiscard]] bool update_resampler(const AVFrame* f, AudioPlayer& player);
    void drain_resampler(AudioPlayer& player);
    void free_resampler();

    // decode() without a decoder: silence up to the end of `pkt` (to its
    // start when it has no duration), from where the last silence ended or,
    // for the first, from the packet's start; nothing before start_at().
    void push_silence(const AVPacket* pkt, AudioPlayer& player);
};
