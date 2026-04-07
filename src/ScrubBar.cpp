#include "ScrubBar.h"
#include "Logger.h"

#include <algorithm>
#include <cmath>

// Vertex shader: fullscreen quad from NDC uniforms.
// gl_VertexID corner order:  0=BL  1=BR  2=TL  3=TR  (triangle strip)
static const char* kVertSrc = R"glsl(
#version 410 core
uniform float u_xmin;
uniform float u_xmax;
uniform float u_ymin;
uniform float u_ymax;
void main() {
    float x = (gl_VertexID & 1) != 0 ? u_xmax : u_xmin;
    float y = (gl_VertexID & 2) != 0 ? u_ymax : u_ymin;
    gl_Position = vec4(x, y, 0.0, 1.0);
}
)glsl";

// Fragment shader: flat color with optional circular clip.
// gl_FragCoord.xy is in framebuffer pixels, origin = bottom-left.
// Set u_thumb_r <= 0 to skip circle clipping (plain quad).
static const char* kFragSrc = R"glsl(
#version 410 core
uniform vec3  u_color;
uniform float u_alpha;
uniform float u_thumb_cx;   // circle center x (fb px)
uniform float u_thumb_cy;   // circle center y (fb px)
uniform float u_thumb_r;    // circle radius (fb px); <= 0 = no clip

out vec4 frag_color;
void main() {
    if (u_thumb_r > 0.0) {
        vec2 d = gl_FragCoord.xy - vec2(u_thumb_cx, u_thumb_cy);
        if (dot(d, d) > u_thumb_r * u_thumb_r) discard;
    }
    frag_color = vec4(u_color, u_alpha);
}
)glsl";

ScrubBar::~ScrubBar()
{
    if (program_) glDeleteProgram(program_);
    if (vao_)     glDeleteVertexArrays(1, &vao_);
}

bool ScrubBar::init()
{
    if (!compile_shaders())
        return false;

    glGenVertexArrays(1, &vao_);

    u_xmin_     = glGetUniformLocation(program_, "u_xmin");
    u_xmax_     = glGetUniformLocation(program_, "u_xmax");
    u_ymin_     = glGetUniformLocation(program_, "u_ymin");
    u_ymax_     = glGetUniformLocation(program_, "u_ymax");
    u_color_    = glGetUniformLocation(program_, "u_color");
    u_alpha_    = glGetUniformLocation(program_, "u_alpha");
    u_thumb_cx_ = glGetUniformLocation(program_, "u_thumb_cx");
    u_thumb_cy_ = glGetUniformLocation(program_, "u_thumb_cy");
    u_thumb_r_  = glGetUniformLocation(program_, "u_thumb_r");
    return true;
}

void ScrubBar::draw_quad(float xmin, float xmax, float ymin, float ymax,
                         float r, float g, float b, float a,
                         float thumb_cx, float thumb_cy, float thumb_r)
{
    glUniform1f(u_xmin_, xmin);
    glUniform1f(u_xmax_, xmax);
    glUniform1f(u_ymin_, ymin);
    glUniform1f(u_ymax_, ymax);
    glUniform3f(u_color_, r, g, b);
    glUniform1f(u_alpha_, a);
    glUniform1f(u_thumb_cx_, thumb_cx);
    glUniform1f(u_thumb_cy_, thumb_cy);
    glUniform1f(u_thumb_r_,  thumb_r);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void ScrubBar::draw(double pos, double duration, int fb_w, int fb_h)
{
    if (duration <= 0.0 || fb_w <= 0 || fb_h <= 0) return;

    double frac = std::clamp(pos / duration, 0.0, 1.0);

    // All NDC values: y=-1 = bottom, y=1 = top.
    auto px_to_ndc_y = [&](float px) {
        return -1.0f + 2.0f * px / static_cast<float>(fb_h);
    };
    auto px_to_ndc_x = [&](float px) {
        return -1.0f + 2.0f * px / static_cast<float>(fb_w);
    };

    // ── Geometry ──────────────────────────────────────────────────────────────
    float backing_ymin = -1.0f;
    float backing_ymax = px_to_ndc_y(static_cast<float>(kBackingPx));

    // Track: kTrackPx tall, centered in the backing panel
    float track_cy  = static_cast<float>(kBackingPx) * 0.5f;
    float track_ymin = px_to_ndc_y(track_cy - kTrackPx * 0.5f);
    float track_ymax = px_to_ndc_y(track_cy + kTrackPx * 0.5f);

    // Playhead thumb: circle centered on track center at current x
    float thumb_cx_px = static_cast<float>(frac) * static_cast<float>(fb_w);
    float thumb_cy_px = track_cy;
    float thumb_r_px  = static_cast<float>(kThumbDiam) * 0.5f;

    // Thumb bounding quad in NDC (slightly over-sized so the circle fits)
    float thumb_ndc_hw = thumb_r_px / static_cast<float>(fb_w) * 2.0f;
    float thumb_ndc_hh = thumb_r_px / static_cast<float>(fb_h) * 2.0f;
    float thumb_cx_ndc = px_to_ndc_x(thumb_cx_px);

    // Fill extends from left edge to playhead center
    float fill_xmax = thumb_cx_ndc;

    // ── Draw ──────────────────────────────────────────────────────────────────
    glUseProgram(program_);
    glBindVertexArray(vao_);
    glViewport(0, 0, fb_w, fb_h);

    // 1. Semi-transparent dark backing panel
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    draw_quad(-1.0f, 1.0f, backing_ymin, backing_ymax,
              0.0f, 0.0f, 0.0f, 0.60f);
    glDisable(GL_BLEND);

    // 2. Track (full width, dim gray)
    draw_quad(-1.0f, 1.0f, track_ymin, track_ymax,
              0.40f, 0.40f, 0.40f);

    // 3. Filled progress (white, up to playhead center)
    if (fill_xmax > -1.0f)
        draw_quad(-1.0f, fill_xmax, track_ymin, track_ymax,
                  1.0f, 1.0f, 1.0f);

    // 4. Circular playhead thumb (white circle, clipped in fragment shader)
    draw_quad(thumb_cx_ndc - thumb_ndc_hw, thumb_cx_ndc + thumb_ndc_hw,
              px_to_ndc_y(thumb_cy_px - thumb_r_px),
              px_to_ndc_y(thumb_cy_px + thumb_r_px),
              1.0f, 1.0f, 1.0f, 1.0f,
              thumb_cx_px, thumb_cy_px, thumb_r_px);
}

bool ScrubBar::compile_shaders()
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
            logger::error("ScrubBar shader compile failed: {}", log);
            glDeleteShader(s);
            return 0;
        }
        return s;
    };

    GLuint vert = compile(GL_VERTEX_SHADER,   kVertSrc);
    GLuint frag = compile(GL_FRAGMENT_SHADER, kFragSrc);
    if (!vert || !frag) {
        glDeleteShader(vert);
        glDeleteShader(frag);
        return false;
    }

    program_ = glCreateProgram();
    glAttachShader(program_, vert);
    glAttachShader(program_, frag);
    glLinkProgram(program_);

    glDeleteShader(vert);
    glDeleteShader(frag);

    GLint ok = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
        logger::error("ScrubBar shader link failed: {}", log);
        return false;
    }
    return true;
}
