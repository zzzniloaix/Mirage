#include "UiWidgets.h"
#include "UiFonts.h"
#include "UiFormat.h"
#include "UiIcons.h"
#include "UiTheme.h"

#include <algorithm>
#include <cmath>

namespace ui {

namespace th = ui::theme;

// Theme colour with the current style alpha (fade) applied.
static ImU32 C(ImU32 c, float mul = 1.0f) { return ImGui::GetColorU32(c, mul); }

void tooltip(const char* text)
{
    if (!text || !*text) return;
    if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled))
        return;
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 5.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, th::kRadiusSmall);
    if (ImGui::BeginTooltip()) {
        ImGui::PushFont(ui_fonts().ui_small);
        ImGui::TextUnformatted(text);
        ImGui::PopFont();
        ImGui::EndTooltip();
    }
    ImGui::PopStyleVar(3);
}

IconBtn icon_button(const char* id, const char* tip, bool enabled)
{
    IconBtn b;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(th::kBtn, th::kBtn));
    const bool hov = ImGui::IsItemHovered();
    const bool act = ImGui::IsItemActive();
    b.c       = ImVec2(p.x + th::kBtn * 0.5f, p.y + th::kBtn * 0.5f);
    b.hovered = enabled && hov;
    b.clicked = enabled && pressed;
    if (b.hovered)
        ImGui::GetWindowDrawList()->AddCircleFilled(b.c, th::kBtn * 0.5f,
                                                    C(act ? th::kPressBg : th::kHoverBg), 32);
    b.col = !enabled ? C(th::kTextFaint) : C(b.hovered ? th::kIconHover : th::kIcon);
    if (hov) tooltip(tip);
    return b;
}

void glass_pill(ImDrawList* dl, ImVec2 min, ImVec2 max, float alpha)
{
    const float r = (max.y - min.y) * 0.5f;
    dl->AddRectFilled(min, max, th::alpha(th::kGlass, alpha), r);
    dl->AddLine(ImVec2(min.x + r, min.y + 0.5f), ImVec2(max.x - r, min.y + 0.5f),
                th::alpha(th::kGlassHair, alpha), 1.0f);
}

ImVec2 chip_size(const char* text, ImFont* font, float fs)
{
    const ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, text);
    return ImVec2(std::round(ts.x + 16.0f), std::round(ts.y + 6.0f));
}

ImVec2 chip(ImDrawList* dl, ImVec2 pos, const char* text, ImFont* font, float fs,
            ImU32 text_col, ImU32 border, float alpha)
{
    const ImVec2 sz = chip_size(text, font, fs);
    const ImVec2 max(pos.x + sz.x, pos.y + sz.y);
    dl->AddRectFilled(pos, max, th::alpha(th::kGlass, alpha), th::kRadiusChip);
    if (border) dl->AddRect(pos, max, th::alpha(border, alpha), th::kRadiusChip, 0, 1.0f);
    dl->AddText(font, fs, ImVec2(pos.x + 8.0f, pos.y + 3.0f), th::alpha(text_col, alpha), text);
    return sz;
}

// ── Seek bar ─────────────────────────────────────────────────────────────────

SeekBarOut seek_bar(const char* id, ImVec2 pos, float width,
                    const SeekBarIn& in, SeekBarState& st, float dt)
{
    SeekBarOut out;
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton(id, ImVec2(width, th::kSeekHit));
    const bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const bool active  = ImGui::IsItemActive();
    const float mx     = ImGui::GetIO().MousePos.x;
    const float mouse_frac = std::clamp((mx - pos.x) / width, 0.0f, 1.0f);

    // Esc cancels the current hold; the rest of the hold is swallowed.
    if (active && !st.cancelled && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        st.cancelled  = true;
        out.cancelled = true;
    }

    const bool dragging = active && !st.cancelled;
    if (dragging) {
        st.drag_frac   = mouse_frac;
        out.dragging   = true;
        out.drag_frac  = mouse_frac;
    } else if (st.active && !active) {
        if (!st.cancelled && in.dur > 0.0)
            out.seek_to = static_cast<double>(st.drag_frac) * in.dur;   // release: one seek
        st.cancelled = false;
    }
    st.active = active;

    const bool  debug_hover = in.debug_hover_frac >= 0.0f;
    const bool  show_hover  = debug_hover || (hovered && !active);
    const float hover_frac  = debug_hover ? std::clamp(in.debug_hover_frac, 0.0f, 1.0f) : mouse_frac;
    out.hovered    = show_hover || dragging;
    out.hover_frac = dragging ? st.drag_frac : (show_hover ? hover_frac : -1.0f);

    const bool expanded = show_hover || dragging;
    st.track_h = debug_hover ? th::kTrackHover
                             : th::ease(st.track_h, expanded ? th::kTrackHover : th::kTrackIdle,
                                        dt, th::kTrackEase);

    const float cy = pos.y + th::kSeekHit * 0.5f;
    const float h  = st.track_h;
    const float x0 = pos.x, x1 = pos.x + width;
    const float y0 = cy - h * 0.5f, y1 = cy + h * 0.5f;
    out.track_min = ImVec2(x0, y0);
    out.track_max = ImVec2(x1, y1);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float r = h * 0.5f;

    if (!(in.dur > 0.0)) {
        // Live / unknown duration: full accent line, no interaction affordances.
        dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), C(th::kAccent, 0.6f), r);
        return out;
    }

    auto frac_x = [&](double t) {
        return x0 + static_cast<float>(std::clamp(t / in.dur, 0.0, 1.0)) * width;
    };
    const float play_x = dragging ? x0 + st.drag_frac * width : frac_x(in.cur);

    // 1. rest
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), C(th::kTrackRest), r);
    // 2. buffered (cur → demuxed-ahead)
    if (in.buffered_end > in.cur) {
        const float bx = frac_x(in.buffered_end);
        if (bx > play_x)
            dl->AddRectFilled(ImVec2(play_x, y0), ImVec2(bx, y1), C(th::kTrackBuffered), r);
    }
    // 3. played
    if (play_x > x0)
        dl->AddRectFilled(ImVec2(x0, y0), ImVec2(std::max(play_x, x0 + h), y1), C(th::kAccent), r);
    // 4. keyframe ticks (hover / drag only)
    if (expanded && in.kf_pts && !in.kf_pts->empty() && in.kf_pts->size() < 20000) {
        for (float tx : decimate_ticks(*in.kf_pts, in.dur, width, 3.0f))
            dl->AddRectFilled(ImVec2(x0 + tx, y0), ImVec2(x0 + tx + 1.0f, y1), C(th::kTick));
    }
    // 5. hover ghost
    if (show_hover && !dragging) {
        const float hx = x0 + hover_frac * width;
        const float gx0 = std::min(hx, play_x), gx1 = std::max(hx, play_x);
        if (gx1 - gx0 > 0.5f)
            dl->AddRectFilled(ImVec2(gx0, y0), ImVec2(gx1, y1), C(IM_COL32(255, 255, 255, 38)), r);
    }
    // 6. thumb dot
    if (expanded) {
        const float dot_x = (debug_hover && !dragging) ? x0 + hover_frac * width : play_x;
        dl->AddCircleFilled(ImVec2(dot_x, cy + 1.0f), th::kThumbDot * 0.5f + 1.0f, C(th::kShadow), 24);
        dl->AddCircleFilled(ImVec2(dot_x, cy), th::kThumbDot * 0.5f, C(IM_COL32_WHITE), 24);
    }
    return out;
}

// ── Timecode ─────────────────────────────────────────────────────────────────

static void split_tc(const std::string& text, std::string& a, std::string& b)
{
    const auto p = text.find(" / ");
    if (p == std::string::npos) { a = text; b.clear(); return; }
    a = text.substr(0, p);
    b = text.substr(p);
}

float timecode_width(const std::string& text, ImFont* mono)
{
    const float fs = 13.0f;
    float w = mono->CalcTextSizeA(fs, FLT_MAX, 0.0f, text.c_str()).x;
    if (text.starts_with("LIVE")) w += 12.0f;
    return w;
}

bool timecode(const char* id, const std::string& text, ImFont* mono)
{
    const float fs = 13.0f;
    const ImVec2 p  = ImGui::GetCursorScreenPos();
    const float  w  = timecode_width(text, mono);
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(w, th::kBtn));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float ty = std::round(p.y + (th::kBtn - fs) * 0.5f - 1.0f);

    if (text.starts_with("LIVE")) {
        // "LIVE ● 00:12.345"
        const char* live = "LIVE";
        float x = p.x;
        dl->AddText(mono, fs, ImVec2(x, ty), C(th::kText), live);
        x += mono->CalcTextSizeA(fs, FLT_MAX, 0.0f, live).x + 6.0f;
        dl->AddCircleFilled(ImVec2(x + 3.0f, ty + fs * 0.5f + 1.0f), 3.0f, C(th::kError), 12);
        x += 12.0f;
        dl->AddText(mono, fs, ImVec2(x, ty), C(th::kText), text.c_str() + 5);
        return clicked;
    }
    std::string a, b;
    split_tc(text, a, b);
    dl->AddText(mono, fs, ImVec2(p.x, ty), C(th::kText), a.c_str());
    if (!b.empty()) {
        const float aw = mono->CalcTextSizeA(fs, FLT_MAX, 0.0f, a.c_str()).x;
        dl->AddText(mono, fs, ImVec2(p.x + aw, ty), C(th::kTextDim), b.c_str());
    }
    return clicked;
}

// ── Overlays ─────────────────────────────────────────────────────────────────

void center_flash(ImDrawList* dl, ImVec2 c, float t01, bool playing)
{
    t01 = std::clamp(t01, 0.0f, 1.0f);
    const float e     = 1.0f - (1.0f - t01) * (1.0f - t01);   // ease-out
    const float scale = 0.9f + 0.2f * e;
    const float a     = 1.0f - e;
    if (a <= 0.0f) return;
    dl->AddCircleFilled(c, 36.0f * scale, IM_COL32(0, 0, 0, static_cast<int>(0.45f * a * 255.0f)), 48);
    const ImU32 col = th::alpha(IM_COL32_WHITE, a);
    if (playing) icons::play (dl, ImVec2(c.x + 2.0f * scale, c.y), 32.0f * scale, col);
    else         icons::pause(dl, c, 32.0f * scale, col);
}

void osd_toast(ImDrawList* dl, float win_w, const std::string& text, ImFont* mono, float alpha, float y)
{
    if (alpha <= 0.0f || text.empty()) return;
    const float fs = 13.0f;
    const ImVec2 ts = mono->CalcTextSizeA(fs, FLT_MAX, 0.0f, text.c_str());
    const float w = ts.x + 28.0f, h = ts.y + 16.0f;
    const ImVec2 min(std::round((win_w - w) * 0.5f), y);
    const ImVec2 max(min.x + w, min.y + h);
    glass_pill(dl, min, max, alpha);
    dl->AddText(mono, fs, ImVec2(min.x + 14.0f, min.y + 8.0f), th::alpha(th::kText, alpha), text.c_str());
}

void spinner(ImDrawList* dl, ImVec2 c, float r, float t, float alpha)
{
    if (alpha <= 0.0f) return;
    constexpr float kPi = 3.14159265f;
    dl->AddCircle(c, r, th::alpha(th::kTrackRest, alpha), 32, 2.5f);
    // Rotating arc whose length breathes between ~25% and ~75% of the circle.
    const float a0  = t * 2.0f * kPi * 1.1f;
    const float len = kPi * (1.0f + 0.5f * std::sin(t * 2.6f));
    dl->PathArcTo(c, r, a0, a0 + len, 24);
    dl->PathStroke(th::alpha(th::kAccent, alpha), ImDrawFlags_None, 2.5f);
}

void busy_pill(ImDrawList* dl, ImVec2 centre, bool top, const std::string& title,
               const std::string& detail, float t, ImFont* ui_font, ImFont* mono, float alpha)
{
    if (alpha <= 0.0f || title.empty()) return;
    const float fs = 14.0f, fs_d = 12.0f;
    const float spin_r = 8.0f, pad_x = 14.0f, gap = 10.0f, h = 36.0f;
    const ImVec2 ts = ui_font->CalcTextSizeA(fs, FLT_MAX, 0.0f, title.c_str());
    const ImVec2 ds = detail.empty() ? ImVec2(0, 0) : mono->CalcTextSizeA(fs_d, FLT_MAX, 0.0f, detail.c_str());
    const float w = pad_x + spin_r * 2.0f + gap + ts.x + (detail.empty() ? 0.0f : gap + ds.x) + pad_x + 2.0f;
    const ImVec2 min(std::round(centre.x - w * 0.5f), std::round(top ? centre.y : centre.y - h * 0.5f));
    const ImVec2 max(min.x + w, min.y + h);
    dl->AddRectFilled(ImVec2(min.x, min.y + 2.0f), ImVec2(max.x, max.y + 2.0f),
                      th::alpha(th::kShadow, alpha), h * 0.5f);
    glass_pill(dl, min, max, alpha);
    const float cy = min.y + h * 0.5f;
    spinner(dl, ImVec2(min.x + pad_x + spin_r, cy), spin_r, t, alpha);
    float x = min.x + pad_x + spin_r * 2.0f + gap;
    dl->AddText(ui_font, fs, ImVec2(x, std::round(cy - ts.y * 0.5f)), th::alpha(th::kText, alpha), title.c_str());
    if (!detail.empty()) {
        x += ts.x + gap;
        dl->AddText(mono, fs_d, ImVec2(x, std::round(cy - ds.y * 0.5f)), th::alpha(th::kTextDim, alpha), detail.c_str());
    }
}

} // namespace ui
