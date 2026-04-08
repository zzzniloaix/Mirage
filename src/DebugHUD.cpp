#define STB_EASY_FONT_IMPLEMENTATION
#include "stb_easy_font.h"

#include "DebugHUD.h"
#include "Logger.h"

#include <format>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdio>

// ── Shaders ───────────────────────────────────────────────────────────────────

// Background: NDC quad from uniforms, no VBO (gl_VertexID trick).
// Triangle-strip order: 0=BL, 1=BR, 2=TL, 3=TR.
// u_rect = (xmin, xmax, ymin, ymax) in NDC.
static const char* kBgVert = R"glsl(
#version 410 core
uniform vec4 u_rect;
void main() {
    float x = (gl_VertexID & 1) != 0 ? u_rect.y : u_rect.x;
    float y = (gl_VertexID & 2) != 0 ? u_rect.w : u_rect.z;
    gl_Position = vec4(x, y, 0.0, 1.0);
}
)glsl";

static const char* kBgFrag = R"glsl(
#version 410 core
uniform vec4 u_bg_color;
out vec4 frag_color;
void main() { frag_color = u_bg_color; }
)glsl";

// Text: pixel-space vertex positions → NDC via fb_size uniform.
// stb_easy_font uses top-left origin (y increases down); NDC y=1 is top.
static const char* kTextVert = R"glsl(
#version 410 core
layout(location = 0) in vec2 a_pos;
uniform vec2 u_fb_size;
void main() {
    vec2 ndc = vec2(
        a_pos.x / u_fb_size.x * 2.0 - 1.0,
        1.0 - a_pos.y / u_fb_size.y * 2.0
    );
    gl_Position = vec4(ndc, 0.0, 1.0);
}
)glsl";

static const char* kTextFrag = R"glsl(
#version 410 core
uniform vec4 u_text_color;
out vec4 frag_color;
void main() { frag_color = u_text_color; }
)glsl";

// ── DebugHUD ─────────────────────────────────────────────────────────────────

DebugHUD::~DebugHUD()
{
    if (bg_program_)   glDeleteProgram(bg_program_);
    if (text_program_) glDeleteProgram(text_program_);
    if (bg_vao_)       glDeleteVertexArrays(1, &bg_vao_);
    if (text_vao_)     glDeleteVertexArrays(1, &text_vao_);
    if (text_vbo_)     glDeleteBuffers(1, &text_vbo_);
}

bool DebugHUD::init()
{
    if (!compile_shaders()) return false;

    // Background quad: uses gl_VertexID — just needs an empty VAO.
    glGenVertexArrays(1, &bg_vao_);

    // Text VBO: vec2 positions, re-uploaded every draw call.
    glGenVertexArrays(1, &text_vao_);
    glGenBuffers(1, &text_vbo_);

    glBindVertexArray(text_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, text_vbo_);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);

    return true;
}

void DebugHUD::draw(const DebugInfo& info, int fb_w, int fb_h, float scale)
{
    if (fb_w <= 0 || fb_h <= 0) return;
    glViewport(0, 0, fb_w, fb_h);

    // ── Format text lines ─────────────────────────────────────────────────────
    const int s_total = static_cast<int>(info.pts);
    const int s_h     = s_total / 3600;
    const int s_m     = (s_total % 3600) / 60;
    const int s_s     = s_total % 60;
    const int ms      = static_cast<int>((info.pts - s_total) * 1000.0);

    std::string l_pts    = std::format("PTS    {:02d}:{:02d}:{:02d}.{:03d}", s_h, s_m, s_s, ms);
    std::string l_frame  = std::format("Frame  {}", info.frame_number);

    std::string l_av;
    if (!info.has_audio)
        l_av = "A/V    N/A";          // no audio track
    else if (std::isnan(info.av_diff_ms))
        l_av = "A/V    --";           // paused / not meaningful
    else
        l_av = std::format("A/V    {:+.1f} ms", info.av_diff_ms);

    std::string l_dec = std::format("Decode {:.2f} ms/f", info.decode_ms);

    const char* lines[4] = { l_pts.c_str(), l_frame.c_str(), l_av.c_str(), l_dec.c_str() };

    // ── Layout ────────────────────────────────────────────────────────────────
    // stb_easy_font renders at ~7px/char wide, ~13px tall at scale 1.
    const float char_h  = 13.0f * scale;
    const float line_sp = 4.0f  * scale;   // extra spacing between lines
    const float pad     = 8.0f  * scale;   // panel padding
    const float margin  = 10.0f * scale;   // distance from window edge

    float text_x = margin + pad;
    float text_y = margin + pad;
    float line_h = char_h + line_sp;

    // Measure widest line for panel sizing.
    float max_text_w = 0.0f;
    for (const char* ln : lines)
        max_text_w = std::max(max_text_w,
            static_cast<float>(stb_easy_font_width(const_cast<char*>(ln))) * scale);

    float panel_w = max_text_w + pad * 2.0f;
    float panel_h = line_h * 4.0f + pad * 2.0f - line_sp;

    // ── Background ────────────────────────────────────────────────────────────
    auto px2x = [&](float px) { return px / static_cast<float>(fb_w) * 2.0f - 1.0f; };
    auto px2y = [&](float px) { return 1.0f - px / static_cast<float>(fb_h) * 2.0f; };

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // ymin (bottom of panel in NDC) < ymax (top of panel in NDC)
    draw_bg(px2x(margin),              px2x(margin + panel_w),
            px2y(margin + panel_h),    px2y(margin),
            0.0f, 0.0f, 0.0f, 0.72f);
    glDisable(GL_BLEND);

    // ── Text ──────────────────────────────────────────────────────────────────
    // A/V diff colour: white < 10 ms, yellow < 50 ms, red ≥ 50 ms
    float av_r = 1.0f, av_g = 1.0f, av_b = 1.0f;
    if (info.has_audio && !std::isnan(info.av_diff_ms)) {
        double abs_diff = std::abs(info.av_diff_ms);
        if      (abs_diff >= 100.0) { av_r = 1.0f; av_g = 0.3f; av_b = 0.3f; }
        else if (abs_diff >=  40.0) { av_r = 1.0f; av_g = 0.9f; av_b = 0.3f; }
    }

    for (int i = 0; i < 4; ++i) {
        float py = text_y + static_cast<float>(i) * line_h;
        float r = 1.0f, g = 1.0f, b = 1.0f;
        if (i == 2) { r = av_r; g = av_g; b = av_b; }
        draw_text(lines[i], text_x, py, r, g, b, scale, fb_w, fb_h);
    }
}

void DebugHUD::draw_inspector(const std::vector<InspectorLine>& lines,
                               int fb_w, int fb_h, float scale)
{
    if (fb_w <= 0 || fb_h <= 0 || lines.empty()) return;
    glViewport(0, 0, fb_w, fb_h);

    // ── Layout ────────────────────────────────────────────────────────────────
    const float char_h  = 13.0f * scale;
    const float line_sp = 4.0f  * scale;
    const float pad     = 8.0f  * scale;
    const float margin  = 10.0f * scale;
    const float line_h  = char_h + line_sp;

    // Measure widest line to determine panel width.
    float max_text_w = 0.0f;
    for (const auto& ln : lines)
        max_text_w = std::max(max_text_w,
            static_cast<float>(stb_easy_font_width(const_cast<char*>(ln.text.c_str()))) * scale);

    float panel_w = max_text_w + pad * 2.0f;
    float panel_h = line_h * static_cast<float>(lines.size()) + pad * 2.0f - line_sp;

    // Anchor top-right.
    float panel_x = static_cast<float>(fb_w) - margin - panel_w;
    float text_x  = panel_x + pad;
    float text_y  = margin + pad;

    // ── Background ────────────────────────────────────────────────────────────
    auto px2x = [&](float px) { return px / static_cast<float>(fb_w) * 2.0f - 1.0f; };
    auto px2y = [&](float px) { return 1.0f - px / static_cast<float>(fb_h) * 2.0f; };

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    draw_bg(px2x(panel_x),              px2x(panel_x + panel_w),
            px2y(margin + panel_h),     px2y(margin),
            0.0f, 0.0f, 0.0f, 0.72f);
    glDisable(GL_BLEND);

    // ── Text ──────────────────────────────────────────────────────────────────
    for (int i = 0; i < static_cast<int>(lines.size()); ++i) {
        float py = text_y + static_cast<float>(i) * line_h;
        // Headers rendered in dim gray; values in white.
        float r = lines[i].header ? 0.55f : 1.0f;
        float g = lines[i].header ? 0.55f : 1.0f;
        float b = lines[i].header ? 0.55f : 1.0f;
        draw_text(lines[i].text.c_str(), text_x, py, r, g, b, scale, fb_w, fb_h);
    }
}

DebugHUD::TrackClick DebugHUD::draw_tracks(
    const std::vector<TrackItem>& audio, int cur_audio_stream,
    const std::vector<TrackItem>& video, int cur_video_stream,
    int fb_w, int fb_h, float scale,
    float fb_click_x, float fb_click_y,
    float fb_cursor_x, float fb_cursor_y)
{
    if (fb_w <= 0 || fb_h <= 0) return {};
    glViewport(0, 0, fb_w, fb_h);

    TrackClick result;

    const float pad     = 10.0f * scale;
    const float char_h  = 13.0f * scale;
    const float line_sp = 5.0f  * scale;
    const float item_h  = char_h + line_sp;
    const float header_h = item_h + 4.0f * scale;  // slightly taller header row

    // Compute each panel's width from its longest label.
    auto panel_width = [&](const std::vector<TrackItem>& items, const char* title) {
        float w = static_cast<float>(stb_easy_font_width(const_cast<char*>(title))) * scale;
        for (const auto& it : items)
            w = std::max(w, static_cast<float>(
                stb_easy_font_width(const_cast<char*>(it.label.c_str()))) * scale);
        return w + pad * 2.0f;
    };

    float aw = panel_width(audio, "AUDIO TRACKS");
    float vw = panel_width(video, "VIDEO TRACKS");
    float gap = 20.0f * scale;

    float ah = header_h + static_cast<float>(audio.size()) * item_h + pad;
    float vh = header_h + static_cast<float>(video.size()) * item_h + pad;

    // Center both panels horizontally; align tops.
    float total_w = aw + gap + vw;
    float ax = (static_cast<float>(fb_w) - total_w) * 0.5f;
    float vx = ax + aw + gap;
    float ay = (static_cast<float>(fb_h) - std::max(ah, vh)) * 0.5f;
    float vy = ay;

    auto px2x = [&](float px) { return px / static_cast<float>(fb_w) * 2.0f - 1.0f; };
    auto px2y = [&](float py) { return 1.0f - py / static_cast<float>(fb_h) * 2.0f; };

    // Draw one dropdown panel; returns the stream_idx of a clicked item or -1.
    auto draw_panel = [&](const std::vector<TrackItem>& items, int cur_stream,
                          float px, float py, float pw, float ph,
                          const char* title) -> int
    {
        int clicked = -1;

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // Panel background
        draw_bg(px2x(px), px2x(px + pw), px2y(py + ph), px2y(py),
                0.08f, 0.08f, 0.10f, 0.92f);

        // Header row background
        draw_bg(px2x(px), px2x(px + pw), px2y(py + header_h), px2y(py),
                0.18f, 0.18f, 0.22f, 1.0f);

        // Item backgrounds: hover + selected highlights
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            float iy = py + header_h + static_cast<float>(i) * item_h;
            bool selected = (items[i].stream_idx == cur_stream);
            bool hovered  = (fb_cursor_x >= px && fb_cursor_x < px + pw &&
                             fb_cursor_y >= iy && fb_cursor_y < iy + item_h);

            if (selected)
                draw_bg(px2x(px), px2x(px + pw), px2y(iy + item_h), px2y(iy),
                        0.20f, 0.40f, 0.70f, 0.55f);
            else if (hovered)
                draw_bg(px2x(px), px2x(px + pw), px2y(iy + item_h), px2y(iy),
                        1.0f, 1.0f, 1.0f, 0.08f);

            // Click detection
            if (fb_click_x >= px && fb_click_x < px + pw &&
                fb_click_y >= iy && fb_click_y < iy + item_h)
                clicked = items[i].stream_idx;
        }

        glDisable(GL_BLEND);

        // Header text
        draw_text(title, px + pad, py + (header_h - char_h) * 0.5f,
                  0.75f, 0.75f, 0.80f, scale, fb_w, fb_h);

        // Item text
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            float iy  = py + header_h + static_cast<float>(i) * item_h + line_sp * 0.5f;
            bool  sel = (items[i].stream_idx == cur_stream);
            float r = sel ? 1.0f : 0.85f;
            float g = sel ? 1.0f : 0.85f;
            float b = sel ? 1.0f : 0.85f;
            draw_text(items[i].label.c_str(), px + pad, iy, r, g, b, scale, fb_w, fb_h);
        }

        return clicked;
    };

    int ac = draw_panel(audio, cur_audio_stream, ax, ay, aw, ah, "AUDIO TRACKS");
    int vc = draw_panel(video, cur_video_stream, vx, vy, vw, vh, "VIDEO TRACKS");
    if (ac >= 0) result.audio = ac;
    if (vc >= 0) result.video = vc;
    return result;
}

void DebugHUD::push_drift(double av_diff_ms)
{
    drift_buf_[drift_head_] = static_cast<float>(av_diff_ms);
    drift_head_ = (drift_head_ + 1) % kDriftCap;
    if (drift_count_ < kDriftCap) ++drift_count_;
}

void DebugHUD::draw_drift_graph(int fb_w, int fb_h, float scale, float bottom_offset_px)
{
    if (fb_w <= 0 || fb_h <= 0 || drift_count_ < 2) return;
    glViewport(0, 0, fb_w, fb_h);

    const float margin   = 10.0f * scale;
    const float pad      = 8.0f  * scale;
    const float char_h   = 13.0f * scale;
    const float line_sp  = 4.0f  * scale;
    // Left gutter reserved for Y-axis labels (e.g. "+100")
    const float gutter_w = 36.0f * scale;
    const float panel_w  = 370.0f * scale;
    const float panel_h  = 134.0f * scale;  // extra line for the sign note
    const float panel_x  = margin;
    const float panel_y  = static_cast<float>(fb_h) - margin - bottom_offset_px - panel_h;

    auto px2x = [&](float px) { return px / static_cast<float>(fb_w) * 2.0f - 1.0f; };
    auto px2y = [&](float py) { return 1.0f - py / static_cast<float>(fb_h) * 2.0f; };

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Dark background
    draw_bg(px2x(panel_x), px2x(panel_x + panel_w),
            px2y(panel_y + panel_h), px2y(panel_y),
            0.0f, 0.0f, 0.0f, 0.72f);

    // Graph area: two header rows at top, left gutter for Y labels.
    float line_h   = char_h + line_sp;
    float header_h = line_h * 2.0f;
    float gx0 = panel_x + pad + gutter_w;   // graph left (after Y-label gutter)
    float gx1 = panel_x + panel_w - pad;    // graph right
    float gy0 = panel_y + pad + header_h;   // graph top (screen-space, increases down)
    float gy1 = panel_y + panel_h - pad;    // graph bottom
    float gh  = gy1 - gy0;
    float gw  = gx1 - gx0;

    const float y_range_ms = 150.0f;

    auto ms_to_py = [&](float ms) -> float {
        float t  = std::clamp(ms / y_range_ms, -1.0f, 1.0f);
        float cy = (gy0 + gy1) * 0.5f;
        return cy - t * (gh * 0.5f);
    };

    float py100  = ms_to_py( 100.0f);
    float py40   = ms_to_py(  40.0f);
    float py0    = ms_to_py(   0.0f);
    float pyn40  = ms_to_py( -40.0f);
    float pyn100 = ms_to_py(-100.0f);

    // Colour bands
    draw_bg(px2x(gx0), px2x(gx1), px2y(gy0),    px2y(py100),  0.6f, 0.0f, 0.0f, 0.35f);
    draw_bg(px2x(gx0), px2x(gx1), px2y(py100),  px2y(py40),   0.6f, 0.4f, 0.0f, 0.25f);
    draw_bg(px2x(gx0), px2x(gx1), px2y(pyn40),  px2y(pyn100), 0.6f, 0.4f, 0.0f, 0.25f);
    draw_bg(px2x(gx0), px2x(gx1), px2y(pyn100), px2y(gy1),    0.6f, 0.0f, 0.0f, 0.35f);

    glDisable(GL_BLEND);

    // ── Horizontal guide lines ────────────────────────────────────────────────
    auto draw_hline = [&](float py, float r, float g, float b) {
        float pts[4] = { gx0, py, gx1, py };
        glUseProgram(text_program_);
        glUniform2f(u_fb_size_, static_cast<float>(fb_w), static_cast<float>(fb_h));
        glUniform4f(u_text_color_, r, g, b, 1.0f);
        glBindVertexArray(text_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, text_vbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(pts), pts, GL_DYNAMIC_DRAW);
        glDrawArrays(GL_LINES, 0, 2);
    };

    draw_hline(py0,    0.65f, 0.65f, 0.65f);
    draw_hline(py40,   0.55f, 0.45f, 0.10f);
    draw_hline(pyn40,  0.55f, 0.45f, 0.10f);
    draw_hline(py100,  0.50f, 0.10f, 0.10f);
    draw_hline(pyn100, 0.50f, 0.10f, 0.10f);

    // ── Y-axis labels (right-aligned in gutter) ────────────────────────────────
    // Vertically centered on each guide line: offset up by half char height.
    float label_x = panel_x + pad;
    float half_ch = char_h * 0.5f;
    struct Tick { float py; const char* text; float r, g, b; };
    Tick ticks[] = {
        { py100,  "+100", 0.80f, 0.30f, 0.30f },
        { py40,   " +40", 0.80f, 0.70f, 0.20f },
        { py0,    "   0", 0.70f, 0.70f, 0.70f },
        { pyn40,  " -40", 0.80f, 0.70f, 0.20f },
        { pyn100, "-100", 0.80f, 0.30f, 0.30f },
    };
    for (auto& t : ticks)
        draw_text(t.text, label_x, t.py - half_ch, t.r, t.g, t.b, scale, fb_w, fb_h);

    // ── Data polyline (white; NaN gaps break the strip) ───────────────────────
    glLineWidth(std::max(1.0f, scale));

    const int n     = drift_count_;
    const int start = (n < kDriftCap) ? 0 : drift_head_;

    std::vector<float> run;
    run.reserve(n * 2);

    auto flush_run = [&]() {
        if (run.size() < 4) { run.clear(); return; }
        glUseProgram(text_program_);
        glUniform2f(u_fb_size_, static_cast<float>(fb_w), static_cast<float>(fb_h));
        glUniform4f(u_text_color_, 1.0f, 1.0f, 1.0f, 1.0f);
        glBindVertexArray(text_vao_);
        glBindBuffer(GL_ARRAY_BUFFER, text_vbo_);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(run.size() * sizeof(float)),
                     run.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_LINE_STRIP, 0, static_cast<GLsizei>(run.size() / 2));
        run.clear();
    };

    for (int i = 0; i < n; ++i) {
        float ms = drift_buf_[(start + i) % kDriftCap];
        if (std::isnan(ms)) {
            flush_run();
        } else {
            float t  = static_cast<float>(i) / static_cast<float>(n - 1);
            float px = gx0 + t * gw;
            float py = ms_to_py(ms);
            run.push_back(px);
            run.push_back(py);
        }
    }
    flush_run();
    glLineWidth(1.0f);

    // ── Header: title + live current value ────────────────────────────────────
    // Find most recent non-NaN sample for live readout.
    float cur_ms = std::numeric_limits<float>::quiet_NaN();
    for (int i = n - 1; i >= 0; --i) {
        float ms = drift_buf_[(start + i) % kDriftCap];
        if (!std::isnan(ms)) { cur_ms = ms; break; }
    }

    std::string header;
    if (std::isnan(cur_ms))
        header = "A/V drift (10s)   --";
    else
        header = std::format("A/V drift (10s)   {:+.1f} ms", cur_ms);

    draw_text(header.c_str(), panel_x + pad, panel_y + pad,
              0.65f, 0.65f, 0.65f, scale, fb_w, fb_h);
    draw_text("+ video ahead  /  - audio ahead",
              panel_x + pad, panel_y + pad + line_h,
              0.38f, 0.38f, 0.38f, scale, fb_w, fb_h);
}

// ── Private helpers ───────────────────────────────────────────────────────────

void DebugHUD::draw_bg(float ndc_xmin, float ndc_xmax,
                        float ndc_ymin, float ndc_ymax,
                        float r, float g, float b, float a)
{
    glUseProgram(bg_program_);
    glUniform4f(u_rect_,     ndc_xmin, ndc_xmax, ndc_ymin, ndc_ymax);
    glUniform4f(u_bg_color_, r, g, b, a);
    glBindVertexArray(bg_vao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

float DebugHUD::draw_text(const char* text, float px, float py,
                           float r, float g, float b,
                           float scale, int fb_w, int fb_h)
{
    // stb_easy_font vertex: { float x, y, z; uint8_t col[4]; } = 16 bytes.
    // Render at origin then scale+translate to target position.
    struct StbVert { float x, y, z; uint8_t col[4]; };
    static char vbuf[16384];

    int num_quads = stb_easy_font_print(0.0f, 0.0f, const_cast<char*>(text),
                                         nullptr, vbuf, sizeof(vbuf));
    if (num_quads == 0) return 0.0f;

    // Convert quads (GL_QUADS, 4 verts each) → triangles (2 per quad, 6 verts each).
    // GL_QUADS is not available in core profile 4.1.
    const int num_tris = num_quads * 2;
    std::vector<float> tri;
    tri.reserve(num_tris * 3 * 2);  // 3 verts/tri, 2 floats/vert

    auto* src = reinterpret_cast<StbVert*>(vbuf);
    for (int q = 0; q < num_quads; ++q) {
        StbVert* v = src + q * 4;
        // quad → two CCW triangles: (0,1,2) and (0,2,3)
        constexpr int order[6] = {0, 1, 2, 0, 2, 3};
        for (int i : order) {
            tri.push_back(px + v[i].x * scale);
            tri.push_back(py + v[i].y * scale);
        }
    }

    glUseProgram(text_program_);
    glUniform2f(u_fb_size_,    static_cast<float>(fb_w), static_cast<float>(fb_h));
    glUniform4f(u_text_color_, r, g, b, 1.0f);

    glBindVertexArray(text_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, text_vbo_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(tri.size() * sizeof(float)),
                 tri.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, num_tris * 3);

    return static_cast<float>(stb_easy_font_width(const_cast<char*>(text))) * scale;
}

bool DebugHUD::compile_shaders()
{
    auto compile = [](GLenum type, const char* src) -> GLuint {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        GLint ok = 0;
        glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[512];
            glGetShaderInfoLog(s, sizeof(log), nullptr, log);
            logger::error("DebugHUD shader compile error: {}", log);
            glDeleteShader(s);
            return 0;
        }
        return s;
    };

    auto link = [](GLuint vert, GLuint frag) -> GLuint {
        GLuint prog = glCreateProgram();
        glAttachShader(prog, vert);
        glAttachShader(prog, frag);
        glLinkProgram(prog);
        glDeleteShader(vert);
        glDeleteShader(frag);
        GLint ok = 0;
        glGetProgramiv(prog, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[512];
            glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
            logger::error("DebugHUD shader link error: {}", log);
            glDeleteProgram(prog);
            return 0;
        }
        return prog;
    };

    // Background program
    GLuint bg_v = compile(GL_VERTEX_SHADER,   kBgVert);
    GLuint bg_f = compile(GL_FRAGMENT_SHADER, kBgFrag);
    if (!bg_v || !bg_f) { glDeleteShader(bg_v); glDeleteShader(bg_f); return false; }
    bg_program_ = link(bg_v, bg_f);
    if (!bg_program_) return false;
    u_rect_     = glGetUniformLocation(bg_program_, "u_rect");
    u_bg_color_ = glGetUniformLocation(bg_program_, "u_bg_color");

    // Text program
    GLuint tx_v = compile(GL_VERTEX_SHADER,   kTextVert);
    GLuint tx_f = compile(GL_FRAGMENT_SHADER, kTextFrag);
    if (!tx_v || !tx_f) { glDeleteShader(tx_v); glDeleteShader(tx_f); return false; }
    text_program_ = link(tx_v, tx_f);
    if (!text_program_) return false;
    u_fb_size_    = glGetUniformLocation(text_program_, "u_fb_size");
    u_text_color_ = glGetUniformLocation(text_program_, "u_text_color");

    return true;
}
