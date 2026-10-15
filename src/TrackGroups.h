#pragma once

// Track-switcher list building, kept free of ImGui so it can be unit-tested.
//
// HLS masters often list the same video rendition once per audio group
// (Apple's bipbop: 8 renditions × 3 audio groups = 24 EXT-X-STREAM-INF), and
// FFmpeg exposes each listing as its own AVStream. Those copies are
// indistinguishable to the user, so the switcher shows one row per rendition.

#include "Demuxer.h"

#include <string>
#include <vector>

struct VideoTrackGroup {
    int              stream_idx;   // stream to switch to when the row is picked
    std::vector<int> members;      // every stream with identical parameters
    std::string      title;        // "1080p · 60 fps"
    std::string      detail;       // "h264 · 1920×1080 · 8.1 Mb/s"
};

// One row per distinct (codec, size, fps, bitrate), highest resolution first,
// then highest bitrate. The row's stream_idx is `cur_stream` when the current
// stream belongs to the group, else the lowest stream index in it.
std::vector<VideoTrackGroup> group_video_tracks(const std::vector<VideoTrackInfo>& tracks,
                                                int cur_stream);

// Row text for audio / subtitle tracks.
std::string audio_track_title(const AudioTrackInfo& t);    // "English" / "Track 24"
std::string audio_track_detail(const AudioTrackInfo& t);   // "aac · Stereo · 48 kHz · 159 kb/s"
std::string subtitle_track_title(const SubtitleTrackInfo& t);
std::string subtitle_track_detail(const SubtitleTrackInfo& t);  // "webvtt · text"

// ISO 639-1/-2 code → English name ("en"/"eng" → "English"); other
// strings (titles, unknown codes) are returned unchanged.
std::string language_name(const std::string& code);

// "Stereo", "Mono", "5.1", "7.1", else "<n> ch".
std::string channel_label(int channels);

// "8.1 Mb/s", "671 kb/s"; empty for <= 0.
std::string bitrate_label(int64_t bits_per_s);
