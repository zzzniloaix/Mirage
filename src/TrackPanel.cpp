#include "TrackPanel.h"
#include "TrackGroups.h"
#include "UiFonts.h"
#include "UiIcons.h"
#include "UiTheme.h"
#include "UiWidgets.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

namespace th = ui::theme;

namespace {

ImU32 C(ImU32 c, float mul = 1.0f) { return ImGui::GetColorU32(c, mul); }

constexpr float kRowH    = 46.0f;
constexpr float kColGap  = 16.0f;
constexpr float kMarkW   = 30.0f;   // check / spinner column
// Logical font sizes (the atlas is rasterised at fb scale; see UiFonts.h).
constexpr float kTitleFs = 14.0f;
constexpr float kSmallFs = 12.0f;

struct Row {
    std::string title, detail;
    bool selected = false, pending = false;
    std::string tip;                // hover tooltip (empty = none)
};

// One selectable two-line row. Returns true when clicked.
bool track_row(const char* id, const Row& r, float w, double t)
{
    ImDrawList*  dl = ImGui::GetWindowDrawList();
    const ImVec2 p  = ImGui::GetCursorScreenPos();
    const bool   clicked = ImGui::InvisibleButton(id, ImVec2(w, kRowH));
    const bool   hovered = ImGui::IsItemHovered();
    const ImVec2 q(p.x + w, p.y + kRowH);

    if (r.pending) {
        const float pulse = 0.14f + 0.08f * static_cast<float>(0.5 + 0.5 * std::sin(t * 6.0));
        dl->AddRectFilled(p, q, C(th::alpha(th::kWarn, pulse)), th::kRadiusSmall);
    } else if (r.selected) {
        dl->AddRectFilled(p, q, C(th::alpha(th::kAccent, 0.16f)), th::kRadiusSmall);
    } else if (hovered) {
        dl->AddRectFilled(p, q, C(th::kHoverBg), th::kRadiusSmall);
    }

    const ImVec2 mark(p.x + kMarkW * 0.5f + 2.0f, p.y + kRowH * 0.5f);
    if (r.pending)       ui::spinner(dl, mark, 7.0f, static_cast<float>(t), ImGui::GetStyle().Alpha);
    else if (r.selected) icons::check(dl, mark, 16.0f, C(th::kAccent));

    const UiFonts& f = ui_fonts();
    const float tx = p.x + kMarkW + 4.0f;
    dl->PushClipRect(p, q, true);
    dl->AddText(f.ui, kTitleFs, ImVec2(tx, p.y + 7.0f),
                C(r.selected || r.pending ? th::kText : th::alpha(th::kText, 0.88f)), r.title.c_str());
    const std::string detail = r.pending ? (r.detail.empty() ? "Loading…" : r.detail + " · Loading…")
                                         : r.detail;
    dl->AddText(f.ui_small, kSmallFs, ImVec2(tx, p.y + 26.0f),
                C(r.pending ? th::kWarn : th::kTextDim), detail.c_str());
    dl->PopClipRect();

    if (hovered && !r.tip.empty()) ui::tooltip(r.tip.c_str());
    return clicked;
}

void column_header(const char* label, int count)
{
    ImDrawList*    dl = ImGui::GetWindowDrawList();
    const UiFonts& f  = ui_fonts();
    const ImVec2   p  = ImGui::GetCursorScreenPos();
    dl->AddText(f.ui_small, kSmallFs, ImVec2(p.x + 4.0f, p.y), C(th::kTextFaint), label);
    if (count >= 0) {
        const float lw = f.ui_small->CalcTextSizeA(kSmallFs, FLT_MAX, 0.0f, label).x;
        const std::string n = std::to_string(count);
        dl->AddText(f.ui_small, kSmallFs, ImVec2(p.x + 4.0f + lw + 6.0f, p.y),
                    C(th::alpha(th::kTextFaint, 0.7f)), n.c_str());
    }
    ImGui::Dummy(ImVec2(0.0f, kSmallFs + 8.0f));
}

} // namespace

TrackPanelOut draw_track_panel(const TrackPanelInputs& in)
{
    TrackPanelOut out;
    static const std::vector<VideoTrackInfo>    kNoV;
    static const std::vector<AudioTrackInfo>    kNoA;
    static const std::vector<SubtitleTrackInfo> kNoS;
    const auto& vt = in.video    ? *in.video    : kNoV;
    const auto& at = in.audio    ? *in.audio    : kNoA;
    const auto& st = in.subtitle ? *in.subtitle : kNoS;

    const auto groups = group_video_tracks(vt, in.cur_video);

    // Columns: Video, Audio, Subtitles — empty ones are left out.
    const bool show_v = !groups.empty(), show_a = !at.empty(), show_s = !st.empty();
    const int  n_cols = static_cast<int>(show_v) + static_cast<int>(show_a) + static_cast<int>(show_s);

    const float avail_w = std::max(200.0f, in.win_w - in.right_inset);
    const float col_w   = 250.0f;
    const float pad_x   = 20.0f;
    const float card_w  = std::min(avail_w - 32.0f,
                                   pad_x * 2.0f + std::max(1, n_cols) * col_w
                                   + std::max(0, n_cols - 1) * kColGap);
    const int   max_rows = std::max({ static_cast<int>(groups.size()), static_cast<int>(at.size()),
                                      static_cast<int>(st.size()) + 1 });
    // Lists scroll once the card would pass ~80% of the window height.
    const float list_h  = std::min(static_cast<float>(max_rows) * (kRowH + 2.0f),
                                   std::max(kRowH * 3.0f, in.win_h * 0.78f - 130.0f));

    ImGui::SetNextWindowPos(ImVec2(avail_w * 0.5f, in.win_h * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(card_w, 0.0f), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(pad_x, 16.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, th::kRadiusPanel);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(kColGap, 2.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, th::vec4(th::kPanel));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    if (ImGui::Begin("##trackpanel", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGui::PushFont(ui_fonts().title);
        ImGui::TextUnformatted("Tracks");
        ImGui::PopFont();
        {   // Close button (top-right), as on the help card.
            const ImVec2 wp = ImGui::GetWindowPos();
            const float  ww = ImGui::GetWindowWidth();
            const ImVec2 save = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImVec2(wp.x + ww - 12.0f - th::kBtn, wp.y + 10.0f));
            auto cb = ui::icon_button("##trackclose", "Close (T)");
            const float r = 5.0f;
            dl->AddLine(ImVec2(cb.c.x - r, cb.c.y - r), ImVec2(cb.c.x + r, cb.c.y + r), cb.col, 2.0f);
            dl->AddLine(ImVec2(cb.c.x + r, cb.c.y - r), ImVec2(cb.c.x - r, cb.c.y + r), cb.col, 2.0f);
            if (cb.clicked) out.close = true;
            ImGui::SetCursorScreenPos(save);
        }
        ImGui::Dummy(ImVec2(0, 10.0f));

        if (n_cols == 0) {
            ImGui::PushStyleColor(ImGuiCol_Text, th::vec4(th::kTextDim));
            ImGui::TextUnformatted("No selectable tracks.");
            ImGui::PopStyleColor();
        } else if (ImGui::BeginTable("##tracks", n_cols, ImGuiTableFlags_SizingStretchSame)) {
            auto list = [&](const char* id, auto&& body) {
                ImGui::BeginChild(id, ImVec2(0.0f, list_h), ImGuiChildFlags_None,
                                  ImGuiWindowFlags_NoNav);
                body(ImGui::GetContentRegionAvail().x);
                ImGui::EndChild();
            };

            if (show_v) {
                ImGui::TableNextColumn();
                column_header("VIDEO", static_cast<int>(groups.size()));
                list("##v", [&](float w) {
                    for (const auto& g : groups) {
                        Row r{ g.title, g.detail };
                        r.selected = std::find(g.members.begin(), g.members.end(), in.cur_video) != g.members.end();
                        r.pending  = in.pending_video >= 0 &&
                                     std::find(g.members.begin(), g.members.end(), in.pending_video) != g.members.end();
                        if (g.members.size() > 1) {
                            r.tip = std::format("{} identical streams (one per audio group):", g.members.size());
                            for (int m : g.members) r.tip += std::format(" #{}", m);
                        }
                        const std::string id = std::format("##v{}", g.stream_idx);
                        if (track_row(id.c_str(), r, w, in.t) && !r.selected) out.video = g.stream_idx;
                    }
                });
            }
            if (show_a) {
                ImGui::TableNextColumn();
                column_header("AUDIO", static_cast<int>(at.size()));
                list("##a", [&](float w) {
                    for (const auto& a : at) {
                        Row r{ audio_track_title(a), audio_track_detail(a) };
                        r.selected = a.stream_idx == in.cur_audio;
                        r.pending  = a.stream_idx == in.pending_audio;
                        const std::string id = std::format("##a{}", a.stream_idx);
                        if (track_row(id.c_str(), r, w, in.t) && !r.selected) out.audio = a.stream_idx;
                    }
                });
            }
            if (show_s) {
                ImGui::TableNextColumn();
                column_header("SUBTITLES", static_cast<int>(st.size()));
                list("##s", [&](float w) {
                    Row off{ "Off", "" };
                    off.selected = in.cur_subtitle < 0;
                    if (track_row("##soff", off, w, in.t) && !off.selected) out.subtitle = -2;
                    for (const auto& s : st) {
                        Row r{ subtitle_track_title(s), subtitle_track_detail(s) };
                        r.selected = s.stream_idx == in.cur_subtitle;
                        r.pending  = s.stream_idx == in.pending_subtitle;
                        const std::string id = std::format("##s{}", s.stream_idx);
                        if (track_row(id.c_str(), r, w, in.t) && !r.selected) out.subtitle = s.stream_idx;
                    }
                });
            }
            ImGui::EndTable();
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
    return out;
}
