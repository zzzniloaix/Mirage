#pragma once

#include <iterator>

// Playback speed presets shared by the keyboard shortcuts ([ / ]), the ImGui
// speed menus, and --speed validation. Max stays at 4.0x: AudioDecoder chains
// two atempo stages (each limited to [0.5, 2.0]), so 4.0x is the fastest speed
// the audio can follow exactly.
inline constexpr double kSpeedPresets[] = { 0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 4.0 };
inline constexpr int    kNumPresets     = static_cast<int>(std::size(kSpeedPresets));

constexpr bool is_speed_preset(double s)
{
    for (double p : kSpeedPresets)
        if (p == s) return true;
    return false;
}
