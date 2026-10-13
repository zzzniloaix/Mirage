#pragma once

// Vector icons drawn with ImDrawList (no icon font). Each icon is designed on
// a 24-unit grid and scaled so the grid spans `sz` logical pixels, centred
// on `c`. Strokes use theme::kIconStroke with round caps.

#include "imgui.h"

namespace icons {

void play           (ImDrawList* dl, ImVec2 c, float sz, ImU32 col);
void pause          (ImDrawList* dl, ImVec2 c, float sz, ImU32 col);
void step_frame_back(ImDrawList* dl, ImVec2 c, float sz, ImU32 col);   // |◀
void step_frame_fwd (ImDrawList* dl, ImVec2 c, float sz, ImU32 col);   // ▶|
void key_back       (ImDrawList* dl, ImVec2 c, float sz, ImU32 col);   // «◆
void key_fwd        (ImDrawList* dl, ImVec2 c, float sz, ImU32 col);   // ◆»
// level: 0 = muted (X), 1 = low (one arc), 2 = high (two arcs)
void volume         (ImDrawList* dl, ImVec2 c, float sz, ImU32 col, int level);
// on: draws the accent underline below the box
void cc             (ImDrawList* dl, ImVec2 c, float sz, ImU32 col, bool on, ImFont* font);
void gear           (ImDrawList* dl, ImVec2 c, float sz, ImU32 col);
// dot: accent dot top-right (any engineer overlay on)
void hud            (ImDrawList* dl, ImVec2 c, float sz, ImU32 col, bool dot);
void fullscreen     (ImDrawList* dl, ImVec2 c, float sz, ImU32 col);
void exit_fullscreen(ImDrawList* dl, ImVec2 c, float sz, ImU32 col);
void menu           (ImDrawList* dl, ImVec2 c, float sz, ImU32 col);
void help           (ImDrawList* dl, ImVec2 c, float sz, ImU32 col, ImFont* font);
void chevron_right  (ImDrawList* dl, ImVec2 c, float sz, ImU32 col);
void chevron_left   (ImDrawList* dl, ImVec2 c, float sz, ImU32 col);
void check          (ImDrawList* dl, ImVec2 c, float sz, ImU32 col);

} // namespace icons
