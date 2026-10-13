#pragma once

// ImGui text-subtitle renderer: word wrap (UTF-8 / CJK aware), box /
// outline / drop-shadow styles, and an eased lift so cues glide above the
// player controls instead of jumping when they show or hide.

#include "imgui.h"

#include <string>
#include <vector>

// Subtitle look (settings ▸ Subtitle style).
struct SubStyle {
    enum Edge { Box, Outline, DropShadow, None };
    int  size_pct    = 100;   // 50..200
    Edge edge        = Box;
    int  box_opacity = 70;    // 0..100, Box only
};

class SubtitleOverlay {
public:
    // Draw `text` inside the video rect (logical px: x, y_top, w, h).
    // reserve_bottom: logical px at the window bottom the cue must clear
    // (control stack + engineer strips + margin); the cue lifts only as far
    // as needed and eases toward it. Empty text still advances the lift.
    void draw(ImDrawList* dl, ImFont* font, const std::string& text,
              ImVec4 video_rect, float win_h, float reserve_bottom,
              const SubStyle& style, float dt);

    float lift() const { return lift_; }

    // Wrap helper (exposed for tests / reuse): greedy word wrap at max_w,
    // honouring '\n', CJK breaking between codepoints, max 3 lines (earliest
    // overflow lines dropped).
    static std::vector<std::string> wrap(ImFont* font, float px, const std::string& text,
                                         float max_w, int max_lines = 3);

private:
    float       lift_      = 0.0f;
    bool        lift_init_ = false;
    std::string last_text_;
};
