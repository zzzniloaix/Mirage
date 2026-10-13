#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "ThumbPlan.h"
#include "FfOpen.h"

#include <algorithm>
#include <numeric>
#include <vector>

using namespace thumbs;

TEST_CASE("sample_stride: duration / (max-1), 0 when unknown", "[thumbs]")
{
    CHECK_THAT(sample_stride(399.0, 400), Catch::Matchers::WithinAbs(1.0, 1e-12));
    CHECK(sample_stride(0.0, 400)  == 0.0);
    CHECK(sample_stride(-1.0, 400) == 0.0);   // live
    CHECK(sample_stride(100.0, 1)  == 0.0);
}

TEST_CASE("sample_keyframes: 10 min clip with 1 s GOP caps at 400", "[thumbs]")
{
    std::vector<double> kf(600);
    std::iota(kf.begin(), kf.end(), 0.0);   // 0,1,...,599
    const double stride = sample_stride(600.0, 400);
    const auto s = sample_keyframes(kf, stride, 400);
    REQUIRE(!s.empty());
    CHECK(s.size() <= 400);
    CHECK(s.front() == 0);
    CHECK(std::is_sorted(s.begin(), s.end()));
    for (std::size_t i = 1; i < s.size(); ++i)
        CHECK(kf[s[i]] - kf[s[i - 1]] >= stride);
}

TEST_CASE("sample_keyframes: sparse keyframes are all kept", "[thumbs]")
{
    const std::vector<double> kf{ 0.0, 2.0, 4.0, 6.0 };
    const auto s = sample_keyframes(kf, sample_stride(8.0, 400), 400);
    CHECK(s == std::vector<std::size_t>{ 0, 1, 2, 3 });
    CHECK(sample_keyframes({}, 1.0, 400).empty());
    // stride 0 (live/unknown) keeps every keyframe
    CHECK(sample_keyframes(kf, 0.0, 400).size() == 4);
}

TEST_CASE("sample_keyframes: hard cap thins uniformly", "[thumbs]")
{
    std::vector<double> kf(1000);
    std::iota(kf.begin(), kf.end(), 0.0);
    const auto s = sample_keyframes(kf, 0.0, 100);   // stride says keep all → cap
    CHECK(s.size() == 100);
    CHECK(s.front() == 0);
    CHECK(std::adjacent_find(s.begin(), s.end(),
                             [](auto a, auto b) { return b <= a; }) == s.end());
}

TEST_CASE("coarse_to_fine: every step-th first, then the rest, each once", "[thumbs]")
{
    const auto o = coarse_to_fine(20, 8);
    REQUIRE(o.size() == 20);
    CHECK(o[0] == 0);
    CHECK(o[1] == 8);
    CHECK(o[2] == 16);
    CHECK(o[3] == 1);
    auto sorted = o;
    std::sort(sorted.begin(), sorted.end());
    for (std::size_t i = 0; i < sorted.size(); ++i) CHECK(sorted[i] == i);

    CHECK(coarse_to_fine(0, 8).empty());
    CHECK(coarse_to_fine(5, 1) == std::vector<std::size_t>{ 0, 1, 2, 3, 4 });
    CHECK(coarse_to_fine(3, 8) == std::vector<std::size_t>{ 0, 1, 2 });
}

TEST_CASE("ffopen::is_network_url", "[thumbs]")
{
    CHECK(ffopen::is_network_url("https://example.com/a.m3u8"));
    CHECK(ffopen::is_network_url("http://10.255.255.1/x.mp4"));
    CHECK(ffopen::is_network_url("rtsp://cam/stream"));
    CHECK_FALSE(ffopen::is_network_url("/Users/me/clip.mp4"));
    CHECK_FALSE(ffopen::is_network_url("file:///tmp/clip.mp4"));
}

TEST_CASE("index_too_sparse: lazy MKV cues / HLS need a fallback", "[thumbs]")
{
    CHECK(index_too_sparse(0, 311.0));     // HLS: no index
    CHECK(index_too_sparse(1, 311.0));     // MKV before Cues are read
    CHECK(index_too_sparse(10, 311.0));    // < 15 for a 5 min clip
    CHECK_FALSE(index_too_sparse(138, 311.0));
    CHECK_FALSE(index_too_sparse(16, 3600.0));   // 16 caps the requirement
    CHECK(index_too_sparse(1, 10.0));      // short clip still wants 2
    CHECK_FALSE(index_too_sparse(2, 10.0));
    CHECK(index_too_sparse(0, -1.0));      // live, nothing
    CHECK_FALSE(index_too_sparse(3, -1.0));
}

TEST_CASE("sample_times: evenly spaced, capped by count and spacing", "[thumbs]")
{
    const auto t = sample_times(600.0, 48, 10.0);
    REQUIRE(t.size() == 48);
    CHECK(t.front() == 0.0);
    CHECK(t.back() < 600.0);
    for (std::size_t i = 1; i < t.size(); ++i)
        CHECK_THAT(t[i] - t[i - 1], Catch::Matchers::WithinAbs(12.5, 1e-9));

    // spacing wins for short clips: 30 s / 10 s -> 4 samples (0, 7.5, 15, 22.5)
    const auto s = sample_times(30.0, 48, 10.0);
    REQUIRE(s.size() == 4);
    CHECK_THAT(s[1], Catch::Matchers::WithinAbs(7.5, 1e-9));

    CHECK(sample_times(5.0, 48, 10.0).size() == 1);
    CHECK(sample_times(-1.0, 48, 10.0).empty());
    CHECK(sample_times(100.0, 0, 1.0).empty());
    CHECK(sample_times(100.0, 10, 0.0).size() == 10);
}

TEST_CASE("pick_thumb_stream: cheapest same-codec variant tall enough", "[thumbs]")
{
    constexpr int H264 = 27, HEVC = 173;
    const std::vector<VideoCandidate> v{
        { 0, H264,  540, 2'177'116 },
        { 1, H264, 1080, 8'001'098 },   // playing
        { 7, H264,  270,   541'052 },
        { 8, H264,  144,   200'000 },   // too short for 160 px thumbnails
        { 9, HEVC,  270,   300'000 },   // other codec
        { 5, H264,  432,         0 },   // unknown bitrate
    };
    CHECK(pick_thumb_stream(v, 1, 160) == 7);
    CHECK(pick_thumb_stream(v, 7, 160) == 7);       // already the cheapest
    CHECK(pick_thumb_stream(v, 42, 160) == 42);     // not a candidate
    CHECK(pick_thumb_stream({}, 3, 160) == 3);
    // playing stream shorter than min_height: anything as tall as it will do
    CHECK(pick_thumb_stream(v, 8, 160) == 8);
    const std::vector<VideoCandidate> small{ { 0, H264, 120, 400'000 }, { 1, H264, 100, 100'000 },
                                             { 2, H264, 120, 150'000 } };
    CHECK(pick_thumb_stream(small, 0, 160) == 2);
    // unknown bitrate on the playing stream: keep it
    CHECK(pick_thumb_stream({ { 0, H264, 1080, 0 }, { 1, H264, 270, 1 } }, 0, 160) == 0);
}
