#include "ScopesPanel.h"
#include "UiFonts.h"
#include "UiTheme.h"
#include "UiWidgets.h"

#include <glad/gl.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <string>
#include <vector>

namespace th = ui::theme;

namespace {

ImU32 C(ImU32 c, float mul = 1.0f) { return ImGui::GetColorU32(c, mul); }

constexpr float kCardW   = 470.0f;
constexpr float kPadX    = 18.0f;
constexpr float kLabelFs = 11.0f;

// SMPTE ST 2084 inverse EOTF: cd/m² → signal, 0..1.
double nits_to_pq(double nits)
{
    constexpr double m1 = 2610.0 / 16384.0, m2 = 2523.0 / 4096.0 * 128.0;
    constexpr double c1 = 3424.0 / 4096.0, c2 = 2413.0 / 4096.0 * 32.0, c3 = 2392.0 / 4096.0 * 32.0;
    const double y = std::pow(std::clamp(nits / 10000.0, 0.0, 1.0), m1);
    return std::pow((c1 + c2 * y) / (1.0 + c3 * y), m2);
}

// Screen y of an 8-bit code on the waveform / parade (code 255 at the top).
float code_y(float top, float h, double code8)
{
    return top + static_cast<float>((255.0 - code8 + 0.5) / 256.0) * h;
}

void label(ImDrawList* dl, ImVec2 p, const std::string& s, ImU32 col)
{
    dl->AddText(ui_fonts().ui_small, kLabelFs, p, col, s.c_str());
}

// Small rounded toggle chip. Returns true when clicked.
bool chip_button(const char* id, const char* text, bool on)
{
    ImFont*      f  = ui_fonts().ui_small;
    const ImVec2 ts = f->CalcTextSizeA(12.0f, FLT_MAX, 0.0f, text);
    const ImVec2 sz(ts.x + 18.0f, 22.0f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, sz);
    const bool hov     = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y),
                      C(on ? th::alpha(th::kAccent, 0.22f) : hov ? th::kHoverBg : th::rgba(255, 255, 255, 0.05f)),
                      th::kRadiusChip);
    dl->AddText(f, 12.0f, ImVec2(p.x + 9.0f, p.y + (sz.y - ts.y) * 0.5f), C(on ? th::kAccent : th::kTextDim), text);
    return clicked;
}

void wave_graticule(ImDrawList* dl, ImVec2 p0, ImVec2 p1, int depth, bool parade, const ScopesInputs& in)
{
    const float h = p1.y - p0.y;
    const int   s = 1 << std::max(0, depth - 8);
    struct Line { double code8; std::string text; bool strong; };
    std::vector<Line> lines;
    if (in.range != AVCOL_RANGE_JPEG)
        lines = { { 235.0, std::to_string(235 * s), true }, { 16.0, std::to_string(16 * s), true },
                  { 125.5, "50%", false } };
    else
        lines = { { 255.0, std::to_string((1 << depth) - 1), true }, { 0.0, "0", true }, { 127.5, "50%", false } };
    for (const auto& l : lines) {
        const float y = code_y(p0.y, h, l.code8);
        dl->AddLine(ImVec2(p0.x, y), ImVec2(p1.x, y),
                    C(l.strong ? th::alpha(th::kWarn, 0.55f) : th::rgba(255, 255, 255, 0.22f)), 1.0f);
        label(dl, ImVec2(p0.x + 3.0f, y - kLabelFs - 1.0f), l.text, C(th::kTextDim));
    }
    if (in.trc == AVCOL_TRC_SMPTE2084) {   // a nits scale on the right
        for (double nits : { 100.0, 203.0, 1000.0, 4000.0 }) {
            const double e     = nits_to_pq(nits);
            const double code8 = in.range == AVCOL_RANGE_JPEG ? e * 255.0 : 16.0 + e * 219.0;
            const float  y     = code_y(p0.y, h, code8);
            dl->AddLine(ImVec2(p1.x - 12.0f, y), ImVec2(p1.x, y), C(th::kAccent), 1.0f);
            const std::string t = std::format("{:g} nits", nits);
            const float tw = ui_fonts().ui_small->CalcTextSizeA(kLabelFs, FLT_MAX, 0.0f, t.c_str()).x;
            label(dl, ImVec2(p1.x - 14.0f - tw, y - kLabelFs * 0.5f - 1.0f), t, C(th::alpha(th::kAccent, 0.85f)));
        }
    }
    if (parade) {
        const float w3 = (p1.x - p0.x) / 3.0f;
        const char* names[3] = { "R′", "G′", "B′" };
        for (int k = 0; k < 3; ++k) {
            if (k > 0) dl->AddLine(ImVec2(p0.x + w3 * k, p0.y), ImVec2(p0.x + w3 * k, p1.y),
                                   C(th::rgba(255, 255, 255, 0.25f)), 1.0f);
            label(dl, ImVec2(p0.x + w3 * k + w3 - 18.0f, p0.y + 3.0f), names[k], C(th::kTextDim));
        }
    }
}

void vector_graticule(ImDrawList* dl, ImVec2 p0, float size, const ScopesInputs& in)
{
    const ImVec2 c(p0.x + size * 0.5f, p0.y + size * 0.5f);
    const ImU32  faint = C(th::rgba(255, 255, 255, 0.20f));
    dl->AddLine(ImVec2(p0.x, c.y), ImVec2(p0.x + size, c.y), faint, 1.0f);
    dl->AddLine(ImVec2(c.x, p0.y), ImVec2(c.x, p0.y + size), faint, 1.0f);
    const bool   limited = in.range != AVCOL_RANGE_JPEG;
    const double scale   = limited ? 224.0 : 255.0;    // 8-bit codes per unit of Cb / Cr
    // Where a normalised (Cb, Cr) lands: Cb to the right, Cr up.
    auto at = [&](double cb, double cr) {
        return ImVec2(p0.x + static_cast<float>((128.0 + scale * cb + 0.5) / 256.0) * size,
                      p0.y + static_cast<float>((255.0 - (128.0 + scale * cr) + 0.5) / 256.0) * size);
    };
    // The chroma extent of the RGB cube (100 %), as a circle through it.
    dl->AddCircle(c, static_cast<float>(scale * 0.5 / 256.0) * size, faint, 64, 1.0f);

    double kr, kb;
    luma_coefficients(in.cs, kr, kb);
    const double kg = 1.0 - kr - kb;
    struct Target { const char* name; double r, g, b; };
    constexpr Target kTargets[] = { { "R", 1, 0, 0 }, { "Mg", 1, 0, 1 }, { "B", 0, 0, 1 },
                                    { "Cy", 0, 1, 1 }, { "G", 0, 1, 0 }, { "Yl", 1, 1, 0 } };
    for (const auto& t : kTargets) {
        for (double amp : { 0.75, 1.0 }) {
            const double r = t.r * amp, g = t.g * amp, b = t.b * amp;
            const double y  = kr * r + kg * g + kb * b;
            const ImVec2 q  = at((b - y) / (2.0 * (1.0 - kb)), (r - y) / (2.0 * (1.0 - kr)));
            if (amp < 1.0) {
                dl->AddRect(ImVec2(q.x - 4.0f, q.y - 4.0f), ImVec2(q.x + 4.0f, q.y + 4.0f),
                            C(th::alpha(th::kWarn, 0.8f)), 0.0f, 0, 1.0f);
                label(dl, ImVec2(q.x + 6.0f, q.y - 6.0f), t.name, C(th::alpha(th::kWarn, 0.9f)));
            } else {
                dl->AddCircle(q, 3.0f, C(th::alpha(th::kWarn, 0.45f)), 12, 1.0f);
            }
        }
    }
    // Skin-tone line, 123° from the +Cb axis.
    const double a = 123.0 * 3.14159265358979323846 / 180.0;
    const ImVec2 e = at(0.5 * std::cos(a), 0.5 * std::sin(a));
    dl->AddLine(c, e, C(th::rgba(255, 200, 150, 0.45f)), 1.0f);
}

void draw_histogram(ImDrawList* dl, ImVec2 p0, ImVec2 p1, const std::vector<float>& hist, int depth,
                    const ScopesInputs& in)
{
    const float w = p1.x - p0.x, h = p1.y - p0.y;
    const int   n = static_cast<int>(hist.size());
    if (n == 0) return;
    // Shade the codes outside the legal range.
    if (in.range != AVCOL_RANGE_JPEG) {
        const double s     = std::ldexp(1.0, depth - 8);
        const double codes = std::ldexp(1.0, depth);
        const float  x16   = p0.x + static_cast<float>(16.0 * s / codes) * w;
        const float  x235  = p0.x + static_cast<float>((235.0 * s + 1.0) / codes) * w;
        dl->AddRectFilled(p0, ImVec2(x16, p1.y), C(th::alpha(th::kError, 0.10f)));
        dl->AddRectFilled(ImVec2(x235, p0.y), p1, C(th::alpha(th::kError, 0.10f)));
    }
    // One line per screen column: the fullest bin it covers, square-root scaled.
    const int cols = std::max(1, static_cast<int>(w));
    for (int x = 0; x < cols; ++x) {
        const int b0 = x * n / cols, b1 = std::max(b0 + 1, (x + 1) * n / cols);
        float v = 0.0f;
        for (int b = b0; b < b1 && b < n; ++b) v = std::max(v, hist[static_cast<std::size_t>(b)]);
        if (v <= 0.0f) continue;
        const float top = p1.y - std::sqrt(v) * h;
        dl->AddLine(ImVec2(p0.x + x + 0.5f, p1.y), ImVec2(p0.x + x + 0.5f, top), C(th::rgba(200, 230, 255, 0.85f)), 1.0f);
    }
}

} // namespace

ScopesPanel::~ScopesPanel()
{
    if (tex_) glDeleteTextures(1, &tex_);
}

ScopesOut ScopesPanel::draw(const ScopesInputs& in)
{
    ScopesOut out;
    const float avail_w = std::max(240.0f, in.win_w - in.right_inset);
    const float card_w  = std::min(kCardW, avail_w - 32.0f);
    const float inner_w = card_w - 2.0f * kPadX;

    // Bottom left above the controls when it opens; movable while open.
    ImGui::SetNextWindowPos(ImVec2(16.0f, in.win_h - in.bottom_inset - 12.0f), ImGuiCond_Appearing, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSize(ImVec2(card_w, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kPadX, 14.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, th::kRadiusPanel);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 6.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, th::vec4(th::kPanel));
    if (ImGui::Begin("##scopes", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImGui::PushFont(ui_fonts().title);
        ImGui::TextUnformatted("Scopes");
        ImGui::PopFont();
        {   // Close (top right).
            const ImVec2 wp = ImGui::GetWindowPos();
            const ImVec2 save = ImGui::GetCursorScreenPos();
            ImGui::SetCursorScreenPos(ImVec2(wp.x + ImGui::GetWindowWidth() - 12.0f - th::kBtn, wp.y + 8.0f));
            auto cb = ui::icon_button("##scclose", "Close (S)");
            const float r = 5.0f;
            dl->AddLine(ImVec2(cb.c.x - r, cb.c.y - r), ImVec2(cb.c.x + r, cb.c.y + r), cb.col, 2.0f);
            dl->AddLine(ImVec2(cb.c.x + r, cb.c.y - r), ImVec2(cb.c.x - r, cb.c.y + r), cb.col, 2.0f);
            if (cb.clicked) out.close = true;
            ImGui::SetCursorScreenPos(save);
        }
        for (int i = 0; i < scopes::kModes; ++i) {
            if (i > 0) ImGui::SameLine();
            const auto m = static_cast<scopes::Mode>(i);
            const std::string id = std::string("##scm") + std::to_string(i);
            if (chip_button(id.c_str(), scopes::mode_name(m), m == in.mode)) out.mode = i;
        }

        const scopes::Result* r = in.result;
        const bool square = in.mode == scopes::Mode::Vectorscope;
        const ImVec2 area(inner_w, square ? 260.0f : in.mode == scopes::Mode::Histogram ? 180.0f : 236.0f);
        const ImVec2 a0 = ImGui::GetCursorScreenPos();
        ImGui::Dummy(area);
        dl->AddRectFilled(a0, ImVec2(a0.x + area.x, a0.y + area.y), C(th::rgba(0, 0, 0, 0.85f)), th::kRadiusSmall);
        const bool current = r && r->ok && r->mode == in.mode;
        if (r && !r->ok) {
            label(dl, ImVec2(a0.x + 10.0f, a0.y + 10.0f), r->error, C(th::kWarn));
        } else if (!current) {
            label(dl, ImVec2(a0.x + 10.0f, a0.y + 10.0f), "Measuring…", C(th::kTextDim));
        } else {
            if (r->image.w > 0 && r->serial != tex_serial_) {
                GLint prev_align = 4;
                glGetIntegerv(GL_UNPACK_ALIGNMENT, &prev_align);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
                if (!tex_) glGenTextures(1, &tex_);
                glBindTexture(GL_TEXTURE_2D, tex_);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                if (r->image.w != tex_w_ || r->image.h != tex_h_) {
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, r->image.w, r->image.h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                                 r->image.rgba.data());
                    tex_w_ = r->image.w;
                    tex_h_ = r->image.h;
                } else {
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, r->image.w, r->image.h, GL_RGBA, GL_UNSIGNED_BYTE,
                                    r->image.rgba.data());
                }
                glBindTexture(GL_TEXTURE_2D, 0);
                glPixelStorei(GL_UNPACK_ALIGNMENT, prev_align);
                tex_serial_ = r->serial;
            }
            const int depth = r->stats.depth;
            switch (in.mode) {
            case scopes::Mode::Waveform:
            case scopes::Mode::Parade: {
                const ImVec2 i0(a0.x + 1.0f, a0.y + 1.0f), i1(a0.x + area.x - 1.0f, a0.y + area.y - 1.0f);
                dl->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(tex_)), i0, i1);
                wave_graticule(dl, i0, i1, depth, in.mode == scopes::Mode::Parade, in);
                break;
            }
            case scopes::Mode::Vectorscope: {
                const float  sz = area.y - 4.0f;
                const ImVec2 i0(a0.x + (area.x - sz) * 0.5f, a0.y + 2.0f);
                dl->AddImage(static_cast<ImTextureID>(static_cast<intptr_t>(tex_)), i0, ImVec2(i0.x + sz, i0.y + sz));
                vector_graticule(dl, i0, sz, in);
                break;
            }
            case scopes::Mode::Histogram:
                draw_histogram(dl, ImVec2(a0.x + 4.0f, a0.y + 4.0f), ImVec2(a0.x + area.x - 4.0f, a0.y + area.y - 4.0f),
                               r->histogram, depth, in);
                break;
            }
        }

        // Statistics of the frame measured.
        if (r && r->ok) {
            const scopes::Stats& st = r->stats;
            // A share of the samples: "<0.01%" rather than a 0.00% that hides a few.
            auto pct = [](double f) {
                return f <= 0.0 ? std::string("0%") : f < 0.0001 ? std::string("<0.01%") : std::format("{:.2f}%", f * 100.0);
            };
            ImGui::PushFont(ui_fonts().ui_small);
            std::string l1 = std::format("Y′ {}–{} · avg {:.1f}", st.y_min, st.y_max, st.y_avg);
            if (st.limited)
                l1 += std::format(" · below black {} · above white {}", pct(st.below_black), pct(st.above_white));
            else
                l1 += " · full range";
            ImGui::TextColored(th::vec4(st.limited && (st.below_black > 0.0 || st.above_white > 0.0) ? th::kWarn : th::kText),
                               "%s", l1.c_str());
            std::string l2 = std::format("{} of {} Y′ codes used", st.codes_used, st.codes_total);
            if (st.limited) l2 += std::format(" · chroma out of range {}", pct(st.chroma_out));
            ImGui::TextColored(th::vec4(th::kTextDim), "%s", l2.c_str());
            if (st.lsb_unused)
                ImGui::TextColored(th::vec4(th::kWarn), "Low %d bits unused on every Y′ sample: an 8-bit source?",
                                   st.depth - 8);
            ImGui::TextColored(th::vec4(th::kTextFaint), "%s",
                               std::format("frame at {:.3f} s · {} samples{}", r->pts, st.samples,
                                           in.measuring ? " · measuring…" : "").c_str());
            ImGui::PopFont();
        }
        if (ImGui::IsWindowFocused() && !ImGui::IsAnyItemActive()) ImGui::SetWindowFocus(nullptr);
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    return out;
}

void draw_pixel_probe(const ProbeInputs& in)
{
    if (!in.view || !in.view->valid()) return;
    const ImVec4 r = in.video_rect;
    const bool   fixed = in.at.x >= 0.0f && in.at.y >= 0.0f;
    const ImVec2 m = fixed ? ImVec2(r.x + in.at.x * r.z, r.y + in.at.y * r.w) : ImGui::GetIO().MousePos;
    if (r.z <= 0.0f || r.w <= 0.0f || m.x < r.x || m.y < r.y || m.x >= r.x + r.z || m.y >= r.y + r.w) return;
    if (!fixed && ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow)) return;   // over a panel or the controls

    const int W = in.view->width(), H = in.view->height();
    const int px = std::clamp(static_cast<int>((m.x - r.x) / r.z * W), 0, W - 1);
    const int py = std::clamp(static_cast<int>((m.y - r.y) / r.w * H), 0, H - 1);
    const PixelView::Codes c = in.view->at(px, py);
    const int d = in.view->depth();
    const PixelValues v = pixel_values(c.y, c.cb, c.cr, d, in.cs, in.range, in.trc);

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    // Crosshair with a gap, over a dark outline so it shows on any picture.
    for (int pass = 0; pass < 2; ++pass) {
        const ImU32 col = pass == 0 ? IM_COL32(0, 0, 0, 170) : IM_COL32(255, 255, 255, 230);
        const float t   = pass == 0 ? 3.0f : 1.0f;
        dl->AddLine(ImVec2(m.x - 14.0f, m.y), ImVec2(m.x - 4.0f, m.y), col, t);
        dl->AddLine(ImVec2(m.x + 4.0f, m.y), ImVec2(m.x + 14.0f, m.y), col, t);
        dl->AddLine(ImVec2(m.x, m.y - 14.0f), ImVec2(m.x, m.y - 4.0f), col, t);
        dl->AddLine(ImVec2(m.x, m.y + 4.0f), ImVec2(m.x, m.y + 14.0f), col, t);
    }

    struct Line { std::string text; ImU32 col; };
    std::vector<Line> lines;
    // No "-0.000" for a value that rounds to zero.
    auto z = [](double x) { return std::abs(x) < 0.0005 ? 0.0 : x; };
    lines.push_back({ std::format("x {}  y {}", px, py), th::kTextDim });
    lines.push_back({ std::format("Y' {:>4}  Cb {:>4}  Cr {:>4}  {}-bit", c.y, c.cb, c.cr, d), th::kText });
    lines.push_back({ std::format("   {:6.3f} {:+6.3f} {:+6.3f}", z(v.y), z(v.cb), z(v.cr)), th::kTextDim });
    // EBU R103's preferred R'G'B' range is -1 % to 101 %: within it, an excursion is quantisation.
    auto out_of = [](double x) { return x < -0.01 || x > 1.01; };
    const bool outside = out_of(v.r) || out_of(v.g) || out_of(v.b);
    lines.push_back({ std::format("R'G'B' {:6.3f} {:6.3f} {:6.3f}", z(v.r), z(v.g), z(v.b)), outside ? th::kWarn : th::kText });
    if (v.has_nits)
        lines.push_back({ std::format("{} Y {:.1f} cd/m2  (R {:.0f} G {:.0f} B {:.0f})",
                                      in.trc == AVCOL_TRC_SMPTE2084 ? "PQ" : "HLG", v.nits_y, v.nits_r, v.nits_g, v.nits_b),
                          th::kAccent });
    if (v.below_black) lines.push_back({ "below black level", th::kWarn });
    if (v.above_white) lines.push_back({ "above white level", th::kWarn });
    if (v.chroma_out)  lines.push_back({ "chroma outside 16-240 (8-bit scale)", th::kWarn });
    if (outside)       lines.push_back({ "R'G'B' outside -1..101 % (EBU R103)", th::kWarn });

    ImFont*     f  = ui_fonts().mono;
    const float fs = 12.0f, lh = 15.0f;
    float tw = 0.0f;
    for (const auto& l : lines) tw = std::max(tw, f->CalcTextSizeA(fs, FLT_MAX, 0.0f, l.text.c_str()).x);
    const ImVec2 sz(tw + 20.0f, lh * static_cast<float>(lines.size()) + 14.0f);
    const ImVec2 disp = ImGui::GetIO().DisplaySize;
    ImVec2 p(m.x + 20.0f, m.y + 20.0f);
    if (p.x + sz.x > disp.x - 8.0f) p.x = m.x - 20.0f - sz.x;
    if (p.y + sz.y > disp.y - 8.0f) p.y = m.y - 20.0f - sz.y;
    dl->AddRectFilled(p, ImVec2(p.x + sz.x, p.y + sz.y), th::kPanel, th::kRadiusSmall);
    dl->AddRect(p, ImVec2(p.x + sz.x, p.y + sz.y), th::kPanelBorder, th::kRadiusSmall);
    for (std::size_t i = 0; i < lines.size(); ++i)
        dl->AddText(f, fs, ImVec2(p.x + 10.0f, p.y + 7.0f + lh * static_cast<float>(i)), lines[i].col,
                    lines[i].text.c_str());
}
