#pragma once

#include <glad/gl.h>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

// Renders a video frame to a fullscreen OpenGL 4.1 quad.
//
// Color pipeline (CPU side, via FFmpeg):
//   decoded YUV (any format) → swscale → RGB24
//   sws_setColorspaceDetails applies:
//     • correct YUV→RGB matrix (BT.601 / BT.709 / BT.2020)
//     • limited→full range expansion
//   output is sRGB-compatible (BT.709 primaries, gamma-encoded, full range)
//
// GPU side: single RGB texture, passthrough GLSL shader — no color math.
class VideoRenderer {
public:
    VideoRenderer() = default;
    ~VideoRenderer();

    VideoRenderer(const VideoRenderer&) = delete;
    VideoRenderer& operator=(const VideoRenderer&) = delete;

    [[nodiscard]] bool init(int width, int height, AVPixelFormat src_fmt,
                            AVColorSpace  colorspace  = AVCOL_SPC_UNSPECIFIED,
                            AVColorRange  color_range = AVCOL_RANGE_UNSPECIFIED);

    // Upload a decoded frame (any format; converted to RGB24 internally).
    void upload(AVFrame* frame);

    // Draw the fullscreen quad.
    void draw();

private:
    GLuint vao_     = 0;
    GLuint program_ = 0;
    GLuint tex_     = 0;   // single sRGB-converted RGB texture

    int src_w_ = 0;
    int src_h_ = 0;

    struct SwsContext* sws_       = nullptr;
    AVFrame*           converted_ = nullptr;   // RGB24 staging frame

    [[nodiscard]] bool   compile_shaders();
    [[nodiscard]] GLuint compile_shader(GLenum type, const char* src);
};
