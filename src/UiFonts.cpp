#include "UiFonts.h"
#include "Logger.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <initializer_list>
#include <unordered_set>

namespace {

constexpr const char* kFontDir = "/System/Library/Fonts/";

// Persistent state: glyph-range arrays must outlive the atlas build.
struct FontState {
    UiFonts             fonts;
    float               built_scale = 0.0f;
    ImVector<ImWchar>   ui_ranges;
    ImVector<ImWchar>   sub_ranges;      // base + every requested codepoint
    ImVector<ImWchar>   cjk_ranges;      // requested codepoints only (Hiragino merge)
    std::unordered_set<char32_t> requested;   // already in sub_ranges
    std::unordered_set<char32_t> pending;     // waiting for the next rebuild
    double              last_rebuild = -1.0;
    bool                have_cjk = false;
};

FontState& state()
{
    static FontState s;
    return s;
}

// Basic Latin + Latin-1 + General Punctuation, plus × … · explicitly.
constexpr ImWchar kUiRanges[] = {
    0x0020, 0x00FF,
    0x2000, 0x206F,
    0x00D7, 0x00D7,
    0x2026, 0x2026,
    0x00B7, 0x00B7,
    0x2190, 0x21FF,   // arrows (← → ↑ ↓ ⇧) for the shortcut card
    0x2318, 0x2318,   // ⌘
    0x25A0, 0x25FF,   // geometric shapes (● ◆ ▶)
    0,
};
// Subtitle base: the UI set plus Latin Extended-A.
constexpr ImWchar kSubExtra[] = { 0x0100, 0x017F, 0 };

std::string font_path(const char* name) { return std::string(kFontDir) + name; }

// Try each candidate in order; returns the first that loads.
ImFont* add_first(std::initializer_list<const char*> names, float px,
                  ImFontConfig cfg, const ImWchar* ranges)
{
    ImGuiIO& io = ImGui::GetIO();
    for (const char* n : names) {
        const std::string p = font_path(n);
        std::error_code ec;
        if (!std::filesystem::exists(p, ec)) continue;
        if (ImFont* f = io.Fonts->AddFontFromFileTTF(p.c_str(), px, &cfg, ranges))
            return f;
        logger::warn("UiFonts: failed to load {}", p);
    }
    return nullptr;
}

ImFont* add_default(float px)
{
    ImFontConfig cfg;
    cfg.SizePixels = std::round(13.0f * px / 14.0f);
    return ImGui::GetIO().Fonts->AddFontDefault(&cfg);
}

void build_ranges(FontState& s)
{
    ImFontGlyphRangesBuilder b;
    b.AddRanges(kUiRanges);
    s.ui_ranges.clear();
    b.BuildRanges(&s.ui_ranges);

    b.AddRanges(kSubExtra);
    ImFontGlyphRangesBuilder cjk;
    s.have_cjk = false;
    for (char32_t c : s.requested) {
        if (c > 0xFFFF) continue;                 // ImWchar is 16-bit in this build
        b.AddChar(static_cast<ImWchar>(c));
        cjk.AddChar(static_cast<ImWchar>(c));
        s.have_cjk = true;
    }
    s.sub_ranges.clear();
    b.BuildRanges(&s.sub_ranges);
    s.cjk_ranges.clear();
    cjk.BuildRanges(&s.cjk_ranges);
}

bool build_atlas(FontState& s, float scale)
{
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    build_ranges(s);

    ImFontConfig crisp;
    crisp.OversampleH = 2;
    crisp.OversampleV = 1;
    crisp.PixelSnapH  = true;

    UiFonts& f = s.fonts;
    f.ui = add_first({ "SFNS.ttf", "HelveticaNeue.ttc" }, 14.0f * scale, crisp, s.ui_ranges.Data);
    if (!f.ui) f.ui = add_default(14.0f * scale);
    f.ui_small = add_first({ "SFNS.ttf", "HelveticaNeue.ttc" }, 12.0f * scale, crisp, s.ui_ranges.Data);
    if (!f.ui_small) f.ui_small = f.ui;
    f.mono = add_first({ "SFNSMono.ttf", "Menlo.ttc", "SFNS.ttf", "HelveticaNeue.ttc" },
                       13.0f * scale, crisp, s.ui_ranges.Data);
    if (!f.mono) f.mono = f.ui;

    ImFontConfig big;
    big.OversampleH = 1;
    big.OversampleV = 1;
    f.title = add_first({ "SFNS.ttf", "HelveticaNeue.ttc" }, 22.0f * scale, big, s.ui_ranges.Data);
    if (!f.title) f.title = f.ui;

    const float sub_px = std::min(48.0f * scale, 96.0f);
    f.sub = add_first({ "SFNS.ttf", "HelveticaNeue.ttc" }, sub_px, big, s.sub_ranges.Data);
    if (f.sub && s.have_cjk) {
        ImFontConfig merge = big;
        merge.MergeMode = true;
        (void)add_first({ "ヒラギノ角ゴシック W3.ttc", "Hiragino Sans GB.ttc",
                          "AppleSDGothicNeo.ttc" },
                        sub_px, merge, s.cjk_ranges.Data);
    }
    if (!f.sub) f.sub = f.ui;

    io.FontDefault     = f.ui;
    io.FontGlobalScale = 1.0f / scale;
    s.built_scale      = scale;
    s.last_rebuild     = glfwGetTime();
    return io.Fonts->Build();
}

} // namespace

const UiFonts& ui_fonts() { return state().fonts; }

bool ui_fonts_build(float fb_scale)
{
    if (!(fb_scale > 0.0f)) fb_scale = 1.0f;
    return build_atlas(state(), fb_scale);
}

void ui_fonts_request_glyphs(std::u32string_view text)
{
    FontState& s = state();
    if (!s.fonts.sub) return;
    for (char32_t c : text) {
        if (c < 0x20 || c > 0xFFFF || c == 0xFFFD) continue;
        if (s.requested.contains(c) || s.pending.contains(c)) continue;
        if (s.fonts.sub->FindGlyphNoFallback(static_cast<ImWchar>(c))) continue;
        s.pending.insert(c);
    }
}

bool ui_fonts_rebuild_if_needed(float fb_scale)
{
    FontState& s = state();
    if (!(fb_scale > 0.0f)) fb_scale = 1.0f;
    const bool scale_changed = std::abs(fb_scale - s.built_scale) > 1e-3f;
    const bool glyphs_due    = !s.pending.empty()
                            && glfwGetTime() - s.last_rebuild >= 0.5;
    if (!scale_changed && !glyphs_due) return false;

    for (char32_t c : s.pending) s.requested.insert(c);
    s.pending.clear();

    ImGui_ImplOpenGL3_DestroyFontsTexture();
    build_atlas(s, fb_scale);
    ImGui_ImplOpenGL3_CreateFontsTexture();
    return true;
}

std::u32string ui_utf8_to_u32(std::string_view s)
{
    std::u32string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        int len = 0;
        char32_t cp = 0;
        if      (c < 0x80)           { cp = c;        len = 1; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; len = 2; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; len = 3; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; len = 4; }
        else { out.push_back(0xFFFD); ++i; continue; }
        if (i + len > s.size()) { out.push_back(0xFFFD); break; }
        bool ok = true;
        for (int k = 1; k < len; ++k) {
            const unsigned char cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        out.push_back(ok ? cp : 0xFFFD);
        i += ok ? len : 1;
    }
    return out;
}
