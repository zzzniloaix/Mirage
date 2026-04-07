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
};
