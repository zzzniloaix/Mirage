#include "UiIcons.h"
#include "UiTheme.h"

#include <cmath>
#include <initializer_list>

namespace icons {

namespace {

constexpr float kPi = 3.14159265358979f;

// Maps the 24-unit design grid onto a box of `sz` px centred on c.
struct Grid {
    ImVec2 c;
    float  k;
    Grid(ImVec2 centre, float sz) : c(centre), k(sz / 24.0f) {}
    ImVec2 operator()(float x, float y) const { return { c.x + (x - 12.0f) * k, c.y + (y - 12.0f) * k }; }
    float  len(float v) const { return v * k; }
};

constexpr float kStroke = ui::theme::kIconStroke;

// Open polyline with round caps (circle at each end).
void stroke(ImDrawList* dl, std::initializer_list<ImVec2> pts, ImU32 col, float w = kStroke)
{
    dl->AddPolyline(pts.begin(), static_cast<int>(pts.size()), col, ImDrawFlags_None, w);
    dl->AddCircleFilled(*pts.begin(), w * 0.5f, col, 8);
    dl->AddCircleFilled(*(pts.end() - 1), w * 0.5f, col, 8);
}

void diamond(ImDrawList* dl, ImVec2 c, float r, ImU32 col)
{
    const ImVec2 p[4] = { { c.x, c.y - r }, { c.x + r, c.y }, { c.x, c.y + r }, { c.x - r, c.y } };
    dl->AddConvexPolyFilled(p, 4, col);
}

void brackets(ImDrawList* dl, const Grid& g, ImU32 col, bool inward)
{
    // Corner brackets with 6-unit arms; corners at (4,4)..(20,20).
    const float a = 6.0f;
    if (!inward) {
        stroke(dl, { g(4, 4 + a),  g(4, 4),   g(4 + a, 4)   }, col);
        stroke(dl, { g(20 - a, 4), g(20, 4),  g(20, 4 + a)  }, col);
        stroke(dl, { g(20, 20 - a),g(20, 20), g(20 - a, 20) }, col);
        stroke(dl, { g(4 + a, 20), g(4, 20),  g(4, 20 - a)  }, col);
    } else {
        stroke(dl, { g(4, 9),  g(9, 9),  g(9, 4)  }, col);
        stroke(dl, { g(15, 4), g(15, 9), g(20, 9) }, col);
        stroke(dl, { g(20, 15),g(15, 15),g(15, 20)}, col);
        stroke(dl, { g(9, 20), g(9, 15), g(4, 15) }, col);
    }
}

} // namespace

void play(ImDrawList* dl, ImVec2 c, float sz, ImU32 col)
{
    Grid g(c, sz);
    dl->AddTriangleFilled(g(7.5f, 4.5f), g(19.5f, 12.0f), g(7.5f, 19.5f), col);
}

void pause(ImDrawList* dl, ImVec2 c, float sz, ImU32 col)
{
    Grid g(c, sz);
    dl->AddRectFilled(g(6, 5),  g(10, 19), col, g.len(1.5f));
    dl->AddRectFilled(g(14, 5), g(18, 19), col, g.len(1.5f));
}

void step_frame_back(ImDrawList* dl, ImVec2 c, float sz, ImU32 col)
{
    Grid g(c, sz);
    dl->AddRectFilled(g(5, 6), g(7.5f, 18), col, g.len(1.0f));
    dl->AddTriangleFilled(g(18, 6), g(18, 18), g(9.5f, 12), col);
}

void step_frame_fwd(ImDrawList* dl, ImVec2 c, float sz, ImU32 col)
{
    Grid g(c, sz);
    dl->AddRectFilled(g(16.5f, 6), g(19, 18), col, g.len(1.0f));
    dl->AddTriangleFilled(g(6, 6), g(14.5f, 12), g(6, 18), col);
}

void key_back(ImDrawList* dl, ImVec2 c, float sz, ImU32 col)
{
    Grid g(c, sz);
    stroke(dl, { g(11, 7),  g(6, 12),  g(11, 17) }, col);
    stroke(dl, { g(16, 7),  g(11, 12), g(16, 17) }, col);
    diamond(dl, g(20, 12), g.len(2.6f), col);
}

void key_fwd(ImDrawList* dl, ImVec2 c, float sz, ImU32 col)
{
    Grid g(c, sz);
    diamond(dl, g(4, 12), g.len(2.6f), col);
    stroke(dl, { g(8, 7),  g(13, 12), g(8, 17)  }, col);
    stroke(dl, { g(13, 7), g(18, 12), g(13, 17) }, col);
}

void volume(ImDrawList* dl, ImVec2 c, float sz, ImU32 col, int level)
{
    Grid g(c, sz);
    // Speaker: box + flared cone.
    const ImVec2 body[6] = { g(3, 9), g(7, 9), g(12, 4.5f), g(12, 19.5f), g(7, 15), g(3, 15) };
    dl->AddConvexPolyFilled(body, 6, col);
    if (level <= 0) {
        stroke(dl, { g(15.5f, 9), g(21, 14.5f) }, col);
        stroke(dl, { g(21, 9),    g(15.5f, 14.5f) }, col);
        return;
    }
    const ImVec2 o = g(12, 12);
    auto arc = [&](float r) {
        dl->PathArcTo(o, g.len(r), -kPi / 4.0f, kPi / 4.0f, 12);
        dl->PathStroke(col, ImDrawFlags_None, kStroke);
    };
    arc(4.5f);
    if (level >= 2) arc(8.5f);
}

void cc(ImDrawList* dl, ImVec2 c, float sz, ImU32 col, bool on, ImFont* font)
{
    Grid g(c, sz);
    dl->AddRect(g(2.5f, 5), g(21.5f, 19), col, g.len(3.0f), ImDrawFlags_None, kStroke * 0.85f);
    const char* txt = "CC";
    if (font) {
        const float fs = g.len(10.0f);
        ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, txt);
        ImVec2 p  = g(12, 12);
        dl->AddText(font, fs, ImVec2(std::round(p.x - ts.x * 0.5f), std::round(p.y - ts.y * 0.5f)), col, txt);
    }
    if (on) {
        dl->AddRectFilled(g(4, 22), g(20, 24), ui::theme::kAccent, g.len(1.0f));
    }
}

void gear(ImDrawList* dl, ImVec2 c, float sz, ImU32 col)
{
    Grid g(c, sz);
    const ImVec2 o = g(12, 12);
    dl->AddCircle(o, g.len(3.0f), col, 16, kStroke);
    dl->AddCircle(o, g.len(7.0f), col, 24, kStroke);
    // 8 rectangular teeth outside the ring.
    for (int i = 0; i < 8; ++i) {
        const float a  = static_cast<float>(i) * kPi / 4.0f;
        const float ca = std::cos(a), sa = std::sin(a);
        const float r0 = g.len(7.0f), r1 = g.len(10.5f), hw = g.len(1.6f);
        auto P = [&](float r, float t) { return ImVec2(o.x + ca * r - sa * t, o.y + sa * r + ca * t); };
        const ImVec2 q[4] = { P(r0, -hw), P(r1, -hw), P(r1, hw), P(r0, hw) };
        dl->AddConvexPolyFilled(q, 4, col);
    }
}

void hud(ImDrawList* dl, ImVec2 c, float sz, ImU32 col, bool dot)
{
    Grid g(c, sz);
    dl->AddRect(g(3, 5), g(21, 19), col, g.len(3.0f), ImDrawFlags_None, kStroke * 0.85f);
    stroke(dl, { g(7, 9.5f),  g(14, 9.5f)  }, col, kStroke * 0.85f);
    stroke(dl, { g(7, 12.5f), g(17, 12.5f) }, col, kStroke * 0.85f);
    stroke(dl, { g(7, 15.5f), g(11, 15.5f) }, col, kStroke * 0.85f);
    if (dot) dl->AddCircleFilled(g(21, 5), g.len(3.2f), ui::theme::kAccent, 12);
}

void fullscreen(ImDrawList* dl, ImVec2 c, float sz, ImU32 col)      { brackets(dl, Grid(c, sz), col, false); }
void exit_fullscreen(ImDrawList* dl, ImVec2 c, float sz, ImU32 col) { brackets(dl, Grid(c, sz), col, true); }

void menu(ImDrawList* dl, ImVec2 c, float sz, ImU32 col)
{
    Grid g(c, sz);
    stroke(dl, { g(4, 6),  g(20, 6)  }, col);
    stroke(dl, { g(4, 12), g(20, 12) }, col);
    stroke(dl, { g(4, 18), g(20, 18) }, col);
}

void help(ImDrawList* dl, ImVec2 c, float sz, ImU32 col, ImFont* font)
{
    Grid g(c, sz);
    dl->AddCircle(g(12, 12), g.len(9.5f), col, 24, kStroke * 0.85f);
    if (font) {
        const float fs = g.len(13.0f);
        ImVec2 ts = font->CalcTextSizeA(fs, FLT_MAX, 0.0f, "?");
        ImVec2 p  = g(12, 12);
        dl->AddText(font, fs, ImVec2(std::round(p.x - ts.x * 0.5f), std::round(p.y - ts.y * 0.5f)), col, "?");
    }
}

void chevron_right(ImDrawList* dl, ImVec2 c, float sz, ImU32 col)
{
    Grid g(c, sz);
    stroke(dl, { g(9, 6), g(15, 12), g(9, 18) }, col);
}

void chevron_left(ImDrawList* dl, ImVec2 c, float sz, ImU32 col)
{
    Grid g(c, sz);
    stroke(dl, { g(15, 6), g(9, 12), g(15, 18) }, col);
}

void check(ImDrawList* dl, ImVec2 c, float sz, ImU32 col)
{
    Grid g(c, sz);
    stroke(dl, { g(5, 12), g(10, 17), g(19, 7) }, col);
}

} // namespace icons
