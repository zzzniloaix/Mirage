#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Sync.h"
#include "SpeedPresets.h"

#include <cmath>
#include <limits>

using Catch::Matchers::WithinAbs;

static constexpr double kFrame60 = 1.0 / 60.0;

// ── frame_duration ────────────────────────────────────────────────────────────

TEST_CASE("frame_duration: PTS delta when valid, fallback otherwise", "[sync]")
{
    CHECK_THAT(frame_duration(10.0 + kFrame60, 10.0, 0.04), WithinAbs(kFrame60, 1e-12));
    CHECK(frame_duration(10.0, 10.0, 0.04)  == 0.04);    // zero delta (duplicate PTS)
    CHECK(frame_duration(9.0,  10.0, 0.04)  == 0.04);    // backwards (post-seek)
    CHECK(frame_duration(12.0, 10.0, 0.04)  == 0.04);    // > 1 s gap (discontinuity)
    CHECK(frame_duration(std::numeric_limits<double>::quiet_NaN(), 10.0, 0.04) == 0.04);
}

// ── compute_video_delay: units ────────────────────────────────────────────────

TEST_CASE("compute_video_delay: in sync returns frame duration in wall time", "[sync]")
{
    const double pts = 5.0 + kFrame60;
    for (double speed : kSpeedPresets) {
        INFO("speed " << speed);
        // Audio exactly at the frame: no correction, delay = media dur / speed.
        double d = compute_video_delay(pts, 5.0, kFrame60, pts, speed);
        CHECK_THAT(d, WithinAbs(kFrame60 / speed, 1e-12));
    }
}

TEST_CASE("compute_video_delay: missing/bogus PTS delta falls back to media frame duration", "[sync]")
{
    // last_frame_dur is media time; the result must still be divided by speed.
    // (Regression: a wall-time value used here fed back as media duration and
    // shrank by speed every frame.)
    double d = compute_video_delay(5.0, 5.0, kFrame60, 5.0, 4.0);
    CHECK_THAT(d, WithinAbs(kFrame60 / 4.0, 1e-12));
    d = compute_video_delay(3.0, 5.0, 0.04, 3.0, 2.0);   // backwards PTS
    CHECK_THAT(d, WithinAbs(0.02, 1e-12));
}

// ── compute_video_delay: speed-aware threshold ────────────────────────────────

TEST_CASE("compute_video_delay: threshold is wall time, so 4x tolerates 4x the media drift", "[sync]")
{
    const double pts = 20.0;
    const double last = pts - kFrame60;

    // 60 ms media behind audio. At 1x that's 60 ms wall (> 40 ms threshold) →
    // show immediately. At 4x it's 15 ms wall — ordinary clock jitter, so the
    // natural (wall) frame duration is kept, less a tenth of the drift.
    CHECK(compute_video_delay(pts, last, kFrame60, pts + 0.060, 1.0) == 0.0);
    CHECK_THAT(compute_video_delay(pts, last, kFrame60, pts + 0.060, 4.0),
               WithinAbs(kFrame60 / 4.0 - 0.1 * 0.015, 1e-12));

    // Same for ahead.
    CHECK(compute_video_delay(pts, last, kFrame60, pts - 0.060, 1.0) > kFrame60);
    CHECK_THAT(compute_video_delay(pts, last, kFrame60, pts - 0.060, 4.0),
               WithinAbs(kFrame60 / 4.0 + 0.1 * 0.015, 1e-12));

    // 200 ms media behind at 4x = 50 ms wall: past the 40 ms floor → catch up.
    CHECK(compute_video_delay(pts, last, kFrame60, pts + 0.200, 4.0) == 0.0);
}

TEST_CASE("compute_video_delay: threshold clamps to [40ms, 100ms] wall", "[sync]")
{
    // 24 fps at 0.25x: a frame lasts 167 ms wall → threshold caps at 100 ms wall
    // = 25 ms media. 30 ms media behind (120 ms wall) must correct.
    const double f24 = 1.0 / 24.0;
    CHECK(compute_video_delay(10.0, 10.0 - f24, f24, 10.030, 0.25) == 0.0);
    // 20 ms media behind (80 ms wall) is inside the window: a tenth of it off.
    CHECK_THAT(compute_video_delay(10.0, 10.0 - f24, f24, 10.020, 0.25),
               WithinAbs(f24 / 0.25 - 0.1 * 0.080, 1e-12));
}

TEST_CASE("compute_video_delay: video ahead holds the frame until audio catches up", "[sync]")
{
    const double pts = 30.0, last = pts - kFrame60;
    // 4x, video 0.4 s media (100 ms wall) ahead → wait frame + 100 ms wall.
    double d = compute_video_delay(pts, last, kFrame60, pts - 0.4, 4.0);
    CHECK_THAT(d, WithinAbs(kFrame60 / 4.0 + 0.1, 1e-9));
    // 1x, 50 ms ahead → frame + 50 ms.
    d = compute_video_delay(pts, last, kFrame60, pts - 0.05, 1.0);
    CHECK_THAT(d, WithinAbs(kFrame60 + 0.05, 1e-9));
}

TEST_CASE("compute_video_delay: huge diffs are seek artifacts, not corrected", "[sync]")
{
    const double pts = 100.0, last = pts - kFrame60;
    CHECK_THAT(compute_video_delay(pts, last, kFrame60, 0.0, 2.0), WithinAbs(kFrame60 / 2.0, 1e-12));
    CHECK_THAT(compute_video_delay(pts, last, kFrame60, 200.0, 2.0), WithinAbs(kFrame60 / 2.0, 1e-12));
}

TEST_CASE("compute_video_delay: non-positive speed is treated as 1x", "[sync]")
{
    CHECK_THAT(compute_video_delay(1.0 + kFrame60, 1.0, kFrame60, 1.0 + kFrame60, 0.0),
               WithinAbs(kFrame60, 1e-12));
}

// ── Speed presets ─────────────────────────────────────────────────────────────

TEST_CASE("speed presets: sorted, include 1x, max 4x (two atempo stages)", "[sync]")
{
    for (int i = 1; i < kNumPresets; ++i)
        CHECK(kSpeedPresets[i] > kSpeedPresets[i - 1]);
    CHECK(is_speed_preset(1.0));
    CHECK(kSpeedPresets[kNumPresets - 1] == 4.0);
    CHECK(kSpeedPresets[0] >= 0.25);
    CHECK_FALSE(is_speed_preset(3.0));
    CHECK_FALSE(is_speed_preset(8.0));
}

TEST_CASE("compute_video_delay: an offset inside the window shrinks frame by frame", "[sync]")
{
    // Video 28 ms behind the audio clock (HLS after its start): no hard
    // correction, but each frame shows a tenth of the offset early.
    double offset = -0.028, pts = 30.0;
    for (int i = 0; i < 40; ++i) {
        const double delay = compute_video_delay(pts + kFrame60, pts, kFrame60, pts + kFrame60 - offset, 1.0);
        offset += kFrame60 - delay;   // shown earlier than its duration: catches up by the difference
        pts += kFrame60;
    }
    CHECK(std::abs(offset) < 0.001);
}
