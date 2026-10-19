#pragma once

// Drawing helpers and palette shared by the network inspector's tabs
// (NetInspector.cpp, NetInspectorAlign.cpp, NetInspectorResponse.cpp).
// Main thread, between ImGui frames.

#include "NetDescribe.h"
#include "NetworkLogger.h"
#include "UiFonts.h"
#include "UiTheme.h"
#include "UiWidgets.h"

#include "imgui.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <ctime>
#include <format>
#include <string>

namespace netui {

namespace th = ui::theme;


// ── Palette (DevTools-inspired, tuned to the player theme) ───────────────────
constexpr ImU32 kSideBg      = th::rgba( 15,  16,  20, 0.985f);
constexpr ImU32 kSideBar     = th::rgba(255, 255, 255, 0.035f);   // toolbar / header strips
constexpr ImU32 kColVideo    = th::kAccent;
constexpr ImU32 kColAudio    = th::rgba(167, 139, 250, 1.00f);    // violet-400
constexpr ImU32 kColSubs     = th::kWarn;
constexpr ImU32 kColManifest = th::rgba( 52, 211, 153, 1.00f);    // emerald-400
constexpr ImU32 kColOther    = th::rgba(148, 163, 184, 1.00f);    // slate-400
constexpr ImU32 kWfConnect   = th::rgba(251, 146,  60, 1.00f);    // orange — DNS/TCP/TLS + queue
constexpr ImU32 kWfWait      = th::rgba( 52, 211, 153, 1.00f);    // green — waiting (TTFB)
constexpr ImU32 kWfDownload  = th::rgba( 56, 189, 248, 1.00f);    // blue — content download
constexpr ImU32 kSelRow      = th::rgba( 56, 189, 248, 0.22f);
constexpr ImU32 kPlayRow     = th::rgba( 56, 189, 248, 0.14f);

constexpr float kHeaderH  = 40.0f;
constexpr float kToolbarH = 36.0f;
constexpr float kChipsH   = 32.0f;
constexpr float kFooterH  = 26.0f;
constexpr float kSplitH   = 6.0f;
constexpr float kPad      = 10.0f;
constexpr float kMinW     = 300.0f;

enum Col { ColName, ColStatus, ColType, ColInit, ColSize, ColTime, ColWaterfall, ColCount };

inline ImU32 C(ImU32 c, float mul = 1.0f) { return ImGui::GetColorU32(c, mul); }

inline ImU32 media_color(const NetDesc& d)
{
    if (d.kind == NetKind::Manifest || d.kind == NetKind::Playlist) return kColManifest;
    switch (d.media) {
        case NetMedia::Video: return kColVideo;
        case NetMedia::Audio: return kColAudio;
        case NetMedia::Subs:  return kColSubs;
        case NetMedia::None:  break;
    }
    return kColOther;
}

inline const char* media_name(NetMedia m)
{
    switch (m) {
        case NetMedia::Video: return "video";
        case NetMedia::Audio: return "audio";
        case NetMedia::Subs:  return "subtitles";
        case NetMedia::None:  break;
    }
    return "";
}

inline bool is_error(const NetRequest& r) { return r.failed || r.status >= 400; }

inline bool is_helper(const NetRequest& r)
{
    return r.initiator == NetInitiator::Thumbnails || r.initiator == NetInitiator::Scrub
        || r.initiator == NetInitiator::Inspector;
}

inline std::string lower(std::string s)
{
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

inline std::string clock_text(std::int64_t t_us)
{
    const std::int64_t ms = NetworkLogger::wall_ms(t_us);
    const std::time_t  t  = static_cast<std::time_t>(ms / 1000);
    std::tm tm{};
    localtime_r(&t, &tm);
    return std::format("{:02}:{:02}:{:02}.{:03}", tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms % 1000));
}

inline std::string partial_note(const NetRequest& r)
{
    const std::string of = r.content_length > 0 ? std::format(" of {}", net_format_bytes(r.content_length)) : "";
    return std::format("Partial: {}{} read. The player stopped reading this response before its end "
                       "(a variant probed at startup or no longer selected, or a full playback buffer). "
                       "Time runs to the last byte; the row continues if reading resumes.",
                       net_format_bytes(std::max<std::int64_t>(r.bytes, 0)), of);
}

inline std::string status_text(const NetRequest& r)
{
    if (r.status > 0) return std::to_string(r.status);
    if (r.failed)     return "failed";
    if (r.protocol == "file") return "—";
    return r.done() ? "—" : "…";
}

// Pill-shaped toggle chip; returns true when clicked.
inline bool chip_button(const char* id, const char* label, bool active, ImU32 dot = 0)
{
    ImFont* f = ui_fonts().ui_small;
    const float fs  = 12.0f;
    const ImVec2 ts = f->CalcTextSizeA(fs, FLT_MAX, 0.0f, label);
    const float dot_w = dot ? 12.0f : 0.0f;
    const ImVec2 sz(std::round(ts.x + 20.0f + dot_w), 22.0f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, sz);
    const bool hov = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 q(p.x + sz.x, p.y + sz.y);
    dl->AddRectFilled(p, q, C(active ? th::alpha(th::kAccent, 0.20f) : hov ? th::kHoverBg : th::rgba(255, 255, 255, 0.05f)),
                      sz.y * 0.5f);
    if (active) dl->AddRect(p, q, C(th::alpha(th::kAccent, 0.55f)), sz.y * 0.5f, 0, 1.0f);
    float x = p.x + 10.0f;
    if (dot) {
        dl->AddCircleFilled(ImVec2(x + 3.5f, p.y + sz.y * 0.5f), 3.5f, C(dot), 12);
        x += dot_w;
    }
    dl->AddText(f, fs, ImVec2(x, std::round(p.y + (sz.y - ts.y) * 0.5f)),
                C(active ? th::kAccentHover : hov ? th::kText : th::kTextDim), label);
    return clicked;
}

// Small square glyph button (× close, clear, record); returns true when clicked.
inline bool glyph_button(const char* id, const char* tip, float sz, bool active = false)
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(sz, sz));
    const bool hov = ImGui::IsItemHovered();
    if (hov || active)
        ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + sz, p.y + sz),
            C(ImGui::IsItemActive() ? th::kPressBg : hov ? th::kHoverBg : th::rgba(255, 255, 255, 0.06f)),
            th::kRadiusSmall);
    if (hov && tip) ui::tooltip(tip);
    return clicked;
}

inline void draw_x(ImDrawList* dl, ImVec2 c, float r, ImU32 col)
{
    dl->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), col, 1.6f);
    dl->AddLine(ImVec2(c.x - r, c.y + r), ImVec2(c.x + r, c.y - r), col, 1.6f);
}

// Uppercase section caption.
inline void section_caption(const char* text)
{
    ImGui::Dummy(ImVec2(0.0f, 4.0f));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, 20.0f));
    ImGui::GetWindowDrawList()->AddText(ui_fonts().ui_small, 11.5f, ImVec2(p.x, p.y + 4.0f), C(th::kTextDim), text);
}

// Key / value row inside a 2-column table.
inline void kv_row(const char* key, const std::string& value, ImU32 col = 0)
{
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::PushStyleColor(ImGuiCol_Text, th::vec4(th::kTextDim));
    ImGui::TextUnformatted(key);
    ImGui::PopStyleColor();
    ImGui::TableSetColumnIndex(1);
    if (col) ImGui::PushStyleColor(ImGuiCol_Text, th::vec4(col));
    ImGui::TextWrapped("%s", value.c_str());
    if (col) ImGui::PopStyleColor();
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        ImGui::SetClipboardText(value.c_str());
}

inline bool begin_kv_table(const char* id, float key_w)
{
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings)) return false;
    ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthFixed, key_w);
    ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch);
    return true;
}

// Right-aligned text in the current table cell.
inline void text_right(const std::string& s, ImU32 col)
{
    const float w = ImGui::CalcTextSize(s.c_str()).x;
    const float avail = ImGui::GetContentRegionAvail().x;
    if (avail > w) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + avail - w);
    ImGui::PushStyleColor(ImGuiCol_Text, th::vec4(col));
    ImGui::TextUnformatted(s.c_str());
    ImGui::PopStyleColor();
}

inline void draw_empty_state(const char* title, const char* body, float w, float h)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImFont* f  = ui_fonts().ui;
    ImFont* fs = ui_fonts().ui_small;
    const float cy = p.y + std::max(40.0f, h * 0.35f);
    const ImVec2 t1 = f->CalcTextSizeA(14.0f, FLT_MAX, 0.0f, title);
    dl->AddText(f, 14.0f, ImVec2(std::round(p.x + (w - t1.x) * 0.5f), cy), C(th::kText), title);
    const float wrap = std::min(w - 40.0f, 320.0f);
    const ImVec2 t2 = fs->CalcTextSizeA(12.0f, FLT_MAX, wrap, body);
    dl->AddText(fs, 12.0f, ImVec2(std::round(p.x + (w - wrap) * 0.5f + (wrap - t2.x) * 0.5f), cy + 24.0f),
                C(th::kTextDim), body, nullptr, wrap);
    ImGui::Dummy(ImVec2(w, h));
}

} // namespace netui
