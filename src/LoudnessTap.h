#pragma once

// The loudness panel's view of the audio (L): every frame of the playing
// track as decoded — in its own channel layout and sample rate, before the
// stereo downmix, the speed change and the volume — measured on the audio
// decode thread (LoudnessMeter.h) and handed to the render thread by media
// time. Measures only while enabled (the panel is open): playback without
// the panel does no extra work.
//
// BS.1770 channel weights from the layout: the LFE channels don't count,
// side / back / surround-direct channels (top-back too) weigh 1.41, the
// rest 1.0 — FFmpeg's ebur128 filter does the same.

#include "LoudnessMeter.h"

extern "C" {
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
}

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct SwrContext;

class LoudnessTap {
public:
    LoudnessTap() = default;
    ~LoudnessTap();
    LoudnessTap(const LoudnessTap&)            = delete;
    LoudnessTap& operator=(const LoudnessTap&) = delete;

    // ── Audio decode thread ──────────────────────────────────────────────────
    // A frame just decoded (any sample format); time_base: its stream's.
    void on_frame(const AVFrame* f, AVRational time_base);
    // A seek or a track switch: the windows start over with the next frame.
    void on_flush() { restart_.store(true); }

    // ── Any thread ───────────────────────────────────────────────────────────
    void set_enabled(bool on);
    [[nodiscard]] bool enabled() const { return enabled_.load(std::memory_order_relaxed); }
    // Integrated loudness, LRA and the maximum true peak start again (at the
    // next frame); momentary and short-term go on.
    void request_reset() { reset_req_.store(true); }

    // ── Render thread ────────────────────────────────────────────────────────
    // The block heard at `pts` (the audio clock).
    [[nodiscard]] bool at(double pts, loudness::Block& out) const { return feed_.at(pts, out); }
    void history(double from, double to, std::vector<loudness::Block>& out) const { feed_.copy_range(from, to, out); }

    struct Layout {
        int                           rate = 0;
        std::string                   name;      // "5.1(side)"
        std::vector<std::string>      labels;    // per channel: "FL", "LFE", ...
        std::vector<loudness::Weight> weights;
        double                        since = 0.0;   // media time integrated loudness counts from
        std::string                   error;         // why nothing is measured (empty: fine)
    };
    [[nodiscard]] Layout layout() const;

private:
    // Rebuild the meter for f's layout / rate / format. False: not measurable.
    bool configure(const AVFrame* f);
    void set_since(double pts);

    std::unique_ptr<loudness::Meter> meter_;      // decode thread from here ...
    AVChannelLayout                  layout_{};
    int                              rate_   = 0;
    int                              format_ = -1;
    SwrContext*                      to_planar_ = nullptr;   // non-planar-float input → planar float
    std::vector<float>               buf_;
    std::vector<const float*>        planes_;
    double                           next_pts_ = 0.0;
    bool                             unusable_ = false;      // this layout / format can't be measured (... to here)
    std::atomic<bool>                restart_{ true };
    std::atomic<bool>                enabled_{ false };
    std::atomic<bool>                reset_req_{ false };
    loudness::Feed                   feed_;
    mutable std::mutex               info_mtx_;
    Layout                           info_;
};
