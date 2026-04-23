#pragma once

#include <vector>
#include <mutex>
#include <atomic>
#include <cstdint>

#include "miniaudio.h"  // declarations only — implementation in AudioPlayer.cpp

// Ring buffer + miniaudio device.
// AudioDecoder writes float stereo samples via push().
// miniaudio callback pulls them via fill_buffer().
class AudioPlayer {
public:
    AudioPlayer() = default;
    ~AudioPlayer();

    AudioPlayer(const AudioPlayer&) = delete;
    AudioPlayer& operator=(const AudioPlayer&) = delete;

    [[nodiscard]] bool init(int sample_rate);
    void stop();
    void set_paused(bool p);

    // Discard all buffered audio after a seek.
    void flush();

    // Called from decode thread.
    // pts: presentation timestamp (seconds) of the first sample in this batch.
    void push(const float* samples, int frame_count, double pts);

    // Called from miniaudio callback thread.
    void fill_buffer(float* out, int frame_count);

    int    sample_rate() const { return sample_rate_; }
    double clock()       const { return clock_.load(); }

    // Estimated device output latency (seconds): time between fill_buffer and audible output.
    double output_latency_seconds() const;

    // Copy the last n downsampled peak values [0,1] into out[0..n-1], chronological order.
    // Each peak represents the max absolute amplitude over kWaveStride samples.
    // Thread-safe.
    void copy_waveform(float* out, int n);

    // Volume control [0, 1]. Applied as a gain multiplier in fill_buffer().
    void  set_volume(float v);
    float volume()  const { return volume_.load(); }

    // Hard mute: fill_buffer outputs zeros without touching the ring buffer.
    // Unlike set_paused(), this works at the sample level so it takes effect
    // within the current callback period regardless of CoreAudio device state.
    void set_muted(bool m) { muted_.store(m); }

    static constexpr int kWaveCap    = 1200;  // ≈3.6 s at 48 kHz / kWaveStride
    static constexpr int kWaveStride = 40;    // samples per peak entry

private:
    ma_device device_      = {};
    bool      running_     = false;
    int       sample_rate_ = 48000;

    // Simple ring buffer (interleaved float stereo)
    static constexpr int    kChannels     = 2;
    static constexpr size_t kRingCapacity = 48000 * kChannels * 2; // 2s headroom

    std::vector<float> ring_;
    size_t             write_pos_    = 0;
    size_t             read_pos_     = 0;
    size_t             count_        = 0;   // samples (not frames)
    mutable std::mutex ring_mtx_;

    // Audio clock: PTS of the sample currently at the speaker output.
    // Updated in fill_buffer() — readable from any thread via clock().
    std::atomic<double> clock_{ 0.0 };
    double              ring_end_pts_{ 0.0 }; // PTS right after last pushed sample

    // Waveform downsampled peak ring buffer (updated in push() under ring_mtx_)
    float wave_buf_[kWaveCap]{};
    int   wave_head_  = 0;
    int   wave_count_ = 0;
    int   wave_accum_ = 0;
    float wave_peak_  = 0.0f;

    std::atomic<float> volume_{ 1.0f };
    std::atomic<bool>  muted_{ false };
};
