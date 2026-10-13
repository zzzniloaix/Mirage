#pragma once

// Reusable ImGui widgets for the player chrome: icon buttons, glass pills,
// the seek bar, timecode, OSD toast and centre flash. Colours and sizes come
// from UiTheme.h.

#include "imgui.h"

#include <string>
#include <vector>

namespace ui {

// ── Icon button ──────────────────────────────────────────────────────────────
// Square hit target (theme::kBtn) at the current cursor. Draws the hover /
// press circle; the caller draws the icon at `c` in `col` afterwards.
struct IconBtn {
    bool   clicked = false;
    bool   hovered = false;
    ImVec2 c;          // centre of the button
    ImU32  col = 0;    // icon colour for the current state
};
IconBtn icon_button(const char* id, const char* tooltip, bool enabled = true);

// Tooltip in the small UI font after the standard hover delay.
void tooltip(const char* text);

// ── Glass pill / chip ────────────────────────────────────────────────────────
// Rounded (h/2) translucent pill with a 1 px top hairline. No blur.
void glass_pill(ImDrawList* dl, ImVec2 min, ImVec2 max, float alpha = 1.0f);

// Small rounded label chip: kGlass fill, `font` text. Returns its size.
// border: 1 px border colour (0 = none).
ImVec2 chip(ImDrawList* dl, ImVec2 pos, const char* text, ImFont* font, float font_size,
            ImU32 text_col, ImU32 border = 0, float alpha = 1.0f);
ImVec2 chip_size(const char* text, ImFont* font, float font_size);

// ── Seek bar ─────────────────────────────────────────────────────────────────

struct SeekBarIn {
    double dur          = 0.0;
    double cur          = 0.0;
    double buffered_end = -1.0;   // < 0: unknown
    const std::vector<double>* kf_pts = nullptr;
    float  debug_hover_frac = -1.0f;   // QA: >= 0 forces the hover state
    float  alpha        = 1.0f;
};

struct SeekBarOut {
    bool   dragging   = false;
    float  drag_frac  = -1.0f;
    double seek_to    = -1.0;   // set only on release
    bool   cancelled  = false;  // Esc pressed during the drag (this frame)
    bool   hovered    = false;
    float  hover_frac = -1.0f;
    ImVec2 track_min, track_max;   // current track rect (screen space)
};

struct SeekBarState {
    bool  active    = false;   // held last frame
    bool  cancelled = false;   // Esc during this hold: swallow until release
    float drag_frac = 0.0f;
    float track_h   = 4.0f;    // eased between kTrackIdle and kTrackHover
};

// Interactive bar spanning [pos.x, pos.x + width] with a kSeekHit tall hit zone.
// Never seeks on hover; seek_to only on release; Esc cancels a drag.
SeekBarOut seek_bar(const char* id, ImVec2 pos, float width,
                    const SeekBarIn& in, SeekBarState& st, float dt);

// ── Timecode ─────────────────────────────────────────────────────────────────
// Draws `text` (split at " / " into primary + dim parts) as a clickable item.
// Returns true when clicked. Live text "LIVE ..." gets a red dot.
bool timecode(const char* id, const std::string& text, ImFont* mono);
float timecode_width(const std::string& text, ImFont* mono);

// ── Overlays (foreground draw list) ──────────────────────────────────────────

// Centred 72 px disc with a play/pause glyph. t01 = progress through the
// flash (0..1); `playing` = new state is playing (draws ▶).
void center_flash(ImDrawList* dl, ImVec2 centre, float t01, bool playing);

// Toast pill at top-centre (y = 56). alpha 0..1.
void osd_toast(ImDrawList* dl, float win_w, const std::string& text, ImFont* mono, float alpha);

} // namespace ui
