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
// SDR SW pipeline:
//   decoded YUV → swscale(BT.601/BT.709/BT.2020 → BT.709 RGB24, full range)
//   GLSL = passthrough.
//
// HDR SW pipeline (color_trc = PQ or HLG):
//   decoded YUV → swscale(BT.2020 matrix → RGB48LE, transfer untouched)
//   GLSL = PQ/HLG EOTF → Hable tonemap → BT.2020→BT.709 primaries → sRGB OETF.
//
// HW zero-copy path (VideoToolbox frames, both SDR and HDR):
//   AV_PIX_FMT_VIDEOTOOLBOX → CVPixelBuffer → IOSurface → two RECT textures
//   (Y-plane + CbCr-plane, NV12/P010 layout). GLSL does YUV→RGB on GPU using
//   a precomputed matrix + offset, then either passthrough (SDR) or the same
//   PQ/HLG tonemap chain (HDR). No sws, no CPU copy, no glTexSubImage2D.
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
    GLuint program_ = 0;    // SW path (SDR passthrough or HDR tonemap on RGB texture)
    GLuint tex_     = 0;    // SW path single RGB texture

    // HW zero-copy path (macOS VideoToolbox → IOSurface).
    GLuint    hw_program_        = 0;
    GLuint    y_tex_             = 0;    // GL_TEXTURE_RECTANGLE, R8 or R16 (luma)
    GLuint    uv_tex_            = 0;    // GL_TEXTURE_RECTANGLE, RG8 or RG16 (chroma)
    AVFrame*  hw_frame_ref_      = nullptr;  // holds the CVPixelBuffer alive across draw()
    int       hw_tex_w_          = 0;
    int       hw_tex_h_          = 0;
    bool      hw_shader_built_   = false;
    bool      hw_is_10bit_       = false;
    bool      hw_last_uploaded_  = false;    // draw() dispatches SW vs HW based on this

    int src_w_ = 0;
    int src_h_ = 0;

    bool  hdr_      = false;
    int   hdr_mode_ = -1;       // 0=PQ, 1=HLG, -1=SDR

    // Cached stream color params — used to build the HW-path YUV→RGB uniforms.
    AVColorSpace                  cs_        = AVCOL_SPC_UNSPECIFIED;
    AVColorRange                  range_     = AVCOL_RANGE_UNSPECIFIED;
    AVColorTransferCharacteristic trc_       = AVCOL_TRC_UNSPECIFIED;

    struct SwsContext* sws_        = nullptr;
    AVFrame*           converted_  = nullptr;   // RGB24 (SDR) or RGB48LE (HDR)
    AVPixelFormat      sws_src_fmt_ = AV_PIX_FMT_NONE;   // format sws_ was built for

    // Build sws_ (freeing any previous) for the given source pixel format.
    // Used at init and again on the fly if frame->format changes mid-stream
    // (e.g., a scrub result arriving in a different yuv layout).
    [[nodiscard]] bool   build_sws(AVPixelFormat src_fmt);

    // Lazily build the HW-path GL program + textures on first HW frame.
    [[nodiscard]] bool   init_hw_lazy(bool ten_bit);

    // Upload the frame's CVPixelBuffer IOSurface planes into y_tex_/uv_tex_.
    // Returns false on any Core Video / CGL error → caller can fall back to SW.
    [[nodiscard]] bool   upload_hw(AVFrame* frame);

    [[nodiscard]] bool   compile_shaders(bool hdr_path);
    [[nodiscard]] GLuint compile_shader(GLenum type, const char* src);
};
