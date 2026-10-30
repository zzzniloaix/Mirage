// loudness::Meter / Feed (ITU-R BS.1770-4, EBU R128) on synthesized signals:
// the EBU Tech 3341 and 3342 test cases, what the standards fix besides
// (channel weights, block timing, true-peak oversampling), and a cross-check
// against FFmpeg's ebur128 filter fed the same samples. A 1 kHz sine of peak
// A on both channels of a stereo signal reads 20·log10(A) LUFS: Tech 3341's
// "−23 dBFS" sines are −23.0 LUFS.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "LoudnessMeter.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <numbers>
#include <string>
#include <thread>
#include <utility>
#include <vector>

extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
#include <libavutil/frame.h>
#include <libavutil/log.h>
}

using Catch::Matchers::WithinAbs;
using loudness::Block;
using loudness::kSilent;
using loudness::Meter;
using loudness::Weight;

namespace {

constexpr double kPi = std::numbers::pi;
const std::vector<Weight> kStereo{ Weight::Normal, Weight::Normal };

double amp(double db) { return std::pow(10.0, db / 20.0); }

// A sine of peak `a` (n: frames from the start of the signal).
struct Sine {
    double a     = 1.0;
    double hz    = 1000.0;
    int    rate  = 48000;
    double phase = 0.0;
    float operator()(int /*channel*/, std::int64_t n) const
    {
        return static_cast<float>(a * std::sin(2.0 * kPi * hz * static_cast<double>(n) / rate + phase));
    }
};

// Noise that is the same everywhere (xorshift64*; <random>'s distributions
// differ between standard libraries): the sum of three uniforms in [−1, 1),
// unit RMS, never beyond ±3.
class Noise {
public:
    explicit Noise(std::uint64_t seed) : s_(seed * 0x9E3779B97F4A7C15ull + 0x2545F4914F6CDD1Dull) {}
    double next() { return uniform() + uniform() + uniform(); }

private:
    double uniform()
    {
        s_ ^= s_ >> 12;
        s_ ^= s_ << 25;
        s_ ^= s_ >> 27;
        return static_cast<double>((s_ * 0x2545F4914F6CDD1Dull) >> 11) * 0x1.0p-52 - 1.0;
    }
    std::uint64_t s_;
};

// Planar samples.
struct Signal {
    int                             rate;
    std::vector<std::vector<float>> ch;
    Signal(int r, int channels, double seconds)
        : rate(r), ch(channels, std::vector<float>(static_cast<std::size_t>(std::llround(seconds * r))))
    {
    }
    std::size_t frames() const { return ch[0].size(); }
};

// Pushes samples into a meter, `chunk` frames at a time, with the pts of
// their frame position, and keeps every block.
struct Feeder {
    Meter&             m;
    int                chunk;
    double             pts0 = 0.0;
    std::int64_t       pos  = 0;   // frames pushed
    std::vector<Block> blocks;

    explicit Feeder(Meter& meter, int chunk_frames = 1024) : m(meter), chunk(chunk_frames) {}

    // `seconds` of gen(c, n): channel c at frame n (counted from the first
    // frame this feeder pushed).
    template <class G>
    void add(double seconds, G&& gen)
    {
        const int nch = m.channels();
        std::vector<std::vector<float>> buf(nch, std::vector<float>(chunk));
        std::vector<const float*>       ptr(nch);
        const std::int64_t total = std::llround(seconds * m.sample_rate());
        for (std::int64_t done = 0; done < total;) {
            const int n = static_cast<int>(std::min<std::int64_t>(chunk, total - done));
            for (int c = 0; c < nch; ++c) {
                for (int i = 0; i < n; ++i) buf[c][i] = gen(c, pos + i);
                ptr[c] = buf[c].data();
            }
            push(ptr.data(), n);
            done += n;
        }
    }
    void add(const Signal& s)
    {
        std::vector<const float*> ptr(s.ch.size());
        for (std::size_t i = 0; i < s.frames(); i += chunk) {
            const int n = static_cast<int>(std::min<std::size_t>(chunk, s.frames() - i));
            for (std::size_t c = 0; c < s.ch.size(); ++c) ptr[c] = s.ch[c].data() + i;
            push(ptr.data(), n);
        }
    }
    // A 1 kHz sine of peak `dbfs` on every channel.
    void tone(double seconds, double dbfs) { add(seconds, Sine{ amp(dbfs), 1000.0, m.sample_rate() }); }

    void push(const float* const* data, int n)
    {
        m.push(data, n, pts0 + static_cast<double>(pos) / m.sample_rate(),
               [this](const Block& b) { blocks.push_back(b); });
        pos += n;
    }
    const Block& last() const { return blocks.back(); }
};

// Largest |field − want| over blocks[from ..].
template <class Field>
double max_dev(const std::vector<Block>& blocks, std::size_t from, Field field, double want)
{
    double dev = 0.0;
    for (std::size_t i = from; i < blocks.size(); ++i)
        dev = std::max(dev, std::abs(static_cast<double>(field(blocks[i])) - want));
    return dev;
}

bool same(float x, float y) { return x == y || (std::isnan(x) && std::isnan(y)); }

// "" if both hold the same blocks (end_pts within rounding), else the first
// difference.
std::string first_difference(const std::vector<Block>& a, const std::vector<Block>& b)
{
    if (a.size() != b.size()) return "block count " + std::to_string(a.size()) + " vs " + std::to_string(b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        const Block& x = a[i];
        const Block& y = b[i];
        const auto   at = [&](const char* what) { return "block " + std::to_string(i) + ": " + what; };
        if (std::abs(x.end_pts - y.end_pts) > 1e-9) return at("end_pts");
        if (!same(x.momentary, y.momentary)) return at("momentary");
        if (!same(x.short_term, y.short_term)) return at("short_term");
        if (!same(x.integrated, y.integrated)) return at("integrated");
        if (!same(x.lra, y.lra)) return at("lra");
        if (!same(x.true_peak_max, y.true_peak_max)) return at("true_peak_max");
        if (x.channels != y.channels) return at("channels");
        for (int c = 0; c < loudness::kMaxChannels; ++c)
            if (!same(x.peak[c], y.peak[c]) || !same(x.true_peak[c], y.true_peak[c]) || !same(x.rms[c], y.rms[c]))
                return at("per-channel levels");
        if (!same(x.correlation, y.correlation)) return at("correlation");
    }
    return "";
}

} // namespace

// ── K-weighting, block size, oversampling ────────────────────────────────────

TEST_CASE("Loudness: K-weighting at 48 kHz is BS.1770-4's tables 1 and 2", "[loudness]")
{
    const loudness::KWeighting k = loudness::k_weighting(48000);
    CHECK_THAT(k.shelf.b0, WithinAbs(1.53512485958697, 1e-9));
    CHECK_THAT(k.shelf.b1, WithinAbs(-2.69169618940638, 1e-9));
    CHECK_THAT(k.shelf.b2, WithinAbs(1.19839281085285, 1e-9));
    CHECK_THAT(k.shelf.a1, WithinAbs(-1.69065929318241, 1e-9));
    CHECK_THAT(k.shelf.a2, WithinAbs(0.73248077421585, 1e-9));
    CHECK(k.highpass.b0 == 1.0);
    CHECK(k.highpass.b1 == -2.0);
    CHECK(k.highpass.b2 == 1.0);
    CHECK_THAT(k.highpass.a1, WithinAbs(-1.99004745483398, 1e-9));
    CHECK_THAT(k.highpass.a2, WithinAbs(0.99007225036621, 1e-9));

    SECTION("other rates: the 48 kHz response, within 0.05 dB") {
        // |H(f)| of both sections, dB.
        const auto response = [](int rate, double hz) {
            const loudness::KWeighting w = loudness::k_weighting(rate);
            const std::complex<double> z = std::polar(1.0, -2.0 * kPi * hz / rate);
            const auto section = [&](const loudness::Biquad& q) {
                return (q.b0 + q.b1 * z + q.b2 * z * z) / (1.0 + q.a1 * z + q.a2 * z * z);
            };
            return 20.0 * std::log10(std::abs(section(w.shelf) * section(w.highpass)));
        };
        CHECK_THAT(response(48000, 20.0), WithinAbs(-13.28, 0.01));      // the RLB high-pass
        CHECK_THAT(response(48000, 1000.0), WithinAbs(0.70, 0.01));      // why −0.691 in the formula
        CHECK_THAT(response(48000, 10000.0), WithinAbs(4.04, 0.01));     // the shelf
        for (const int rate : { 32000, 44100, 88200, 96000, 192000 })
            for (const double hz : { 20.0, 60.0, 200.0, 1000.0, 2000.0, 5000.0, 10000.0, 15000.0 }) {
                CAPTURE(rate, hz);
                CHECK_THAT(response(rate, hz), WithinAbs(response(48000, hz), 0.05));
            }
    }
}

TEST_CASE("Loudness: blocks are 100 ms of samples; true peak oversampled below 192 kHz", "[loudness]")
{
    CHECK(loudness::block_frames(48000) == 4800);
    CHECK(loudness::block_frames(44100) == 4410);
    CHECK(loudness::block_frames(96000) == 9600);
    CHECK(loudness::block_frames(11025) == 1103);   // 1102.5, rounded
    CHECK(loudness::true_peak_oversampling(44100) == 4);
    CHECK(loudness::true_peak_oversampling(48000) == 4);
    CHECK(loudness::true_peak_oversampling(88200) == 4);
    CHECK(loudness::true_peak_oversampling(96000) == 2);
    CHECK(loudness::true_peak_oversampling(176400) == 2);
    CHECK(loudness::true_peak_oversampling(192000) == 1);

    const Meter m(44100, kStereo);
    CHECK(m.sample_rate() == 44100);
    CHECK(m.channels() == 2);
    CHECK(m.block_frames() == 4410);
    CHECK(m.oversampling() == 4);
    // Only the first kMaxChannels are measured.
    const Meter wide(48000, std::vector<Weight>(20, Weight::Normal));
    CHECK(wide.channels() == loudness::kMaxChannels);
}

// ── Loudness (EBU Tech 3341) ─────────────────────────────────────────────────

TEST_CASE("Loudness: EBU Tech 3341 cases 1 and 2 — steady sines read their level", "[loudness]")
{
    for (const double level : { -23.0, -33.0 }) {
        CAPTURE(level);
        Meter  m(48000, kStereo);
        Feeder f(m);
        f.tone(20.0, level);
        REQUIRE(f.blocks.size() == 200);
        const Block& b = f.last();
        CHECK_THAT(b.momentary, WithinAbs(level, 0.1));
        CHECK_THAT(b.short_term, WithinAbs(level, 0.1));
        CHECK_THAT(b.integrated, WithinAbs(level, 0.1));
        CHECK(b.lra < 0.1f);
        // Nothing until a window is full, then steady.
        CHECK(f.blocks[2].momentary == kSilent);
        CHECK(f.blocks[28].short_term == kSilent);
        CHECK(max_dev(f.blocks, 3, [](const Block& x) { return x.momentary; }, level) < 0.1);
        CHECK(max_dev(f.blocks, 29, [](const Block& x) { return x.short_term; }, level) < 0.1);
        CHECK(max_dev(f.blocks, 3, [](const Block& x) { return x.integrated; }, level) < 0.1);
    }
}

TEST_CASE("Loudness: EBU Tech 3341 cases 3 to 5 — the gates of integrated loudness", "[loudness]")
{
    Meter  m(48000, kStereo);
    Feeder f(m);
    SECTION("case 3: −36 / −23 / −36 dBFS for 10 / 60 / 10 s — the relative gate drops the quiet parts") {
        f.tone(10.0, -36.0);
        f.tone(60.0, -23.0);
        f.tone(10.0, -36.0);
        CHECK_THAT(f.last().integrated, WithinAbs(-23.0, 0.1));
    }
    SECTION("case 4: −72 / −36 / −23 / −36 / −72 dBFS for 10 / 10 / 60 / 10 / 10 s — and the absolute one") {
        f.tone(10.0, -72.0);
        CHECK(f.last().integrated == kSilent);   // all below −70 LUFS so far
        f.tone(10.0, -36.0);
        f.tone(60.0, -23.0);
        f.tone(10.0, -36.0);
        f.tone(10.0, -72.0);
        CHECK_THAT(f.last().integrated, WithinAbs(-23.0, 0.1));
    }
    SECTION("case 5: −26 / −20 / −26 dBFS for 20 / 20.1 / 20 s — nothing gated, the energy mean") {
        f.tone(20.0, -26.0);
        f.tone(20.1, -20.0);
        f.tone(20.0, -26.0);
        CHECK_THAT(f.last().integrated, WithinAbs(-23.0, 0.1));
    }
}

TEST_CASE("Loudness: channel weights", "[loudness]")
{
    // 5 s of the 1 kHz sine at −20 dBFS on the channels `on` marks, silence
    // on the others — but channel `extra_ch` carries a 50 Hz sine of 0.9.
    const auto measure = [](std::vector<Weight> w, std::vector<bool> on, int extra_ch = -1) {
        Meter      m(48000, std::move(w));
        Feeder     f(m);
        const Sine s{ amp(-20.0), 1000.0, 48000 };
        const Sine lfe{ 0.9, 50.0, 48000 };
        f.add(5.0, [&](int c, std::int64_t n) {
            if (c == extra_ch) return lfe(c, n);
            return on[c] ? s(c, n) : 0.0f;
        });
        return f.last();
    };
    using enum Weight;
    const Block lr = measure({ Normal, Normal }, { true, true });
    CHECK_THAT(lr.integrated, WithinAbs(-20.0, 0.05));

    SECTION("mono: left alone reads 3 dB below the stereo sine") {
        const Block b = measure({ Normal }, { true });
        CHECK_THAT(b.integrated, WithinAbs(-23.01, 0.05));
        CHECK_THAT(b.momentary, WithinAbs(-23.01, 0.05));
    }
    SECTION("an equal centre adds half of L+R's energy") {
        const Block lrc = measure({ Normal, Normal, Normal }, { true, true, true });
        CHECK_THAT(lrc.integrated - lr.integrated, WithinAbs(10.0 * std::log10(1.5), 1e-3));
        CHECK_THAT(lrc.momentary - lr.momentary, WithinAbs(10.0 * std::log10(1.5), 1e-3));
    }
    SECTION("a surround channel counts 1.41 times") {
        const Block s = measure({ Normal, Normal, Surround, Surround }, { true, true, true, true });
        CHECK_THAT(s.integrated - lr.integrated, WithinAbs(10.0 * std::log10((2.0 + 2.0 * 1.41) / 2.0), 1e-3));
        const Block ls = measure({ Normal, Normal, Surround, Surround }, { false, false, true, false });
        const Block l  = measure({ Normal, Normal }, { true, false });
        CHECK_THAT(ls.integrated - l.integrated, WithinAbs(10.0 * std::log10(1.41), 1e-3));
    }
    SECTION("LFE counts for nothing in loudness, but is measured") {
        const Block x = measure({ Normal, Normal, Excluded }, { true, true, false }, 2);
        CHECK(x.integrated == lr.integrated);
        CHECK(x.momentary == lr.momentary);
        CHECK(x.short_term == lr.short_term);
        CHECK_THAT(x.peak[2], WithinAbs(20.0 * std::log10(0.9), 0.01));
        CHECK_THAT(x.true_peak_max, WithinAbs(20.0 * std::log10(0.9), 0.05));   // the LFE's
    }
}

TEST_CASE("Loudness: the −23 dBFS sine at other sample rates", "[loudness]")
{
    for (const int rate : { 32000, 44100, 96000, 192000 }) {
        CAPTURE(rate);
        Meter  m(rate, kStereo);
        Feeder f(m, 999);
        f.tone(10.0, -23.0);
        REQUIRE(f.blocks.size() == 100);
        CHECK_THAT(f.blocks[0].end_pts, WithinAbs(0.1, 1e-12));
        CHECK_THAT(f.last().end_pts, WithinAbs(10.0, 1e-9));
        CHECK_THAT(f.last().momentary, WithinAbs(-23.0, 0.1));
        CHECK_THAT(f.last().short_term, WithinAbs(-23.0, 0.1));
        CHECK_THAT(f.last().integrated, WithinAbs(-23.0, 0.1));
        CHECK_THAT(f.last().true_peak_max, WithinAbs(-23.0, 0.05));
    }
}

// ── Loudness range (EBU Tech 3342) ───────────────────────────────────────────

TEST_CASE("Loudness: EBU Tech 3342 cases 1 to 4 — loudness range", "[loudness]")
{
    // Stereo 1 kHz sines, 20 s at each level; Tech 3342 allows ±1 LU.
    struct Case {
        std::vector<double> levels;
        double              lra;
    };
    for (const Case& c : { Case{ { -20.0, -30.0 }, 10.0 }, Case{ { -20.0, -15.0 }, 5.0 },
                           Case{ { -40.0, -20.0 }, 20.0 },
                           Case{ { -50.0, -35.0, -20.0, -35.0, -50.0 }, 15.0 } }) {   // −50: below the relative gate
        CAPTURE(c.levels);
        Meter  m(48000, kStereo);
        Feeder f(m);
        for (const double l : c.levels) f.tone(20.0, l);
        CHECK_THAT(f.last().lra, WithinAbs(c.lra, 0.1));
    }
}

// ── True peak, per-block levels ──────────────────────────────────────────────

TEST_CASE("Loudness: true peak", "[loudness]")
{
    SECTION("fs/4 at 45°: samples of ±0.7071 (−3.01 dBFS), crests of 1.0 between them") {
        for (const int rate : { 44100, 48000, 96000 }) {
            CAPTURE(rate);
            Meter  m(rate, { Weight::Normal });
            Feeder f(m);
            f.add(1.0, [](int, std::int64_t n) { return static_cast<float>(std::sin(kPi / 2.0 * n + kPi / 4.0)); });
            CHECK(max_dev(f.blocks, 0, [](const Block& b) { return b.peak[0]; }, -3.0103) < 0.001);
            // Every block, the first too; ±0.3 dB would do.
            CHECK(max_dev(f.blocks, 0, [](const Block& b) { return b.true_peak[0]; }, 0.0) < 0.05);
            CHECK_THAT(f.last().true_peak_max, WithinAbs(0.0, 0.05));
        }
    }
    SECTION("fs/6 at 60° and fs/8 at 67.5°, peak 0.5, as in Tech 3341's true-peak cases") {
        for (const auto& [period, phase] : { std::pair{ 6.0, 60.0 }, std::pair{ 8.0, 67.5 } }) {
            CAPTURE(period);
            Meter  m(48000, { Weight::Normal });
            Feeder f(m);
            f.add(1.0, [&](int, std::int64_t n) {
                return static_cast<float>(0.5 * std::sin(2.0 * kPi * n / period + phase * kPi / 180.0));
            });
            CHECK(f.last().peak[0] < -6.5f);
            CHECK_THAT(f.last().true_peak_max, WithinAbs(-6.02, 0.2));
        }
    }
    SECTION("a full-scale 100 Hz sine from its crest on: 0 dBTP from the first block") {
        Meter  m(48000, kStereo);
        Feeder f(m);
        f.add(2.0, [](int, std::int64_t n) { return static_cast<float>(std::cos(2.0 * kPi * 100.0 * n / 48000.0)); });
        CHECK(f.blocks[0].peak[0] == 0.0f);
        CHECK(max_dev(f.blocks, 0, [](const Block& b) { return b.true_peak[0]; }, 0.0) < 0.05);
        CHECK_THAT(f.last().true_peak_max, WithinAbs(0.0, 0.05));
    }
    SECTION("192 kHz is not oversampled: the true peak is the sample peak") {
        Meter  m(192000, { Weight::Normal });
        Feeder f(m);
        f.add(0.5, [](int, std::int64_t n) { return static_cast<float>(std::sin(kPi / 2.0 * n + kPi / 4.0)); });
        CHECK(f.last().true_peak[0] == f.last().peak[0]);
    }
    SECTION("sample peak <= true peak; the blocks' true peaks make up true_peak_max") {
        Meter  m(48000, kStereo);
        Feeder f(m);
        Noise  a(7), b(8);
        f.add(3.0, [&](int c, std::int64_t n) {
            const double s = 0.3 * std::sin(2.0 * kPi * 15000.0 * n / 48000.0);
            return static_cast<float>(c == 0 ? 0.2 * a.next() + s : 0.1 * b.next());
        });
        float running = kSilent;
        bool  ordered = true, built = true;
        for (const Block& x : f.blocks) {
            for (int c = 0; c < 2; ++c) {
                ordered = ordered && x.peak[c] <= x.true_peak[c];
                running = std::max(running, x.true_peak[c]);
            }
            built = built && x.true_peak_max == running;
        }
        CHECK(ordered);
        CHECK(built);
        CHECK(f.last().true_peak_max > f.last().peak[0]);   // 15 kHz content: crests between samples
    }
}

TEST_CASE("Loudness: per-block peak, RMS and correlation", "[loudness]")
{
    SECTION("levels of a sine, a square wave and silence") {
        Meter  m(48000, { Weight::Normal, Weight::Normal, Weight::Normal });
        Feeder f(m);
        f.add(1.0, [](int c, std::int64_t n) {
            if (c == 0) return static_cast<float>(0.5 * std::sin(2.0 * kPi * 1000.0 * n / 48000.0 + 0.3));
            if (c == 1) return (n / 24) % 2 == 0 ? 1.0f : -1.0f;   // 1 kHz square, full scale
            return 0.0f;
        });
        const Block& b = f.last();
        CHECK(b.channels == 3);
        CHECK_THAT(b.peak[0], WithinAbs(-6.02, 0.02));
        CHECK_THAT(b.rms[0], WithinAbs(-6.02 - 3.01, 0.01));
        CHECK(b.peak[1] == 0.0f);
        CHECK_THAT(b.rms[1], WithinAbs(0.0, 1e-6));
        CHECK(b.peak[2] == kSilent);
        CHECK(b.true_peak[2] == kSilent);
        CHECK(b.rms[2] == kSilent);
        for (int c = 3; c < loudness::kMaxChannels; ++c) CHECK(b.peak[c] == kSilent);   // no such channel
    }
    SECTION("correlation: the same signal +1, inverted −1, unrelated noise about 0") {
        Noise  a(11), b(12);
        Signal both(48000, 2, 2.0), inverted(48000, 2, 2.0), unrelated(48000, 2, 2.0);
        for (std::size_t i = 0; i < both.frames(); ++i) {
            const auto l = static_cast<float>(0.25 * a.next());
            const auto r = static_cast<float>(0.25 * b.next());
            both.ch[0][i] = both.ch[1][i] = l;
            inverted.ch[0][i]  = l;
            inverted.ch[1][i]  = -l;
            unrelated.ch[0][i] = l;
            unrelated.ch[1][i] = r;
        }
        const auto blocks = [](const Signal& s) {
            Meter  m(48000, kStereo);
            Feeder f(m);
            f.add(s);
            return f.blocks;
        };
        const auto corr = [](const Block& x) { return x.correlation; };
        CHECK(max_dev(blocks(both), 0, corr, 1.0) < 1e-6);
        CHECK(max_dev(blocks(inverted), 0, corr, -1.0) < 1e-6);
        const std::vector<Block> u = blocks(unrelated);
        CHECK(max_dev(u, 0, corr, 0.0) < 0.1);   // a block's estimate: ±1/√4800
        double mean = 0.0;
        for (const Block& x : u) mean += x.correlation / static_cast<double>(u.size());
        CHECK(std::abs(mean) < 0.02);
    }
    SECTION("no correlation with one channel, or a silent one") {
        Meter  mono(48000, { Weight::Normal });
        Feeder fm(mono);
        fm.tone(0.5, -20.0);
        CHECK(std::isnan(fm.last().correlation));
        Meter  st(48000, kStereo);
        Feeder fs(st);
        fs.add(0.5, [](int c, std::int64_t n) { return c == 0 ? Sine{ 0.1 }(c, n) : 0.0f; });
        CHECK(std::isnan(fs.last().correlation));
    }
}

// ── Timing, push sizes, resets ───────────────────────────────────────────────

namespace {

// The block of `blocks` ending at t.
const Block& block_ending(const std::vector<Block>& blocks, double t)
{
    const auto b = std::find_if(blocks.begin(), blocks.end(), [&](const Block& x) { return std::abs(x.end_pts - t) < 1e-6; });
    REQUIRE(b != blocks.end());
    return *b;
}

} // namespace

TEST_CASE("Loudness: a block ends 100 ms after the pts of its first sample", "[loudness]")
{
    std::vector<float>  quarter(9600, 0.25f);
    const float*        d[2] = { quarter.data(), quarter.data() };
    std::vector<double> ends;
    const auto          keep = [&](const Block& b) { ends.push_back(b.end_pts); };

    SECTION("from the pts pushed") {
        Meter  m(48000, kStereo);
        Feeder f(m, 1000);
        f.pts0 = 12.5;
        f.tone(1.0, -20.0);
        REQUIRE(f.blocks.size() == 10);
        for (std::size_t i = 0; i < f.blocks.size(); ++i)
            CHECK_THAT(f.blocks[i].end_pts, WithinAbs(12.5 + 0.1 * static_cast<double>(i + 1), 1e-9));
    }
    SECTION("44.1 kHz: 4410 samples") {
        Meter m(44100, kStereo);
        m.push(d, 4409, 0.0, keep);
        CHECK(ends.empty());
        m.push(d, 1, 4409.0 / 44100.0, keep);
        REQUIRE(ends.size() == 1);
        CHECK_THAT(ends[0], WithinAbs(0.1, 1e-12));
    }
    SECTION("a pts jump between pushes: each push's pts is taken as given") {
        Meter m(48000, kStereo);
        m.push(d, 3000, 10.0, keep);
        m.push(d, 9600, 50.0, keep);
        REQUIRE(ends.size() == 2);
        CHECK_THAT(ends[0], WithinAbs(10.1, 1e-12));   // its first sample came at 10.0
        CHECK_THAT(ends[1], WithinAbs(50.0 + 1800.0 / 48000.0 + 0.1, 1e-12));
    }
    SECTION("no pts: on from the previous push") {
        Meter        m(48000, kStereo);
        const double none = std::numeric_limits<double>::quiet_NaN();
        m.push(d, 2400, 5.0, keep);
        m.push(d, 2400, none, keep);
        m.push(d, 4800, none, keep);
        REQUIRE(ends.size() == 2);
        CHECK_THAT(ends[0], WithinAbs(5.1, 1e-12));
        CHECK_THAT(ends[1], WithinAbs(5.2, 1e-12));
    }
}

TEST_CASE("Loudness: how the samples are split into pushes changes nothing", "[loudness]")
{
    // 7.3 s of stereo with something for every measurement: noise at changing
    // levels (silent for a second), a burst of fs/4 at 45° (crests between
    // samples), the right channel partly the left.
    Signal s(48000, 2, 7.3);
    Noise  a(21), b(22);
    for (std::size_t i = 0; i < s.frames(); ++i) {
        const double t    = static_cast<double>(i) / 48000.0;
        const double g    = t < 2.0 ? 0.05 : t < 4.0 ? 0.3 : t < 5.0 ? 0.0 : 0.1;
        const double tone = t > 3.0 && t < 3.5 ? 0.4 * std::sin(kPi / 2.0 * static_cast<double>(i) + kPi / 4.0) : 0.0;
        const double l    = g * a.next() + tone;
        s.ch[0][i] = static_cast<float>(l);
        s.ch[1][i] = static_cast<float>(0.5 * l + g * b.next());
    }
    const auto blocks_of = [&](auto chunk_size) {
        Meter              m(48000, kStereo);
        std::vector<Block> out;
        int                k = 0;
        for (std::size_t i = 0; i < s.frames();) {
            const auto   n    = static_cast<int>(std::min(static_cast<std::size_t>(chunk_size(k++)), s.frames() - i));
            const float* d[2] = { s.ch[0].data() + i, s.ch[1].data() + i };
            m.push(d, n, 3.0 + static_cast<double>(i) / 48000.0, [&](const Block& x) { out.push_back(x); });
            i += static_cast<std::size_t>(n);
        }
        return out;
    };
    const std::vector<Block> whole = blocks_of([&](int) { return s.frames(); });
    REQUIRE(whole.size() == 73);
    CHECK(whole.back().lra > 1.0f);
    CHECK(whole.back().true_peak_max > -8.0f);

    CHECK(first_difference(whole, blocks_of([](int) { return 1; })) == "");
    CHECK(first_difference(whole, blocks_of([](int) { return 7; })) == "");
    CHECK(first_difference(whole, blocks_of([](int) { return 4800; })) == "");
    CHECK(first_difference(whole, blocks_of([](int) { return 4801; })) == "");
    CHECK(first_difference(whole, blocks_of([](int k) { return 1 + (static_cast<long>(k) * 7919) % 20000; })) == "");
    CHECK(first_difference(whole, blocks_of([](int k) { return k % 2 != 0 ? 3 * 48000 : 1; })) == "");
}

TEST_CASE("Loudness: reset() starts over as a new meter would", "[loudness]")
{
    // What comes after the reset: 6 s from pts 100.
    Signal after(48000, 2, 6.0);
    Noise  a(31);
    for (std::size_t i = 0; i < after.frames(); ++i) {
        const auto n = static_cast<std::int64_t>(i);
        after.ch[0][i] = Sine{ amp(-23.0) }(0, n) + static_cast<float>(0.02 * a.next());
        after.ch[1][i] = Sine{ amp(-26.0), 400.0 }(1, n);
    }
    Meter  fresh(48000, kStereo);
    Feeder ff(fresh);
    ff.pts0 = 100.0;
    ff.add(after);

    // A loud 2.345 s first: it ends inside a block, the filters and
    // windows full of it.
    Meter  used(48000, kStereo);
    Feeder before(used, 777);
    before.add(2.345, [](int c, std::int64_t n) { return static_cast<float>(0.7 * std::sin(0.01 * (c + 1) * n)); });
    REQUIRE(before.blocks.size() == 23);
    used.reset();
    Feeder fa(used);
    fa.pts0 = 100.0;
    fa.add(after);
    CHECK(first_difference(ff.blocks, fa.blocks) == "");
}

TEST_CASE("Loudness: reset_integrated() restarts integrated, LRA and true_peak_max only", "[loudness]")
{
    // −20 dBFS for 10.05 s — the reset falls inside a block, on a zero
    // crossing of the sine — then −30 dBFS for 20 s.
    Meter  kept(48000, kStereo), restarted(48000, kStereo);
    Feeder fk(kept), fr(restarted);
    fk.tone(10.05, -20.0);
    fr.tone(10.05, -20.0);
    restarted.reset_integrated();
    fk.tone(20.0, -30.0);
    fr.tone(20.0, -30.0);
    REQUIRE(fk.blocks.size() == fr.blocks.size());

    // The windows and every block's own levels go on as if nothing happened.
    bool continuous = true;
    for (std::size_t i = 0; i < fk.blocks.size(); ++i) {
        const Block& k = fk.blocks[i];
        const Block& r = fr.blocks[i];
        continuous = continuous && same(k.momentary, r.momentary) && same(k.short_term, r.short_term) &&
                     k.peak == r.peak && k.true_peak == r.true_peak && k.rms == r.rms &&
                     same(k.correlation, r.correlation);
    }
    CHECK(continuous);

    // Integrated: only 400 ms windows all after the reset; the first ends
    // with the fourth block after the one the reset fell in.
    CHECK(block_ending(fr.blocks, 10.4).integrated == kSilent);
    CHECK_THAT(block_ending(fr.blocks, 10.5).integrated, WithinAbs(-30.0, 0.05));
    CHECK_THAT(fr.last().integrated, WithinAbs(-30.0, 0.05));
    CHECK(fk.last().integrated > -26.0f);
    // LRA: short-term values after the reset only — all −30.
    CHECK(fr.last().lra < 0.1f);
    CHECK_THAT(fk.last().lra, WithinAbs(10.0, 0.5));
    // True peak: what came after the reset, even in the block it fell in
    // (whose own peak is the −20 dBFS before).
    const Block& straddling = block_ending(fr.blocks, 10.1);
    CHECK_THAT(straddling.peak[0], WithinAbs(-20.0, 0.05));
    CHECK_THAT(straddling.true_peak_max, WithinAbs(-30.0, 0.05));
    CHECK_THAT(fr.last().true_peak_max, WithinAbs(-30.0, 0.05));
    CHECK_THAT(fk.last().true_peak_max, WithinAbs(-20.0, 0.05));
}

TEST_CASE("Loudness: NaN samples spoil the windows they fall in, not the meter", "[loudness]")
{
    Meter  m(48000, kStereo);
    Feeder f(m);
    f.tone(2.0, -23.0);
    f.add(0.01, [](int, std::int64_t) { return std::numeric_limits<float>::quiet_NaN(); });
    f.tone(6.0, -23.0);
    const Block& b = f.last();
    CHECK_THAT(b.momentary, WithinAbs(-23.0, 0.1));
    CHECK_THAT(b.short_term, WithinAbs(-23.0, 0.1));
    CHECK_THAT(b.integrated, WithinAbs(-23.0, 0.1));
    CHECK_THAT(b.true_peak_max, WithinAbs(-23.0, 0.1));
}

// ── Feed ─────────────────────────────────────────────────────────────────────

namespace {

Block ending(double t)
{
    Block b;
    b.end_pts   = t;
    b.momentary = static_cast<float>(-t);
    return b;
}

std::vector<double> ends_of(const std::vector<Block>& blocks)
{
    std::vector<double> out;
    for (const Block& b : blocks) out.push_back(b.end_pts);
    return out;
}

} // namespace

TEST_CASE("Loudness Feed: the block heard at a pts, and a range of them", "[loudness]")
{
    loudness::Feed     feed;
    Block              out;
    std::vector<Block> range;
    CHECK(feed.size() == 0);
    CHECK_FALSE(feed.at(1.0, out));
    feed.copy_range(-1e9, 1e9, range);
    CHECK(range.empty());

    for (int k = 1; k <= 8; ++k) feed.publish(ending(0.125 * k));
    CHECK(feed.size() == 8);
    CHECK_FALSE(feed.at(0.1, out));   // before the first block's end
    REQUIRE(feed.at(0.125, out));
    CHECK(out.end_pts == 0.125);
    REQUIRE(feed.at(0.2, out));
    CHECK(out.end_pts == 0.125);
    REQUIRE(feed.at(0.99, out));
    CHECK(out.end_pts == 0.875);
    REQUIRE(feed.at(100.0, out));
    CHECK(out.end_pts == 1.0);
    CHECK(out.momentary == -1.0f);

    feed.copy_range(0.25, 0.5, range);
    CHECK(ends_of(range) == std::vector<double>{ 0.25, 0.375, 0.5 });
    feed.copy_range(5.0, 6.0, range);
    CHECK(range.empty());

    SECTION("a pts jump forward: a clock still before it gets the blocks before it") {
        feed.publish(ending(50.125));
        feed.publish(ending(50.25));
        REQUIRE(feed.at(1.1, out));
        CHECK(out.end_pts == 1.0);
        REQUIRE(feed.at(50.2, out));
        CHECK(out.end_pts == 50.125);
        feed.copy_range(0.9, 50.2, range);
        CHECK(ends_of(range) == std::vector<double>{ 1.0, 50.125 });
    }
    SECTION("clear") {
        feed.clear();
        CHECK(feed.size() == 0);
        CHECK_FALSE(feed.at(100.0, out));
        feed.publish(ending(3.0));
        REQUIRE(feed.at(100.0, out));
        CHECK(out.end_pts == 3.0);
    }
}

TEST_CASE("Loudness Feed: the history is bounded", "[loudness]")
{
    loudness::Feed feed(1.0);   // ten blocks
    for (int k = 1; k <= 25; ++k) feed.publish(ending(0.125 * k));
    CHECK(feed.size() == 10);
    Block out;
    CHECK_FALSE(feed.at(1.9, out));   // 0.125 .. 1.875 are gone
    REQUIRE(feed.at(2.0, out));
    CHECK(out.end_pts == 2.0);
    std::vector<Block> all;
    feed.copy_range(-1e9, 1e9, all);
    REQUIRE(all.size() == 10);
    CHECK(all.front().end_pts == 2.0);
    CHECK(all.back().end_pts == 0.125 * 25);
}

TEST_CASE("Loudness Feed: a writer and a reader at once", "[loudness]")
{
    loudness::Feed    feed(2.0);
    constexpr int     kBlocks = 20000;
    std::atomic<bool> done{ false };
    std::thread       writer([&] {
        for (int k = 1; k <= kBlocks; ++k) feed.publish(ending(0.125 * k));
        done = true;
    });
    bool               consistent = true;
    std::vector<Block> range;
    Block              out;
    while (!done) {
        feed.copy_range(-1e9, 1e9, range);
        for (std::size_t i = 1; i < range.size(); ++i)
            consistent = consistent && range[i].end_pts == range[i - 1].end_pts + 0.125;
        if (feed.at(1e9, out)) consistent = consistent && out.end_pts <= 0.125 * kBlocks;
    }
    writer.join();
    CHECK(consistent);
    REQUIRE(feed.at(1e9, out));
    CHECK(out.end_pts == 0.125 * kBlocks);
    CHECK(feed.size() == 20);
}

// ── Cross-check: FFmpeg's ebur128 filter ─────────────────────────────────────

namespace {

// A minute of program-like stereo: noise (low-passed; the right channel
// 0.6 the left) in 5 s sections 39 dB apart at most, and in the quiet fourth
// section a quarter second of fs/4 at 45°, faded in and out: crests of 0.9
// (−0.92 dBTP) between samples of 0.64 (−3.93 dBFS).
Signal stereo_program(int rate, double seconds)
{
    static constexpr double kLevels[] = { -18, -30, -24, -42, -16, -27, -36, -21, -55, -25, -19, -33 };
    Signal s(rate, 2, seconds);
    Noise  a(1), b(2);
    double la = 0.0, lb = 0.0;   // one-pole low-pass states
    const auto section = static_cast<std::size_t>(5 * rate);
    for (std::size_t i = 0; i < s.frames(); ++i) {
        const double g = amp(kLevels[(i / section) % std::size(kLevels)]);
        la += 0.3 * (a.next() - la);
        lb += 0.3 * (b.next() - lb);
        s.ch[0][i] = static_cast<float>(g * la);
        s.ch[1][i] = static_cast<float>(g * (0.6 * la + 0.8 * lb));
    }
    const auto at = static_cast<std::size_t>(16 * rate), len = static_cast<std::size_t>(rate / 4),
               fade = static_cast<std::size_t>(rate / 200);
    for (std::size_t k = 0; k < len; ++k) {
        const auto   edge = static_cast<double>(std::min(k, len - 1 - k));
        const double env  = edge < static_cast<double>(fade) ? 0.5 - 0.5 * std::cos(kPi * edge / static_cast<double>(fade)) : 1.0;
        s.ch[0][at + k] += static_cast<float>(0.9 * env * std::sin(kPi / 2.0 * static_cast<double>(at + k) + kPi / 4.0));
    }
    return s;
}

// Half a minute of 5.1 (L R C LFE Ls Rs): unrelated noise on every channel,
// its level changing every 3 s on a pattern of its own; the LFE low-passed
// hard, and the loudest. Weighted wrong, the integrated loudness moves well
// outside the 0.1 LU the check allows: +0.75 LU with the LFE counted,
// −0.29 LU with the surround channels counted as front ones.
Signal surround_program(int rate, double seconds)
{
    static constexpr double kBase[6] = { -22, -24, -20, -18, -28, -26 };
    static constexpr double kSteps[] = { 0, -6, 3, -15, -2, 4, -9, 0, -30, 1 };
    Signal     s(rate, 6, seconds);
    const auto section = static_cast<std::size_t>(3 * rate);
    for (int c = 0; c < 6; ++c) {
        Noise        n(100 + static_cast<std::uint64_t>(c));
        const double a    = c == 3 ? 0.01 : 0.3;
        const double unit = std::sqrt(a / (2.0 - a));   // RMS of the low-passed noise
        double       lp   = 0.0;
        for (std::size_t i = 0; i < s.frames(); ++i) {
            lp += a * (n.next() - lp);
            const double level = kBase[c] + kSteps[(i / section + static_cast<std::size_t>(c)) % std::size(kSteps)];
            s.ch[c][i] = static_cast<float>(amp(level) * lp / unit);
        }
    }
    return s;
}

// What FFmpeg's ebur128 filter (metadata=1:peak=true) makes of a signal:
// the lavfi.r128.* metadata of the last frames out. Its peaks are linear.
std::map<std::string, double> ebur128(const Signal& s, const char* layout)
{
    // Quiet while it lives: the filter logs a summary when freed.
    struct Graph {
        int             level = av_log_get_level();
        AVFilterGraph*  g     = avfilter_graph_alloc();
        AVFrame*        in    = av_frame_alloc();
        AVFrame*        out   = av_frame_alloc();
        AVChannelLayout layout{};
        Graph() { av_log_set_level(AV_LOG_ERROR); }
        ~Graph()
        {
            av_frame_free(&in);
            av_frame_free(&out);
            av_channel_layout_uninit(&layout);
            avfilter_graph_free(&g);
            av_log_set_level(level);
        }
    } gr;
    REQUIRE(gr.g);
    REQUIRE(av_channel_layout_from_string(&gr.layout, layout) >= 0);
    REQUIRE(gr.layout.nb_channels == static_cast<int>(s.ch.size()));

    char args[160];
    std::snprintf(args, sizeof args, "time_base=1/%d:sample_rate=%d:sample_fmt=fltp:channel_layout=%s", s.rate,
                  s.rate, layout);
    AVFilterContext *src = nullptr, *meter = nullptr, *sink = nullptr;
    REQUIRE(avfilter_graph_create_filter(&src, avfilter_get_by_name("abuffer"), "in", args, nullptr, gr.g) >= 0);
    REQUIRE(avfilter_graph_create_filter(&meter, avfilter_get_by_name("ebur128"), "ebur128", "metadata=1:peak=true",
                                         nullptr, gr.g) >= 0);
    REQUIRE(avfilter_graph_create_filter(&sink, avfilter_get_by_name("abuffersink"), "out", nullptr, nullptr, gr.g) >= 0);
    REQUIRE(avfilter_link(src, 0, meter, 0) >= 0);
    REQUIRE(avfilter_link(meter, 0, sink, 0) >= 0);
    REQUIRE(avfilter_graph_config(gr.g, nullptr) >= 0);

    std::map<std::string, double> meta;
    const auto drain = [&] {
        while (av_buffersink_get_frame(sink, gr.out) >= 0) {
            const AVDictionaryEntry* e = nullptr;
            while ((e = av_dict_get(gr.out->metadata, "lavfi.r128.", e, AV_DICT_IGNORE_SUFFIX)))
                meta[e->key] = std::strtod(e->value, nullptr);
            av_frame_unref(gr.out);
        }
    };
    constexpr std::size_t kFrame = 1000;
    bool                  fed    = true;
    for (std::size_t i = 0; fed && i < s.frames(); i += kFrame) {
        AVFrame* f     = gr.in;
        f->format      = AV_SAMPLE_FMT_FLTP;
        f->sample_rate = s.rate;
        f->nb_samples  = static_cast<int>(std::min(kFrame, s.frames() - i));
        f->pts         = static_cast<std::int64_t>(i);
        fed = av_channel_layout_copy(&f->ch_layout, &gr.layout) >= 0 && av_frame_get_buffer(f, 0) >= 0;
        if (!fed) break;
        for (std::size_t c = 0; c < s.ch.size(); ++c)
            std::copy_n(s.ch[c].data() + i, f->nb_samples, reinterpret_cast<float*>(f->extended_data[c]));
        fed = av_buffersrc_add_frame(src, f) >= 0;   // takes the buffers and resets f
        drain();
    }
    REQUIRE(fed);
    REQUIRE(av_buffersrc_add_frame(src, nullptr) >= 0);   // end of stream
    drain();
    return meta;
}

} // namespace

TEST_CASE("Loudness: agrees with FFmpeg's ebur128 filter on the same samples", "[loudness]")
{
    const auto compare = [](const Signal& s, std::vector<Weight> weights, const char* layout) {
        Meter  m(s.rate, std::move(weights));
        Feeder f(m);
        f.add(s);
        std::array<float, loudness::kMaxChannels> tp{};
        tp.fill(kSilent);
        for (const Block& b : f.blocks)
            for (int c = 0; c < m.channels(); ++c) tp[c] = std::max(tp[c], b.true_peak[c]);

        const std::map<std::string, double> ff = ebur128(s, layout);
        for (const char* key : { "lavfi.r128.M", "lavfi.r128.S", "lavfi.r128.I", "lavfi.r128.LRA", "lavfi.r128.true_peak" }) {
            CAPTURE(key);
            REQUIRE(ff.count(key) == 1);
        }
        const auto   db   = [](double linear) { return 20.0 * std::log10(linear); };
        const Block& last = f.last();
        CHECK_THAT(last.integrated, WithinAbs(ff.at("lavfi.r128.I"), 0.1));
        CHECK_THAT(last.lra, WithinAbs(ff.at("lavfi.r128.LRA"), 1.0));
        CHECK_THAT(last.true_peak_max, WithinAbs(db(ff.at("lavfi.r128.true_peak")), 0.5));
        CHECK_THAT(last.momentary, WithinAbs(ff.at("lavfi.r128.M"), 0.1));
        CHECK_THAT(last.short_term, WithinAbs(ff.at("lavfi.r128.S"), 0.1));
        for (int c = 0; c < m.channels(); ++c) {
            const std::string key = "lavfi.r128.true_peaks_ch" + std::to_string(c);
            CAPTURE(key);
            REQUIRE(ff.count(key) == 1);
            CHECK_THAT(tp[c], WithinAbs(db(ff.at(key)), 0.5));
        }
        return last;
    };
    using enum Weight;
    SECTION("stereo, 48 kHz") {
        const Block last = compare(stereo_program(48000, 60.0), kStereo, "stereo");
        CHECK(last.true_peak_max > -1.5f);   // the burst between samples, not its samples
        CHECK(last.lra > 5.0f);
    }
    SECTION("stereo, 44.1 kHz") { compare(stereo_program(44100, 30.0), kStereo, "stereo"); }
    SECTION("5.1, 48 kHz") { compare(surround_program(48000, 30.0), { Normal, Normal, Normal, Excluded, Surround, Surround }, "5.1"); }
}
