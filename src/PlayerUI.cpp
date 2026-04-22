#include "PlayerUI.h"

#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <nfd.h>

#include <cstdio>
#include <algorithm>
#include <format>

// Speed presets — must match main.cpp
static constexpr double kSpeedPresets[] = { 0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 4.0 };
static constexpr int    kNumPresets     = static_cast<int>(std::size(kSpeedPresets));

// Auto-hide timing
static constexpr double kHideDelaySec = 4.0;
static constexpr double kFadeSec      = 0.4;

// VLC palette
static constexpr ImVec4 kOrange    { 1.00f, 0.53f, 0.00f, 1.00f };  // #FF8800
static constexpr ImVec4 kOrangeHov { 1.00f, 0.63f, 0.15f, 1.00f };
static constexpr ImVec4 kOrangeAct { 0.85f, 0.43f, 0.00f, 1.00f };
static constexpr ImU32  kOrangeU32 = IM_COL32(255, 136,   0, 255);
static constexpr ImU32  kTrackU32  = IM_COL32( 55,  55,  55, 255);
static constexpr ImU32  kBgU32     = IM_COL32( 30,  30,  30, 255);

// ── helpers ───────────────────────────────────────────────────────────────────

static void fmt_time(char* buf, int n, double sec)
{
    int s = (sec < 0.0) ? 0 : static_cast<int>(sec);
    std::snprintf(buf, n, "%02d:%02d:%02d", s / 3600, (s % 3600) / 60, s % 60);
}

// VLC-style horizontal seek bar drawn with ImDrawList.
// Returns true when the user drags (value in *frac updated).
static bool vlc_seek_bar(const char* id, float* frac, float width, float alpha)
{
    constexpr float kTrackH  = 4.0f;
    constexpr float kHitH    = 16.0f;   // taller hit zone for easy clicking
    constexpr float kThumbR  = 6.5f;

    ImVec2 tl = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton(id, ImVec2(width, kHitH));
    bool hovered = ImGui::IsItemHovered();
    bool active  = ImGui::IsItemActive();
    bool changed = false;

    if (active) {
        float mx = ImGui::GetIO().MousePos.x;
        *frac = std::clamp((mx - tl.x) / width, 0.0f, 1.0f);
        changed = true;
    }

    // Draw centered on the hit zone
    float cy      = tl.y + kHitH * 0.5f;
    float y0      = cy - kTrackH * 0.5f;
    float y1      = cy + kTrackH * 0.5f;
    float fill_x  = tl.x + (*frac) * width;
    float thumb_r = (hovered || active) ? kThumbR : kThumbR - 1.5f;
    ImU32 oa = IM_COL32(255, 136, 0, static_cast<int>(alpha * 255));

    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Track
    dl->AddRectFilled(ImVec2(tl.x, y0), ImVec2(tl.x + width, y1),
        IM_COL32(55, 55, 55, static_cast<int>(alpha * 200)), 2.0f);
    // Filled (played) portion
    if (fill_x > tl.x)
        dl->AddRectFilled(ImVec2(tl.x, y0), ImVec2(fill_x, y1), oa, 2.0f);
    // Thumb — white circle with orange border
    dl->AddCircleFilled(ImVec2(fill_x, cy), thumb_r,
        IM_COL32(240, 240, 240, static_cast<int>(alpha * 255)));
    if (hovered || active)
        dl->AddCircle(ImVec2(fill_x, cy), thumb_r + 1.0f, oa);

    return changed;
}

// Compact VLC-style volume bar.
static bool vlc_vol_bar(const char* id, float* vol, float width, float alpha)
{
    constexpr float kH = 3.0f;
    constexpr float kHitH = 12.0f;
    ImVec2 tl = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton(id, ImVec2(width, kHitH));
    bool active  = ImGui::IsItemActive();
    bool changed = false;

    if (active) {
        float mx = ImGui::GetIO().MousePos.x;
        *vol = std::clamp((mx - tl.x) / width, 0.0f, 1.0f);
        changed = true;
    }

    float cy     = tl.y + kHitH * 0.5f;
    float y0     = cy - kH * 0.5f;
    float y1     = cy + kH * 0.5f;
    float fill_x = tl.x + (*vol) * width;
    ImU32 oa = IM_COL32(255, 136, 0, static_cast<int>(alpha * 200));

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(ImVec2(tl.x, y0), ImVec2(tl.x + width, y1),
        IM_COL32(55, 55, 55, static_cast<int>(alpha * 180)), 1.5f);
    if (fill_x > tl.x)
        dl->AddRectFilled(ImVec2(tl.x, y0), ImVec2(fill_x, y1), oa, 1.5f);

    // small thumb
    dl->AddCircleFilled(ImVec2(fill_x, cy), 5.0f,
        IM_COL32(220, 220, 220, static_cast<int>(alpha * 240)));

    return changed;
}

// Flat transport button — transparent bg, orange glow on hover/active.
static bool flat_button(const char* label, float w = 0.0f, float h = 0.0f)
{
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.f, .53f, 0.f, .18f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(1.f, .53f, 0.f, .35f));
    bool r = ImGui::Button(label, ImVec2(w, h));
    ImGui::PopStyleColor(3);
    return r;
}

// ── PlayerUI ──────────────────────────────────────────────────────────────────

PlayerUI::~PlayerUI() { shutdown(); }

bool PlayerUI::init(GLFWwindow* window)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // ── VLC-style dark theme ─────────────────────────────────────────────────
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding    = 4.0f;
    s.FrameRounding     = 3.0f;
    s.ScrollbarRounding = 3.0f;
    s.GrabRounding      = 3.0f;
    s.PopupRounding     = 4.0f;
    s.WindowBorderSize  = 0.0f;
    s.FrameBorderSize   = 0.0f;
    s.ItemSpacing       = ImVec2(8.0f, 5.0f);
    s.WindowPadding     = ImVec2(12.0f, 10.0f);

    // Charcoal background (#1e1e1e family)
    s.Colors[ImGuiCol_WindowBg]      = ImVec4(0.12f, 0.12f, 0.12f, 0.97f);
    s.Colors[ImGuiCol_ChildBg]       = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    s.Colors[ImGuiCol_PopupBg]       = ImVec4(0.10f, 0.10f, 0.10f, 0.97f);
    s.Colors[ImGuiCol_MenuBarBg]     = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    s.Colors[ImGuiCol_Border]        = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);

    // Text
    s.Colors[ImGuiCol_Text]          = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);
    s.Colors[ImGuiCol_TextDisabled]  = ImVec4(0.40f, 0.40f, 0.40f, 1.00f);

    // VLC orange accent
    s.Colors[ImGuiCol_ButtonHovered] = kOrangeHov;
    s.Colors[ImGuiCol_ButtonActive]  = kOrangeAct;
    s.Colors[ImGuiCol_Button]        = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
    s.Colors[ImGuiCol_Header]        = ImVec4(1.f, .53f, 0.f, 0.25f);
    s.Colors[ImGuiCol_HeaderHovered] = ImVec4(1.f, .53f, 0.f, 0.45f);
    s.Colors[ImGuiCol_HeaderActive]  = ImVec4(1.f, .53f, 0.f, 0.75f);
    s.Colors[ImGuiCol_CheckMark]     = kOrange;
    s.Colors[ImGuiCol_SliderGrab]    = kOrange;
    s.Colors[ImGuiCol_SliderGrabActive] = kOrangeHov;
    s.Colors[ImGuiCol_FrameBg]       = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
    s.Colors[ImGuiCol_FrameBgHovered]= ImVec4(0.24f, 0.24f, 0.24f, 1.00f);
    s.Colors[ImGuiCol_TitleBg]       = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    s.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    s.Colors[ImGuiCol_ScrollbarBg]   = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
    s.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.28f, 0.28f, 0.28f, 1.00f);
    s.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.38f, 0.38f, 0.38f, 1.00f);
    s.Colors[ImGuiCol_SeparatorHovered] = ImVec4(1.f, .53f, 0.f, 0.60f);
    s.Colors[ImGuiCol_Tab]           = ImVec4(0.12f, 0.12f, 0.12f, 1.00f);
    s.Colors[ImGuiCol_TabHovered]    = ImVec4(1.f, .53f, 0.f, 0.75f);
    s.Colors[ImGuiCol_TabActive]     = ImVec4(0.85f, .43f, 0.f, 1.00f);

    if (!ImGui_ImplGlfw_InitForOpenGL(window, /*install_callbacks=*/true)) return false;
    if (!ImGui_ImplOpenGL3_Init("#version 410 core")) return false;

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
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void PlayerUI::notify_activity()
{
    last_activity_ = glfwGetTime();
}

float PlayerUI::visible_bar_height_logical() const
{
    return (bar_alpha_ > 0.005f) ? kVlcBarH : 0.0f;
}

// ── Menu bar + VLC-style control bar ─────────────────────────────────────────

PlayerUIState PlayerUI::build(
    double duration_s, double cur_pts,
    bool   paused,     double speed,  float volume,
    bool   has_audio,
    bool   show_hud,   bool show_inspector, bool show_drift,
    bool   show_tracks,bool show_waveform,  bool show_network,
    bool   show_help,  bool show_vmaf,
    bool   has_manifest_variants)
{
    PlayerUIState out{};

    // ── Menu bar ──────────────────────────────────────────────────────────────
    if (ImGui::BeginMainMenuBar()) {
        menu_bar_h_ = ImGui::GetFrameHeight();

        if (ImGui::BeginMenu("Media")) {
            if (ImGui::MenuItem("Open Media...", "Ctrl+O")) out.quit = false;  // placeholder
            ImGui::Separator();
            if (ImGui::MenuItem("Quit", "Q / Esc")) out.quit = true;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Playback")) {
            if (ImGui::MenuItem(paused ? "Play" : "Pause", "Space"))
                out.play_pause_clicked = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Jump Backward 10s",  "Left"))  out.seek_to = std::max(0.0, cur_pts - 10.0);
            if (ImGui::MenuItem("Jump Forward  10s",  "Right")) out.seek_to = cur_pts + 10.0;
            if (ImGui::MenuItem("Jump Backward 60s",  "Down"))  out.seek_to = std::max(0.0, cur_pts - 60.0);
            if (ImGui::MenuItem("Jump Forward  60s",  "Up"))    out.seek_to = cur_pts + 60.0;
            ImGui::Separator();
            if (ImGui::BeginMenu("Speed")) {
                for (int i = 0; i < kNumPresets; ++i) {
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), "%.2gx", kSpeedPresets[i]);
                    bool sel = (kSpeedPresets[i] == speed);
                    if (ImGui::MenuItem(buf, nullptr, sel)) out.speed_set = kSpeedPresets[i];
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            bool v;
            v = show_hud;       if (ImGui::MenuItem("Debug HUD",       "D", &v)) out.toggle_hud       = true;
            v = show_inspector; if (ImGui::MenuItem("Stream Inspector", "I", &v)) out.toggle_inspector = true;
            v = show_waveform;  if (ImGui::MenuItem("Waveform",         "W", &v)) out.toggle_waveform  = true;
            v = show_network;   if (ImGui::MenuItem("Network Log",      "N", &v)) out.toggle_network   = true;
            v = show_drift;     if (ImGui::MenuItem("A/V Drift Graph",  "G", &v)) out.toggle_drift     = true;
            v = show_tracks;    if (ImGui::MenuItem("Track Switcher",   "T", &v)) out.toggle_tracks    = true;
            v = show_vmaf;      if (ImGui::MenuItem("VMAF Panel",       "V", &v)) out.toggle_vmaf      = true;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Analyze")) {
            if (ImGui::MenuItem("VMAF — Single File...",
                                nullptr, false, true))
                out.vmaf_pick_ref = true;
            if (ImGui::MenuItem("VMAF — Manifest Variants",
                                nullptr, false, has_manifest_variants))
                out.vmaf_analyze_manifest = true;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Help")) {
            bool v = show_help;
            if (ImGui::MenuItem("Key Controls", "H", &v)) out.toggle_help = true;
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // ── Auto-hide alpha ───────────────────────────────────────────────────────
    double now  = glfwGetTime();
    double idle = now - last_activity_;
    float  alpha;
    if (idle < kHideDelaySec)
        alpha = 1.0f;
    else if (idle < kHideDelaySec + kFadeSec)
        alpha = 1.0f - static_cast<float>((idle - kHideDelaySec) / kFadeSec);
    else
        alpha = 0.0f;
    bar_alpha_ = alpha;

    if (alpha <= 0.005f) return out;

    ImGuiIO& io = ImGui::GetIO();
    float win_w = io.DisplaySize.x;
    float win_h = io.DisplaySize.y;

    // ── VLC control bar — positioned at the very bottom ───────────────────────
    ImGui::SetNextWindowPos(ImVec2(0.0f, win_h - kVlcBarH), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(win_w, kVlcBarH), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.88f * alpha);

    // Slight orange top-border line (VLC's separator between video and controls)
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.f, .53f, 0.f, 0.30f * alpha));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(12.0f, 8.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,      ImVec2(6.0f,  4.0f));

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    if (ImGui::Begin("##vlcbar", nullptr, flags)) {

        // ── Row 1: seek bar ────────────────────────────────────────────────────
        float avail_w = ImGui::GetContentRegionAvail().x;

        if (duration_s > 0.0) {
            float frac = static_cast<float>(std::clamp(cur_pts / duration_s, 0.0, 1.0));
            if (vlc_seek_bar("##seek", &frac, avail_w, alpha)) {
                out.seek_to = frac * duration_s;
                notify_activity();
            }
        } else {
            // Live stream — just draw a flat orange line
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(
                p, ImVec2(p.x + avail_w, p.y + 4.0f), kOrangeU32);
            ImGui::Dummy(ImVec2(avail_w, 16.0f));
        }

        // ── Row 2: transport + time + volume + speed ───────────────────────────
        ImGui::PushStyleColor(ImGuiCol_Text,
            ImVec4(0.88f, 0.88f, 0.88f, alpha));

        // Transport buttons (flat, orange glow on hover)
        if (flat_button("|<", 32.0f))  out.seek_to = 0.0;
        ImGui::SameLine();
        if (flat_button("<<", 32.0f))  out.seek_to = std::max(0.0, cur_pts - 10.0);
        ImGui::SameLine();

        // Play / Pause — slightly larger, orange text
        ImGui::PushStyleColor(ImGuiCol_Text, paused
            ? ImVec4(kOrange.x, kOrange.y, kOrange.z, alpha)
            : ImVec4(0.95f, 0.95f, 0.95f, alpha));
        if (flat_button(paused ? "  >  " : " || ", 42.0f))
            out.play_pause_clicked = true;
        ImGui::PopStyleColor();

        ImGui::SameLine();
        if (flat_button(">>", 32.0f))  out.seek_to = cur_pts + 10.0;
        ImGui::SameLine();
        if (flat_button(">|", 32.0f) && duration_s > 0.0)
            out.seek_to = duration_s - 0.5;

        // Time display — center of the row
        char time_buf[32];
        if (duration_s > 0.0) {
            char pos[12], dur[12];
            fmt_time(pos, sizeof(pos), cur_pts);
            fmt_time(dur, sizeof(dur), duration_s);
            std::snprintf(time_buf, sizeof(time_buf), "%s / %s", pos, dur);
        } else {
            fmt_time(time_buf, sizeof(time_buf), cur_pts);
        }
        float time_w = ImGui::CalcTextSize(time_buf).x;
        float mid_x  = (avail_w - time_w) * 0.5f + ImGui::GetWindowPos().x + 12.0f;
        // Only center if there's room; otherwise just SameLine
        if (mid_x > ImGui::GetCursorScreenPos().x + 8.0f)
            ImGui::SetCursorScreenPos(ImVec2(mid_x, ImGui::GetCursorScreenPos().y));
        else
            ImGui::SameLine(0.0f, 14.0f);
        ImGui::TextUnformatted(time_buf);

        // Right-aligned: volume + speed
        float right_x = ImGui::GetWindowPos().x + avail_w + 12.0f
                      - (has_audio ? 104.0f : 0.0f) - 56.0f;
        ImVec2 csp = ImGui::GetCursorScreenPos();
        if (right_x > csp.x + 20.0f)
            ImGui::SetCursorScreenPos(ImVec2(right_x, csp.y));
        else
            ImGui::SameLine(0.0f, 14.0f);

        // Volume
        if (has_audio) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.65f, 0.65f, 0.65f, alpha));
            ImGui::TextUnformatted("vol");
            ImGui::PopStyleColor();
            ImGui::SameLine(0.0f, 4.0f);
            float vol = volume;
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 4.0f);  // center on text line
            if (vlc_vol_bar("##vol", &vol, 72.0f, alpha))
                out.volume_set = vol;
            ImGui::SameLine(0.0f, 10.0f);
        }

        // Speed combo
        ImGui::SetNextItemWidth(54.0f);
        char spd_buf[12];
        std::snprintf(spd_buf, sizeof(spd_buf), "%.2gx", speed);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 4.0f);
        if (ImGui::BeginCombo("##spd", spd_buf, ImGuiComboFlags_NoArrowButton)) {
            for (int i = 0; i < kNumPresets; ++i) {
                char lbl[12];
                std::snprintf(lbl, sizeof(lbl), "%.2gx", kSpeedPresets[i]);
                bool sel = (kSpeedPresets[i] == speed);
                if (ImGui::Selectable(lbl, sel)) out.speed_set = kSpeedPresets[i];
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        ImGui::PopStyleColor();  // text alpha
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor();  // border

    return out;
}

void PlayerUI::end_frame()
{
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// ── Launch screen (VLC "Open Media" style) ────────────────────────────────────

std::string PlayerUI::draw_launcher(const std::vector<std::string>& recents)
{
    std::string result;

    ImGuiIO& io = ImGui::GetIO();
    float win_w = io.DisplaySize.x;
    float win_h = io.DisplaySize.y;

    // Fill background with VLC charcoal
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(win_w, win_h));
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin("##bg", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::End();
    ImGui::PopStyleVar();

    // ── Centered card ─────────────────────────────────────────────────────────
    constexpr float kCardW = 560.0f;
    constexpr float kCardH = 440.0f;
    float cx = (win_w - kCardW) * 0.5f;
    float cy = (win_h - kCardH) * 0.5f;

    ImGui::SetNextWindowPos(ImVec2(cx, cy), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kCardW, kCardH), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(1.0f);

    // Card uses a slightly lighter charcoal
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.16f, 0.16f, 0.16f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border,   ImVec4(0.22f, 0.22f, 0.22f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28.0f, 24.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(8.0f,   7.0f));

    ImGuiWindowFlags card_flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    if (ImGui::Begin("##launcher", nullptr, card_flags)) {

        // ── Header: orange title ──────────────────────────────────────────────
        ImGui::PushStyleColor(ImGuiCol_Text, kOrange);
        ImGui::SetWindowFontScale(1.9f);
        float tw = ImGui::CalcTextSize("MIRAGE").x;
        ImGui::SetCursorPosX((kCardW - tw) * 0.5f);
        ImGui::TextUnformatted("MIRAGE");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopStyleColor();

        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.48f, 0.48f, 0.48f, 1.0f));
        const char* sub = "Video Engineering Player";
        float sw = ImGui::CalcTextSize(sub).x;
        ImGui::SetCursorPosX((kCardW - sw) * 0.5f);
        ImGui::TextUnformatted(sub);
        ImGui::PopStyleColor();

        ImGui::Spacing();

        // Orange separator line (VLC style)
        {
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddLine(
                p, ImVec2(p.x + kCardW - 56.0f, p.y),
                kOrangeU32, 1.5f);
        }
        ImGui::Dummy(ImVec2(0, 6.0f));

        // ── Open Media header ─────────────────────────────────────────────────
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.60f, 0.60f, 1.0f));
        ImGui::TextUnformatted("OPEN MEDIA");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        // ── Drop zone ─────────────────────────────────────────────────────────
        constexpr float kZoneH = 96.0f;
        constexpr float kZoneW = kCardW - 56.0f;

        ImVec2 ztl = ImGui::GetCursorScreenPos();
        ImVec2 zbr = ImVec2(ztl.x + kZoneW, ztl.y + kZoneH);
        ImDrawList* dl = ImGui::GetWindowDrawList();

        dl->AddRectFilled(ztl, zbr, IM_COL32(20, 20, 20, 255), 4.0f);
        dl->AddRect(ztl, zbr, IM_COL32(255, 136, 0, 90), 4.0f, 0, 1.2f);

        float zmx = ztl.x + kZoneW * 0.5f;
        float zmy = ztl.y + kZoneH * 0.5f;
        float lh  = ImGui::GetTextLineHeightWithSpacing();
        const char* l1 = "Drop a video file or stream URL here";
        const char* l2 = "Drag & drop  or  type below";
        dl->AddText(ImVec2(zmx - ImGui::CalcTextSize(l1).x * 0.5f, zmy - lh),
            IM_COL32(200, 200, 200, 210), l1);
        dl->AddText(ImVec2(zmx - ImGui::CalcTextSize(l2).x * 0.5f, zmy + 3.0f),
            IM_COL32(120, 120, 120, 180), l2);

        ImGui::Dummy(ImVec2(kZoneW, kZoneH));
        ImGui::Spacing();

        // ── URL / path input ──────────────────────────────────────────────────
        ImGui::PushStyleColor(ImGuiCol_Text,   ImVec4(0.50f, 0.50f, 0.50f, 1.0f));
        ImGui::TextUnformatted("File path or network URL (HLS / DASH)");
        ImGui::PopStyleColor();

        float input_w = kZoneW - 86.0f - 82.0f;
        ImGui::SetNextItemWidth(input_w);
        bool enter_pressed = ImGui::InputText("##url", url_buf_, sizeof(url_buf_),
            ImGuiInputTextFlags_EnterReturnsTrue);
        if (enter_pressed && url_buf_[0] != '\0')
            result = url_buf_;

        ImGui::SameLine(0.0f, 6.0f);

        // Orange primary "Open" button
        ImGui::PushStyleColor(ImGuiCol_Button,        kOrange);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kOrangeHov);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  kOrangeAct);
        ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.05f, 0.05f, 0.05f, 1.0f));
        if (ImGui::Button("Open", ImVec2(80.0f, 0.0f)) && url_buf_[0] != '\0')
            result = url_buf_;
        ImGui::PopStyleColor(4);

        ImGui::SameLine(0.0f, 6.0f);
        if (ImGui::Button("Browse...", ImVec2(-1.0f, 0.0f))) {
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
            ImGui::Spacing();
            // Orange micro-separator
            {
                ImVec2 p = ImGui::GetCursorScreenPos();
                dl->AddLine(p, ImVec2(p.x + kZoneW, p.y),
                    IM_COL32(255, 136, 0, 50), 1.0f);
            }
            ImGui::Dummy(ImVec2(0.0f, 3.0f));

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.45f, 0.45f, 0.45f, 1.0f));
            ImGui::TextUnformatted("RECENT");
            ImGui::PopStyleColor();

            ImGui::BeginChild("##recents", ImVec2(0.0f, 0.0f), false,
                ImGuiWindowFlags_NoScrollbar);
            for (const auto& r : recents) {
                const char* disp = r.c_str();
                if (r.size() > 58) disp = r.c_str() + (r.size() - 58);

                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.72f, 0.72f, 0.72f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                    ImVec4(1.f, .53f, 0.f, 0.15f));
                if (ImGui::Selectable(disp)) result = r;
                ImGui::PopStyleColor(2);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", r.c_str());
            }
            ImGui::EndChild();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(3);
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
