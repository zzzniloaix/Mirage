#include "LoudnessPanel.h"
#include "UiFonts.h"
#include "UiTheme.h"
#include "UiWidgets.h"

#include "imgui.h"
#include "imgui_internal.h"   // GImGui->MovingWindow: the card is being dragged

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string>

namespace th = ui::theme;

namespace {

ImU32 C(ImU32 c, float mul = 1.0f) { return ImGui::GetColorU32(c, mul); }

constexpr float kCardW = 400.0f;
constexpr ImU32 kGood  = th::rgba( 74, 222, 128, 1.00f);   // within the target
constexpr ImU32 kShort = th::rgba( 56, 189, 248, 1.00f);   // short-term line
constexpr ImU32 kMom   = th::rgba(255, 255, 255, 0.35f);   // momentary line

// Delivery targets: programme loudness, its tolerance, the true-peak ceiling.
struct Target { const char* chip; const char* tip; double lufs, tol, tp; };
constexpr Target kTargets[] = {
    { "R128 -23",  "EBU R128: -23.0 LUFS ±0.5 LU, true peak <= -1 dBTP",       -23.0, 0.5, -1.0 },
    { "A/85 -24",  "ATSC A/85: -24 LKFS ±2 LU, true peak <= -2 dBTP",           -24.0, 2.0, -2.0 },
    { "-16",       "Streaming: -16 LUFS ±1 LU, true peak <= -1 dBTP",           -16.0, 1.0, -1.0 },
    { "-14",       "Streaming: -14 LUFS ±1 LU, true peak <= -1 dBTP",           -14.0, 1.0, -1.0 },
};
constexpr int kTargetCount = static_cast<int>(sizeof kTargets / sizeof kTargets[0]);

std::string lufs_text(float v) { return std::isfinite(v) ? std::format("{:.1f}", v) : std::string("—"); }

bool chip_button(const char* id, const char* text, bool on, const char* tip)
{
    ImFont*      f  = ui_fonts().ui_small;
    const ImVec2 ts = f->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, text);
    const ImVec2 sz(ts.x + 16.0f, 22.0f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, sz);
    const bool hov     = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y),
                      C(on ? th::alpha(th::kAccent, 0.22f) : hov ? th::kHoverBg : th::rgba(255, 255, 255, 0.05f)),
                      th::kRadiusChip);
    dl->AddText(f, 12.0f, ImVec2(p.x + 8.0f, p.y + (sz.y - ts.y) * 0.5f), C(on ? th::kAccent : th::kTextDim), text);
    if (hov && tip) ui::tooltip(tip);
    return clicked;
}

// One big reading: a small label over the value.
void reading(ImDrawList* dl, ImVec2 p, const char* label, const std::string& value, ImU32 col)
{
    const UiFonts& f = ui_fonts();
    dl->AddText(f.ui_small, 11.0f, p, C(th::kTextFaint), label);
    dl->AddText(f.title, 22.0f, ImVec2(p.x, p.y + 14.0f), C(col), value.c_str());
}

} // namespace

LoudnessOut LoudnessPanel::draw(const LoudnessInputs& in)
{
    LoudnessOut out;
    const float avail_w = std::max(240.0f, in.win_w - in.right_inset);
    const float card_w  = std::min(kCardW, avail_w - 32.0f);
    const float inner_w = card_w - 36.0f;
    const Target& tg = kTargets[std::clamp(target_, 0, kTargetCount - 1)];

    // Docked bottom right above the controls (it grows upward) until dragged.
    if (!moved_)
        ImGui::SetNextWindowPos(ImVec2(avail_w - 16.0f, in.win_h - in.bottom_inset - 12.0f), ImGuiCond_Always,
                                ImVec2(1.0f, 1.0f));
    ImGui::SetNextWindowSize(ImVec2(card_w, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(18.0f, 14.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, th::kRadiusPanel);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 6.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, th::vec4(th::kPanel));
    if (ImGui::Begin("##loudness", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize)) {
        if (ImGui::IsWindowAppearing()) moved_ = false;
        if (ImGui::GetCurrentContext()->MovingWindow == ImGui::GetCurrentWindow()) moved_ = true;
        ImDrawList*    dl = ImGui::GetWindowDrawList();
        const UiFonts& fo = ui_fonts();
        ImGui::PushFont(fo.title);
        ImGui::TextUnformatted("Loudness");
        ImGui::PopFont();
        const ImVec2 wp = ImGui::GetWindowPos();
        if (in.layout && !in.layout->name.empty()) {
            const std::string s = std::format("{} · {:g} kHz", in.layout->name, in.layout->rate / 1000.0);
            const float tw = fo.ui_small->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, s.c_str()).x;
            dl->AddText(fo.ui_small, 12.0f, ImVec2(wp.x + card_w - 18.0f - th::kBtn - 8.0f - tw, wp.y + 20.0f),
                        C(th::kTextDim), s.c_str());
        }
        {   // Close (top right).
            const ImVec2 save = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImVec2(wp.x + ImGui::GetWindowWidth() - 12.0f - th::kBtn, wp.y + 8.0f));
            auto cb = ui::icon_button("##ldclose", "Close (L)");
            const float r = 5.0f;
            dl->AddLine(ImVec2(cb.c.x - r, cb.c.y - r), ImVec2(cb.c.x + r, cb.c.y + r), cb.col, 2.0f);
            dl->AddLine(ImVec2(cb.c.x + r, cb.c.y - r), ImVec2(cb.c.x - r, cb.c.y + r), cb.col, 2.0f);
            if (cb.clicked) out.close = true;
            ImGui::SetCursorScreenPos(save);
        }
        for (int i = 0; i < kTargetCount; ++i) {
            if (i > 0) ImGui::SameLine();
            const std::string id = "##ldt" + std::to_string(i);
            if (chip_button(id.c_str(), kTargets[i].chip, i == target_, kTargets[i].tip)) target_ = i;
        }
        ImGui::SameLine(0.0f, 12.0f);
        if (chip_button("##ldreset", "Reset", false, "Restart integrated loudness, LRA and the true-peak maximum"))
            out.reset = true;

        if (!in.has_audio || (in.layout && !in.layout->error.empty()) || !in.block) {
            ImGui::PushFont(fo.ui_small);
            ImGui::TextColored(th::vec4(th::kTextDim), "%s",
                               !in.has_audio ? "No audio track."
                               : (in.layout && !in.layout->error.empty()) ? in.layout->error.c_str()
                               : "Measuring… (from the next audio decoded)");
            ImGui::PopFont();
        } else {
            const loudness::Block& b = *in.block;
            // ── Readings ────────────────────────────────────────────────────
            const ImVec2 r0 = ImGui::GetCursorScreenPos();
            const bool   i_ok = std::isfinite(b.integrated) && std::abs(b.integrated - tg.lufs) <= tg.tol;
            reading(dl, r0, "MOMENTARY", lufs_text(b.momentary), th::kText);
            reading(dl, ImVec2(r0.x + inner_w * 0.34f, r0.y), "SHORT-TERM", lufs_text(b.short_term), th::kText);
            reading(dl, ImVec2(r0.x + inner_w * 0.68f, r0.y), "INTEGRATED", lufs_text(b.integrated),
                    !std::isfinite(b.integrated) ? th::kTextDim : i_ok ? kGood : th::kWarn);
            ImGui::Dummy(ImVec2(inner_w, 46.0f));
            ImGui::PushFont(fo.ui_small);
            const bool tp_over = std::isfinite(b.true_peak_max) && b.true_peak_max > tg.tp;
            ImGui::TextColored(th::vec4(th::kTextDim), "LUFS   ·   LRA %.1f LU   ·", b.lra);
            ImGui::SameLine(0.0f, 4.0f);
            ImGui::TextColored(th::vec4(tp_over ? th::kError : th::kTextDim), "true peak max %s dBTP",
                               lufs_text(b.true_peak_max).c_str());
            std::string verdict;
            if (!std::isfinite(b.integrated)) verdict = "Integrated: not enough audio above the gates yet";
            else if (i_ok) verdict = std::format("Integrated within ±{:g} LU of {:g}", tg.tol, tg.lufs);
            else verdict = std::format("Integrated {:+.1f} LU from {:g}", b.integrated - tg.lufs, tg.lufs);
            if (tp_over) verdict += std::format(" · true peak over {:g} dBTP", tg.tp);
            ImGui::TextColored(th::vec4(i_ok && !tp_over ? kGood : th::kWarn), "%s", verdict.c_str());
            ImGui::TextColored(th::vec4(th::kTextFaint), "integrated and LRA since %.1f s (seeks and Reset restart them)",
                               in.layout ? in.layout->since : 0.0);
            ImGui::PopFont();

            // ── A minute of history: short-term, momentary, the target ──────
            const ImVec2 h0 = ImGui::GetCursorScreenPos();
            const ImVec2 hs(inner_w, 90.0f);
            ImGui::Dummy(hs);
            dl->AddRectFilled(h0, ImVec2(h0.x + hs.x, h0.y + hs.y), C(th::rgba(0, 0, 0, 0.5f)), th::kRadiusSmall);
            constexpr double kLo = -50.0, kHi = 0.0, kSpan = 60.0;
            auto y_of = [&](double v) {
                return h0.y + static_cast<float>((kHi - std::clamp(v, kLo, kHi)) / (kHi - kLo)) * hs.y;
            };
            auto x_of = [&](double t) { return h0.x + static_cast<float>((t - (in.now_pts - kSpan)) / kSpan) * hs.x; };
            for (double v : { -10.0, -20.0, -30.0, -40.0 }) {
                dl->AddLine(ImVec2(h0.x, y_of(v)), ImVec2(h0.x + hs.x, y_of(v)), C(th::rgba(255, 255, 255, 0.06f)), 1.0f);
                dl->AddText(fo.ui_small, 10.0f, ImVec2(h0.x + 3.0f, y_of(v) - 11.0f), C(th::kTextFaint),
                            std::format("{:g}", v).c_str());
            }
            dl->AddLine(ImVec2(h0.x, y_of(tg.lufs)), ImVec2(h0.x + hs.x, y_of(tg.lufs)), C(th::alpha(kGood, 0.6f)), 1.0f);
            if (in.history && in.history->size() >= 2) {
                std::vector<ImVec2> s, m;
                for (const auto& h : *in.history) {
                    if (h.end_pts < in.now_pts - kSpan || h.end_pts > in.now_pts) continue;
                    if (std::isfinite(h.short_term)) s.emplace_back(x_of(h.end_pts), y_of(h.short_term));
                    if (std::isfinite(h.momentary))  m.emplace_back(x_of(h.end_pts), y_of(h.momentary));
                }
                if (m.size() >= 2) dl->AddPolyline(m.data(), static_cast<int>(m.size()), C(kMom), 0, 1.0f);
                if (s.size() >= 2) dl->AddPolyline(s.data(), static_cast<int>(s.size()), C(kShort), 0, 1.8f);
            }
            dl->AddText(fo.ui_small, 10.0f, ImVec2(h0.x + hs.x - 110.0f, h0.y + 3.0f), C(th::kTextFaint),
                        "short-term · momentary");

            // ── Channels: peak (bar), RMS (inner), true peak (tick) ──────────
            const int nch = std::min(b.channels, loudness::kMaxChannels);
            if (nch > 0) {
                // Peak hold over the last second of blocks, so the bars don't flicker.
                std::array<float, loudness::kMaxChannels> hold = b.peak;
                if (in.history)
                    for (const auto& h : *in.history)
                        if (h.end_pts > in.now_pts - 1.0 && h.end_pts <= in.now_pts)
                            for (int c = 0; c < nch; ++c) hold[static_cast<std::size_t>(c)] = std::max(hold[static_cast<std::size_t>(c)], h.peak[static_cast<std::size_t>(c)]);
                const ImVec2 m0 = ImGui::GetCursorScreenPos();
                const float  mh = 86.0f, label_h = 14.0f;
                ImGui::Dummy(ImVec2(inner_w, mh + label_h));
                const float colw = std::min(34.0f, inner_w / static_cast<float>(nch));
                auto y_db = [&](float db) {
                    const float f = std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
                    return m0.y + mh - f * mh;
                };
                for (float db : { -6.0f, -18.0f, -40.0f })
                    dl->AddLine(ImVec2(m0.x, y_db(db)), ImVec2(m0.x + colw * nch, y_db(db)), C(th::rgba(255, 255, 255, 0.06f)), 1.0f);
                for (int c = 0; c < nch; ++c) {
                    const auto  cu = static_cast<std::size_t>(c);
                    const float x0 = m0.x + c * colw + 4.0f, x1 = m0.x + (c + 1) * colw - 4.0f;
                    dl->AddRectFilled(ImVec2(x0, m0.y), ImVec2(x1, m0.y + mh), C(th::rgba(255, 255, 255, 0.05f)), 2.0f);
                    const float pk = hold[cu];
                    const ImU32 col = pk > -1.0f ? th::kError : pk > -9.0f ? th::kWarn : kGood;
                    if (std::isfinite(pk))
                        dl->AddRectFilled(ImVec2(x0, y_db(pk)), ImVec2(x1, m0.y + mh), C(th::alpha(col, 0.55f)), 2.0f);
                    if (std::isfinite(b.rms[cu]))
                        dl->AddRectFilled(ImVec2(x0 + 4.0f, y_db(b.rms[cu])), ImVec2(x1 - 4.0f, m0.y + mh), C(col), 1.0f);
                    if (std::isfinite(b.true_peak[cu]))
                        dl->AddLine(ImVec2(x0, y_db(b.true_peak[cu])), ImVec2(x1, y_db(b.true_peak[cu])), C(th::kText), 1.0f);
                    const std::string lab = (in.layout && cu < in.layout->labels.size()) ? in.layout->labels[cu]
                                                                                           : std::format("{}", c + 1);
                    const bool excluded = in.layout && cu < in.layout->weights.size()
                                       && in.layout->weights[cu] == loudness::Weight::Excluded;
                    const float lw = fo.ui_small->CalcTextSizeA(10.0f, FLT_MAX, 0.0f, lab.c_str()).x;
                    dl->AddText(fo.ui_small, 10.0f, ImVec2((x0 + x1 - lw) * 0.5f, m0.y + mh + 2.0f),
                                C(excluded ? th::kTextFaint : th::kTextDim), lab.c_str());
                    if (ImGui::IsMouseHoveringRect(ImVec2(x0, m0.y), ImVec2(x1, m0.y + mh + label_h)))
                        ui::tooltip(std::format("{}{}\npeak {} dBFS (1 s hold) · RMS {} dBFS · true peak {} dBTP", lab,
                                                excluded ? " — not counted in loudness (LFE)" : "",
                                                lufs_text(pk), lufs_text(b.rms[cu]), lufs_text(b.true_peak[cu])).c_str());
                }
                dl->AddText(fo.ui_small, 10.0f, ImVec2(m0.x + colw * nch + 8.0f, y_db(-6.0f) - 6.0f), C(th::kTextFaint), "-6");
                dl->AddText(fo.ui_small, 10.0f, ImVec2(m0.x + colw * nch + 8.0f, y_db(-18.0f) - 6.0f), C(th::kTextFaint), "-18");
                dl->AddText(fo.ui_small, 10.0f, ImVec2(m0.x + colw * nch + 8.0f, y_db(-40.0f) - 6.0f), C(th::kTextFaint), "-40 dBFS");
            }

            // ── Phase correlation of the first two channels ──────────────────
            if (nch >= 2) {
                ImGui::PushFont(fo.ui_small);
                const std::string l0 = in.layout && in.layout->labels.size() >= 2
                    ? in.layout->labels[0] + "/" + in.layout->labels[1] : std::string("1/2");
                ImGui::TextColored(th::vec4(th::kTextDim), "Correlation %s", l0.c_str());
                ImGui::PopFont();
                const ImVec2 c0 = ImGui::GetCursorScreenPos();
                const ImVec2 cs(inner_w, 10.0f);
                ImGui::Dummy(cs);
                dl->AddRectFilled(c0, ImVec2(c0.x + cs.x, c0.y + cs.y), C(th::rgba(255, 255, 255, 0.06f)), 3.0f);
                dl->AddLine(ImVec2(c0.x + cs.x * 0.5f, c0.y), ImVec2(c0.x + cs.x * 0.5f, c0.y + cs.y), C(th::kTextFaint), 1.0f);
                if (std::isfinite(b.correlation)) {
                    const float x = c0.x + cs.x * (0.5f + 0.5f * std::clamp(b.correlation, -1.0f, 1.0f));
                    const ImU32 col = b.correlation < 0.0f ? th::kError : b.correlation < 0.3f ? th::kWarn : kGood;
                    dl->AddRectFilled(ImVec2(x - 3.0f, c0.y - 2.0f), ImVec2(x + 3.0f, c0.y + cs.y + 2.0f), C(col), 2.0f);
                }
                dl->AddText(fo.ui_small, 10.0f, ImVec2(c0.x, c0.y + cs.y + 2.0f), C(th::kTextFaint), "-1");
                dl->AddText(fo.ui_small, 10.0f, ImVec2(c0.x + cs.x - 10.0f, c0.y + cs.y + 2.0f), C(th::kTextFaint), "+1");
                ImGui::Dummy(ImVec2(0.0f, 8.0f));
            }
        }
        if (ImGui::IsWindowFocused() && !ImGui::IsAnyItemActive()) ImGui::SetWindowFocus(nullptr);
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    return out;
}
