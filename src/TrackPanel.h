#pragma once

// Track switcher card (T key): Video / Audio / Subtitles columns drawn with
// Dear ImGui in the player theme. Must be called between PlayerUI's
// begin_frame() and end_frame().

#include "Demuxer.h"

#include <vector>

struct TrackPanelInputs {
    const std::vector<VideoTrackInfo>*    video    = nullptr;
    const std::vector<AudioTrackInfo>*    audio    = nullptr;
    const std::vector<SubtitleTrackInfo>* subtitle = nullptr;
    int cur_video = -1, cur_audio = -1, cur_subtitle = -1;   // -1 subtitle = off
    // Stream whose switch is still loading (-1 = none).
    int pending_video = -1, pending_audio = -1, pending_subtitle = -1;
    double t = 0.0;              // seconds, drives the spinner
    float  win_w = 0.0f, win_h = 0.0f;   // logical display size
    float  right_inset = 0.0f;   // logical px taken by the network sidebar
};

struct TrackPanelOut {
    int  video = -1, audio = -1;
    int  subtitle = -1;          // -2 = "Off" picked, >= 0 = stream
    bool close = false;
};

TrackPanelOut draw_track_panel(const TrackPanelInputs& in);
