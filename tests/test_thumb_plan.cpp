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
