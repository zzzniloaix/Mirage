#include "FrameInspector.h"
#include "UiFonts.h"
#include "UiTheme.h"
#include "UiWidgets.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <string>

namespace th = ui::theme;

namespace {

ImU32 C(ImU32 c, float mul = 1.0f) { return ImGui::GetColorU32(c, mul); }

constexpr float kCardW   = 420.0f;
constexpr float kLabelW  = 118.0f;

ImU32 level_colour(frameinfo::Level l)
{
    switch (l) {
    case frameinfo::Level::Dim:   return th::kTextFaint;
    case frameinfo::Level::Info:  return th::kAccent;
    case frameinfo::Level::Warn:  return th::kWarn;
    case frameinfo::Level::Error: return th::kError;
    default:                      return th::kText;
    }
}

// Section header: the title in faint capitals after a disclosure triangle,
// with the row count while closed. Click toggles; returns whether the
// section is open. The state is kept per title for the session.
bool section_header(const std::string& title, std::size_t rows, bool default_open)
{
    static std::map<std::string, bool> open_state;
    const auto it = open_state.try_emplace(title, default_open).first;

    std::string up = title;
    for (char& c : up) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (!it->second) up += "  " + std::to_string(rows);

    ImGui::Dummy(ImVec2(0.0f, 6.0f));
    const ImVec2 p = ImGui::GetCursorScreenPos();
    constexpr float kH = 16.0f;
    const std::string id = "##sec_" + title;
    if (ImGui::InvisibleButton(id.c_str(), ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), kH)))
        it->second = !it->second;
    ImDrawList* dl  = ImGui::GetWindowDrawList();
    const ImU32 col = C(ImGui::IsItemHovered() ? th::kTextDim : th::kTextFaint);
    const ImVec2 c(p.x + 4.0f, p.y + kH * 0.5f);
    if (it->second) dl->AddTriangleFilled(ImVec2(c.x - 4.0f, c.y - 2.0f), ImVec2(c.x + 4.0f, c.y - 2.0f),
                                          ImVec2(c.x, c.y + 3.0f), col);
    else            dl->AddTriangleFilled(ImVec2(c.x - 2.0f, c.y - 4.0f), ImVec2(c.x - 2.0f, c.y + 4.0f),
                                          ImVec2(c.x + 3.0f, c.y), col);
    dl->AddText(ui_fonts().ui_small, 12.0f, ImVec2(p.x + 14.0f, p.y + 1.0f), col, up.c_str());
    return it->second;
}

void rows_table(const char* id, const std::vector<frameinfo::Row>& rows)
{
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit)) return;
    ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, kLabelW);
    ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
    for (const auto& r : rows) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::PushStyleColor(ImGuiCol_Text, th::vec4(th::kTextDim));
        ImGui::TextWrapped("%s", r.label.c_str());
        ImGui::PopStyleColor();
        if (!r.tip.empty() && ImGui::IsItemHovered()) ui::tooltip(r.tip.c_str());
        ImGui::TableNextColumn();
        ImGui::PushStyleColor(ImGuiCol_Text, th::vec4(level_colour(r.level)));
        ImGui::TextWrapped("%s", r.value.c_str());
        ImGui::PopStyleColor();
        if (!r.tip.empty() && ImGui::IsItemHovered()) ui::tooltip(r.tip.c_str());
    }
    ImGui::EndTable();
}

// One finding: a coloured dot, "label: value" wrapped beside it.
void finding_row(const frameinfo::Row& r)
{
    ImDrawList*  dl = ImGui::GetWindowDrawList();
    const ImVec2 p  = ImGui::GetCursorScreenPos();
    const float  fs = ImGui::GetFontSize();
    dl->AddCircleFilled(ImVec2(p.x + 5.0f, p.y + fs * 0.5f + 1.0f), 3.5f, C(level_colour(r.level)));
    ImGui::SetCursorScreenPos(ImVec2(p.x + 16.0f, p.y));
    ImGui::PushTextWrapPos(0.0f);
    ImGui::TextColored(th::vec4(level_colour(r.level)), "%s:", r.label.c_str());
    ImGui::SameLine();
    ImGui::TextUnformatted(r.value.c_str());
    ImGui::PopTextWrapPos();
    if (!r.tip.empty() && ImGui::IsItemHovered()) ui::tooltip(r.tip.c_str());
}

} // namespace

FrameInspectorOut draw_frame_inspector(const FrameInspectorInputs& in)
{
    FrameInspectorOut out;
    const float avail_w = std::max(240.0f, in.win_w - in.right_inset);
    const float card_w  = std::min(kCardW, avail_w - 32.0f);
    const float max_h   = std::max(160.0f, in.win_h - in.top_inset - in.bottom_inset - 24.0f);

    // Top right under the top bar when it opens; movable while open.
    ImGui::SetNextWindowPos(ImVec2(avail_w - 16.0f - card_w, in.top_inset + 8.0f), ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(card_w, 0.0f), ImVec2(card_w, max_h));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 14.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, th::kRadiusPanel);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 3.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, th::vec4(th::kPanel));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    // NoNav: a focused card must not take the keyboard from the player's shortcuts.
    if (ImGui::Begin("##frameinspector", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_AlwaysAutoResize)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGui::PushFont(ui_fonts().title);
        ImGui::TextUnformatted("Frame");
        ImGui::PopFont();
        {   // Copy and close buttons (top right), as on the track panel.
            const ImVec2 wp   = ImGui::GetWindowPos();
            const float  ww   = ImGui::GetWindowWidth();
            const ImVec2 save = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImVec2(wp.x + ww - 12.0f - th::kBtn, wp.y + 8.0f));
            auto cb = ui::icon_button("##ficlose", "Close (P)");
            const float r = 5.0f;
            dl->AddLine(ImVec2(cb.c.x - r, cb.c.y - r), ImVec2(cb.c.x + r, cb.c.y + r), cb.col, 2.0f);
            dl->AddLine(ImVec2(cb.c.x + r, cb.c.y - r), ImVec2(cb.c.x - r, cb.c.y + r), cb.col, 2.0f);
            if (cb.clicked) out.close = true;
            ImGui::SetCursorScreenPos(ImVec2(wp.x + ww - 12.0f - 2.0f * th::kBtn, wp.y + 8.0f));
            auto cp = ui::icon_button("##ficopy", "Copy as text", in.report != nullptr);
            // Two overlapping sheets.
            dl->AddRect(ImVec2(cp.c.x - 6.0f, cp.c.y - 3.0f), ImVec2(cp.c.x + 3.0f, cp.c.y + 7.0f), cp.col, 2.0f, 0, 1.6f);
            dl->AddRect(ImVec2(cp.c.x - 3.0f, cp.c.y - 7.0f), ImVec2(cp.c.x + 6.0f, cp.c.y + 3.0f), cp.col, 2.0f, 0, 1.6f);
            if (cp.clicked && in.report) {
                ImGui::SetClipboardText(in.report->text.c_str());
                out.copied = true;
            }
            ImGui::SetCursorScreenPos(save);
        }

        if (!in.report) {
            ImGui::PushStyleColor(ImGuiCol_Text, th::vec4(th::kTextDim));
            ImGui::TextUnformatted("No frame on screen yet.");
            ImGui::PopStyleColor();
        } else {
            const frameinfo::Report& rep = *in.report;
            // Subtitle: the first rows of the Frame section, on one line.
            if (!rep.sections.empty()) {
                std::string sub;
                for (const auto& r : rep.sections.front().rows) {
                    if (r.label != "Type" && r.label != "Number" && r.label != "PTS") continue;
                    if (!sub.empty()) sub += "  ·  ";
                    sub += r.label == "Number" ? "#" + r.value : r.value;
                }
                ImGui::PushFont(ui_fonts().mono);
                ImGui::PushStyleColor(ImGuiCol_Text, th::vec4(th::kTextDim));
                ImGui::TextUnformatted(sub.c_str());
                ImGui::PopStyleColor();
                ImGui::PopFont();
            }
            ImGui::Dummy(ImVec2(0.0f, 4.0f));

            // The body scrolls once the card reaches the space it has.
            const float used = ImGui::GetCursorPosY() + 14.0f;
            ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f), ImVec2(FLT_MAX, std::max(80.0f, max_h - used)));
            if (ImGui::BeginChild("##fibody", ImVec2(card_w - 36.0f, 0.0f), ImGuiChildFlags_AutoResizeY,
                                  ImGuiWindowFlags_NoNav)) {
                if (!rep.findings.empty()) {
                    for (const auto& r : rep.findings) finding_row(r);
                    ImGui::Dummy(ImVec2(0.0f, 2.0f));
                }
                int n = 0;
                for (const auto& s : rep.sections) {
                    // x265 lists well over a hundred options: closed until asked for.
                    const bool open_default = !(s.title == "Encoder settings" && s.rows.size() > 12);
                    const std::string id = "##fis" + std::to_string(n++);
                    if (section_header(s.title, s.rows.size(), open_default)) rows_table(id.c_str(), s.rows);
                }
            }
            ImGui::EndChild();
        }
        // Leave no keyboard focus here (see NetInspector::draw).
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !ImGui::IsAnyItemActive())
            ImGui::SetWindowFocus(nullptr);
    }
    ImGui::End();
    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(4);
    return out;
}
