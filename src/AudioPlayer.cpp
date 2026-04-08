#define MINIAUDIO_IMPLEMENTATION
#include "AudioPlayer.h"
#include "Logger.h"

#include <cstring>
#include <algorithm>

static void audio_callback(ma_device* device, void* out, const void*, ma_uint32 frame_count)
{
    auto* player = static_cast<AudioPlayer*>(device->pUserData);
    player->fill_buffer(static_cast<float*>(out), static_cast<int>(frame_count));
}

AudioPlayer::~AudioPlayer()
{
    stop();
}

bool AudioPlayer::init(int sample_rate)
{
    sample_rate_ = sample_rate;
    ring_.resize(kRingCapacity, 0.0f);

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format   = ma_format_f32;
    cfg.playback.channels = kChannels;
    cfg.sampleRate        = static_cast<ma_uint32>(sample_rate);
    cfg.dataCallback      = audio_callback;
    cfg.pUserData         = this;

    if (ma_device_init(nullptr, &cfg, &device_) != MA_SUCCESS) {
        logger::error("AudioPlayer: ma_device_init failed");
        return false;
    }

    if (ma_device_start(&device_) != MA_SUCCESS) {
        logger::error("AudioPlayer: ma_device_start failed");
        ma_device_uninit(&device_);
        return false;
    }

    running_ = true;
    logger::info("AudioPlayer: {} Hz stereo float32  device latency {:.1f} ms ({} frames × {} periods)",
        sample_rate,
        output_latency_seconds() * 1000.0,
        device_.playback.internalPeriodSizeInFrames,
        device_.playback.internalPeriods);
    return true;
}

void AudioPlayer::stop()
{
    if (running_) {
        ma_device_stop(&device_);
        ma_device_uninit(&device_);
        running_ = false;
    }
}

void AudioPlayer::set_paused(bool p)
{
    if (!running_) return;
    if (p) ma_device_stop(&device_);
    else   ma_device_start(&device_);
}

void AudioPlayer::flush()
{
    std::lock_guard lock(ring_mtx_);
    write_pos_    = 0;
    read_pos_     = 0;
    count_        = 0;
    ring_end_pts_ = 0.0;
    clock_.store(0.0);
}

void AudioPlayer::push(const float* samples, int frame_count, double pts)
{
    const int n = frame_count * kChannels;
    std::lock_guard lock(ring_mtx_);

    for (int i = 0; i < n; i++) {
        if (count_ >= kRingCapacity) break;
        ring_[write_pos_] = samples[i];
        write_pos_ = (write_pos_ + 1) % kRingCapacity;
        count_++;
    }

    // PTS of the hypothetical "next sample" after everything in the buffer
    ring_end_pts_ = pts + static_cast<double>(frame_count) / sample_rate_;
}

double AudioPlayer::output_latency_seconds() const
{
    if (!running_) return 0.0;
    ma_uint32 frames = device_.playback.internalPeriodSizeInFrames
                     * device_.playback.internalPeriods;
    return static_cast<double>(frames) / static_cast<double>(sample_rate_);
}

void AudioPlayer::fill_buffer(float* out, int frame_count)
{
    const int n = frame_count * kChannels;
    std::lock_guard lock(ring_mtx_);

    int available = static_cast<int>(count_);
    int to_read   = std::min(n, available);
    int silence   = n - to_read;

    for (int i = 0; i < to_read; i++) {
        out[i] = ring_[read_pos_];
        read_pos_ = (read_pos_ + 1) % kRingCapacity;
    }
    count_ -= to_read;

    if (silence > 0)
        std::memset(out + to_read, 0, silence * sizeof(float));

    // Audio clock = PTS at end of buffer minus time still buffered
    clock_.store(ring_end_pts_ - static_cast<double>(count_ / kChannels) / sample_rate_);
}
