#pragma once

#include <glad/gl.h>

// Renders a scrub bar overlay at the bottom of the framebuffer.
//
// Layout (bottom → top, in framebuffer pixels):
//   kBackingPx  — semi-transparent dark panel for contrast
//   kTrackPx    — thin gray track centered in the panel
//   fill        — white progress fill
//   kThumbDiam  — circular white playhead at current position
//
// kHitPx is the logical window-pixel height used by main.cpp for mouse hit-testing.
class ScrubBar {
public:
    ScrubBar() = default;
    ~ScrubBar();

    ScrubBar(const ScrubBar&) = delete;
    ScrubBar& operator=(const ScrubBar&) = delete;

    [[nodiscard]] bool init();

    // Draw the bar. pos and duration are in seconds.
    // If duration <= 0 the bar is not drawn (live streams).
    void draw(double pos, double duration, int fb_w, int fb_h);

    // Logical window-pixel height of the interactive zone (for mouse hit-testing).
    static constexpr int kHitPx = 40;

private:
    static constexpr int kBackingPx = 32;  // total panel height (fb px)
    static constexpr int kTrackPx   = 4;   // thin track line height (fb px)
    static constexpr int kThumbDiam = 14;  // playhead circle diameter (fb px)

    GLuint vao_     = 0;
    GLuint program_ = 0;

    GLint u_xmin_     = -1;
    GLint u_xmax_     = -1;
    GLint u_ymin_     = -1;
    GLint u_ymax_     = -1;
    GLint u_color_    = -1;
    GLint u_alpha_    = -1;
    GLint u_thumb_cx_ = -1;  // circle center x in fb pixels; < 0 = disabled
    GLint u_thumb_cy_ = -1;  // circle center y in fb pixels
    GLint u_thumb_r_  = -1;  // circle radius in fb pixels; <= 0 = no clipping

    void draw_quad(float xmin, float xmax, float ymin, float ymax,
                   float r, float g, float b, float a = 1.0f,
                   float thumb_cx = -1.0f, float thumb_cy = 0.0f, float thumb_r = 0.0f);

    [[nodiscard]] bool compile_shaders();
};
