#include "FramesPanel.h"
#include "UiFonts.h"
#include "UiTheme.h"
#include "UiWidgets.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace th = ui::theme;

// ── Model ─────────────────────────────────────────────────────────────────────

void FramesModel::start(const std::string& url, int stream, const std::string& master)
{
    scan_.start(url, stream, master);
    read_.clear();
    shown_.clear();
    bitrate_.clear();
    summary_      = {};
    info_.reset();
    seen_version_ = 0;
    built_at_     = -1.0;
    built_count_  = 0;
    started_      = true;
    stream_       = stream;
}

void FramesModel::update(double now_s, double rebuild_s)
{
    if (!started_) return;
    if (const std::uint64_t v = scan_.version(); v != seen_version_) {
        seen_version_ = v;
        scan_.copy(read_, read_.size());
        if (!info_) info_ = scan_.info();
    }
    const bool ended = scan_.done();
    if (read_.size() == built_count_ || (!ended && built_at_ >= 0.0 && now_s - built_at_ < rebuild_s)) return;

    // Sorting is O(n log n): not for every batch of a long scan.
    shown_.clear();
    for (const auto& f : framescan::display_order(read_))
        if (!f.discard) shown_.push_back(f);
    bitrate_     = framescan::rolling_bitrate(shown_, 1.0);
    summary_     = framescan::summarize(shown_, 1.0);
    built_at_    = now_s;
    built_count_ = read_.size();
    info_        = scan_.info();
}

// ── Panel ─────────────────────────────────────────────────────────────────────

namespace {

ImU32 C(ImU32 c, float mul = 1.0f) { return ImGui::GetColorU32(c, mul); }

constexpr ImU32 kTypeI  = th::rgba(255,  97, 110, 1.00f);
constexpr ImU32 kTypeP  = th::rgba( 56, 189, 248, 1.00f);
constexpr ImU32 kTypeB  = th::rgba(167, 139, 250, 1.00f);
constexpr ImU32 kTypeBn = th::rgba(167, 139, 250, 0.50f);   // B nothing refers to (H.264)
constexpr ImU32 kTypeS  = th::rgba(251, 191,  36, 1.00f);
constexpr ImU32 kTypeU  = th::rgba(180, 180, 190, 0.85f);
constexpr ImU32 kRate   = th::rgba(250, 204,  21, 0.95f);

ImU32 type_colour(const framescan::Frame& f)
{
    switch (f.type) {
    case 'I': return kTypeI;
    case 'P': return kTypeP;
    case 'B': return f.ref == 0 ? kTypeBn : kTypeB;
    case 'S': return kTypeS;
    default:  return kTypeU;
    }
}

std::string bytes_text(double b)
{
    if (b >= 1e6) return std::format("{:.2f} MB", b / 1e6);
    if (b >= 1e3) return std::format("{:.1f} kB", b / 1e3);
    return std::format("{:.0f} B", b);
}

std::string rate_text(double bps)
{
    if (bps >= 1e6) return std::format("{:.2f} Mb/s", bps / 1e6);
    return std::format("{:.0f} kb/s", bps / 1e3);
}

// The frame on screen: the last one shown at or before `pts`.
int frame_at(const std::vector<framescan::Frame>& fr, double pts)
{
    const auto it = std::upper_bound(fr.begin(), fr.end(), pts + 1e-6,
                                     [](double t, const framescan::Frame& f) { return t < f.pts; });
    return std::clamp(static_cast<int>(it - fr.begin()) - 1, 0, std::max(0, static_cast<int>(fr.size()) - 1));
}

} // namespace

FramesPanelOut FramesPanel::draw(const FramesPanelInputs& in)
{
    FramesPanelOut out;
    const FramesModel* m = in.model;
    const float avail_w = std::max(360.0f, in.win_w - in.right_inset);
    const float card_w  = avail_w - 32.0f;
    constexpr float kPad = 16.0f, kChartH = 140.0f, kOverH = 30.0f;

    ImGui::SetNextWindowPos(ImVec2(16.0f, in.win_h - in.bottom_inset - 10.0f), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSize(ImVec2(card_w, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kPad, 12.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, th::kRadiusPanel);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 6.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, th::vec4(th::kPanel));
    if (ImGui::Begin("##frames", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImDrawList*    dl = ImGui::GetWindowDrawList();
        const UiFonts& fo = ui_fonts();
        const float inner_w = card_w - 2.0f * kPad;

        // ── Header: title, summary, close ───────────────────────────────────
        ImGui::PushFont(fo.ui);
        ImGui::TextUnformatted("Frames");
        ImGui::PopFont();
        if (m && m->started() && !m->frames().empty()) {
            const auto& s = m->summary();
            ImGui::PushFont(fo.ui_small);
            ImGui::SameLine(0.0f, 14.0f);
            ImGui::AlignTextToFramePadding();
            const std::string codec = m->info() ? m->info()->codec : std::string("?");
            ImGui::TextColored(th::vec4(th::kTextDim), "%s · %zu frames ·", codec.c_str(), s.frames);
            auto count = [](const char* label, std::size_t n, ImU32 col) {
                ImGui::SameLine(0.0f, 5.0f);
                ImGui::TextColored(th::vec4(col), "%s %zu", label, n);
            };
            count("I", s.i, kTypeI);
            count("P", s.p, kTypeP);
            count("B", s.b, kTypeB);
            if (s.s) count("S", s.s, kTypeS);
            if (s.unknown) count("?", s.unknown, kTypeU);
            ImGui::SameLine(0.0f, 5.0f);
            std::string tail;
            if (s.intervals > 0)
                tail += std::format("· GOP {:.1f} frames ({}–{}), {:.2f} s ", s.avg_frames, s.min_frames,
                                    s.max_frames, s.avg_s);
            tail += std::format("· {} mean · {} peak (1 s)", rate_text(s.mean_bps), rate_text(s.peak_bps));
            if (!m->info() || !m->info()->parser) tail += " · no parser: types unknown";
            if (m->state() == FrameScan::State::Running)
                tail += std::format(" · scanning {:.0f}%", m->progress() * 100.0);
            ImGui::TextColored(th::vec4(th::kTextDim), "%s", tail.c_str());
            ImGui::PopFont();
        }
        {
            const ImVec2 wp = ImGui::GetWindowPos();
            const ImVec2 save = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImVec2(wp.x + ImGui::GetWindowWidth() - 10.0f - th::kBtn, wp.y + 4.0f));
            auto cb = ui::icon_button("##frclose", "Close (B)");
            const float r = 5.0f;
            dl->AddLine(ImVec2(cb.c.x - r, cb.c.y - r), ImVec2(cb.c.x + r, cb.c.y + r), cb.col, 2.0f);
            dl->AddLine(ImVec2(cb.c.x + r, cb.c.y - r), ImVec2(cb.c.x - r, cb.c.y + r), cb.col, 2.0f);
            if (cb.clicked) out.close = true;
            ImGui::SetCursorScreenPos(save);
        }

        // ── Nothing to draw yet ──────────────────────────────────────────────
        if (!m || !m->started() || m->frames().empty()) {
            ImGui::PushFont(fo.ui_small);
            if (in.network && (!m || !m->started())) {
                ImGui::TextColored(th::vec4(th::kTextDim),
                                   "A scan reads the whole stream: for HLS, every segment of this variant is downloaded.");
                if (ImGui::Button("Scan this stream")) out.start_scan = true;
            } else if (m && m->state() == FrameScan::State::Failed) {
                ImGui::TextColored(th::vec4(th::kError), "Scan failed: %s", m->error().c_str());
            } else {
                ImGui::TextColored(th::vec4(th::kTextDim), "Scanning the stream… %.0f%%", m ? m->progress() * 100.0 : 0.0);
            }
            ImGui::PopFont();
        } else {
            const auto& fr   = m->frames();
            const auto& rate = m->bitrate();
            const int   n    = static_cast<int>(fr.size());
            const int   cur  = frame_at(fr, in.cur_pts);

            // ── Chart: the frames around the one on screen ───────────────────
            const ImVec2 c0 = ImGui::GetCursorScreenPos();
            const ImVec2 cs(inner_w, kChartH);
            ImGui::InvisibleButton("##frchart", cs);
            const bool hovered = ImGui::IsItemHovered();
            const ImGuiIO& io = ImGui::GetIO();
            if (hovered && io.MouseWheel != 0.0f)
                span_ = std::clamp(static_cast<int>(std::lround(span_ * std::pow(1.25, -io.MouseWheel))), 20,
                                   std::max(20, n));
            const int span  = std::min(std::max(span_, 1), n);
            const int first = std::clamp(cur - span / 2, 0, std::max(0, n - span));
            const int last  = first + span;   // exclusive

            std::uint32_t max_size = 1;
            double        max_rate = 1.0;
            for (int i = first; i < last; ++i) {
                max_size = std::max(max_size, fr[static_cast<std::size_t>(i)].size);
                if (std::isfinite(rate[static_cast<std::size_t>(i)])) max_rate = std::max(max_rate, rate[static_cast<std::size_t>(i)]);
            }
            const double ymax = max_size * 1.1, rmax = max_rate * 1.15;
            dl->AddRectFilled(c0, ImVec2(c0.x + cs.x, c0.y + cs.y), C(th::rgba(0, 0, 0, 0.55f)), th::kRadiusSmall);
            for (int g = 1; g <= 3; ++g) {   // size grid
                const float y = c0.y + cs.y * (1.0f - g / 4.0f);
                dl->AddLine(ImVec2(c0.x, y), ImVec2(c0.x + cs.x, y), C(th::rgba(255, 255, 255, 0.07f)), 1.0f);
            }

            const float bw = cs.x / static_cast<float>(span);
            auto x_of = [&](int i) { return c0.x + (i - first) * bw; };
            // Keyframe (GOP) boundaries behind the bars.
            for (int i = first; i < last; ++i)
                if (fr[static_cast<std::size_t>(i)].key)
                    dl->AddLine(ImVec2(x_of(i), c0.y), ImVec2(x_of(i), c0.y + cs.y), C(th::rgba(255, 255, 255, 0.22f)), 1.0f);
            if (bw >= 1.0f) {
                const float gap = bw >= 4.0f ? 1.0f : 0.0f;
                for (int i = first; i < last; ++i) {
                    const auto& f = fr[static_cast<std::size_t>(i)];
                    const float h = static_cast<float>(cs.y * f.size / ymax);
                    dl->AddRectFilled(ImVec2(x_of(i), c0.y + cs.y - h), ImVec2(x_of(i) + bw - gap, c0.y + cs.y),
                                      C(type_colour(f)));
                }
            } else {
                // More frames than pixels: per column the largest frame, in the colour of
                // the "strongest" type among them (I over P over B).
                const int cols = static_cast<int>(cs.x);
                for (int x = 0; x < cols; ++x) {
                    const int a = first + x * span / cols, b = std::max(a + 1, first + (x + 1) * span / cols);
                    std::uint32_t big = 0;
                    const framescan::Frame* pick = nullptr;
                    for (int i = a; i < b && i < last; ++i) {
                        const auto& f = fr[static_cast<std::size_t>(i)];
                        big = std::max(big, f.size);
                        auto rank = [](char t) { return t == 'I' ? 3 : t == 'P' ? 2 : t == 'B' ? 1 : 0; };
                        if (!pick || rank(f.type) > rank(pick->type)) pick = &f;
                    }
                    if (!pick) continue;
                    const float h = static_cast<float>(cs.y * big / ymax);
                    dl->AddLine(ImVec2(c0.x + x + 0.5f, c0.y + cs.y), ImVec2(c0.x + x + 0.5f, c0.y + cs.y - h),
                                C(type_colour(*pick)), 1.0f);
                }
            }
            // Rolling one-second bitrate over the bars.
            {
                std::vector<ImVec2> pts;
                const int step = std::max(1, span / std::max(1, static_cast<int>(cs.x)));
                for (int i = first; i < last; i += step) {
                    const double r = rate[static_cast<std::size_t>(i)];
                    if (!std::isfinite(r)) continue;
                    pts.emplace_back(x_of(i) + bw * 0.5f, c0.y + cs.y - static_cast<float>(cs.y * r / rmax));
                }
                if (pts.size() >= 2) dl->AddPolyline(pts.data(), static_cast<int>(pts.size()), C(kRate), 0, 1.5f);
            }
            // The frame on screen.
            {
                const float x = x_of(cur) + bw * 0.5f;
                dl->AddLine(ImVec2(x, c0.y), ImVec2(x, c0.y + cs.y), C(th::kText), 1.0f);
            }
            dl->AddText(fo.ui_small, 11.0f, ImVec2(c0.x + 4.0f, c0.y + 3.0f), C(th::kTextDim),
                        bytes_text(ymax / 1.1).c_str());
            {
                const std::string t = rate_text(max_rate);
                const float tw = fo.ui_small->CalcTextSizeA(11.0f, FLT_MAX, 0.0f, t.c_str()).x;
                dl->AddText(fo.ui_small, 11.0f, ImVec2(c0.x + cs.x - tw - 4.0f, c0.y + 3.0f), C(kRate), t.c_str());
            }
            // Hover: the frame under the mouse; click: seek to it.
            if (hovered) {
                const int i = std::clamp(first + static_cast<int>((io.MousePos.x - c0.x) / bw), first, last - 1);
                const auto& f = fr[static_cast<std::size_t>(i)];
                const float x = x_of(i);
                dl->AddRect(ImVec2(x - 0.5f, c0.y), ImVec2(x + std::max(bw, 1.0f) + 0.5f, c0.y + cs.y),
                            C(th::rgba(255, 255, 255, 0.6f)), 0.0f, 0, 1.0f);
                std::string tip = std::format("#{} · {}{}{}\n{} ({} bytes)", i, f.type, f.key ? " · keyframe" : "",
                                              f.ref == 0 ? " · not referenced" : "", bytes_text(f.size), f.size);
                if (std::isfinite(f.pts)) tip += std::format("\npts {:.3f} s", f.pts);
                if (std::isfinite(f.dts)) tip += std::format("  dts {:.3f} s", f.dts);
                if (std::isfinite(rate[static_cast<std::size_t>(i)]))
                    tip += std::format("\n1 s bitrate {}", rate_text(rate[static_cast<std::size_t>(i)]));
                tip += "\nclick: show this frame · wheel: zoom";
                ImGui::SetTooltip("%s", tip.c_str());
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && std::isfinite(f.pts)) out.seek_to = f.pts;
            }

            // ── Overview: the whole stream, the view's span marked ───────────
            const ImVec2 o0 = ImGui::GetCursorScreenPos();
            const ImVec2 os(inner_w, kOverH);
            ImGui::InvisibleButton("##froverview", os);
            const bool ohov = ImGui::IsItemHovered();
            dl->AddRectFilled(o0, ImVec2(o0.x + os.x, o0.y + os.y), C(th::rgba(0, 0, 0, 0.45f)), th::kRadiusSmall);
            const int ocols = std::max(1, static_cast<int>(os.x));
            if (static_cast<int>(over_.size()) != ocols || over_n_ != n) {
                over_.assign(static_cast<std::size_t>(ocols), 0.0);
                for (int x = 0; x < ocols; ++x) {
                    const int a = static_cast<int>(static_cast<long long>(x) * n / ocols);
                    const int b = std::max(a + 1, static_cast<int>(static_cast<long long>(x + 1) * n / ocols));
                    for (int i = a; i < b && i < n; ++i)
                        if (std::isfinite(rate[static_cast<std::size_t>(i)]))
                            over_[static_cast<std::size_t>(x)] = std::max(over_[static_cast<std::size_t>(x)], rate[static_cast<std::size_t>(i)]);
                }
                over_max_ = std::max(1.0, *std::max_element(over_.begin(), over_.end()));
                over_n_   = n;
            }
            for (int x = 0; x < ocols; ++x) {
                const float h = static_cast<float>((os.y - 2.0f) * over_[static_cast<std::size_t>(x)] / over_max_);
                if (h > 0.0f)
                    dl->AddLine(ImVec2(o0.x + x + 0.5f, o0.y + os.y), ImVec2(o0.x + x + 0.5f, o0.y + os.y - h),
                                C(th::rgba(250, 204, 21, 0.55f)), 1.0f);
            }
            const float vx0 = o0.x + os.x * first / n, vx1 = o0.x + os.x * last / n;
            dl->AddRect(ImVec2(vx0, o0.y), ImVec2(std::max(vx1, vx0 + 2.0f), o0.y + os.y), C(th::kAccent), 2.0f, 0, 1.0f);
            const float px = o0.x + os.x * (cur + 0.5f) / n;
            dl->AddLine(ImVec2(px, o0.y), ImVec2(px, o0.y + os.y), C(th::kText), 1.0f);
            if (ohov) {
                const int i = std::clamp(static_cast<int>((io.MousePos.x - o0.x) / os.x * n), 0, n - 1);
                ImGui::SetTooltip("#%d · %.3f s · peak 1 s bitrate here %s\nclick: go there", i, fr[static_cast<std::size_t>(i)].pts,
                                  rate_text(over_[static_cast<std::size_t>(std::clamp(static_cast<int>(io.MousePos.x - o0.x), 0, ocols - 1))]).c_str());
                if (ImGui::IsItemClicked(ImGuiMouseButton_Left) && std::isfinite(fr[static_cast<std::size_t>(i)].pts))
                    out.seek_to = fr[static_cast<std::size_t>(i)].pts;
            }
        }
        if (ImGui::IsWindowFocused() && !ImGui::IsAnyItemActive()) ImGui::SetWindowFocus(nullptr);
        out.height = ImGui::GetWindowHeight() + 10.0f;
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    return out;
}
