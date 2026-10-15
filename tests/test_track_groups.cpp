#include <catch2/catch_test_macros.hpp>

#include "TrackGroups.h"

#include <vector>

namespace {

VideoTrackInfo v(int idx, int w, int h, double fps, int64_t br)
{
    VideoTrackInfo t;
    t.stream_idx = idx;
    t.codec_name = "h264";
    t.width = w; t.height = h; t.fps = fps; t.bit_rate = br;
    return t;
}

} // namespace

TEST_CASE("group_video_tracks: one row per rendition, bipbop-style 8x3 master", "[tracks]")
{
    // Apple bipbop fMP4: 8 renditions listed once per audio group (3 groups).
    const VideoTrackInfo base[] = {
        v(0,  960,  540, 60, 2012740), v(1, 1920, 1080, 60, 8107224),
        v(2, 1920, 1080, 60, 6116084), v(3, 1920, 1080, 60, 4505392),
        v(4, 1280,  720, 60, 3078340), v(5,  768,  432, 30, 1114496),
        v(6,  640,  360, 30,  671632), v(7,  480,  270, 30,  359628),
    };
    std::vector<VideoTrackInfo> tracks;
    for (int g = 0; g < 3; ++g)
        for (auto t : base) { t.stream_idx += 8 * g; tracks.push_back(t); }

    const auto groups = group_video_tracks(tracks, /*cur_stream=*/9);
    REQUIRE(groups.size() == 8);

    // Highest resolution first, then bitrate.
    CHECK(groups[0].title == "1080p · 60 fps");
    CHECK(groups[0].detail == "h264 · 1920×1080 · 8.1 Mb/s");
    CHECK(groups[1].detail == "h264 · 1920×1080 · 6.1 Mb/s");
    CHECK(groups[3].title == "720p · 60 fps");
    CHECK(groups[7].title == "270p · 30 fps");
    CHECK(groups[7].detail == "h264 · 480×270 · 360 kb/s");

    // Each group holds its three copies; the playing copy (9) represents its group.
    CHECK(groups[0].members == std::vector<int>{ 1, 9, 17 });
    CHECK(groups[0].stream_idx == 9);
    CHECK(groups[1].stream_idx == 2);   // lowest index when not playing
}

TEST_CASE("group_video_tracks: different fps or bitrate stay separate", "[tracks]")
{
    const std::vector<VideoTrackInfo> tracks = {
        v(0, 1920, 1080, 60.0,   5'000'000),
        v(1, 1920, 1080, 59.94,  5'000'000),
        v(2, 1920, 1080, 60.0,   3'000'000),
    };
    const auto groups = group_video_tracks(tracks, 0);
    REQUIRE(groups.size() == 3);
    CHECK(groups[0].members == std::vector<int>{ 0 });
}

TEST_CASE("group_video_tracks: single local track, unknown fields", "[tracks]")
{
    auto groups = group_video_tracks({ v(0, 1920, 1080, 23.976, 0) }, 0);
    REQUIRE(groups.size() == 1);
    CHECK(groups[0].title == "1080p · 24 fps");
    CHECK(groups[0].detail == "h264 · 1920×1080");

    groups = group_video_tracks({ v(3, 0, 0, 0.0, 0) }, -1);
    REQUIRE(groups.size() == 1);
    CHECK(groups[0].title == "Track 3");
    CHECK(group_video_tracks({}, -1).empty());
}

TEST_CASE("audio / subtitle row text", "[tracks]")
{
    AudioTrackInfo a{ 24, "en", "aac", 48000, 2, 159000 };
    CHECK(audio_track_title(a)  == "English");
    CHECK(audio_track_detail(a) == "aac · Stereo · 48 kHz · 159 kb/s");
    AudioTrackInfo b{ 25, "", "ac3", 44100, 6, 0 };
    CHECK(audio_track_title(b)  == "Track 25");
    CHECK(audio_track_detail(b) == "ac3 · 5.1 · 44.1 kHz");

    SubtitleTrackInfo s;
    s.stream_idx = 27; s.language = "en"; s.codec_name = "webvtt"; s.is_forced = true;
    CHECK(subtitle_track_title(s)  == "English");
    CHECK(subtitle_track_detail(s) == "webvtt · text · forced");

    CHECK(language_name("JPN") == "Japanese");
    CHECK(language_name("Director commentary") == "Director commentary");
    CHECK(channel_label(8) == "7.1");
    CHECK(channel_label(3) == "3 ch");
    CHECK(bitrate_label(0).empty());
    CHECK(bitrate_label(999'400) == "999 kb/s");
}
