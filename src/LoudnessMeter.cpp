#include "LoudnessMeter.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <numbers>

namespace loudness {

namespace {

constexpr double kAbsGate    = -70.0;   // LUFS
constexpr double kRelGateI   = -10.0;   // LU below the mean of what passed kAbsGate: integrated
constexpr double kRelGateLra = -20.0;   // ... LRA
constexpr int    kMomentary  = 4;       // blocks: 400 ms
constexpr int    kShortTerm  = 30;      // blocks: 3 s

// Histogram bins: 0.01 LU from the absolute gate up to +30 LUFS; anything
// louder goes into the top bin (with its exact energy).
constexpr double kHistMin  = kAbsGate;
constexpr double kHistStep = 0.01;
constexpr int    kHistBins = 10000;

constexpr int kTaps = kInterpolatorTaps;
static_assert(kTaps % 2 == 0, "interpolate() sums even and odd taps apart");
// Kaiser window of the interpolator, 32 taps per phase. Its passband error:
// MEASUREMENT PENDING.
constexpr double kKaiserBeta = 4.5;

// Mean square below −150 dBFS: silent, for the correlation.
constexpr double kSilentPower = 1e-15;
// Filter state below this is flushed at block ends, so it never decays into
// subnormal numbers (slow on some CPUs) through a long silence.
constexpr double kTinyState = 1e-25;

double lufs_of(double energy)
{
    return energy > 0.0 ? -0.691 + 10.0 * std::log10(energy) : -std::numeric_limits<double>::infinity();
}

float db_amplitude(double v) { return v > 0.0 ? static_cast<float>(20.0 * std::log10(v)) : kSilent; }
float db_power(double p)     { return p > 0.0 ? static_cast<float>(10.0 * std::log10(p)) : kSilent; }

// The bin holding a value of `lufs` (finite, >= kHistMin).
int bin_of(double lufs)
{
    const double x = (lufs - kHistMin) / kHistStep;
    if (x >= kHistBins) return kHistBins - 1;
    return std::max(0, static_cast<int>(x));
}

// The first bin a gate at `lufs` lets through: bins whose centre is at or
// above it (kHistBins: none).
int first_bin(double lufs)
{
    const double x = std::ceil((lufs - kHistMin) / kHistStep - 0.5);
    if (!(x > 0.0)) return 0;
    if (x >= kHistBins) return kHistBins;
    return static_cast<int>(x);
}

// Modified Bessel function of the first kind, order 0 (power series).
double bessel_i0(double x)
{
    const double q = x * x / 4.0;
    double sum = 1.0, term = 1.0;
    for (int k = 1; k < 100 && term > sum * 1e-17; ++k) {
        term *= q / (static_cast<double>(k) * k);
        sum += term;
    }
    return sum;
}

// The true-peak interpolator for oversampling `factor`: a Kaiser-windowed
// sinc cut off at the input's Nyquist frequency, as polyphase filters over
// the last kTaps samples. Phase p (1 .. factor−1) interpolates p/factor of a
// sample after the window's sample kTaps/2 − 1; phase 0 would be that sample
// itself. Each phase is scaled to a DC gain of exactly 1. Coefficient k of
// phase p (k = 0: the oldest sample) is at [k·(factor−1) + p − 1].
std::vector<float> design_interpolator(int factor)
{
    const int phases = factor - 1;
    std::vector<float> coef(static_cast<std::size_t>(kTaps) * phases);
    const double i0_beta = bessel_i0(kKaiserBeta);
    for (int p = 1; p < factor; ++p) {
        const double at = kTaps / 2 - 1 + static_cast<double>(p) / factor;
        double h[kTaps];
        double sum = 0.0;
        for (int k = 0; k < kTaps; ++k) {
            const double t = k - at;   // never 0: p > 0
            const double r = 2.0 * t / kTaps;
            const double w = bessel_i0(kKaiserBeta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0_beta;
            h[k] = std::sin(std::numbers::pi * t) / (std::numbers::pi * t) * w;
            sum += h[k];
        }
        for (int k = 0; k < kTaps; ++k)
            coef[static_cast<std::size_t>(k) * phases + (p - 1)] = static_cast<float>(h[k] / sum);
    }
    return coef;
}

} // namespace

KWeighting k_weighting(int sample_rate)
{
    const double fs = sample_rate;
    KWeighting k;
    {
        // Pre-filter: a high shelf from its analog prototype.
        constexpr double f0 = 1681.974450955533, g = 3.999843853973347, q = 0.7071752369554196;
        const double kk = std::tan(std::numbers::pi * f0 / fs);
        const double vh = std::pow(10.0, g / 20.0);
        const double vb = std::pow(vh, 0.4996667741545416);
        const double a0 = 1.0 + kk / q + kk * kk;
        k.shelf = { (vh + vb * kk / q + kk * kk) / a0, 2.0 * (kk * kk - vh) / a0,
                    (vh - vb * kk / q + kk * kk) / a0, 2.0 * (kk * kk - 1.0) / a0,
                    (1.0 - kk / q + kk * kk) / a0 };
    }
    {
        // RLB high-pass. Its numerator stays 1, −2, 1 as in BS.1770's table.
        constexpr double f0 = 38.13547087602444, q = 0.5003270373238773;
        const double kk = std::tan(std::numbers::pi * f0 / fs);
        const double a0 = 1.0 + kk / q + kk * kk;
        k.highpass = { 1.0, -2.0, 1.0, 2.0 * (kk * kk - 1.0) / a0, (1.0 - kk / q + kk * kk) / a0 };
    }
    return k;
}

int block_frames(int sample_rate) { return std::max(1, (sample_rate + 5) / 10); }

int true_peak_oversampling(int sample_rate)
{
    return sample_rate < 96000 ? 4 : sample_rate < 192000 ? 2 : 1;
}

// ── Meter ────────────────────────────────────────────────────────────────────

Meter::Meter(int sample_rate, std::vector<Weight> channels)
    : rate_(std::max(sample_rate, 1)),
      nch_(static_cast<int>(std::min<std::size_t>(channels.size(), kMaxChannels))),
      block_n_(loudness::block_frames(rate_)),
      os_(true_peak_oversampling(rate_)),
      kw_(k_weighting(rate_))
{
    for (int c = 0; c < nch_; ++c) ch_[c].gain = gain(channels[c]);
    if (os_ > 1) {
        tp_coef_ = design_interpolator(os_);
        tp_hist_.assign(static_cast<std::size_t>(nch_) * 2 * kTaps, 0.0f);
    }
    for (Histogram* h : { &hist_i_, &hist_s_ }) {
        h->count.assign(kHistBins, 0);
        h->energy.assign(kHistBins, 0.0);
    }
    reset();
}

double Meter::begin_push(double pts, int frames)
{
    if (!std::isfinite(pts)) pts = next_pts_;
    if (frames > 0) next_pts_ = pts + static_cast<double>(frames) / rate_;
    return pts;
}

int Meter::feed(const float* const* data, int offset, int frames, double pts)
{
    if (fill_ == 0) block_start_ = pts + static_cast<double>(offset) / rate_;
    const int n = std::min(frames, block_n_ - fill_);

    for (int c = 0; c < nch_; ++c) weigh(ch_[c], data[c] + offset, n);
    if (nch_ >= 2) {
        const float* x = data[0] + offset;
        const float* y = data[1] + offset;
        double xy = xy_;
        for (int i = 0; i < n; ++i) xy += static_cast<double>(x[i]) * y[i];
        xy_ = xy;
    }
    if (os_ > 1) {
        // In runs that share what their inter-sample values count for.
        for (int i = 0; i < n;) {
            Inter mode = Inter::Post;
            int   run  = n - i;
            if (tp_skip_ > 0) {
                mode = Inter::Skip;
                run  = std::min(run, tp_skip_);
            } else if (tp_hold_ > 0) {
                mode = Inter::Pre;
                run  = std::min(run, tp_hold_);
            }
            for (int c = 0; c < nch_; ++c) {
                float*       hist = tp_hist_.data() + static_cast<std::size_t>(c) * 2 * kTaps;
                const float* x    = data[c] + offset + i;
                if (os_ == 4) interpolate<3>(ch_[c], hist, x, run, mode);
                else          interpolate<1>(ch_[c], hist, x, run, mode);
            }
            tp_pos_  = (tp_pos_ + run) % kTaps;
            tp_skip_ = std::max(0, tp_skip_ - run);
            tp_hold_ = std::max(0, tp_hold_ - run);
            i += run;
        }
    }

    fill_ += n;
    if (fill_ == block_n_) {
        finish_block();
        fill_  = 0;
        ready_ = true;
    }
    return n;
}

void Meter::weigh(Channel& ch, const float* x, int n) const
{
    const Biquad& f = kw_.shelf;
    const Biquad& h = kw_.highpass;
    double s1 = ch.s1, s2 = ch.s2, t1 = ch.t1, t2 = ch.t2;
    double kw = ch.kw, sq = ch.sq;
    float  peak = ch.peak;
    for (int i = 0; i < n; ++i) {
        const double v = x[i];
        const double y = f.b0 * v + s1;
        s1 = f.b1 * v - f.a1 * y + s2;
        s2 = f.b2 * v - f.a2 * y;
        const double z = h.b0 * y + t1;
        t1 = h.b1 * y - h.a1 * z + t2;
        t2 = h.b2 * y - h.a2 * z;
        kw += z * z;
        sq += v * v;
        peak = std::max(peak, std::fabs(x[i]));
    }
    ch.s1 = s1;
    ch.s2 = s2;
    ch.t1 = t1;
    ch.t2 = t2;
    ch.kw = kw;
    ch.sq = sq;
    ch.peak = peak;
}

template <int Phases>
void Meter::interpolate(Channel& ch, float* hist, const float* x, int n, Inter mode) const
{
    const float* coef = tp_coef_.data();
    int   pos = tp_pos_;
    float m   = 0.0f;
    for (int i = 0; i < n; ++i) {
        hist[pos] = hist[pos + kTaps] = x[i];
        const float* w = hist + pos + 1;   // the last kTaps samples, oldest first
        if (++pos == kTaps) pos = 0;
        if (mode == Inter::Skip) continue;
        // Even and odd taps summed apart: two dependency chains per phase
        // run about twice as fast as one.
        float even[Phases] = {}, odd[Phases] = {};
        for (int k = 0; k < kTaps; k += 2)
            for (int p = 0; p < Phases; ++p) {
                even[p] += coef[k * Phases + p] * w[k];
                odd[p]  += coef[(k + 1) * Phases + p] * w[k + 1];
            }
        for (int p = 0; p < Phases; ++p) m = std::max(m, std::fabs(even[p] + odd[p]));
    }
    float& to = mode == Inter::Pre ? ch.pre_ip : ch.ip;
    to = std::max(to, m);
}

double Meter::window(int blocks) const
{
    int i = ring_pos_ - blocks;   // ring_pos_: the oldest of the ring
    if (i < 0) i += kShortTerm;
    double sum = 0.0;
    for (int j = 0; j < blocks; ++j) {
        sum += ring_[i];
        if (++i == kShortTerm) i = 0;
    }
    return sum / (static_cast<double>(blocks) * block_n_);
}

void Meter::finish_block()
{
    Block& b   = block_;
    b.end_pts  = block_start_ + static_cast<double>(block_n_) / rate_;
    b.channels = nch_;

    double e = 0.0;
    for (int c = 0; c < nch_; ++c)
        if (ch_[c].gain != 0.0) e += ch_[c].gain * ch_[c].kw;
    ring_[ring_pos_] = e;
    ring_pos_ = (ring_pos_ + 1) % kShortTerm;
    ++blocks_;
    ++since_ri_;

    b.momentary = b.short_term = kSilent;
    if (blocks_ >= kMomentary) {
        // A gating block of the integrated loudness: once the window is all
        // after the last reset_integrated().
        const double m = window(kMomentary);
        const double l = lufs_of(m);
        b.momentary = static_cast<float>(l);
        if (since_ri_ >= kMomentary && std::isfinite(m) && l >= kAbsGate) {
            add(hist_i_, m, l);
            const double g = gated_mean(hist_i_, kRelGateI);
            integrated_ = g > 0.0 ? static_cast<float>(lufs_of(g)) : kSilent;
        }
    }
    if (blocks_ >= kShortTerm) {
        const double s = window(kShortTerm);
        const double l = lufs_of(s);
        b.short_term = static_cast<float>(l);
        if (since_ri_ >= kShortTerm && std::isfinite(s) && l >= kAbsGate) {
            add(hist_s_, s, l);
            lra_ = range(hist_s_);
        }
    }
    b.integrated = integrated_;
    b.lra        = lra_;

    const double inv = 1.0 / block_n_;
    for (int c = 0; c < nch_; ++c) {
        const Channel& ch = ch_[c];
        const float    pk = std::max(ch.peak, ch.pre_peak);
        b.peak[c]      = db_amplitude(pk);
        b.true_peak[c] = db_amplitude(std::max({ pk, ch.ip, ch.pre_ip }));
        b.rms[c]       = db_power(ch.sq * inv);
        tp_max_ = std::max(tp_max_, static_cast<double>(std::max(ch.peak, ch.ip)));
    }
    b.true_peak_max = db_amplitude(tp_max_);

    b.correlation = std::numeric_limits<float>::quiet_NaN();
    if (nch_ >= 2) {
        const double xx = ch_[0].sq, yy = ch_[1].sq;
        const double silent = kSilentPower * block_n_;
        if (xx >= silent && yy >= silent)
            b.correlation = static_cast<float>(std::clamp(xy_ / std::sqrt(xx * yy), -1.0, 1.0));
    }

    // Start the next block.
    xy_ = 0.0;
    for (int c = 0; c < nch_; ++c) {
        Channel& ch = ch_[c];
        ch.kw = ch.sq = 0.0;
        ch.peak = ch.ip = ch.pre_peak = ch.pre_ip = 0.0f;
        if (!std::isfinite(ch.s1 + ch.s2 + ch.t1 + ch.t2)) {
            ch.s1 = ch.s2 = ch.t1 = ch.t2 = 0.0;   // NaN / inf samples: recover
            continue;
        }
        for (double* s : { &ch.s1, &ch.s2, &ch.t1, &ch.t2 })
            if (std::fabs(*s) < kTinyState) *s = 0.0;
    }
}

void Meter::clear(Histogram& h)
{
    if (h.hi >= h.lo) {
        std::fill(h.count.begin() + h.lo, h.count.begin() + h.hi + 1, 0u);
        std::fill(h.energy.begin() + h.lo, h.energy.begin() + h.hi + 1, 0.0);
    }
    h.n     = 0;
    h.total = 0.0;
    h.lo    = kHistBins;
    h.hi    = -1;
}

void Meter::add(Histogram& h, double energy, double lufs)
{
    const int j = bin_of(lufs);
    ++h.count[j];
    h.energy[j] += energy;
    ++h.n;
    h.total += energy;
    h.lo = std::min(h.lo, j);
    h.hi = std::max(h.hi, j);
}

double Meter::gated_mean(const Histogram& h, double rel_lu)
{
    if (h.n == 0) return 0.0;
    const int from = std::max(first_bin(lufs_of(h.total / static_cast<double>(h.n)) + rel_lu), h.lo);
    double        e = 0.0;
    std::uint64_t n = 0;
    for (int j = from; j <= h.hi; ++j) {
        e += h.energy[j];
        n += h.count[j];
    }
    return n > 0 ? e / static_cast<double>(n) : 0.0;
}

float Meter::range(const Histogram& h)
{
    if (h.n == 0) return 0.0f;
    const int from = std::max(first_bin(lufs_of(h.total / static_cast<double>(h.n)) + kRelGateLra), h.lo);
    std::uint64_t n = 0;
    for (int j = from; j <= h.hi; ++j) n += h.count[j];
    if (n == 0) return 0.0f;
    // Tech 3342's reference code: of the values sorted, the ones at
    // round((n − 1)·10 %) and round((n − 1)·95 %). FFmpeg's ebur128 takes the
    // one at round(n·10 %) − 1 for P10: one lower at times, which across a
    // steep level change can be a few tenths of an LU.
    const auto low  = static_cast<std::uint64_t>(std::llround(static_cast<double>(n - 1) * 0.10));
    const auto high = static_cast<std::uint64_t>(std::llround(static_cast<double>(n - 1) * 0.95));
    std::uint64_t seen = 0;
    int           lo   = -1;
    for (int j = from; j <= h.hi; ++j) {
        seen += h.count[j];
        if (lo < 0 && seen > low) lo = j;
        if (seen > high) return static_cast<float>((j - lo) * kHistStep);
    }
    return 0.0f;
}

void Meter::clear_integrated()
{
    clear(hist_i_);
    clear(hist_s_);
    integrated_ = kSilent;
    lra_        = 0.0f;
    tp_max_     = 0.0;
}

void Meter::reset_integrated()
{
    clear_integrated();
    since_ri_ = fill_ > 0 ? -1 : 0;
    // The block in progress keeps what it measured for itself; from here
    // on only what comes after counts for true_peak_max, and inter-sample
    // values come kTaps/2 samples late.
    for (int c = 0; c < nch_; ++c) {
        Channel& ch = ch_[c];
        ch.pre_peak = std::max(ch.pre_peak, ch.peak);
        ch.pre_ip   = std::max(ch.pre_ip, ch.ip);
        ch.peak = ch.ip = 0.0f;
    }
    tp_hold_ = os_ > 1 ? kTaps / 2 : 0;
}

void Meter::reset()
{
    for (int c = 0; c < nch_; ++c) {
        const double g = ch_[c].gain;
        ch_[c]      = Channel{};
        ch_[c].gain = g;
    }
    std::fill(tp_hist_.begin(), tp_hist_.end(), 0.0f);
    tp_pos_  = 0;
    tp_skip_ = os_ > 1 ? kTaps - 1 : 0;   // until the history holds only samples pushed
    tp_hold_ = 0;

    fill_        = 0;
    block_start_ = 0.0;
    xy_          = 0.0;
    next_pts_    = 0.0;
    ready_       = false;

    ring_.fill(0.0);
    ring_pos_ = 0;
    blocks_   = 0;
    since_ri_ = 0;
    clear_integrated();
}

// ── Feed ─────────────────────────────────────────────────────────────────────

Feed::Feed(double seconds)
    : ring_(static_cast<std::size_t>(std::max(1.0, std::ceil(seconds * 10.0))))
{
}

void Feed::publish(const Block& b)
{
    std::lock_guard lk(mtx_);
    const std::size_t cap = ring_.size();
    ring_[(head_ + count_) % cap] = b;
    if (count_ < cap) ++count_;
    else head_ = (head_ + 1) % cap;
}

void Feed::clear()
{
    std::lock_guard lk(mtx_);
    head_  = 0;
    count_ = 0;
}

bool Feed::at(double pts, Block& out) const
{
    std::lock_guard lk(mtx_);
    const std::size_t cap = ring_.size();
    for (std::size_t i = count_; i-- > 0;) {
        const Block& b = ring_[(head_ + i) % cap];
        if (b.end_pts <= pts) {
            out = b;
            return true;
        }
    }
    return false;
}

void Feed::copy_range(double from, double to, std::vector<Block>& out) const
{
    out.clear();
    out.reserve(ring_.size());   // the ring's size never changes: no allocation under the lock
    std::lock_guard lk(mtx_);
    const std::size_t cap = ring_.size();
    for (std::size_t i = 0; i < count_; ++i) {
        const Block& b = ring_[(head_ + i) % cap];
        if (b.end_pts >= from && b.end_pts <= to) out.push_back(b);
    }
}

std::size_t Feed::size() const
{
    std::lock_guard lk(mtx_);
    return count_;
}

} // namespace loudness
