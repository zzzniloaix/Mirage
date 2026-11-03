#pragma once

// Loudness metering per ITU-R BS.1770-4 and EBU R128 (EBU Tech 3341 meter,
// Tech 3342 loudness range), for the audio panel: the audio decode thread
// pushes the decoded samples of the playing track into a Meter and publishes
// what it measures into a Feed; the render thread reads the Feed at the
// audio clock.
//
// A Meter measures in blocks of 100 ms of samples (block_frames(): rate / 10
// rounded, 4410 at 44.1 kHz). Each block reports:
//  * momentary (the last 400 ms) and short-term (the last 3 s) loudness,
//    LUFS = −0.691 + 10·log10(Σ_c G_c · mean(z_c²)), z = the channel
//    K-weighted (BS.1770's pre-filter and RLB high-pass, designed for the
//    sample rate from their analog prototypes as libebur128 does), G = the
//    channel's Weight;
//  * integrated loudness since the last reset: the 400 ms windows ending at
//    every block (75 % overlap) gated at −70 LUFS, then at 10 LU below the
//    mean of those that passed;
//  * loudness range (Tech 3342): P95 − P10 of the short-term values ending at
//    every block, gated at −70 LUFS, then at 20 LU below their mean;
//  * true peak: each channel oversampled ×4 below 96 kHz, ×2 below 192 kHz
//    (none above) by a windowed-sinc polyphase interpolator designed at
//    construction; per channel for the block, and the maximum over all
//    channels since the last reset;
//  * per channel: sample peak, true peak and unweighted RMS; the phase
//    correlation of channels 0 and 1.
//
// Integrated loudness and LRA come from histograms of the gated values
// (0.01 LU bins, each with the exact energy sum of its values), not from the
// values themselves: fixed memory however long the program. A block's true
// peak also covers inter-sample points of the block before: the interpolator
// (kInterpolatorTaps long) delivers them 16 samples late. Right after
// construction or reset() it has no audio before the first sample: for the
// first 31 samples only the samples themselves count, not inter-sample values
// made up from the silence before them (the interpolator would ring at the
// step from silence: a full-scale sine starting at its crest would read over
// 0 dBTP).
//
// Results do not depend on how the samples are split into pushes. push()
// does not allocate (everything is sized at construction) and costs about 110
// multiply-adds per sample and channel below 96 kHz, 96 of them the
// true-peak interpolator's.
//
// Thread safety: a Meter is used from one thread (it pushes and resets); a
// Feed is mutex-protected, for one writer and any number of readers.

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <vector>

namespace loudness {

// A channel's weight G in the loudness sum (BS.1770-4 table 3): 1.0 for
// left, right, centre (and any other front channel), 1.41 (+1.5 dB) for the
// surround channels, 0 for LFE. The channel is measured either way (peaks,
// RMS, true peak).
enum class Weight : std::uint8_t { Normal, Surround, Excluded };

[[nodiscard]] constexpr double gain(Weight w)
{
    return w == Weight::Normal ? 1.0 : w == Weight::Surround ? 1.41 : 0.0;
}

inline constexpr int   kMaxChannels = 16;
// A level of nothing: digital silence, or no window full yet.
inline constexpr float kSilent = -std::numeric_limits<float>::infinity();
// Taps per phase of the true-peak interpolator (its length in samples).
inline constexpr int   kInterpolatorTaps = 32;

namespace detail {
constexpr std::array<float, kMaxChannels> silent_channels()
{
    std::array<float, kMaxChannels> a{};
    for (float& v : a) v = kSilent;
    return a;
}
} // namespace detail

// One block (100 ms of samples). Per-channel entries from `channels` on are
// kSilent.
struct Block {
    double end_pts       = 0.0;       // media seconds at the end: the pts of its first sample + its length
    float  momentary     = kSilent;   // LUFS, the 400 ms ending here (kSilent until 400 ms were pushed)
    float  short_term    = kSilent;   // LUFS, the 3 s ending here (kSilent until 3 s were pushed)
    float  integrated    = kSilent;   // LUFS since the last reset, gated (kSilent: nothing passed the gates)
    float  lra           = 0.0f;      // LU since the last reset (Tech 3342)
    float  true_peak_max = kSilent;   // dBTP, all channels, since the last reset
    int    channels      = 0;
    std::array<float, kMaxChannels> peak      = detail::silent_channels();   // sample peak in the block, dBFS
    std::array<float, kMaxChannels> true_peak = detail::silent_channels();   // true peak in the block, dBTP (≥ peak)
    std::array<float, kMaxChannels> rms       = detail::silent_channels();   // unweighted, dBFS: a full-scale sine reads −3.01
    // Σxy / √(Σx²·Σy²) of channels 0 and 1 in the block: +1 the same signal,
    // −1 one inverted, ~0 unrelated. NaN with fewer than 2 channels or one
    // of them silent (RMS below −150 dBFS).
    float correlation = std::numeric_limits<float>::quiet_NaN();
};

// A biquad section, a0 normalized to 1: y = b0·x + b1·x₋₁ + b2·x₋₂ − a1·y₋₁ − a2·y₋₂.
struct Biquad {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
};
// BS.1770's K-weighting for a sample rate: the pre-filter (a high shelf,
// +4 dB from about 5 kHz up), then the RLB high-pass (~38 Hz, b = 1, −2, 1).
// At 48 kHz these are BS.1770-4's tables 1 and 2.
struct KWeighting {
    Biquad shelf, highpass;
};
[[nodiscard]] KWeighting k_weighting(int sample_rate);

// Frames in a 100 ms block: rate / 10, rounded (at least 1).
[[nodiscard]] int block_frames(int sample_rate);
// The true-peak oversampling factor: 4 below 96 kHz, 2 below 192 kHz, else 1.
[[nodiscard]] int true_peak_oversampling(int sample_rate);

class Meter {
public:
    // channels: the weight of each channel, in the order push() gets them;
    // only the first kMaxChannels are measured. sample_rate: 8 kHz and up
    // (the K-weighting needs a rate above 3.4 kHz).
    Meter(int sample_rate, std::vector<Weight> channels);

    // Planar float samples data[c][0 .. frames) for c < channels(), nominal
    // level ±1.0; pts = media seconds of the first sample (not finite: the
    // previous push's end — 0 for the first). Calls on_block(const Block&)
    // for every block this push completes, in order, before returning; it
    // must not call back into the meter. A block's end_pts follows the pts
    // of the push its first sample came in.
    template <class F>
    void push(const float* const* data, int frames, double pts, F&& on_block)
    {
        pts = begin_push(pts, frames);
        int done = 0;
        while (done < frames) {
            done += feed(data, done, frames - done, pts);
            if (ready_) {
                ready_ = false;
                on_block(static_cast<const Block&>(block_));
            }
        }
    }

    // Restart integrated loudness, LRA and true_peak_max from here: they
    // take in only the windows / samples after this call, as a fresh
    // meter's would. Momentary and short-term go on uninterrupted.
    void reset_integrated();
    // Start over (after a seek): filter states, windows, the block in
    // progress (its samples are dropped) and everything reset_integrated()
    // clears. The next push reads as if into a new meter.
    void reset();

    [[nodiscard]] int sample_rate() const { return rate_; }
    [[nodiscard]] int channels() const { return nch_; }
    [[nodiscard]] int block_frames() const { return block_n_; }
    [[nodiscard]] int oversampling() const { return os_; }

private:
    // One channel's filter state and what the block in progress has
    // measured so far.
    struct Channel {
        double gain = 1.0;
        double s1 = 0.0, s2 = 0.0;      // pre-filter state (transposed direct form II)
        double t1 = 0.0, t2 = 0.0;      // RLB high-pass state
        double kw = 0.0;                // Σ z² (K-weighted)
        double sq = 0.0;                // Σ x²
        float  peak = 0.0f;             // max |x|
        float  ip = 0.0f;               // max |inter-sample value|
        float  pre_peak = 0.0f;         // ... the part of the block before reset_integrated()
        float  pre_ip = 0.0f;
    };
    // Gated values (400 ms energies for integrated, 3 s ones for LRA) in
    // 0.01 LU bins from −70 LUFS up.
    struct Histogram {
        std::vector<std::uint32_t> count;    // values in each bin
        std::vector<double>        energy;   // their energy sum
        std::uint64_t n     = 0;             // all values
        double        total = 0.0;           // all energies
        int           lo = 0, hi = -1;       // occupied bins
    };
    // What the inter-sample values computed from a run of samples count
    // for: nothing (their window reaches back before reset()), the block only
    // (they lie before a reset_integrated()), or the block and true_peak_max.
    enum class Inter { Skip, Pre, Post };

    double begin_push(double pts, int frames);
    // Up to the end of the block in progress; returns the frames taken.
    int  feed(const float* const* data, int offset, int frames, double pts);
    void weigh(Channel& ch, const float* x, int n) const;
    template <int Phases>
    void interpolate(Channel& ch, float* hist, const float* x, int n, Inter mode) const;
    void finish_block();
    [[nodiscard]] double window(int blocks) const;   // mean energy of the last `blocks` blocks
    void clear_integrated();

    static void  clear(Histogram& h);
    static void  add(Histogram& h, double energy, double lufs);
    [[nodiscard]] static double gated_mean(const Histogram& h, double rel_lu);   // 0: none
    [[nodiscard]] static float  range(const Histogram& h);

    int        rate_;
    int        nch_;
    int        block_n_;
    int        os_;
    KWeighting kw_;
    std::array<Channel, kMaxChannels> ch_{};

    // True-peak interpolator: phases 1 .. os_−1 (phase 0 is the sample
    // itself), coefficient k of each phase interleaved; per channel the last
    // kTaps samples, stored twice so they are contiguous from tp_pos_ + 1.
    std::vector<float> tp_coef_;
    std::vector<float> tp_hist_;
    int tp_pos_  = 0;
    int tp_skip_ = 0;    // samples whose inter-sample values are dropped (after reset())
    int tp_hold_ = 0;    // ... count only for the block (just after reset_integrated())

    // The block in progress.
    int    fill_        = 0;     // frames in it
    double block_start_ = 0.0;   // pts of its first sample
    double xy_          = 0.0;   // Σ x0·x1
    double next_pts_    = 0.0;   // pts after the last sample pushed
    bool   ready_       = false; // block_ completed, not handed out yet
    Block  block_;

    // Weighted energy Σ_c G_c·Σz² of the last 30 blocks, a ring.
    std::array<double, 30> ring_{};
    int          ring_pos_ = 0;
    std::int64_t blocks_   = 0;   // completed since reset()
    std::int64_t since_ri_ = 0;   // ... since reset_integrated() (−1: called inside a block)

    Histogram hist_i_;            // momentary energies, for integrated
    Histogram hist_s_;            // short-term energies, for LRA
    float     integrated_ = kSilent;
    float     lra_        = 0.0f;
    double    tp_max_     = 0.0;  // linear
};

// The blocks of the last minute or so, handed from the audio decode thread
// to the render thread.
class Feed {
public:
    // Keeps the newest blocks covering `seconds` (at 100 ms each).
    explicit Feed(double seconds = 60.0);

    // Writer: append a block (the oldest goes once the history is full).
    void publish(const Block& b);
    // Writer or reader: forget everything (a flush, a track switch).
    void clear();

    // The newest block with end_pts <= pts — at the audio clock, the block
    // that has just been heard. False if there is none. Newest means
    // published last: after a jump back in pts, clear() first, or a clock
    // still before the jump gets blocks from after it.
    [[nodiscard]] bool at(double pts, Block& out) const;
    // Every block with end_pts in [from, to], oldest first, into `out`
    // (replacing what it held; its capacity is reused).
    void copy_range(double from, double to, std::vector<Block>& out) const;
    [[nodiscard]] std::size_t size() const;

private:
    mutable std::mutex mtx_;
    std::vector<Block> ring_;
    std::size_t        head_  = 0;   // the oldest
    std::size_t        count_ = 0;
};

} // namespace loudness
