#pragma once

// Design tokens for the player chrome: colours, geometry, timing.
// All sizes are logical (window) pixels; multiply by DisplayFramebufferScale
// for framebuffer pixels. Every literal colour/size used by PlayerUI and the
// Ui* helpers lives here so the look can be tuned in one place.

#include "imgui.h"

#include <algorithm>
#include <cmath>

namespace ui::theme {

// ── Colours ──────────────────────────────────────────────────────────────────
// ImU32 is ABGR-packed by IM_COL32(r, g, b, a).

constexpr ImU32 rgba(int r, int g, int b, float a)
{
    return IM_COL32(r, g, b, static_cast<int>(a * 255.0f + 0.5f));
}

inline constexpr ImU32 kAccent        = rgba( 56, 189, 248, 1.00f);  // #38BDF8 sky-400
inline constexpr ImU32 kAccentHover   = rgba(125, 211, 252, 1.00f);  // #7DD3FC sky-300
inline constexpr ImU32 kText          = rgba(235, 237, 242, 1.00f);  // #EBEDF2
inline constexpr ImU32 kTextDim       = rgba(255, 255, 255, 0.60f);
inline constexpr ImU32 kTextFaint     = rgba(255, 255, 255, 0.40f);
inline constexpr ImU32 kIcon          = rgba(255, 255, 255, 0.92f);
inline constexpr ImU32 kIconHover     = rgba(255, 255, 255, 1.00f);
inline constexpr ImU32 kHoverBg       = rgba(255, 255, 255, 0.12f);
inline constexpr ImU32 kPressBg       = rgba(255, 255, 255, 0.20f);
inline constexpr ImU32 kGlass         = rgba( 18,  20,  24, 0.66f);  // #121418
inline constexpr ImU32 kGlassHair     = rgba(255, 255, 255, 0.10f);
inline constexpr ImU32 kPanel         = rgba( 17,  18,  22, 0.90f);  // #111216
inline constexpr ImU32 kPanelBorder   = rgba(255, 255, 255, 0.08f);
inline constexpr ImU32 kTrackRest     = rgba(255, 255, 255, 0.25f);
inline constexpr ImU32 kTrackBuffered = rgba(255, 255, 255, 0.45f);
inline constexpr ImU32 kTick          = rgba(255, 255, 255, 0.35f);
inline constexpr ImU32 kTickFaint     = rgba(255, 255, 255, 0.14f);  // keyframe known only as a segment start
inline constexpr ImU32 kError         = rgba(255,  97, 110, 1.00f);  // #FF616E
inline constexpr ImU32 kWarn          = rgba(251, 191,  36, 1.00f);  // #FBBF24
inline constexpr ImU32 kShadow        = rgba(  0,   0,   0, 0.35f);

inline constexpr float kScrimBottomA  = 0.70f;   // black, fades to 0 upward
inline constexpr float kScrimTopA     = 0.45f;   // black, fades to 0 downward

// ImVec4 form of a packed colour (for ImGuiStyle::Colors).
inline ImVec4 vec4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

// Multiply a packed colour's alpha by a (0..1).
inline ImU32 alpha(ImU32 c, float a)
{
    const unsigned ca = (c >> IM_COL32_A_SHIFT) & 0xFF;
    const unsigned na = static_cast<unsigned>(static_cast<float>(ca) * std::clamp(a, 0.0f, 1.0f) + 0.5f);
    return (c & ~IM_COL32_A_MASK) | (na << IM_COL32_A_SHIFT);
}

// ── Geometry ─────────────────────────────────────────────────────────────────

inline constexpr float kGrid          = 4.0f;
inline constexpr float kGutter        = 16.0f;
inline constexpr float kPillH         = 40.0f;
inline constexpr float kPillPadX      = 6.0f;
inline constexpr float kBtn           = 36.0f;
inline constexpr float kIconSz        = 20.0f;   // spec token "kIcon" (size)
inline constexpr float kIconStroke    = 2.0f;
inline constexpr float kSeekHit       = 24.0f;
inline constexpr float kTrackIdle     = 4.0f;
inline constexpr float kTrackHover    = 8.0f;
inline constexpr float kThumbDot      = 14.0f;
inline constexpr float kRadiusPanel   = 12.0f;
inline constexpr float kRadiusCard    = 10.0f;
inline constexpr float kRadiusSmall   = 6.0f;
inline constexpr float kRadiusChip    = 6.0f;
inline constexpr float kMenuRowH      = 34.0f;
inline constexpr float kMenuMinW      = 260.0f;
inline constexpr float kTopBarH       = 44.0f;
inline constexpr float kTopScrimH     = 72.0f;
inline constexpr float kBottomScrimH  = 168.0f;
inline constexpr float kBottomMargin  = 12.0f;
inline constexpr float kSeekGap       = 8.0f;    // between seek hit zone and pills
// 12 margin + 40 pills + 8 gap + 24 seek hit zone
inline constexpr float kControlStackH = kBottomMargin + kPillH + kSeekGap + kSeekHit;
static_assert(kControlStackH == 84.0f);

// ── Timing (seconds) ─────────────────────────────────────────────────────────

inline constexpr double kHideDelay    = 2.0;
inline constexpr float  kFade         = 0.20f;
inline constexpr float  kTrackEase    = 0.12f;
inline constexpr float  kSubLiftEase  = 0.28f;
inline constexpr float  kToastHold    = 1.0f;
inline constexpr float  kToastFade    = 0.20f;
inline constexpr float  kFlashDur     = 0.50f;

// Exponential smoothing toward target; reaches ~95% after `dur` seconds.
inline float ease(float v, float target, float dt, float dur)
{
    if (dur <= 0.0f) return target;
    const float k = 1.0f - std::exp(-dt / (dur / 3.0f));
    v += (target - v) * k;
    if (std::abs(target - v) < 1e-3f) v = target;
    return v;
}

} // namespace ui::theme
