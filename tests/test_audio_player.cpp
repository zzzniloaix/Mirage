#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "AudioPlayer.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using Catch::Matchers::WithinAbs;
using namespace std::chrono_literals;

// Ring/clock math only — init_ring() skips the miniaudio device, and the
// tests call fill_buffer() by hand in place of the audio callback.

static constexpr int kRate = 48000;

// Push n frames of a constant value covering n * tempo / rate media seconds.
static void push_frames(AudioPlayer& p, int n, double pts, double tempo, float v = 0.25f)
{
    std::vector<float> buf(static_cast<size_t>(n) * AudioPlayer::kChannels, v);
    p.push(buf.data(), n, pts, n * tempo / kRate);
}

static void drain(AudioPlayer& p, int n)
{
    std::vector<float> out(static_cast<size_t>(n) * AudioPlayer::kChannels);
    p.fill_buffer(out.data(), n);
}

TEST_CASE("AudioPlayer clock: 1x — clock is end pts minus buffered time", "[audio]")
{
    AudioPlayer p;
    p.init_ring(kRate);
    push_frames(p, 4800, 10.0, 1.0);    // 10.0 .. 10.1
    drain(p, 1200);                     // 25 ms played
    CHECK_THAT(p.clock(), WithinAbs(10.025, 1e-9));
    drain(p, 3600);
    CHECK_THAT(p.clock(), WithinAbs(10.1, 1e-9));
}

TEST_CASE("AudioPlayer clock: 4x tempo — each output frame covers 4 media frames", "[audio]")
{
    AudioPlayer p;
    p.init_ring(kRate);
    push_frames(p, 4800, 20.0, 4.0);    // 0.1 s wall of audio = 0.4 s media
    drain(p, 2400);                     // 50 ms wall played → 200 ms media
    CHECK_THAT(p.clock(), WithinAbs(20.2, 1e-9));
}

TEST_CASE("AudioPlayer clock: mixed tempo segments drain in order", "[audio]")
{
    AudioPlayer p;
    p.init_ring(kRate);
    push_frames(p, 4800, 0.0, 1.0);     // 0.0 .. 0.1 at 1x
    push_frames(p, 4800, 0.1, 4.0);     // 0.1 .. 0.5 at 4x
    push_frames(p, 4800, 0.5, 0.5);     // 0.5 .. 0.55 at 0.5x

    drain(p, 2400);                     // inside 1x seg
    CHECK_THAT(p.clock(), WithinAbs(0.05, 1e-9));
    drain(p, 4800);                     // finish 1x seg + half of 4x seg
    CHECK_THAT(p.clock(), WithinAbs(0.3, 1e-9));
    drain(p, 4800);                     // finish 4x + half of 0.5x seg
    CHECK_THAT(p.clock(), WithinAbs(0.525, 1e-9));
    drain(p, 2400);
    CHECK_THAT(p.clock(), WithinAbs(0.55, 1e-9));
}

TEST_CASE("AudioPlayer clock: underrun doesn't move clock past pushed audio", "[audio]")
{
    AudioPlayer p;
    p.init_ring(kRate);
    push_frames(p, 960, 1.0, 2.0);      // 20 ms wall, 40 ms media
    drain(p, 2000);                     // more than buffered
    CHECK_THAT(p.clock(), WithinAbs(1.04, 1e-9));
    CHECK(p.underrun_count() == 1);
    CHECK(p.silent_frames() == 2000 - 960);
}

TEST_CASE("AudioPlayer flush resets clock and segments", "[audio]")
{
    AudioPlayer p;
    p.init_ring(kRate);
    push_frames(p, 4800, 5.0, 4.0);
    p.flush();
    CHECK(p.clock() == 0.0);
    push_frames(p, 4800, 60.0, 1.0);    // post-seek audio at 1x
    drain(p, 2400);
    CHECK_THAT(p.clock(), WithinAbs(60.05, 1e-9));
    CHECK(p.underrun_count() == 0);     // post-flush refill gap isn't an underrun
}

TEST_CASE("AudioPlayer push: blocks on a full ring instead of dropping", "[audio]")
{
    AudioPlayer p;
    p.init_ring(kRate);
    const int cap = p.max_buffered_frames();
    push_frames(p, cap, 0.0, 1.0);      // fill exactly

    std::atomic<bool> done{ false };
    std::thread t([&] {
        push_frames(p, 4800, static_cast<double>(cap) / kRate, 1.0);
        done = true;
    });
    std::this_thread::sleep_for(100ms);
    CHECK_FALSE(done.load());           // waiting for space, not dropping

    // Drain in callback-sized chunks until the producer gets through.
    for (int i = 0; i < 200 && !done.load(); ++i) {
        drain(p, 480);
        std::this_thread::sleep_for(1ms);
    }
    t.join();
    CHECK(done.load());
    CHECK(p.dropped_frames() == 0);

    // Clock = end of everything pushed minus what's still buffered: no jump.
    drain(p, 0);
    const double end = static_cast<double>(cap + 4800) / kRate;
    CHECK(p.clock() <= end + 1e-9);
    CHECK(p.clock() > 0.0);
}

TEST_CASE("AudioPlayer push: flush wakes a blocked push and drops the stale rest", "[audio]")
{
    AudioPlayer p;
    p.init_ring(kRate);
    push_frames(p, p.max_buffered_frames(), 0.0, 1.0);

    std::atomic<bool> done{ false };
    std::thread t([&] { push_frames(p, 4800, 99.0, 1.0); done = true; });
    std::this_thread::sleep_for(50ms);
    CHECK_FALSE(done.load());
    p.flush();                          // e.g. seek while paused
    t.join();
    CHECK(done.load());
    CHECK(p.dropped_frames() == 4800);
    CHECK(p.clock() == 0.0);
}

TEST_CASE("AudioPlayer push: interrupt (shutdown) unblocks and stops future waits", "[audio]")
{
    AudioPlayer p;
    p.init_ring(kRate);
    push_frames(p, p.max_buffered_frames(), 0.0, 1.0);

    std::atomic<bool> done{ false };
    std::thread t([&] { push_frames(p, 4800, 1.0, 1.0); done = true; });
    std::this_thread::sleep_for(50ms);
    CHECK_FALSE(done.load());
    p.interrupt();
    t.join();
    CHECK(done.load());

    // After interrupt a push into a full ring returns immediately.
    auto t0 = std::chrono::steady_clock::now();
    push_frames(p, 4800, 2.0, 1.0);
    CHECK(std::chrono::steady_clock::now() - t0 < 40ms);
}

TEST_CASE("AudioPlayer push: muted ring (scrub) never blocks", "[audio]")
{
    AudioPlayer p;
    p.init_ring(kRate);
    push_frames(p, p.max_buffered_frames(), 0.0, 1.0);
    p.set_muted(true);

    auto t0 = std::chrono::steady_clock::now();
    push_frames(p, 4800, 1.0, 1.0);
    CHECK(std::chrono::steady_clock::now() - t0 < 40ms);
    CHECK(p.dropped_frames() == 4800);
}

TEST_CASE("AudioPlayer push: mute while blocked releases the producer", "[audio]")
{
    AudioPlayer p;
    p.init_ring(kRate);
    push_frames(p, p.max_buffered_frames(), 0.0, 1.0);

    std::atomic<bool> done{ false };
    std::thread t([&] { push_frames(p, 4800, 1.0, 1.0); done = true; });
    std::this_thread::sleep_for(50ms);
    CHECK_FALSE(done.load());
    p.set_muted(true);
    t.join();
    CHECK(done.load());
}
