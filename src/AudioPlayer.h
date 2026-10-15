#pragma once

#include <vector>
#include <mutex>
#include <condition_variable>
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
    // Allocate the ring buffer only — no miniaudio device. Unit tests drive
    // push()/fill_buffer() by hand to check the ring and clock math.
    void init_ring(int sample_rate);
    void stop();
    void set_paused(bool p);

    // Discard all buffered audio after a seek. Also wakes a push() blocked on a
    // full ring; that push drops the rest of its (now stale) batch.
    void flush();

    // Called from decode thread.
    // pts: presentation timestamp (seconds) of the first sample in this batch.
    // media_dur: media seconds covered by this batch. Differs from
    // frame_count / sample_rate when atempo has time-stretched the audio.
    // Blocks while the ring is full so no audio is dropped (the clock would
    // jump ahead of what is audible). Gives up — dropping the remainder — on
    // flush(), interrupt(), or while muted (a muted ring doesn't drain).
    // Waits indefinitely while paused: the device restarts on resume.
    void push(const float* samples, int frame_count, double pts, double media_dur);

    // Shutdown: wake any blocked push() and stop it from ever waiting again
    // (it drops instead). Call before joining the audio decode thread.
    void interrupt();

    // Called from miniaudio callback thread.
    void fill_buffer(float* out, int frame_count);

    // Audio has been pushed since the last flush() (the device has something
    // to play). Thread-safe.
    bool primed() const { std::lock_guard lk(ring_mtx_); return primed_; }

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
    void set_muted(bool m) { muted_.store(m); space_cv_.notify_all(); }

    // QA counters (monotonic; reset only by construction).
    // Underruns: callbacks that ran out of buffered audio mid-playback (only
    // counted once audio has been pushed since the last flush — the post-seek
    // refill gap is expected). Silent frames: frames zero-filled by those.
    // Dropped frames: frames push() discarded (flush/mute/interrupt).
    int64_t underrun_count()   const { return underruns_.load(); }
    int64_t silent_frames()    const { return silent_frames_.load(); }
    int64_t dropped_frames()   const { return dropped_frames_.load(); }

    static constexpr int kWaveCap    = 1200;  // ≈3.6 s at 48 kHz / kWaveStride
    static constexpr int kWaveStride = 40;    // samples per peak entry
    static constexpr int kChannels   = 2;     // interleaved float stereo

    // Frames push() will buffer before blocking (see kMaxBufferedSec).
    int max_buffered_frames() const { return static_cast<int>(max_buffered_ / kChannels); }

private:
    ma_device device_      = {};
    bool      running_     = false;
    int       sample_rate_ = 48000;

    // Simple ring buffer (interleaved float stereo)
    static constexpr size_t kRingCapacity = 48000 * kChannels * 2; // 2s headroom

    // push() blocks once this much (wall) audio is buffered. Kept well under the
    // ring capacity because buffered audio was rendered at the speed in effect
    // when it was decoded: a speed change only becomes audible — and moves the
    // clock video follows — after the buffer drains. 0.5 s still rides out
    // decode/demux hiccups many times longer than one device period.
    static constexpr double kMaxBufferedSec = 0.5;
    size_t max_buffered_ = kRingCapacity;   // samples; set by init_ring()

    std::vector<float> ring_;
    size_t             write_pos_    = 0;
    size_t             read_pos_     = 0;
    size_t             count_        = 0;   // samples (not frames)
    mutable std::mutex ring_mtx_;
    std::condition_variable space_cv_;          // signalled when ring space frees up
    uint64_t           flush_gen_    = 0;       // bumped by flush() (under ring_mtx_)
    bool               interrupted_  = false;   // set by interrupt() (under ring_mtx_)
    bool               primed_       = false;   // audio pushed since last flush

    // Audio clock: PTS of the sample currently at the speaker output.
    // Updated in fill_buffer() — readable from any thread via clock().
    std::atomic<double> clock_{ 0.0 };
    double              ring_end_pts_{ 0.0 }; // PTS right after last pushed sample

    // Buffered audio as runs of frames sharing one tempo (media sec per output sec),
    // so the clock stays correct while audio rendered at an old speed drains.
    // Fixed-size ring: no allocation on the audio callback thread.
    struct TempoSeg { size_t frames; double tempo; };
    static constexpr int kMaxSegs = 64;
    TempoSeg segs_[kMaxSegs]{};
    int      seg_head_  = 0;
    int      seg_count_ = 0;

    // Waveform downsampled peak ring buffer (updated in push() under ring_mtx_)
    float wave_buf_[kWaveCap]{};
    int   wave_head_  = 0;
    int   wave_count_ = 0;
    int   wave_accum_ = 0;
    float wave_peak_  = 0.0f;

    std::atomic<float> volume_{ 1.0f };
    std::atomic<bool>  muted_{ false };

    std::atomic<int64_t> underruns_{ 0 };
    std::atomic<int64_t> silent_frames_{ 0 };
    std::atomic<int64_t> dropped_frames_{ 0 };
};
