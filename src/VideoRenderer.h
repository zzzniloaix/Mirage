#pragma once

#include <glad/gl.h>

#include "ColourInterpretation.h"

#include <cstdint>

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
//   (Y-plane + CbCr-plane, NV12/P010 layout, 8- or 16-bit texels). GLSL does
//   YUV→RGB on GPU using a precomputed matrix + offset, then either
//   passthrough (SDR) or the same PQ/HLG tonemap chain (HDR). No sws, no CPU
//   copy, no glTexSubImage2D. A buffer of another layout ('y416') is copied to
//   the CPU and takes the SW pipeline.
//
// Each frame's own colour tags are followed (upload()): a change of
// interpretation updates the uniforms, and a flip between SDR and HDR
// rebuilds the programs and the staging frame too.
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

    // The colour interpretation frames are converted with, as of the last
    // upload(): the stream's tags at init(), then each frame's specified ones
    // (an unspecified tag keeps the value before it). A matrix no tag gives is
    // derived for the HDR state and the frame size at init() and again only
    // when a frame flips between SDR and HDR; otherwise it is kept, whatever
    // the frame size (next_interpretation()). Any range but full is treated as
    // limited. Primaries are reported, not used.
    using Interpretation = ColourInterpretation;
    [[nodiscard]] Interpretation interpretation() const { return interp_; }

private:
    GLuint vao_     = 0;
    GLuint program_ = 0;    // SW path (SDR passthrough or HDR tonemap on RGB texture)
    GLuint tex_     = 0;    // SW path single RGB texture

    // HW zero-copy path (macOS VideoToolbox → IOSurface). hw_frame_ref_ doubles
    // as the "HW mode active" signal for draw() — non-null iff the last upload
    // was a VT frame whose CVPixelBuffer must stay alive through draw().
    GLuint    hw_program_        = 0;
    GLuint    y_tex_             = 0;    // GL_TEXTURE_RECTANGLE, R8 or R16 (luma)
    GLuint    uv_tex_            = 0;    // GL_TEXTURE_RECTANGLE, RG8 or RG16 (chroma)
    AVFrame*  hw_frame_ref_      = nullptr;

    // The buffers the HW path bound last, which its matrix uniforms are built
    // for: their pixel format ('x420', ...; 0 = none yet), container and
    // sample depth. The matrix is uploaded again when the format changes.
    std::uint32_t hw_format_         = 0;
    int           hw_container_bits_ = 0;
    int           hw_sample_bits_    = 0;
    std::uint32_t hw_refused_        = 0;   // last buffer format refused (logged once)

    // A VideoToolbox frame upload_hw() did not bind, copied to the CPU for
    // the SW path; reused frame to frame.
    AVFrame*      hw_copy_           = nullptr;
    bool          hw_copy_failed_    = false;   // the last copy failed (logged once)

    int src_w_ = 0;
    int src_h_ = 0;

    bool  hdr_      = false;
    int   hdr_mode_ = -1;       // 0=PQ, 1=HLG, -1=SDR

    ColourTags           tags_;     // the tags followed (follow_tags())
    ColourInterpretation interp_;   // what frames are converted with (next_interpretation())

    struct SwsContext* sws_        = nullptr;
    AVFrame*           converted_  = nullptr;   // RGB24 (SDR) or RGB48LE (HDR)
    AVPixelFormat      sws_src_fmt_ = AV_PIX_FMT_NONE;   // format sws_ was built for

    // Build sws_ (freeing any previous) for the given source pixel format.
    // Used at init and again on the fly if frame->format changes mid-stream
    // (e.g., a scrub result arriving in a different yuv layout).
    [[nodiscard]] bool   build_sws(AVPixelFormat src_fmt);

    // (Re)allocate the SW path's staging frame (converted_) and texture
    // (tex_) for the current size and HDR mode: at init, on a frame size
    // change and on an SDR/HDR flip.
    [[nodiscard]] bool   alloc_sw_target();

    // Apply a new interpretation (upload(), between two draws): every time
    // the uniforms of both programs and sws's coefficients; on a Program
    // change the programs, the staging frame and texture and sws too.
    [[nodiscard]] bool   reconfigure(const ColourInterpretation& next, InterpretationChange change);

    // SW program uniforms: sampler, HDR mode.
    void                 set_sw_uniforms();

    // HW-path textures, made on the first HW frame and kept; the HW program,
    // if there is none.
    [[nodiscard]] bool   init_hw_lazy();

    // Build hw_program_ (deleting any previous) for the current HDR mode.
    [[nodiscard]] bool   build_hw_program();

    // HW program uniforms: YUV→RGB matrix + offset for interp_ and the bound
    // buffers' depths, HDR mode.
    void                 set_hw_uniforms();

    // Upload the frame's CVPixelBuffer IOSurface planes into y_tex_/uv_tex_.
    // Returns false for a buffer it refuses (hw_container_bits()) or on any
    // Core Video / CGL error → caller copies the frame to the CPU instead.
    [[nodiscard]] bool   upload_hw(AVFrame* frame);

    // A CPU copy of a VideoToolbox frame (av_hwframe_transfer_data, in its
    // pool's software format) in hw_copy_, or nullptr.
    [[nodiscard]] AVFrame* copy_to_cpu(const AVFrame* frame);

    [[nodiscard]] bool   compile_shaders(bool hdr_path);
    [[nodiscard]] GLuint compile_shader(GLenum type, const char* src);
};
