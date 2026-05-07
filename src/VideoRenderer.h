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
// SDR pipeline:
//   decoded YUV → swscale(BT.601/BT.709/BT.2020 → BT.709 RGB24, full range)
//   GLSL = passthrough.
//
// HDR pipeline (color_trc = PQ or HLG):
//   decoded YUV → swscale(BT.2020 matrix → RGB48LE, transfer untouched)
//   GLSL = PQ/HLG EOTF → Hable tonemap → BT.2020→BT.709 primaries → sRGB OETF.
//   16-bit-per-channel texture preserves HDR range through the GPU upload.
class VideoRenderer {
public:
    VideoRenderer() = default;
    ~VideoRenderer();

    VideoRenderer(const VideoRenderer&) = delete;
    VideoRenderer& operator=(const VideoRenderer&) = delete;

    [[nodiscard]] bool init(int width, int height, AVPixelFormat src_fmt,
                            AVColorSpace                  colorspace = AVCOL_SPC_UNSPECIFIED,
                            AVColorRange                  range      = AVCOL_RANGE_UNSPECIFIED,
                            AVColorTransferCharacteristic trc        = AVCOL_TRC_UNSPECIFIED,
                            AVColorPrimaries              primaries  = AVCOL_PRI_UNSPECIFIED);

    // Upload a decoded frame (any format; converted internally).
    void upload(AVFrame* frame);

    // Draw the fullscreen quad.
    void draw();

    // True if HDR tonemapping path is active.
    [[nodiscard]] bool hdr() const { return hdr_; }

    // 0=PQ, 1=HLG, -1 if SDR. Useful for debug HUD.
    [[nodiscard]] int hdr_mode() const { return hdr_mode_; }

private:
    GLuint vao_     = 0;
    GLuint program_ = 0;
    GLuint tex_     = 0;

    int src_w_ = 0;
    int src_h_ = 0;

    bool  hdr_      = false;
    int   hdr_mode_ = -1;       // 0=PQ, 1=HLG, -1=SDR

    struct SwsContext* sws_       = nullptr;
    AVFrame*           converted_ = nullptr;   // RGB24 (SDR) or RGB48LE (HDR)

    [[nodiscard]] bool   compile_shaders(bool hdr_path);
    [[nodiscard]] GLuint compile_shader(GLenum type, const char* src);
};
