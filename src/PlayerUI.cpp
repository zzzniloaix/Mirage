#include "PlayerUI.h"
#include "SpeedPresets.h"
#include "ThumbnailStrip.h"
#include "UiFonts.h"
#include "UiFormat.h"
#include "UiIcons.h"
#include "UiTheme.h"
#include "UiWidgets.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <nfd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <format>

namespace th = ui::theme;

// ── helpers ───────────────────────────────────────────────────────────────────

namespace {

// Theme colour with the current style alpha (chrome fade) applied.
ImU32 C(ImU32 c, float mul = 1.0f) { return ImGui::GetColorU32(c, mul); }

std::string speed_label(double s) { return std::format("{:g}×", s); }

constexpr ImGuiWindowFlags kOverlayFlags =
    ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
    ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
    ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus |
    ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

// Cut a UTF-8 string to fit max_w, appending "…".
std::string ellipsize(const std::string& s, ImFont* f, float fs, float max_w)
{
    if (f->CalcTextSizeA(fs, FLT_MAX, 0.0f, s.c_str()).x <= max_w) return s;
    std::string t = s;
    while (!t.empty()) {
        do { t.pop_back(); } while (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0x80);
        if (!t.empty() && (static_cast<unsigned char>(t.back()) & 0xC0) == 0xC0) t.pop_back();
        std::string cand = t + "…";
        if (f->CalcTextSizeA(fs, FLT_MAX, 0.0f, cand.c_str()).x <= max_w) return cand;
    }
    return "…";
}

// ── Popover rows ─────────────────────────────────────────────────────────────

enum class RowTail { None, Chevron, Toggle };

struct RowOpts {
    const char* value   = nullptr;   // right-aligned, dim
    RowTail     tail    = RowTail::None;
    bool        on      = false;     // Toggle state
    int         check   = -1;        // -1 no check column, 0 unchecked, 1 checked
    bool        enabled = true;
    float       label_a = 1.0f;      // label alpha multiplier
};

float row_width()
{
    return std::max(ImGui::GetContentRegionAvail().x, th::kMenuMinW - 12.0f);
}

void draw_toggle(ImDrawList* dl, ImVec2 min, bool on)
{
    const ImVec2 max(min.x + 30.0f, min.y + 18.0f);
    dl->AddRectFilled(min, max, on ? C(th::kAccent) : C(IM_COL32(255, 255, 255, 56)), 9.0f);
    const float kx = on ? max.x - 9.0f : min.x + 9.0f;
    dl->AddCircleFilled(ImVec2(kx, min.y + 9.0f), 7.0f, C(IM_COL32_WHITE), 16);
}

bool menu_row(const char* id, const char* label, const RowOpts& o = {})
{
    const float  w = row_width();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(id, ImVec2(w, th::kMenuRowH)) && o.enabled;
    const bool hov = o.enabled && ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (hov)
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + th::kMenuRowH),
                          C(ImGui::IsItemActive() ? th::kPressBg : th::kHoverBg), 8.0f);
    const float cy = p.y + th::kMenuRowH * 0.5f;
    const float fs = ImGui::GetFontSize();
    float x = p.x + 10.0f;
    if (o.check >= 0) {
        if (o.check == 1) icons::check(dl, ImVec2(x + 8.0f, cy), 16.0f, C(th::kAccent));
        x += 26.0f;
    }
    const ImU32 lc = o.enabled ? C(th::kText, o.label_a) : C(th::kTextFaint);
    dl->AddText(ImVec2(x, std::round(cy - fs * 0.5f - 1.0f)), lc, label);

    float rx = p.x + w - 10.0f;
    if (o.tail == RowTail::Chevron) {
        icons::chevron_right(dl, ImVec2(rx - 6.0f, cy), 14.0f, C(th::kTextDim));
        rx -= 20.0f;
    } else if (o.tail == RowTail::Toggle) {
        draw_toggle(dl, ImVec2(rx - 30.0f, cy - 9.0f), o.on);
        rx -= 40.0f;
    }
    if (o.value && *o.value) {
        const float vw = ImGui::CalcTextSize(o.value).x;
        dl->AddText(ImVec2(rx - vw, std::round(cy - fs * 0.5f - 1.0f)), C(th::kTextDim), o.value);
    }
    return clicked;
}

void menu_header(const char* text)
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(row_width(), 26.0f));
    ImFont* f = ui_fonts().ui_small;
    ImGui::GetWindowDrawList()->AddText(f, 12.0f, ImVec2(p.x + 10.0f, p.y + 9.0f), C(th::kTextDim), text);
}

void menu_separator()
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float  w = row_width();
    ImGui::Dummy(ImVec2(w, 9.0f));
    ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x + 8.0f, p.y + 4.5f), ImVec2(p.x + w - 8.0f, p.y + 4.5f),
                                        C(th::kPanelBorder), 1.0f);
}

// Back-row for sub-pages: "‹  Title".
bool menu_back(const char* id, const char* title)
{
    const float  w = row_width();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(w, th::kMenuRowH));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (ImGui::IsItemHovered())
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + th::kMenuRowH), C(th::kHoverBg), 8.0f);
    const float cy = p.y + th::kMenuRowH * 0.5f;
    icons::chevron_left(dl, ImVec2(p.x + 16.0f, cy), 14.0f, C(th::kText));
    dl->AddText(ImVec2(p.x + 30.0f, std::round(cy - ImGui::GetFontSize() * 0.5f - 1.0f)), C(th::kText), title);
    menu_separator();
    return clicked;
}

// Popover chrome: kPanel, radius 12, 1 px border, rows flush.
bool begin_popover(const char* id, ImVec2 anchor, ImVec2 pivot)
{
    if (!ImGui::IsPopupOpen(id)) return false;
    ImGui::SetNextWindowPos(anchor, ImGuiCond_Always, pivot);
    ImGui::SetNextWindowSizeConstraints(ImVec2(th::kMenuMinW, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(0.0f, 0.0f));
    if (ImGui::BeginPopup(id, ImGuiWindowFlags_NoMove)) return true;
    ImGui::PopStyleVar(2);
    return false;
}

void end_popover()
{
    ImGui::EndPopup();
    ImGui::PopStyleVar(2);
}

std::string audio_track_label(const AudioTrackInfo& t)
{
    std::string s = std::format("#{}", t.stream_idx);
    if (!t.language.empty()) s += " · " + t.language;
    s += std::format(" · {} {}ch", t.codec_name, t.channels);
    if (t.bit_rate > 0) s += std::format(" · {} kbps", t.bit_rate / 1000);
    return s;
}

} // namespace

// ── PlayerUI ──────────────────────────────────────────────────────────────────

PlayerUI::~PlayerUI() { shutdown(); }

float PlayerUI::top_inset_logical() { return th::kTopBarH; }

bool PlayerUI::init(GLFWwindow* window)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    window_ = window;

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;   // overlay layout is fully code-driven

    // ── Theme: dark glass over video, sky accent (tokens in UiTheme.h) ──────
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding    = th::kRadiusPanel;
    s.PopupRounding     = th::kRadiusPanel;
    s.FrameRounding     = 8.0f;
    s.GrabRounding      = 8.0f;
    s.ChildRounding     = th::kRadiusCard;
    s.ScrollbarRounding = 8.0f;
    s.TabRounding       = 6.0f;
    s.WindowPadding     = ImVec2(12.0f, 12.0f);
    s.FramePadding      = ImVec2(10.0f, 6.0f);
    s.ItemSpacing       = ImVec2(8.0f, 6.0f);
    s.ItemInnerSpacing  = ImVec2(8.0f, 6.0f);
    s.PopupBorderSize   = 1.0f;
    s.WindowBorderSize  = 1.0f;
    s.FrameBorderSize   = 0.0f;
    s.ScrollbarSize     = 10.0f;
    s.GrabMinSize       = 10.0f;

    const ImVec4 accent = th::vec4(th::kAccent);
    auto with_a = [](ImVec4 v, float a) { v.w = a; return v; };
    s.Colors[ImGuiCol_WindowBg]          = th::vec4(th::kPanel);
    s.Colors[ImGuiCol_ChildBg]           = ImVec4(0, 0, 0, 0);
    s.Colors[ImGuiCol_PopupBg]           = th::vec4(th::kPanel);
    s.Colors[ImGuiCol_MenuBarBg]         = ImVec4(0, 0, 0, 0);
    s.Colors[ImGuiCol_Border]            = th::vec4(th::kPanelBorder);
    s.Colors[ImGuiCol_BorderShadow]      = ImVec4(0, 0, 0, 0);
    s.Colors[ImGuiCol_Text]              = th::vec4(th::kText);
    s.Colors[ImGuiCol_TextDisabled]      = th::vec4(th::kTextFaint);
    s.Colors[ImGuiCol_Button]            = th::vec4(IM_COL32(255, 255, 255, 20));
    s.Colors[ImGuiCol_ButtonHovered]     = th::vec4(th::kHoverBg);
    s.Colors[ImGuiCol_ButtonActive]      = th::vec4(th::kPressBg);
    s.Colors[ImGuiCol_Header]            = th::vec4(IM_COL32(255, 255, 255, 18));
    s.Colors[ImGuiCol_HeaderHovered]     = th::vec4(th::kHoverBg);
    s.Colors[ImGuiCol_HeaderActive]      = th::vec4(th::kPressBg);
    s.Colors[ImGuiCol_CheckMark]         = accent;
    s.Colors[ImGuiCol_SliderGrab]        = accent;
    s.Colors[ImGuiCol_SliderGrabActive]  = th::vec4(th::kAccentHover);
    s.Colors[ImGuiCol_NavHighlight]      = accent;
    s.Colors[ImGuiCol_FrameBg]           = th::vec4(IM_COL32(255, 255, 255, 15));
    s.Colors[ImGuiCol_FrameBgHovered]    = th::vec4(IM_COL32(255, 255, 255, 26));
    s.Colors[ImGuiCol_FrameBgActive]     = th::vec4(IM_COL32(255, 255, 255, 36));
    s.Colors[ImGuiCol_TitleBg]           = th::vec4(th::kPanel);
    s.Colors[ImGuiCol_TitleBgActive]     = th::vec4(th::kPanel);
    s.Colors[ImGuiCol_TitleBgCollapsed]  = th::vec4(th::kPanel);
    s.Colors[ImGuiCol_ScrollbarBg]       = ImVec4(0, 0, 0, 0);
    s.Colors[ImGuiCol_ScrollbarGrab]     = th::vec4(IM_COL32(255, 255, 255, 40));
    s.Colors[ImGuiCol_ScrollbarGrabHovered] = th::vec4(IM_COL32(255, 255, 255, 70));
    s.Colors[ImGuiCol_ScrollbarGrabActive]  = with_a(accent, 0.7f);
    s.Colors[ImGuiCol_Separator]         = th::vec4(th::kPanelBorder);
    s.Colors[ImGuiCol_SeparatorHovered]  = with_a(accent, 0.6f);
    s.Colors[ImGuiCol_SeparatorActive]   = accent;
    s.Colors[ImGuiCol_Tab]               = th::vec4(IM_COL32(255, 255, 255, 10));
    s.Colors[ImGuiCol_TabHovered]        = th::vec4(th::kHoverBg);
    s.Colors[ImGuiCol_TabActive]         = th::vec4(th::kPressBg);
    s.Colors[ImGuiCol_TableHeaderBg]     = th::vec4(IM_COL32(255, 255, 255, 12));
    s.Colors[ImGuiCol_TableBorderStrong] = th::vec4(th::kPanelBorder);
    s.Colors[ImGuiCol_TableBorderLight]  = th::vec4(IM_COL32(255, 255, 255, 12));
    s.Colors[ImGuiCol_TableRowBg]        = ImVec4(0, 0, 0, 0);
    s.Colors[ImGuiCol_TableRowBgAlt]     = th::vec4(IM_COL32(255, 255, 255, 6));
    s.Colors[ImGuiCol_PlotLines]         = accent;

    if (!ImGui_ImplGlfw_InitForOpenGL(window, /*install_callbacks=*/true)) return false;
    if (!ImGui_ImplOpenGL3_Init("#version 410 core")) return false;

    // System fonts at the window's current backing scale.
    {
        int ww = 0, fw = 0, h = 0;
        glfwGetWindowSize(window, &ww, &h);
        glfwGetFramebufferSize(window, &fw, &h);
        const float scale = (ww > 0) ? static_cast<float>(fw) / static_cast<float>(ww) : 1.0f;
        if (!ui_fonts_build(scale)) return false;
    }

    last_activity_ = glfwGetTime();
    inited_ = true;
    return true;
}

void PlayerUI::shutdown()
{
    if (!inited_) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    inited_ = false;
}

void PlayerUI::begin_frame()
{
    if (window_) {
        int ww = 0, fw = 0, h = 0;
        glfwGetWindowSize(window_, &ww, &h);
        glfwGetFramebufferSize(window_, &fw, &h);
        if (ww > 0) ui_fonts_rebuild_if_needed(static_cast<float>(fw) / static_cast<float>(ww));
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGui::PushFont(ui_fonts().ui);
}

void PlayerUI::notify_activity()
{
    last_activity_ = glfwGetTime();
}

float PlayerUI::visible_bar_height_logical() const
{
    const float stack = th::kControlStackH * stack_vis_;
    return showing_mini_ ? std::max(stack, 2.0f) : stack;
}

void PlayerUI::toast(std::string text)
{
    toast_text_ = std::move(text);
    toast_t0_   = glfwGetTime();
}

// ── Frame build ───────────────────────────────────────────────────────────────

PlayerUIState PlayerUI::build(const PlayerUIInputs& in)
{
    PlayerUIState out{};
    ImGuiIO& io = ImGui::GetIO();
    const float  win_w = io.DisplaySize.x;
    const float  win_h = io.DisplaySize.y;
    const float  dt    = std::clamp(io.DeltaTime, 0.0f, 0.1f);
    const double now   = glfwGetTime();

    // Centre flash on every play/pause transition (key, button, QA paths alike).
    const int paused_i = in.paused ? 1 : 0;
    if (prev_paused_ >= 0 && paused_i != prev_paused_) {
        flash_t0_      = now;
        flash_playing_ = !in.paused;
    }
    prev_paused_ = paused_i;

    // ── Visibility ────────────────────────────────────────────────────────────
    const bool mouse_ok = ImGui::IsMousePosValid() && io.MousePos.x >= 0.0f && io.MousePos.y >= 0.0f
                       && io.MousePos.x < win_w && io.MousePos.y < win_h;
    const bool cursor_in_chrome = mouse_ok
        && (io.MousePos.y >= win_h - th::kControlStackH || io.MousePos.y < th::kTopBarH);
    const bool any_popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
    const bool want_visible = in.force_visible || !pending_menu_.empty()
        || (now - last_activity_ < th::kHideDelay) || in.paused || any_popup
        || seek_.active || vol_dragging_ || (cursor_in_chrome && now - last_activity_ < 30.0);
    ui_alpha_  = in.force_hidden ? 0.0f
               : in.force_visible ? 1.0f : th::ease(ui_alpha_, want_visible ? 1.0f : 0.0f, dt, th::kFade);
    stack_vis_ = ui_alpha_;

    if (ui_alpha_ < 0.01f && mouse_ok && !in.force_visible && !in.show_help)
        ImGui::SetMouseCursor(ImGuiMouseCursor_None);

    // Double-click on the video toggles fullscreen.
    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !io.WantCaptureMouse)
        out.toggle_fullscreen = true;

    // ── Scrims (behind windows, above video + GL HUD) ────────────────────────
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    if (ui_alpha_ > 0.005f) {
        const int ab = static_cast<int>(th::kScrimBottomA * ui_alpha_ * 255.0f);
        const int at = static_cast<int>(th::kScrimTopA    * ui_alpha_ * 255.0f);
        bg->AddRectFilledMultiColor(ImVec2(0, win_h - th::kBottomScrimH), ImVec2(win_w, win_h),
            IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, ab), IM_COL32(0, 0, 0, ab));
        bg->AddRectFilledMultiColor(ImVec2(0, 0), ImVec2(win_w, th::kTopScrimH),
            IM_COL32(0, 0, 0, at), IM_COL32(0, 0, 0, at), IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0));
    }

    // Mini progress line when the chrome is hidden.
    showing_mini_ = mini_progress_ && ui_alpha_ < 0.01f && in.duration > 0.0;
    if (showing_mini_) {
        const float x = static_cast<float>(std::clamp(in.cur_pts / in.duration, 0.0, 1.0)) * win_w;
        bg->AddRectFilled(ImVec2(0, win_h - 2.0f), ImVec2(win_w, win_h), IM_COL32(255, 255, 255, 38));
        bg->AddRectFilled(ImVec2(0, win_h - 2.0f), ImVec2(x, win_h), th::alpha(th::kAccent, 0.85f));
    }

    // Subtitles: above the scrims, below windows / popovers.
    draw_subtitle(in, win_w, win_h, dt);

    // ── Chrome windows ────────────────────────────────────────────────────────
    if (ui_alpha_ > 0.005f) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ui_alpha_);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
        build_top_bar(in, out, win_w);
        build_controls(in, out, win_w, win_h, dt);
        ImGui::PopStyleVar(4);
    } else {
        seek_.active = false;   // window gone: no drag can be in progress
    }

    // Help card (independent of the chrome fade).
    if (in.show_help || pending_menu_ == "help") {
        if (!in.show_help) out.toggle_help = true;
        build_help_card(out, win_w, win_h);
    }
    if (pending_menu_ == "help") pending_menu_.clear();

    // ── Foreground: centre flash + toast ──────────────────────────────────────
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    if (flash_t0_ >= 0.0) {
        const float t = static_cast<float>((now - flash_t0_) / th::kFlashDur);
        if (t < 1.0f) ui::center_flash(fg, ImVec2(win_w * 0.5f, win_h * 0.5f), t, flash_playing_);
        else flash_t0_ = -1.0;
    }
    if (toast_t0_ >= 0.0) {
        const float age = static_cast<float>(now - toast_t0_);
        float a = 1.0f;
        if (age > th::kToastHold) a = 1.0f - (age - th::kToastHold) / th::kToastFade;
        if (a <= 0.0f) toast_t0_ = -1.0;
        else ui::osd_toast(fg, win_w, toast_text_, ui_fonts().mono, a);
    }
    return out;
}

// ── Top bar: menu, title, info chips, help ────────────────────────────────────

void PlayerUI::build_top_bar(const PlayerUIInputs& in, PlayerUIState& out, float win_w)
{
    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(win_w, th::kTopBarH), ImGuiCond_Always);
    if (!ImGui::Begin("##top", nullptr, kOverlayFlags)) { ImGui::End(); return; }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float by = (th::kTopBarH - th::kBtn) * 0.5f;

    // ≡ menu
    ImGui::SetCursorScreenPos(ImVec2(8.0f, by));
    auto mb = ui::icon_button("##menu", "Menu");
    icons::menu(dl, mb.c, th::kIconSz, mb.col);
    if (mb.clicked || pending_menu_ == "menu") {
        ImGui::OpenPopup("##mainmenu");
        if (pending_menu_ == "menu") pending_menu_.clear();
    }
    const ImVec2 menu_anchor(8.0f, by + th::kBtn + 6.0f);

    // Help button (right edge)
    const float help_x = win_w - 8.0f - th::kBtn;
    ImGui::SetCursorScreenPos(ImVec2(help_x, by));
    auto hb = ui::icon_button("##help", "Keyboard shortcuts (H / ?)");
    icons::help(dl, hb.c, th::kIconSz, hb.col, ui_fonts().ui_small);
    if (hb.clicked) out.toggle_help = true;

    // Info chips, right-aligned before the help button.
    struct Chip { std::string text; ImU32 col; };
    std::vector<Chip> chips;
    const auto& inf = in.info;
    if (!inf.codec.empty())  chips.push_back({ inf.codec,  th::kText });
    if (!inf.res.empty())    chips.push_back({ inf.res,    th::kText });
    if (!inf.fps.empty())    chips.push_back({ inf.fps,    th::kText });
    if (!inf.range.empty())  chips.push_back({ inf.range,  inf.hdr ? th::kWarn : th::kText });
    if (!inf.decode.empty()) chips.push_back({ inf.decode, th::kText });
    chips.push_back({ std::format("drop {}", inf.dropped), inf.dropped > 0 ? th::kError : th::kText });

    ImFont* mono = ui_fonts().mono;
    const float cfs = 12.0f, gap = 6.0f;
    const float title_x  = 8.0f + th::kBtn + 6.0f;
    const float title_max = std::max(60.0f, win_w * 0.40f);
    ImFont* uif = ui_fonts().ui;
    const std::string title = ellipsize(inf.title, uif, 14.0f, title_max);
    const float title_w = uif->CalcTextSizeA(14.0f, FLT_MAX, 0.0f, title.c_str()).x;

    float chips_w = 0.0f;
    for (const auto& c : chips) chips_w += ui::chip_size(c.text.c_str(), mono, cfs).x + gap;
    const float chips_room = help_x - 8.0f - (title_x + title_w + 24.0f);
    std::string dropped_tip;
    std::size_t first = 0;
    while (first < chips.size() && chips_w > chips_room) {
        chips_w -= ui::chip_size(chips[first].text.c_str(), mono, cfs).x + gap;
        dropped_tip += (dropped_tip.empty() ? "" : "  ·  ") + chips[first].text;
        ++first;
    }
    float cx = help_x - 6.0f - chips_w + gap;
    const float alpha = ImGui::GetStyle().Alpha;
    for (std::size_t i = first; i < chips.size(); ++i) {
        const ImVec2 cs = ui::chip_size(chips[i].text.c_str(), mono, cfs);
        const ImVec2 cp(std::round(cx), std::round((th::kTopBarH - cs.y) * 0.5f));
        ui::chip(dl, cp, chips[i].text.c_str(), mono, cfs, chips[i].col, 0, alpha);
        if (i == first && !dropped_tip.empty()) {
            ImGui::SetCursorScreenPos(cp);
            ImGui::InvisibleButton("##chiptip", cs);
            ui::tooltip(dropped_tip.c_str());
        }
        cx += cs.x + gap;
    }

    // Title (full path in tooltip)
    if (!title.empty()) {
        const float ty = std::round((th::kTopBarH - 14.0f) * 0.5f - 2.0f);
        dl->AddText(uif, 14.0f, ImVec2(title_x, ty), C(th::kText), title.c_str());
        ImGui::SetCursorScreenPos(ImVec2(title_x, ty));
        ImGui::InvisibleButton("##title", ImVec2(std::max(1.0f, title_w), 18.0f));
        ui::tooltip(inf.title_full.c_str());
    }

    if (begin_popover("##mainmenu", menu_anchor, ImVec2(0, 0))) {
        build_menu_popup(in, out);
        end_popover();
    }
    ImGui::End();
}

void PlayerUI::build_menu_popup(const PlayerUIInputs& in, PlayerUIState& out)
{
    // The classic menu tree, kept intact so every entry point survives.
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 8.0f));
    ImGui::Dummy(ImVec2(th::kMenuMinW - 12.0f, 0.0f));
    if (ImGui::BeginMenu("Media")) {
        if (ImGui::MenuItem("Quit", "Q / Esc")) out.quit = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Playback")) {
        if (ImGui::MenuItem(in.paused ? "Play" : "Pause", "Space")) out.play_pause_clicked = true;
        ImGui::Separator();
        const double t = in.cur_pts;
        if (ImGui::MenuItem("Jump Backward 10s", "Left"))  out.seek_to = std::max(0.0, t - 10.0);
        if (ImGui::MenuItem("Jump Forward 10s",  "Right")) out.seek_to = t + 10.0;
        if (ImGui::MenuItem("Jump Backward 60s", "Down"))  out.seek_to = std::max(0.0, t - 60.0);
        if (ImGui::MenuItem("Jump Forward 60s",  "Up"))    out.seek_to = t + 60.0;
        ImGui::Separator();
        if (ImGui::MenuItem("Prev Keyframe", ","))         out.step_key   = -1;
        if (ImGui::MenuItem("Next Keyframe", "."))         out.step_key   = +1;
        if (ImGui::MenuItem("Back 1 Frame",  "Shift+,"))   out.step_frame = -1;
        if (ImGui::MenuItem("Fwd 1 Frame",   "Shift+."))   out.step_frame = +1;
        ImGui::Separator();
        if (ImGui::BeginMenu("Speed")) {
            for (double sp : kSpeedPresets)
                if (ImGui::MenuItem(speed_label(sp).c_str(), nullptr, sp == in.speed)) out.speed_set = sp;
            ImGui::EndMenu();
        }
        if (ImGui::MenuItem("Mute", "M", in.has_audio && in.volume <= 0.0f, in.has_audio))
            out.mute_toggle = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
        bool v;
        v = in.show_hud;       if (ImGui::MenuItem("Debug HUD",          "D", &v)) out.toggle_hud       = true;
        v = in.show_inspector; if (ImGui::MenuItem("Stream Inspector",   "I", &v)) out.toggle_inspector = true;
        v = in.show_waveform;  if (ImGui::MenuItem("Waveform",           "W", &v)) out.toggle_waveform  = true;
        v = in.show_network;   if (ImGui::MenuItem("Network Log",        "N", &v)) out.toggle_network   = true;
        v = in.show_drift;     if (ImGui::MenuItem("A/V Drift Graph",    "G", &v)) out.toggle_drift     = true;
        v = in.show_tracks;    if (ImGui::MenuItem("Track Switcher",     "T", &v)) out.toggle_tracks    = true;
        v = in.show_vmaf;      if (ImGui::MenuItem("VMAF Panel",         "V", &v)) out.toggle_vmaf      = true;
        v = in.show_filmstrip; if (ImGui::MenuItem("Keyframe Filmstrip", "K", &v)) out.toggle_filmstrip = true;
        ImGui::Separator();
        v = in.fullscreen;     if (ImGui::MenuItem("Fullscreen",         "F", &v)) out.toggle_fullscreen = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Analyze")) {
        if (ImGui::MenuItem("VMAF — Single File...", nullptr, false, true))
            out.vmaf_pick_ref = true;
        if (ImGui::MenuItem("VMAF — Manifest Variants", nullptr, false, in.has_manifest_variants))
            out.vmaf_analyze_manifest = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
        bool v = in.show_help;
        if (ImGui::MenuItem("Key Controls", "H", &v)) out.toggle_help = true;
        ImGui::EndMenu();
    }
    ImGui::PopStyleVar(2);
}

// ── Bottom stack: seek bar + pills ────────────────────────────────────────────

void PlayerUI::build_controls(const PlayerUIInputs& in, PlayerUIState& out,
                              float win_w, float win_h, float dt)
{
    const float top = win_h - th::kControlStackH;
    ImGui::SetNextWindowPos(ImVec2(0, top), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(win_w, th::kControlStackH), ImGuiCond_Always);
    if (!ImGui::Begin("##controls", nullptr, kOverlayFlags)) { ImGui::End(); return; }

    ImDrawList* dl   = ImGui::GetWindowDrawList();
    ImGuiIO&    io   = ImGui::GetIO();
    ImFont*     mono = ui_fonts().mono;
    const float alpha = ImGui::GetStyle().Alpha;

    // ── Seek bar ──────────────────────────────────────────────────────────────
    ui::SeekBarIn sbi;
    sbi.dur              = in.duration;
    sbi.cur              = in.cur_pts;
    sbi.buffered_end     = in.buffered_end_pts;
    sbi.kf_pts           = in.kf_pts;
    sbi.debug_hover_frac = in.debug_hover_frac;
    const ui::SeekBarOut sb = ui::seek_bar("##seek", ImVec2(th::kGutter, top),
                                           win_w - 2.0f * th::kGutter, sbi, seek_, dt);
    out.seek_bar_dragging = sb.dragging;
    out.seek_bar_frac     = sb.drag_frac;
    out.seek_cancelled    = sb.cancelled;
    if (sb.seek_to >= 0.0) out.seek_to = sb.seek_to;
    if (sb.dragging || sb.seek_to >= 0.0) notify_activity();

    // ── Left pill: transport, volume, timecode ────────────────────────────────
    const float pill_y = win_h - th::kBottomMargin - th::kPillH;
    const float btn_y  = pill_y + (th::kPillH - th::kBtn) * 0.5f;
    const bool  show_kf    = win_w >= 520.0f;
    const bool  show_frame = win_w >= 640.0f;

    double fps = in.fps;
    if (tc_mode_ == 1 && !(fps > 0.0)) tc_mode_ = 2;
    const std::string tc = ui::format_timecode(in.cur_pts, in.duration, tc_mode_, fps);
    const float tc_w = ui::timecode_width(tc, mono);
    const float vol_w = 80.0f * vol_expand_;

    float lw = th::kPillPadX + th::kBtn;
    if (show_kf)    lw += 2.0f + th::kBtn + 2.0f + th::kBtn;
    if (show_frame) lw += 2.0f + th::kBtn + 2.0f + th::kBtn;
    lw += 8.0f + th::kBtn + (vol_w > 1.0f ? vol_w + 4.0f : 0.0f) + 8.0f + tc_w + 14.0f;
    ui::glass_pill(dl, ImVec2(th::kGutter, pill_y), ImVec2(th::kGutter + lw, pill_y + th::kPillH), alpha);

    float x = th::kGutter + th::kPillPadX;
    auto place = [&](float adv) { ImGui::SetCursorScreenPos(ImVec2(x, btn_y)); x += adv; };

    place(th::kBtn);
    auto pb = ui::icon_button("##play", in.paused ? "Play (Space)" : "Pause (Space)");
    if (in.paused) icons::play(dl, pb.c, th::kIconSz, pb.col);
    else           icons::pause(dl, pb.c, th::kIconSz, pb.col);
    if (pb.clicked) out.play_pause_clicked = true;

    if (show_kf) {
        x += 2.0f; place(th::kBtn);
        auto b1 = ui::icon_button("##kfprev", "Prev keyframe (,)");
        icons::key_back(dl, b1.c, th::kIconSz, b1.col);
        if (b1.clicked) out.step_key = -1;
        x += 2.0f; place(th::kBtn);
        auto b2 = ui::icon_button("##kfnext", "Next keyframe (.)");
        icons::key_fwd(dl, b2.c, th::kIconSz, b2.col);
        if (b2.clicked) out.step_key = +1;
    }
    if (show_frame) {
        x += 2.0f; place(th::kBtn);
        auto b1 = ui::icon_button("##frprev", "Back 1 frame (⇧,)");
        icons::step_frame_back(dl, b1.c, th::kIconSz, b1.col);
        if (b1.clicked) out.step_frame = -1;
        x += 2.0f; place(th::kBtn);
        auto b2 = ui::icon_button("##frnext", "Fwd 1 frame (⇧.)");
        icons::step_frame_fwd(dl, b2.c, th::kIconSz, b2.col);
        if (b2.clicked) out.step_frame = +1;
    }

    // Volume: icon toggles mute; hover reveals an 80 px slider; wheel ±5 %.
    x += 8.0f; place(th::kBtn);
    const float vol_x0 = x - th::kBtn;
    const float vol = in.has_audio ? in.volume : 0.0f;
    auto vb = ui::icon_button("##vol", in.has_audio ? "Mute (M)" : "No audio", in.has_audio);
    icons::volume(dl, vb.c, th::kIconSz, vb.col, vol <= 0.0f ? 0 : (vol < 0.5f ? 1 : 2));
    if (vb.clicked) out.mute_toggle = true;
    bool vol_hover = vb.hovered;
    if (in.has_audio && vol_w > 1.0f) {
        x += 2.0f;
        ImGui::SetCursorScreenPos(ImVec2(x, btn_y));
        ImGui::InvisibleButton("##volslider", ImVec2(vol_w + 2.0f, th::kBtn));
        const bool s_hov = ImGui::IsItemHovered(), s_act = ImGui::IsItemActive();
        vol_hover = vol_hover || s_hov || s_act;
        vol_dragging_ = s_act;
        if (s_act) {
            const float v = std::clamp((io.MousePos.x - x) / 80.0f, 0.0f, 1.0f);
            if (std::abs(v - vol) > 1e-4f) out.volume_set = v;
        }
        const float cy = btn_y + th::kBtn * 0.5f;
        const float tx1 = x + vol_w;
        dl->PushClipRect(ImVec2(x - 1.0f, btn_y), ImVec2(tx1 + 8.0f, btn_y + th::kBtn), true);
        dl->AddRectFilled(ImVec2(x, cy - 2.0f), ImVec2(x + 80.0f, cy + 2.0f), C(th::kTrackRest), 2.0f);
        const float fx = x + 80.0f * (out.volume_set >= 0.0f ? out.volume_set : vol);
        dl->AddRectFilled(ImVec2(x, cy - 2.0f), ImVec2(fx, cy + 2.0f), C(th::kAccent), 2.0f);
        if (s_hov || s_act) dl->AddCircleFilled(ImVec2(fx, cy), 6.0f, C(IM_COL32_WHITE), 16);
        dl->PopClipRect();
        x += vol_w + 2.0f;
    } else {
        vol_dragging_ = false;
    }
    if (in.has_audio) {
        const bool over_vol = ImGui::IsMouseHoveringRect(ImVec2(vol_x0, btn_y),
                                                         ImVec2(x, btn_y + th::kBtn));
        if ((vol_hover || over_vol) && io.MouseWheel != 0.0f)
            out.volume_set = std::clamp(vol + (io.MouseWheel > 0.0f ? 0.05f : -0.05f), 0.0f, 1.0f);
        vol_expand_ = th::ease(vol_expand_, (vol_hover || over_vol || vol_dragging_) ? 1.0f : 0.0f, ImGui::GetIO().DeltaTime, 0.12f);
    } else {
        vol_expand_ = 0.0f;
    }

    // Timecode (click cycles display mode)
    x += 8.0f;
    ImGui::SetCursorScreenPos(ImVec2(x, btn_y));
    if (ui::timecode("##tc", tc, mono)) {
        tc_mode_ = (tc_mode_ + 1) % 3;
        if (tc_mode_ == 1 && !(fps > 0.0)) tc_mode_ = 2;
    }
    ui::tooltip(in.duration > 0.0 ? "Click: time / frames / remaining" : nullptr);

    // ── Right pill: speed chip, CC, settings, overlays, fullscreen ────────────
    const bool  show_speed = in.speed != 1.0;
    const std::string spd  = speed_label(in.speed);
    const ImVec2 spd_sz    = ui::chip_size(spd.c_str(), mono, 13.0f);
    float rw = th::kPillPadX * 2.0f + 4.0f * th::kBtn + 3.0f * 2.0f;
    if (show_speed) rw += spd_sz.x + 8.0f;
    const float rx0 = win_w - th::kGutter - rw;
    ui::glass_pill(dl, ImVec2(rx0, pill_y), ImVec2(win_w - th::kGutter, pill_y + th::kPillH), alpha);
    x = rx0 + th::kPillPadX;

    if (show_speed) {
        const ImVec2 cp(x + 2.0f, std::round(pill_y + (th::kPillH - spd_sz.y) * 0.5f));
        ImGui::SetCursorScreenPos(cp);
        if (ImGui::InvisibleButton("##speedchip", spd_sz)) out.speed_set = 1.0;
        const bool h = ImGui::IsItemHovered();
        ui::chip(dl, cp, spd.c_str(), mono, 13.0f, th::kText, h ? th::kAccentHover : th::kAccent, alpha);
        ui::tooltip("Speed ([ / ]) — click to reset");
        x += spd_sz.x + 8.0f;
    }

    const bool has_subs = in.sub_choices && !in.sub_choices->empty();
    const bool sub_on   = has_subs && in.sub_active >= 0;
    place(th::kBtn);
    auto ccb = ui::icon_button("##cc", "Subtitles (C)");
    icons::cc(dl, ccb.c, th::kIconSz, has_subs ? ccb.col : C(th::kTextFaint), sub_on, ui_fonts().ui_small);
    const float cc_right = x;
    if (ccb.clicked || pending_menu_ == "cc") {
        cc_style_page_ = false;
        ImGui::OpenPopup("##ccpop");
        if (pending_menu_ == "cc") pending_menu_.clear();
    }

    x += 2.0f; place(th::kBtn);
    auto gb = ui::icon_button("##gear", "Settings");
    icons::gear(dl, gb.c, th::kIconSz, gb.col);
    const float gear_right = x;
    if (gb.clicked || pending_menu_ == "settings") {
        settings_page_ = SettingsPage::Root;
        ImGui::OpenPopup("##settings");
        if (pending_menu_ == "settings") pending_menu_.clear();
    }

    const bool any_overlay = in.show_hud || in.show_inspector || in.show_drift || in.show_tracks
                          || in.show_waveform || in.show_network || in.show_vmaf || in.show_filmstrip;
    x += 2.0f; place(th::kBtn);
    auto ob = ui::icon_button("##overlays", "Engineer overlays");
    icons::hud(dl, ob.c, th::kIconSz, ob.col, any_overlay);
    const float ov_right = x;
    if (ob.clicked || pending_menu_ == "overlays") {
        ImGui::OpenPopup("##overlays");
        if (pending_menu_ == "overlays") pending_menu_.clear();
    }

    x += 2.0f; place(th::kBtn);
    auto fb = ui::icon_button("##fs", in.fullscreen ? "Exit fullscreen (F)" : "Fullscreen (F)");
    if (in.fullscreen) icons::exit_fullscreen(dl, fb.c, th::kIconSz, fb.col);
    else               icons::fullscreen(dl, fb.c, th::kIconSz, fb.col);
    if (fb.clicked) out.toggle_fullscreen = true;

    // ── Hover preview card (drawn over everything in this window) ─────────────
    if (sb.hovered && in.duration > 0.0) draw_hover_card(in, sb, win_w);

    // ── Popovers (anchored above their buttons) ───────────────────────────────
    const float pop_y = pill_y - 8.0f;
    if (begin_popover("##ccpop", ImVec2(cc_right, pop_y), ImVec2(1, 1))) {
        if (cc_style_page_) {
            if (menu_back("##ccback", "Subtitle style")) cc_style_page_ = false;
            build_sub_style_rows(out);
        } else {
            build_sub_rows(in, out);
            menu_separator();
            if (menu_row("##ccstyle", "Style", { .tail = RowTail::Chevron })) cc_style_page_ = true;
        }
        end_popover();
    }
    if (begin_popover("##settings", ImVec2(gear_right, pop_y), ImVec2(1, 1))) {
        build_settings_popup(in, out);
        end_popover();
    }
    if (begin_popover("##overlays", ImVec2(ov_right, pop_y), ImVec2(1, 1))) {
        build_overlays_popup(in, out);
        end_popover();
    }

    ImGui::End();
}

// ── Popover contents ──────────────────────────────────────────────────────────

void PlayerUI::build_sub_rows(const PlayerUIInputs& in, PlayerUIState& out)
{
    menu_header("SUBTITLES");
    const int n = in.sub_choices ? static_cast<int>(in.sub_choices->size()) : 0;
    if (n > 0) {
        if (menu_row("##suboff", "Off", { .check = in.sub_active < 0 ? 1 : 0 })) {
            out.sub_choice_set = -1;
            ImGui::CloseCurrentPopup();
        }
        for (int i = 0; i < n; ++i) {
            const SubChoice& c = (*in.sub_choices)[static_cast<std::size_t>(i)];
            const char* badge = c.kind == SubChoice::Sidecar ? "sidecar"
                              : c.is_forced ? "forced" : c.is_hi ? "SDH"
                              : c.is_default ? "default" : nullptr;
            const std::string id  = std::format("##sub{}", i);
            const std::string lbl = c.label();
            const bool active = i == in.sub_active;
            if (menu_row(id.c_str(), lbl.c_str(),
                         { .value = badge, .check = active ? 1 : 0, .label_a = active ? 1.0f : 0.85f })) {
                out.sub_choice_set = i;
                ImGui::CloseCurrentPopup();
            }
        }
    } else {
        menu_row("##subnone", "No subtitle tracks", { .enabled = false });
    }
    if (menu_row("##subopen", "Open subtitle file…")) {
        out.sub_open_file = true;
        ImGui::CloseCurrentPopup();
    }
}

void PlayerUI::build_sub_style_rows(PlayerUIState& out)
{
    menu_header("SIZE");
    static constexpr int kSizes[] = { 50, 75, 100, 125, 150, 200 };
    for (int s : kSizes) {
        const std::string id = std::format("##sz{}", s), lbl = std::format("{}%", s);
        if (menu_row(id.c_str(), lbl.c_str(), { .check = sub_style_.size_pct == s ? 1 : 0 })) {
            sub_style_.size_pct = s; out.sub_style_changed = true;
        }
    }
    menu_header("EDGE");
    static constexpr const char* kEdges[] = { "Box", "Outline", "Drop shadow", "None" };
    for (int e = 0; e < 4; ++e) {
        const std::string id = std::format("##edge{}", e);
        if (menu_row(id.c_str(), kEdges[e], { .check = static_cast<int>(sub_style_.edge) == e ? 1 : 0 })) {
            sub_style_.edge = static_cast<SubStyle::Edge>(e); out.sub_style_changed = true;
        }
    }
    if (sub_style_.edge == SubStyle::Box) {
        menu_header("BOX OPACITY");
        static constexpr int kOps[] = { 0, 25, 50, 70, 100 };
        for (int o : kOps) {
            const std::string id = std::format("##op{}", o), lbl = std::format("{}%", o);
            if (menu_row(id.c_str(), lbl.c_str(), { .check = sub_style_.box_opacity == o ? 1 : 0 })) {
                sub_style_.box_opacity = o; out.sub_style_changed = true;
            }
        }
    }
}

void PlayerUI::build_settings_popup(const PlayerUIInputs& in, PlayerUIState& out)
{
    switch (settings_page_) {
    case SettingsPage::Root: {
        menu_header("PLAYBACK");
        const std::string spd = speed_label(in.speed);
        if (menu_row("##s_speed", "Speed", { .value = spd.c_str(), .tail = RowTail::Chevron }))
            settings_page_ = SettingsPage::Speed;

        std::string aud = "none";
        if (in.audio_tracks)
            for (const auto& t : *in.audio_tracks)
                if (t.stream_idx == in.audio_idx)
                    aud = std::format("{}{}{}", t.stream_idx, t.language.empty() ? "" : " · ", t.language);
        if (menu_row("##s_audio", "Audio track",
                     { .value = aud.c_str(), .tail = RowTail::Chevron,
                       .enabled = in.audio_tracks && !in.audio_tracks->empty() }))
            settings_page_ = SettingsPage::Audio;

        std::string sub = "Off";
        if (in.sub_choices && in.sub_active >= 0 && in.sub_active < static_cast<int>(in.sub_choices->size()))
            sub = (*in.sub_choices)[static_cast<std::size_t>(in.sub_active)].label();
        sub = ellipsize(sub, ImGui::GetFont(), ImGui::GetFontSize(), 130.0f);
        if (menu_row("##s_subs", "Subtitles", { .value = sub.c_str(), .tail = RowTail::Chevron }))
            settings_page_ = SettingsPage::Subtitles;
        if (menu_row("##s_style", "Subtitle style", { .tail = RowTail::Chevron }))
            settings_page_ = SettingsPage::Style;

        menu_header("VIDEO");
        menu_row("##s_dec", "Decoder", { .value = in.info.decode.empty() ? "?" : in.info.decode.c_str(),
                                         .label_a = 0.85f });
        if (menu_row("##s_film", "Keyframe filmstrip (K)", { .tail = RowTail::Toggle, .on = in.show_filmstrip }))
            out.toggle_filmstrip = true;
        if (menu_row("##s_mini", "Mini progress line", { .tail = RowTail::Toggle, .on = mini_progress_ }))
            mini_progress_ = !mini_progress_;
        if (menu_row("##s_info", "Video info…", { .value = "I + D" })) {
            if (!in.show_inspector) out.toggle_inspector = true;
            if (!in.show_hud)       out.toggle_hud       = true;
            ImGui::CloseCurrentPopup();
        }
        if (!(in.fps > 0.0) || true) {
            menu_header("NAVIGATE");
            if (menu_row("##s_kfp", "Prev keyframe", { .value = "," }))  out.step_key   = -1;
            if (menu_row("##s_kfn", "Next keyframe", { .value = "." }))  out.step_key   = +1;
            if (menu_row("##s_frp", "Back 1 frame",  { .value = "⇧," })) out.step_frame = -1;
            if (menu_row("##s_frn", "Fwd 1 frame",   { .value = "⇧." })) out.step_frame = +1;
        }
        break;
    }
    case SettingsPage::Speed:
        if (menu_back("##s_back", "Speed")) settings_page_ = SettingsPage::Root;
        for (double sp : kSpeedPresets) {
            const std::string id = std::format("##spd{}", sp), lbl = speed_label(sp);
            if (menu_row(id.c_str(), lbl.c_str(), { .check = sp == in.speed ? 1 : 0 }))
                out.speed_set = sp;
        }
        break;
    case SettingsPage::Audio:
        if (menu_back("##s_back", "Audio track")) settings_page_ = SettingsPage::Root;
        if (in.audio_tracks)
            for (const auto& t : *in.audio_tracks) {
                const std::string id = std::format("##aud{}", t.stream_idx), lbl = audio_track_label(t);
                if (menu_row(id.c_str(), lbl.c_str(), { .check = t.stream_idx == in.audio_idx ? 1 : 0 }))
                    out.audio_track_set = t.stream_idx;
            }
        break;
    case SettingsPage::Subtitles:
        if (menu_back("##s_back", "Subtitles")) settings_page_ = SettingsPage::Root;
        build_sub_rows(in, out);
        break;
    case SettingsPage::Style:
        if (menu_back("##s_back", "Subtitle style")) settings_page_ = SettingsPage::Root;
        build_sub_style_rows(out);
        break;
    }
}

void PlayerUI::build_overlays_popup(const PlayerUIInputs& in, PlayerUIState& out)
{
    menu_header("ENGINEER OVERLAYS");
    struct Item { const char* id; const char* label; const char* key; bool on; bool* flag; };
    const Item items[] = {
        { "##ov_d", "HUD",               "D", in.show_hud,       &out.toggle_hud },
        { "##ov_i", "Inspector",         "I", in.show_inspector, &out.toggle_inspector },
        { "##ov_g", "A/V drift",         "G", in.show_drift,     &out.toggle_drift },
        { "##ov_t", "Tracks",            "T", in.show_tracks,    &out.toggle_tracks },
        { "##ov_w", "Waveform",          "W", in.show_waveform,  &out.toggle_waveform },
        { "##ov_n", "Network",           "N", in.show_network,   &out.toggle_network },
        { "##ov_v", "VMAF",              "V", in.show_vmaf,      &out.toggle_vmaf },
        { "##ov_h", "Help",              "H", in.show_help,      &out.toggle_help },
        { "##ov_k", "Keyframe filmstrip","K", in.show_filmstrip, &out.toggle_filmstrip },
    };
    for (const auto& it : items)
        if (menu_row(it.id, it.label, { .value = it.key, .tail = RowTail::Toggle, .on = it.on }))
            *it.flag = true;
}

// ── Help / shortcut card ──────────────────────────────────────────────────────

void PlayerUI::build_help_card(PlayerUIState& out, float win_w, float win_h)
{
    struct K { const char* keys; const char* label; };
    static constexpr K kLeft[] = {
        { "Space",        "Play / pause" },
        { "← →","Seek ±10 s" },
        { "↑ ↓","Seek ±60 s" },
        { ", .",          "Prev / next keyframe" },
        { "⇧, ⇧.", "Back / fwd 1 frame" },
        { "[ ]",          "Speed down / up" },
        { "M",            "Mute" },
        { "C",            "Subtitles on / off" },
        { "⇧C",      "Next subtitle track" },
        { "A",            "Next audio track" },
        { "F",            "Fullscreen (or double-click)" },
    };
    static constexpr K kRight[] = {
        { "K",            "Keyframe filmstrip" },
        { "D",            "Debug HUD" },
        { "I",            "Stream inspector" },
        { "G",            "A/V drift graph" },
        { "T",            "Track switcher" },
        { "W",            "Audio waveform" },
        { "N",            "Network log + tags" },
        { "V",            "VMAF panel" },
        { "H  ?",         "This help" },
        { "Esc",          "Exit fullscreen / cancel drag / quit" },
        { "Q",            "Quit" },
    };

    const float card_w = std::min(640.0f, win_w - 32.0f);
    ImGui::SetNextWindowPos(ImVec2(win_w * 0.5f, win_h * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(card_w, 0.0f), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 16.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, th::kRadiusPanel);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, th::vec4(th::kPanel));
    if (ImGui::Begin("##helpcard", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGui::PushFont(ui_fonts().title);
        ImGui::TextUnformatted("Keyboard shortcuts");
        ImGui::PopFont();
        // Close button (top-right)
        {
            const ImVec2 wp = ImGui::GetWindowPos();
            const float  ww = ImGui::GetWindowWidth();
            const ImVec2 save = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImVec2(wp.x + ww - 12.0f - th::kBtn, wp.y + 10.0f));
            auto cb = ui::icon_button("##helpclose", "Close (H)");
            const float r = 5.0f;
            dl->AddLine(ImVec2(cb.c.x - r, cb.c.y - r), ImVec2(cb.c.x + r, cb.c.y + r), cb.col, 2.0f);
            dl->AddLine(ImVec2(cb.c.x + r, cb.c.y - r), ImVec2(cb.c.x - r, cb.c.y + r), cb.col, 2.0f);
            if (cb.clicked) out.toggle_help = true;
            ImGui::SetCursorScreenPos(save);
        }
        ImGui::Dummy(ImVec2(0, 4.0f));

        ImFont* mono = ui_fonts().mono;
        const float col_w = (ImGui::GetContentRegionAvail().x - 16.0f) * 0.5f;
        const float key_w = 74.0f;
        auto column = [&](const K* rows, int n, float x0, float y0) {
            float y = y0;
            for (int i = 0; i < n; ++i) {
                const ImVec2 ks = ui::chip_size(rows[i].keys, mono, 12.0f);
                ui::chip(dl, ImVec2(x0, y), rows[i].keys, mono, 12.0f, th::kText, th::kGlassHair);
                (void)ks;
                dl->AddText(ImVec2(x0 + key_w + 8.0f, y + 1.0f), C(th::kText, 0.9f), rows[i].label);
                y += 26.0f;
            }
            return y;
        };
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const int nl = static_cast<int>(std::size(kLeft)), nr = static_cast<int>(std::size(kRight));
        const float y1 = column(kLeft,  nl, p.x, p.y);
        const float y2 = column(kRight, nr, p.x + col_w + 16.0f, p.y);
        ImGui::Dummy(ImVec2(col_w * 2.0f + 16.0f, std::max(y1, y2) - p.y));
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
}

// ── Hover preview card ────────────────────────────────────────────────────────

void PlayerUI::draw_hover_card(const PlayerUIInputs& in, const ui::SeekBarOut& sb, float win_w)
{
    const double t = std::clamp(static_cast<double>(sb.hover_frac), 0.0, 1.0) * in.duration;
    const float  hx = sb.track_min.x + sb.hover_frac * (sb.track_max.x - sb.track_min.x);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRectFullScreen();
    const float alpha = ImGui::GetStyle().Alpha;
    ImFont* mono  = ui_fonts().mono;
    ImFont* small = ui_fonts().ui_small;
    const bool hours = in.duration >= 3600.0;

    std::optional<ThumbnailStrip::ThumbRef> thumb;
    if (in.thumbs) thumb = in.thumbs->nearest_at_or_before(t, in.thumbs->preview_max_gap());

    // Chip: time, plus a KF line when a thumbnail is shown.
    const std::string tline = ui::format_clock(t, hours);
    std::string kline;
    if (thumb) {
        kline = "KF " + ui::format_clock(thumb->pts, hours) + " · I";
        if (t - thumb->pts > 0.5) kline += std::format("  (+{:.1f} s)", t - thumb->pts);
    }
    const ImVec2 ts = mono->CalcTextSizeA(13.0f, FLT_MAX, 0.0f, tline.c_str());
    const ImVec2 ks = kline.empty() ? ImVec2(0, 0) : small->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, kline.c_str());
    const float chip_w = std::round(std::max(ts.x, ks.x) + 16.0f);
    const float chip_h = std::round(ts.y + 6.0f + (kline.empty() ? 0.0f : ks.y + 2.0f));
    const float lift   = in.extra_bottom_logical;
    const float chip_y = std::round(sb.track_min.y - 10.0f - chip_h - lift);
    const float chip_x = std::round(std::clamp(hx - chip_w * 0.5f, th::kGutter, win_w - th::kGutter - chip_w));
    dl->AddRectFilled(ImVec2(chip_x, chip_y), ImVec2(chip_x + chip_w, chip_y + chip_h),
                      th::alpha(th::kGlass, alpha), th::kRadiusChip);
    dl->AddText(mono, 13.0f, ImVec2(chip_x + (chip_w - ts.x) * 0.5f, chip_y + 3.0f),
                th::alpha(th::kText, alpha), tline.c_str());
    if (!kline.empty())
        dl->AddText(small, 12.0f, ImVec2(chip_x + (chip_w - ks.x) * 0.5f, chip_y + 3.0f + ts.y + 2.0f),
                    th::alpha(th::kTextDim, alpha), kline.c_str());

    if (thumb && thumb->tex) {
        const float aspect = (in.video_aspect > 0.0f) ? in.video_aspect : 16.0f / 9.0f;
        float cw = 192.0f, ch = std::round(192.0f / aspect);
        if (ch > 216.0f) { ch = 216.0f; cw = std::round(216.0f * aspect); }
        ch = std::max(ch, 72.0f);
        const float cx = std::round(std::clamp(hx - cw * 0.5f, th::kGutter, win_w - th::kGutter - cw));
        const float cy = chip_y - 4.0f - ch;
        const ImVec2 mn(cx, cy), mx(cx + cw, cy + ch);
        // Soft shadow: three stacked rounded rects.
        for (int i = 1; i <= 3; ++i)
            dl->AddRectFilled(ImVec2(mn.x, mn.y + 2.0f * i), ImVec2(mx.x, mx.y + 2.0f * i),
                              th::alpha(th::kShadow, alpha / 3.0f), th::kRadiusSmall);
        dl->AddRectFilled(mn, mx, th::alpha(IM_COL32(0, 0, 0, 230), alpha), th::kRadiusSmall);
        // Letterbox the thumbnail inside the card if its aspect differs.
        const float ta = (thumb->h > 0) ? static_cast<float>(thumb->w) / static_cast<float>(thumb->h) : aspect;
        ImVec2 imn = mn, imx = mx;
        if (std::abs(ta - cw / ch) > 0.02f) {
            if (ta > cw / ch) { const float h = cw / ta; imn.y = mn.y + (ch - h) * 0.5f; imx.y = imn.y + h; }
            else              { const float w = ch * ta; imn.x = mn.x + (cw - w) * 0.5f; imx.x = imn.x + w; }
        }
        dl->AddImageRounded(static_cast<ImTextureID>(static_cast<intptr_t>(thumb->tex)), imn, imx,
                            ImVec2(0, 0), ImVec2(1, 1), th::alpha(IM_COL32_WHITE, alpha), th::kRadiusSmall);
        dl->AddRect(mn, mx, th::alpha(IM_COL32(255, 255, 255, 38), alpha), th::kRadiusSmall, 0, 1.0f);
    }
    dl->PopClipRect();
}

// ── Subtitles ─────────────────────────────────────────────────────────────────

void PlayerUI::draw_subtitle(const PlayerUIInputs& in, float /*win_w*/, float win_h, float dt)
{
    const float reserve = visible_bar_height_logical() + in.extra_bottom_logical + 12.0f;
    sub_overlay_.draw(ImGui::GetBackgroundDrawList(), ui_fonts().sub, in.subtitle_text,
                      in.video_rect, win_h, reserve, sub_style_, dt);
}

void PlayerUI::end_frame()
{
    ImGui::PopFont();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// ── Launch screen ─────────────────────────────────────────────────────────────

std::string PlayerUI::draw_launcher(const std::vector<std::string>& recents,
                                    const std::string& error_msg)
{
    std::string result;

    ImGuiIO& io = ImGui::GetIO();
    const float win_w = io.DisplaySize.x;
    const float win_h = io.DisplaySize.y;

    // ── Centered card ─────────────────────────────────────────────────────────
    constexpr float kCardW = 560.0f;
    constexpr float kCardH = 440.0f;
    const float cx = std::round((win_w - kCardW) * 0.5f);
    const float cy = std::round((win_h - kCardH) * 0.5f);

    ImGui::SetNextWindowPos(ImVec2(cx, cy), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kCardW, kCardH), ImGuiCond_Always);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, th::vec4(th::rgba(24, 25, 30, 1.0f)));
    ImGui::PushStyleColor(ImGuiCol_Border,   th::vec4(th::kPanelBorder));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(32.0f, 28.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(10.0f,  8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, th::kRadiusPanel);

    constexpr ImGuiWindowFlags card_flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    if (ImGui::Begin("##launcher", nullptr, card_flags)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // ── Header ────────────────────────────────────────────────────────────
        ImGui::PushFont(ui_fonts().title);
        ImGui::PushStyleColor(ImGuiCol_Text, th::vec4(th::kText));
        const char* title = "Mirage";
        ImGui::SetCursorPosX((kCardW - ImGui::CalcTextSize(title).x) * 0.5f);
        ImGui::TextUnformatted(title);
        ImGui::PopStyleColor();
        ImGui::PopFont();

        ImGui::PushStyleColor(ImGuiCol_Text, th::vec4(th::kTextDim));
        const char* sub = "Video engineering player";
        ImGui::SetCursorPosX((kCardW - ImGui::CalcTextSize(sub).x) * 0.5f);
        ImGui::TextUnformatted(sub);
        ImGui::PopStyleColor();

        ImGui::Dummy(ImVec2(0, 14.0f));

        // ── Open Media header ─────────────────────────────────────────────────
        ImGui::PushFont(ui_fonts().ui_small);
        ImGui::PushStyleColor(ImGuiCol_Text, th::vec4(th::kTextDim));
        ImGui::TextUnformatted("OPEN MEDIA");
        ImGui::PopStyleColor();
        ImGui::PopFont();

        // ── Drop zone ─────────────────────────────────────────────────────────
        constexpr float kZoneH = 96.0f;
        constexpr float kZoneW = kCardW - 64.0f;
        const ImVec2 ztl = ImGui::GetCursorScreenPos();
        const ImVec2 zbr(ztl.x + kZoneW, ztl.y + kZoneH);
        dl->AddRectFilled(ztl, zbr, IM_COL32(255, 255, 255, 8), 10.0f);
        dl->AddRect(ztl, zbr, th::alpha(th::kAccent, 0.35f), 10.0f, 0, 1.0f);

        const float zmx = ztl.x + kZoneW * 0.5f;
        const float zmy = ztl.y + kZoneH * 0.5f;
        const float lh  = ImGui::GetTextLineHeightWithSpacing();
        const char* l1 = "Drop a video file or stream URL here";
        const char* l2 = "Drag & drop, or type a path below";
        dl->AddText(ImVec2(zmx - ImGui::CalcTextSize(l1).x * 0.5f, zmy - lh), th::kText, l1);
        dl->AddText(ImVec2(zmx - ImGui::CalcTextSize(l2).x * 0.5f, zmy + 3.0f), th::kTextDim, l2);
        ImGui::Dummy(ImVec2(kZoneW, kZoneH));
        ImGui::Spacing();

        // ── Error banner (only shown after a failed open) ─────────────────────
        if (!error_msg.empty()) {
            const ImVec2 etl = ImGui::GetCursorScreenPos();
            const float  ew  = kZoneW;
            const float  eh  = ImGui::GetTextLineHeight() + 14.0f;
            dl->AddRectFilled(etl, ImVec2(etl.x + ew, etl.y + eh), th::alpha(th::kError, 0.14f), 8.0f);
            dl->AddRect(etl, ImVec2(etl.x + ew, etl.y + eh), th::alpha(th::kError, 0.6f), 8.0f, 0, 1.0f);
            dl->AddText(ImVec2(etl.x + 12.0f, etl.y + 7.0f), th::kText, error_msg.c_str());
            ImGui::Dummy(ImVec2(ew, eh + 6.0f));
        }

        // ── URL / path input ──────────────────────────────────────────────────
        ImGui::PushStyleColor(ImGuiCol_Text, th::vec4(th::kTextDim));
        ImGui::TextUnformatted("File path or network URL (HLS / DASH)");
        ImGui::PopStyleColor();

        const float input_w = kZoneW - 90.0f - 96.0f;
        ImGui::SetNextItemWidth(input_w);
        if (ImGui::InputText("##url", url_buf_, sizeof(url_buf_), ImGuiInputTextFlags_EnterReturnsTrue)
            && url_buf_[0] != '\0')
            result = url_buf_;

        ImGui::SameLine(0.0f, 8.0f);
        // Primary action: accent fill, dark text.
        ImGui::PushStyleColor(ImGuiCol_Button,        th::vec4(th::kAccent));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, th::vec4(th::kAccentHover));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  th::vec4(th::rgba(14, 165, 233, 1.0f)));
        ImGui::PushStyleColor(ImGuiCol_Text,          th::vec4(th::rgba(8, 20, 30, 1.0f)));
        if (ImGui::Button("Open", ImVec2(84.0f, 0.0f)) && url_buf_[0] != '\0')
            result = url_buf_;
        ImGui::PopStyleColor(4);

        ImGui::SameLine(0.0f, 8.0f);
        if (ImGui::Button("Browse…", ImVec2(-1.0f, 0.0f))) {
            nfdu8char_t* out_path = nullptr;
            nfdu8filteritem_t filters[] = {
                { "Video / Manifest", "mp4,mkv,mov,avi,ts,m2ts,m3u8,mpd,webm,flv" },
                { "All files", "*" }
            };
            if (NFD_OpenDialogU8(&out_path, filters, 2, nullptr) == NFD_OKAY && out_path) {
                result = out_path;
                NFD_FreePathU8(out_path);
            }
        }

        // ── Recent files ──────────────────────────────────────────────────────
        if (!recents.empty()) {
            ImGui::Dummy(ImVec2(0.0f, 6.0f));
            ImGui::PushFont(ui_fonts().ui_small);
            ImGui::PushStyleColor(ImGuiCol_Text, th::vec4(th::kTextDim));
            ImGui::TextUnformatted("RECENT");
            ImGui::PopStyleColor();
            ImGui::PopFont();

            ImGui::BeginChild("##recents", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_NoScrollbar);
            for (const auto& r : recents) {
                const std::string disp = ellipsize(r, ImGui::GetFont(), ImGui::GetFontSize(), kZoneW - 16.0f);
                ImGui::PushID(r.c_str());
                if (ImGui::Selectable(disp.c_str())) result = r;
                ImGui::PopID();
                if (ImGui::IsItemHovered()) ui::tooltip(r.c_str());
            }
            ImGui::EndChild();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(2);

    return result;
}

// ── VMAF results window ───────────────────────────────────────────────────────

bool PlayerUI::draw_vmaf_results(const std::vector<VMAFResultEntry>& entries,
                                  bool analyzing, float progress,
                                  float pos_frac)
{
    bool export_requested = false;

    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowSize(ImVec2(640.0f, 480.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(
        ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
        ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

    if (!ImGui::Begin("VMAF Analysis", nullptr, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return false;
    }

    // Status / progress
    if (analyzing) {
        ImGui::TextColored(ImVec4(0.90f, 0.70f, 0.20f, 1.0f),
                           "Analyzing... %.0f%%", progress * 100.0f);
        ImGui::SameLine();
        ImGui::ProgressBar(progress, ImVec2(-1.0f, 0.0f));
    } else if (!entries.empty()) {
        ImGui::TextColored(ImVec4(0.30f, 0.90f, 0.30f, 1.0f), "Analysis complete.");
        ImGui::SameLine();
        if (ImGui::Button("Export JSON")) export_requested = true;
    } else {
        ImGui::TextDisabled("No analysis running. Use Analyze menu to start.");
    }

    ImGui::Separator();

    if (entries.empty()) {
        ImGui::End();
        return export_requested;
    }

    // ── Summary table ─────────────────────────────────────────────────────────
    constexpr ImGuiTableFlags kTblFlags =
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY;

    if (ImGui::BeginTable("vmaf_table", 6, kTblFlags,
                          ImVec2(0.0f, entries.size() > 4 ? 160.0f : 0.0f))) {
        ImGui::TableSetupColumn("Label",      ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Resolution", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Bitrate",    ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn("Mean",       ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Min",        ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("P5",         ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableHeadersRow();

        auto score_color = [](double v) -> ImVec4 {
            if (v < 0.0) return ImVec4(0.55f, 0.55f, 0.55f, 1.0f);
            if (v >= 80.0) return ImVec4(0.25f, 0.90f, 0.35f, 1.0f);
            if (v >= 60.0) return ImVec4(0.90f, 0.75f, 0.20f, 1.0f);
            return ImVec4(0.90f, 0.30f, 0.25f, 1.0f);
        };

        for (const auto& e : entries) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            if (!e.error.empty())
                ImGui::TextColored(ImVec4(0.90f, 0.30f, 0.25f, 1.0f),
                                   "%s", e.label.c_str());
            else
                ImGui::TextUnformatted(e.label.c_str());

            ImGui::TableSetColumnIndex(1);
            if (e.width > 0)
                ImGui::Text("%dx%d", e.width, e.height);
            else
                ImGui::TextDisabled("—");

            ImGui::TableSetColumnIndex(2);
            if (e.bandwidth > 0)
                ImGui::Text("%.1f Mbps", e.bandwidth / 1e6);
            else
                ImGui::TextDisabled("—");

            auto fmt_score = [&](int col, double v) {
                ImGui::TableSetColumnIndex(col);
                if (!e.done) { ImGui::TextDisabled("..."); return; }
                if (!e.error.empty()) { ImGui::TextDisabled("err"); return; }
                ImGui::TextColored(score_color(v), "%.1f", v);
            };
            fmt_score(3, e.mean);
            fmt_score(4, e.min_val);
            fmt_score(5, e.p5);
        }
        ImGui::EndTable();
    }

    // ── Per-frame graph for the first completed entry with per_frame data ─────
    const VMAFResultEntry* graph_e = nullptr;
    for (const auto& e : entries)
        if (e.done && e.error.empty() && !e.per_frame.empty()) { graph_e = &e; break; }

    if (graph_e) {
        ImGui::Spacing();
        ImGui::Text("Per-frame VMAF: %s", graph_e->label.c_str());

        ImVec2 canvas_tl = ImGui::GetCursorScreenPos();
        ImVec2 canvas_sz = ImVec2(ImGui::GetContentRegionAvail().x,
                                  std::max(80.0f, ImGui::GetContentRegionAvail().y - 8.0f));
        ImGui::InvisibleButton("##vmaf_graph", canvas_sz);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 br = ImVec2(canvas_tl.x + canvas_sz.x, canvas_tl.y + canvas_sz.y);

        // Background
        dl->AddRectFilled(canvas_tl, br, IM_COL32(20, 20, 20, 200));

        // Score bands
        float h = canvas_sz.y;
        float y80 = canvas_tl.y + h * (1.0f - 0.80f);
        float y60 = canvas_tl.y + h * (1.0f - 0.60f);
        dl->AddRectFilled(canvas_tl, ImVec2(br.x, y80),
                          IM_COL32(20, 100, 20, 60));
        dl->AddRectFilled(ImVec2(canvas_tl.x, y80), ImVec2(br.x, y60),
                          IM_COL32(120, 90, 10, 60));
        dl->AddRectFilled(ImVec2(canvas_tl.x, y60), br,
                          IM_COL32(100, 20, 20, 60));

        // Line strip (downsampled)
        const auto& pf = graph_e->per_frame;
        int N = static_cast<int>(pf.size());
        int W = static_cast<int>(canvas_sz.x);
        if (N > 0 && W > 1) {
            std::vector<ImVec2> pts;
            pts.reserve(W);
            for (int xi = 0; xi < W; ++xi) {
                int f0 = xi * N / W;
                int f1 = std::max(f0 + 1, (xi + 1) * N / W);
                f1 = std::min(f1, N);
                double sum = 0.0;
                for (int fi = f0; fi < f1; ++fi) sum += pf[fi];
                double avg = sum / (f1 - f0);
                float vx = canvas_tl.x + static_cast<float>(xi);
                float vy = canvas_tl.y + h * (1.0f - static_cast<float>(avg) / 100.0f);
                pts.push_back(ImVec2(vx, vy));
            }
            dl->AddPolyline(pts.data(), (int)pts.size(),
                            IM_COL32(100, 200, 255, 220), 0, 1.5f);
        }

        // Playback position marker
        if (pos_frac >= 0.0f && pos_frac <= 1.0f) {
            float mx = canvas_tl.x + pos_frac * canvas_sz.x;
            dl->AddLine(ImVec2(mx, canvas_tl.y), ImVec2(mx, br.y),
                        IM_COL32(255, 255, 255, 160), 1.5f);

            // Score at current position
            int fi = static_cast<int>(pos_frac * N);
            fi = std::clamp(fi, 0, N - 1);
            dl->AddText(ImVec2(mx + 4.0f, canvas_tl.y + 4.0f),
                        IM_COL32(255, 255, 255, 220),
                        std::format("{:.1f}", pf[fi]).c_str());
        }
    }

    ImGui::End();
    return export_requested;
}
