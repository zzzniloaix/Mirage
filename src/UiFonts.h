#pragma once

// System-font loading for the ImGui chrome. Fonts are read from
// /System/Library/Fonts at runtime (nothing is vendored); every slot falls
// back to ImGui's built-in font when the system file is missing.
//
// Fonts are rasterised at size × framebuffer scale and drawn with
// io.FontGlobalScale = 1/scale, so text stays crisp on Retina displays.

#include <string_view>

struct ImFont;

struct UiFonts {
    ImFont* ui       = nullptr;   // SF 14   — labels, menus
    ImFont* ui_small = nullptr;   // SF 12   — tooltips, uppercase headers
    ImFont* mono     = nullptr;   // SF Mono 13 — timecodes, chips, toasts
    ImFont* title    = nullptr;   // SF 22   — launcher title
    ImFont* sub      = nullptr;   // SF 48 raster (≤ 96 fb px) — subtitles, drawn scaled
};

// The process-wide font set (valid after ui_fonts_build()).
const UiFonts& ui_fonts();

// Build the atlas. Call after ImGui::CreateContext() and before the first
// NewFrame(). Returns false only if not even the default font could be added.
bool ui_fonts_build(float fb_scale);

// Queue codepoints missing from the subtitle font (UTF-32). Cheap; call when
// the displayed cue changes.
void ui_fonts_request_glyphs(std::u32string_view text);

// Rebuild the atlas when glyphs are pending (at most every 500 ms) or the
// framebuffer scale changed. Must run before ImGui::NewFrame(). Returns true
// when the atlas was rebuilt.
bool ui_fonts_rebuild_if_needed(float fb_scale);

// UTF-8 → UTF-32 (invalid bytes become U+FFFD).
std::u32string ui_utf8_to_u32(std::string_view s);
