#include "SubtitleOverlay.h"
#include "UiFonts.h"
#include "UiTheme.h"

#include <algorithm>
#include <cmath>

namespace {

bool is_cjk(char32_t c)
{
    return (c >= 0x2E80 && c <= 0x9FFF)     // CJK radicals, kana, unified ideographs
        || (c >= 0xAC00 && c <= 0xD7AF)     // Hangul syllables
        || (c >= 0xF900 && c <= 0xFAFF)     // compatibility ideographs
        || (c >= 0xFF00 && c <= 0xFFEF);    // half/full-width forms
}

void append_utf8(std::string& out, char32_t c)
{
    if (c < 0x80) { out += static_cast<char>(c); }
    else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

float text_w(ImFont* f, float px, const std::string& s)
{
    return f->CalcTextSizeA(px, FLT_MAX, 0.0f, s.c_str()).x;
}

void trim(std::string& s)
{
    while (!s.empty() && s.back() == ' ')  s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && s[i] == ' ') ++i;
    s.erase(0, i);
}

} // namespace

std::vector<std::string> SubtitleOverlay::wrap(ImFont* font, float px, const std::string& text,
                                               float max_w, int max_lines)
{
    std::vector<std::string> lines;
    const std::u32string u = ui_utf8_to_u32(text);

    // Tokens: a word (+ trailing spaces) or a single CJK codepoint; '\n' forces a break.
    std::string line, tok;
    auto flush_tok = [&] {
        if (tok.empty()) return;
        std::string cand = line + tok;
        std::string cand_trim = cand;
        trim(cand_trim);
        if (!line.empty() && text_w(font, px, cand_trim) > max_w) {
            trim(line);
            lines.push_back(line);
            line = tok;
            if (!line.empty() && line.front() == ' ') trim(line);
        } else {
            line = std::move(cand);
        }
        tok.clear();
    };

    for (char32_t c : u) {
        if (c == '\n') {
            flush_tok();
            trim(line);
            lines.push_back(line);
            line.clear();
            continue;
        }
        if (c == '\r') continue;
        if (is_cjk(c)) {
            flush_tok();
            append_utf8(tok, c);
            flush_tok();
            continue;
        }
        append_utf8(tok, c);
        if (c == ' ') flush_tok();
    }
    flush_tok();
    trim(line);
    if (!line.empty()) lines.push_back(line);

    std::erase_if(lines, [](const std::string& l) { return l.empty(); });
    if (max_lines > 0 && static_cast<int>(lines.size()) > max_lines)
        lines.erase(lines.begin(), lines.end() - max_lines);
    return lines;
}

void SubtitleOverlay::draw(ImDrawList* dl, ImFont* font, const std::string& text,
                           ImVec4 vr, float win_h, float reserve_bottom,
                           const SubStyle& style, float dt)
{
    const float vx = vr.x, vy = vr.y, vw = vr.z, vh = vr.w;
    if (!font || vw <= 0.0f || vh <= 0.0f) return;

    // Lift: how far the 5%-margin baseline must rise to clear the controls.
    const float base_bottom = vy + vh - 0.05f * vh;
    const float target = std::max(0.0f, base_bottom - (win_h - reserve_bottom));
    if (!lift_init_) { lift_ = target; lift_init_ = true; }
    else lift_ = ui::theme::ease(lift_, target, dt, ui::theme::kSubLiftEase);

    if (text.empty()) return;
    if (text != last_text_) {
        last_text_ = text;
        ui_fonts_request_glyphs(ui_utf8_to_u32(text));
    }

    const float px = std::clamp(0.05f * vh * static_cast<float>(style.size_pct) / 100.0f, 16.0f, 96.0f);
    const float line_h = std::round(px * 1.2f);
    const auto lines = wrap(font, px, text, 0.80f * vw);
    if (lines.empty()) return;

    const float bottom = base_bottom - lift_;
    float y = std::round(bottom - line_h * static_cast<float>(lines.size()));
    const float cx = vx + vw * 0.5f;

    const ImU32 white = IM_COL32_WHITE;
    for (const auto& ln : lines) {
        const ImVec2 ts = font->CalcTextSizeA(px, FLT_MAX, 0.0f, ln.c_str());
        const float  tx = std::round(cx - ts.x * 0.5f);
        const float  ty = std::round(y + (line_h - ts.y) * 0.5f);
        const char*  s  = ln.c_str();

        switch (style.edge) {
        case SubStyle::Box: {
            const int a = std::clamp(style.box_opacity, 0, 100) * 255 / 100;
            if (a > 0) {
                const float padx = 0.5f * px;
                dl->AddRectFilled(ImVec2(tx - padx, y), ImVec2(tx + ts.x + padx, y + line_h),
                                  IM_COL32(0, 0, 0, a), 4.0f);
            }
            break;
        }
        case SubStyle::Outline: {
            const float o = std::max(1.5f, px * 0.05f);
            const ImU32 oc = IM_COL32(0, 0, 0, 230);
            static constexpr float kDirs[8][2] = { {-1,-1},{0,-1},{1,-1},{-1,0},{1,0},{-1,1},{0,1},{1,1} };
            for (const auto& d : kDirs)
                dl->AddText(font, px, ImVec2(tx + d[0] * o, ty + d[1] * o), oc, s);
            dl->AddText(font, px, ImVec2(tx, ty + 1.0f), IM_COL32(0, 0, 0, 160), s);
            break;
        }
        case SubStyle::DropShadow: {
            const float o = std::max(1.0f, 2.0f * px * 0.04f);
            dl->AddText(font, px, ImVec2(tx + o, ty + o), IM_COL32(0, 0, 0, 204), s);
            break;
        }
        case SubStyle::None: break;
        }
        dl->AddText(font, px, ImVec2(tx, ty), white, s);
        y += line_h;
    }
}
