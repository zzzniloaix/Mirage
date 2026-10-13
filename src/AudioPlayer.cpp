#define MINIAUDIO_IMPLEMENTATION
#include "AudioPlayer.h"
#include "Logger.h"

#include <cstring>
#include <algorithm>
#include <cmath>

static void audio_callback(ma_device* device, void* out, const void*, ma_uint32 frame_count)
{
    auto* player = static_cast<AudioPlayer*>(device->pUserData);
    player->fill_buffer(static_cast<float*>(out), static_cast<int>(frame_count));
}

AudioPlayer::~AudioPlayer()
{
    stop();
}

void AudioPlayer::init_ring(int sample_rate)
{
    sample_rate_ = sample_rate;
    ring_.assign(kRingCapacity, 0.0f);
    max_buffered_ = std::min(kRingCapacity,
        static_cast<size_t>(kMaxBufferedSec * sample_rate) * kChannels);
}

bool AudioPlayer::init(int sample_rate)
{
    init_ring(sample_rate);

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

void AudioPlayer::interrupt()
{
    {
        std::lock_guard lock(ring_mtx_);
        interrupted_ = true;
    }
    space_cv_.notify_all();
}

void AudioPlayer::stop()
{
    interrupt();   // a push() waiting on a stopped device would never wake
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
    std::unique_lock lock(ring_mtx_);
    ++flush_gen_;
    primed_       = false;
    write_pos_    = 0;
    read_pos_     = 0;
    count_        = 0;
    ring_end_pts_ = 0.0;
    seg_head_     = 0;
    seg_count_    = 0;
    clock_.store(0.0);
    // Reset waveform so stale peaks don't show after seek
    wave_head_  = 0;
    wave_count_ = 0;
    wave_accum_ = 0;
    wave_peak_  = 0.0f;
    lock.unlock();
    space_cv_.notify_all();
}

void AudioPlayer::push(const float* samples, int frame_count, double pts, double media_dur)
{
    if (frame_count <= 0) return;
    std::unique_lock lock(ring_mtx_);
    const uint64_t gen = flush_gen_;

    // Tempo of this batch: media seconds per output second.
    const double tempo = media_dur * sample_rate_ / frame_count;

    // Write in chunks as space frees up. ring_end_pts_ only advances by what
    // actually reached the ring, so clock() never runs ahead of audible audio.
    int done = 0;   // frames written so far
    while (done < frame_count) {
        const size_t free_frames = (count_ < max_buffered_) ? (max_buffered_ - count_) / kChannels : 0;
        if (free_frames == 0) {
            if (interrupted_ || muted_.load()) break;
            // Bounded slices so a missed notify (e.g. set_muted) can't hang us.
            space_cv_.wait_for(lock, std::chrono::milliseconds(50));
            if (flush_gen_ != gen) break;   // seek: the rest of this batch is stale
            continue;
        }

        const int    chunk = static_cast<int>(std::min<size_t>(free_frames, frame_count - done));
        const float* src   = samples + static_cast<size_t>(done) * kChannels;
        for (int i = 0; i < chunk * kChannels; ++i) {
            ring_[write_pos_] = src[i];
            write_pos_ = (write_pos_ + 1) % kRingCapacity;
        }
        count_ += static_cast<size_t>(chunk) * kChannels;

        // Record the tempo of what was written. Extend the tail segment when the
        // tempo is unchanged (the common case); if the segment ring is full, fold
        // into the tail — only a burst of >64 speed changes in 2 s could hit that.
        TempoSeg* tail = seg_count_ > 0
            ? &segs_[(seg_head_ + seg_count_ - 1) % kMaxSegs] : nullptr;
        if (tail && (std::abs(tail->tempo - tempo) < 1e-6 || seg_count_ == kMaxSegs)) {
            tail->frames += static_cast<size_t>(chunk);
        } else {
            segs_[(seg_head_ + seg_count_) % kMaxSegs] = { static_cast<size_t>(chunk), tempo };
            ++seg_count_;
        }

        done += chunk;
        // PTS of the hypothetical "next sample" after everything in the buffer
        ring_end_pts_ = pts + media_dur * done / frame_count;
        primed_ = true;
    }

    if (done < frame_count)
        dropped_frames_.fetch_add(frame_count - done);
    if (done == 0 || flush_gen_ != gen) return;   // nothing new / flushed: no stale peaks
    frame_count = done;

    // ── Waveform downsampling (left channel peak, every kWaveStride samples) ──
    for (int i = 0; i < frame_count; ++i) {
        float s = std::abs(samples[i * kChannels]);
        if (s > wave_peak_) wave_peak_ = s;
        if (++wave_accum_ >= kWaveStride) {
            wave_buf_[wave_head_] = wave_peak_;
            wave_head_ = (wave_head_ + 1) % kWaveCap;
            if (wave_count_ < kWaveCap) ++wave_count_;
            wave_accum_ = 0;
            wave_peak_  = 0.0f;
        }
    }
}

void AudioPlayer::set_volume(float v)
{
    volume_.store(std::clamp(v, 0.0f, 1.0f));
}

void AudioPlayer::copy_waveform(float* out, int n)
{
    std::lock_guard lock(ring_mtx_);
    int avail = std::min(n, wave_count_);
    int start = (wave_count_ < kWaveCap) ? 0 : wave_head_;
    for (int i = 0; i < avail; ++i)
        out[i] = wave_buf_[(start + i) % kWaveCap];
    for (int i = avail; i < n; ++i)
        out[i] = 0.0f;
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

    // Hard mute: output silence without draining the ring buffer.
    // This takes effect within the current callback period, unlike ma_device_stop
    // which has CoreAudio-level latency before the last queued buffer drains.
    if (muted_.load()) {
        std::memset(out, 0, static_cast<size_t>(n) * sizeof(float));
        return;
    }

    std::unique_lock lock(ring_mtx_);

    int available = static_cast<int>(count_);
    int to_read   = std::min(n, available);
    int silence   = n - to_read;

    for (int i = 0; i < to_read; i++) {
        out[i] = ring_[read_pos_];
        read_pos_ = (read_pos_ + 1) % kRingCapacity;
    }
    count_ -= to_read;

    // Consume the frames just read from the head tempo segments.
    size_t frames_read = static_cast<size_t>(to_read / kChannels);
    while (frames_read > 0 && seg_count_ > 0) {
        TempoSeg& head = segs_[seg_head_];
        size_t take = std::min(frames_read, head.frames);
        head.frames -= take;
        frames_read -= take;
        if (head.frames == 0) {
            seg_head_ = (seg_head_ + 1) % kMaxSegs;
            --seg_count_;
        }
    }

    if (silence > 0) {
        std::memset(out + to_read, 0, silence * sizeof(float));
        if (primed_) {
            underruns_.fetch_add(1);
            silent_frames_.fetch_add(silence / kChannels);
        }
    }

    // Apply volume gain
    float vol = volume_.load();
    if (vol != 1.0f) {
        for (int i = 0; i < to_read; ++i) out[i] *= vol;
    }

    // Audio clock = PTS at end of buffer minus media time still buffered.
    // Each buffered frame covers `tempo` media frames (tempo = playback speed).
    double buffered_media = 0.0;
    for (int i = 0; i < seg_count_; ++i) {
        const TempoSeg& seg = segs_[(seg_head_ + i) % kMaxSegs];
        buffered_media += static_cast<double>(seg.frames) * seg.tempo;
    }
    clock_.store(ring_end_pts_ - buffered_media / sample_rate_);

    lock.unlock();
    if (to_read > 0) space_cv_.notify_one();
}
