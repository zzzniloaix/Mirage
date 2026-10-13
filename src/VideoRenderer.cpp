#include "VideoRenderer.h"
#include "Logger.h"
#include "YuvMatrix.h"

extern "C" {
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
}

#ifdef __APPLE__
  #include <CoreVideo/CoreVideo.h>
  #include <IOSurface/IOSurface.h>
  #include <OpenGL/OpenGL.h>
  #include <OpenGL/CGLIOSurface.h>
#endif

// ── Shared vertex shader (fullscreen triangle) ────────────────────────────────

// av_get_pix_fmt_name() returns nullptr for AV_PIX_FMT_NONE; std::format
// would dereference it.
static const char* pix_fmt_name(AVPixelFormat f)
{
    const char* n = av_get_pix_fmt_name(f);
    return n ? n : "none";
}

static const char* kVertSrc = R"glsl(
#version 410 core
out vec2 uv;
void main() {
    vec2 pos = vec2(
        float((gl_VertexID & 1) * 4 - 1),
        float((gl_VertexID >> 1) * 4 - 1)
    );
    uv = pos * 0.5 + 0.5;
    uv.y = 1.0 - uv.y;
    gl_Position = vec4(pos, 0.0, 1.0);
}
)glsl";

// ── SDR fragment shader (passthrough) ─────────────────────────────────────────

static const char* kFragSdr = R"glsl(
#version 410 core
uniform sampler2D tex_rgb;
in  vec2 uv;
out vec4 frag_color;
void main() {
    frag_color = vec4(texture(tex_rgb, uv).rgb, 1.0);
}
)glsl";

// ── HW zero-copy fragment shaders (RECT textures, NV12/P010 planes) ───────────
//
// Two rectangle textures: Y (single channel) + CbCr (two channels). Sampled
// with non-normalized coords via textureSize(). YUV→RGB uses a precomputed
// matrix + offset uniform (baked for the stream's colorspace + range +
// bit depth), so the shader is agnostic to BT.601/709/2020 or 8/10-bit.
//
// SDR variant: matrix output is display-referred BT.709 sRGB-encoded linear
// approximation (matrix already includes the standard limited-range scaling).
// HDR variant: matrix output is still transfer-encoded RGB in BT.2020
// primaries; PQ/HLG EOTF + tonemap + primary conversion + sRGB OETF follow.

static const char* kFragSdrHw = R"glsl(
#version 410 core
uniform sampler2DRect tex_y;
uniform sampler2DRect tex_uv;
uniform mat3          u_yuv2rgb;
uniform vec3          u_yuv_offset;
in  vec2  uv;
out vec4  frag_color;
void main() {
    vec2 y_size  = vec2(textureSize(tex_y));
    vec2 uv_size = vec2(textureSize(tex_uv));
    float y   = texture(tex_y,  uv * y_size ).r;
    vec2  cbcr = texture(tex_uv, uv * uv_size).rg;
    vec3  rgb = u_yuv2rgb * (vec3(y, cbcr) - u_yuv_offset);
    frag_color = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
)glsl";

static const char* kFragHdrHw = R"glsl(
#version 410 core
uniform sampler2DRect tex_y;
uniform sampler2DRect tex_uv;
uniform mat3          u_yuv2rgb;
uniform vec3          u_yuv_offset;
uniform int           u_hdr_mode;
in  vec2  uv;
out vec4  frag_color;

vec3 pq_eotf(vec3 e) {
    const float m1 = 2610.0 / 16384.0;
    const float m2 = 2523.0 / 4096.0 * 128.0;
    const float c1 = 3424.0 / 4096.0;
    const float c2 = 2413.0 / 4096.0 * 32.0;
    const float c3 = 2392.0 / 4096.0 * 32.0;
    vec3 ep  = pow(max(e, vec3(0.0)), vec3(1.0 / m2));
    vec3 num = max(ep - vec3(c1), vec3(0.0));
    vec3 den = c2 - c3 * ep;
    return 10000.0 * pow(num / den, vec3(1.0 / m1));
}
vec3 hlg_inv_oetf(vec3 e) {
    const float a = 0.17883277;
    const float b = 0.28466892;
    const float c = 0.55991073;
    vec3 lo = (e * e) / 3.0;
    vec3 hi = (exp((e - vec3(c)) / a) + vec3(b)) / 12.0;
    return mix(lo, hi, step(vec3(0.5), e));
}
vec3 hlg_ootf(vec3 scene, float peak_nits) {
    float y = dot(scene, vec3(0.2627, 0.6780, 0.0593));
    float gain = peak_nits * pow(max(y, 1e-6), 0.2);
    return scene * gain;
}
vec3 hable(vec3 x) {
    const float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;
    return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F)) - E/F;
}
const float kHableWhite = 11.2;
const mat3 kBt2020ToBt709 = mat3(
     1.6605, -0.1246, -0.0182,
    -0.5876,  1.1329, -0.1006,
    -0.0728, -0.0083,  1.1187
);
vec3 srgb_oetf(vec3 x) {
    vec3 lo = 12.92 * x;
    vec3 hi = 1.055 * pow(max(x, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
    return mix(lo, hi, step(vec3(0.0031308), x));
}

void main() {
    vec2 y_size  = vec2(textureSize(tex_y));
    vec2 uv_size = vec2(textureSize(tex_uv));
    float y    = texture(tex_y,  uv * y_size ).r;
    vec2  cbcr = texture(tex_uv, uv * uv_size).rg;
    // YUV→RGB in the (still transfer-encoded) BT.2020 domain.
    vec3 e = u_yuv2rgb * (vec3(y, cbcr) - u_yuv_offset);

    vec3 lin;
    if (u_hdr_mode == 0) {
        const float kPeakNits = 1000.0;
        lin = pq_eotf(e) / kPeakNits;
    } else {
        const float kPeakNits = 1000.0;
        vec3 scene = hlg_inv_oetf(e);
        lin = hlg_ootf(scene, kPeakNits) / kPeakNits;
    }
    vec3 toned  = hable(lin) / hable(vec3(kHableWhite));
    vec3 rgb709 = clamp(kBt2020ToBt709 * toned, vec3(0.0), vec3(1.0));
    frag_color  = vec4(srgb_oetf(rgb709), 1.0);
}
)glsl";

// ── HDR fragment shader (PQ or HLG → tonemap → BT.709 sRGB) ───────────────────
//
// Input texture is BT.2020-primary, transfer-encoded RGB16 normalized to [0,1].
// `u_hdr_mode` selects: 0=PQ (SMPTE 2084), 1=HLG (ARIB STD-B67).
// Hable filmic tonemap operator; peak luminance assumed 1000 nits for PQ
// (HDR10 "MaxCLL" content typical) and 1000 nits for HLG OOTF target.
//
// Pipeline:
//   1. Sample 16-bit normalized RGB
//   2. EOTF → linear scene/display light (nits)
//   3. Normalize by peak nits → [0,1] linear range
//   4. Hable tonemap → SDR linear
//   5. BT.2020 → BT.709 primary matrix (linear-light)
//   6. Clamp [0,1] then sRGB OETF → display-encoded
static const char* kFragHdr = R"glsl(
#version 410 core
uniform sampler2D tex_rgb;
uniform int       u_hdr_mode;     // 0=PQ, 1=HLG
in  vec2  uv;
out vec4  frag_color;

// SMPTE ST 2084 PQ inverse EOTF: encoded [0,1] → linear nits [0, 10000].
vec3 pq_eotf(vec3 e) {
    const float m1 = 2610.0 / 16384.0;
    const float m2 = 2523.0 / 4096.0 * 128.0;
    const float c1 = 3424.0 / 4096.0;
    const float c2 = 2413.0 / 4096.0 * 32.0;
    const float c3 = 2392.0 / 4096.0 * 32.0;
    vec3 ep = pow(max(e, vec3(0.0)), vec3(1.0 / m2));
    vec3 num = max(ep - vec3(c1), vec3(0.0));
    vec3 den = c2 - c3 * ep;
    return 10000.0 * pow(num / den, vec3(1.0 / m1));
}

// ARIB STD-B67 HLG inverse OETF: encoded [0,1] → scene linear [0,12].
vec3 hlg_inv_oetf(vec3 e) {
    const float a = 0.17883277;
    const float b = 0.28466892;     // 1 - 4*a
    const float c = 0.55991073;     // 0.5 - a*ln(4*a)
    vec3 lo = (e * e) / 3.0;
    vec3 hi = (exp((e - vec3(c)) / a) + vec3(b)) / 12.0;
    return mix(lo, hi, step(vec3(0.5), e));
}

// HLG OOTF: scene → display linear, with system gamma 1.2 and peak nits.
// Y is computed with BT.2020 luma weights.
vec3 hlg_ootf(vec3 scene, float peak_nits) {
    float y = dot(scene, vec3(0.2627, 0.6780, 0.0593));
    float gain = peak_nits * pow(max(y, 1e-6), 0.2);
    return scene * gain;
}

// John Hable's filmic curve ("Uncharted 2" tonemap). Input/output linear [0,∞].
vec3 hable(vec3 x) {
    const float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;
    return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F)) - E/F;
}
const float kHableWhite = 11.2;     // white point pre-tonemap

// BT.2020 → BT.709 primaries (linear light), per ITU-R BT.2087 derivation.
const mat3 kBt2020ToBt709 = mat3(
     1.6605, -0.1246, -0.0182,
    -0.5876,  1.1329, -0.1006,
    -0.0728, -0.0083,  1.1187
);

// sRGB OETF (gamma encode).
vec3 srgb_oetf(vec3 x) {
    vec3 lo = 12.92 * x;
    vec3 hi = 1.055 * pow(max(x, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055;
    return mix(lo, hi, step(vec3(0.0031308), x));
}

void main() {
    vec3 e = texture(tex_rgb, uv).rgb;
    vec3 lin;                       // BT.2020 linear, normalized to SDR [0,1] range

    if (u_hdr_mode == 0) {
        // PQ: decode to nits, normalize by 1000-nit reference peak.
        const float kPeakNits = 1000.0;
        lin = pq_eotf(e) / kPeakNits;
    } else {
        // HLG: scene-linear, then OOTF to display-linear, normalize by 1000-nit peak.
        const float kPeakNits = 1000.0;
        vec3 scene = hlg_inv_oetf(e);
        lin = hlg_ootf(scene, kPeakNits) / kPeakNits;
    }

    // Hable tonemap with white-point normalization.
    vec3 toned = hable(lin) / hable(vec3(kHableWhite));

    // BT.2020 → BT.709 in linear light, clamp, then encode for sRGB display.
    vec3 rgb709 = clamp(kBt2020ToBt709 * toned, vec3(0.0), vec3(1.0));
    frag_color  = vec4(srgb_oetf(rgb709), 1.0);
}
)glsl";

// ── Color space helpers ───────────────────────────────────────────────────────

static int av_cs_to_sws(AVColorSpace cs)
{
    switch (cs) {
        case AVCOL_SPC_BT709:        return SWS_CS_ITU709;
        case AVCOL_SPC_BT2020_NCL:
        case AVCOL_SPC_BT2020_CL:    return SWS_CS_BT2020;
        case AVCOL_SPC_SMPTE170M:
        case AVCOL_SPC_BT470BG:
        default:                     return SWS_CS_DEFAULT;   // ITU601
    }
}

// Map AVColorTransferCharacteristic to our HDR mode (0=PQ, 1=HLG, -1=SDR).
static int detect_hdr_mode(AVColorTransferCharacteristic trc)
{
    switch (trc) {
        case AVCOL_TRC_SMPTE2084:    return 0;   // PQ
        case AVCOL_TRC_ARIB_STD_B67: return 1;   // HLG
        default:                     return -1;
    }
}

// ── VideoRenderer ─────────────────────────────────────────────────────────────

VideoRenderer::~VideoRenderer()
{
    if (sws_)          sws_freeContext(sws_);
    if (converted_)    av_frame_free(&converted_);
    if (hw_frame_ref_) av_frame_free(&hw_frame_ref_);
    if (tex_)          glDeleteTextures(1, &tex_);
    if (y_tex_)        glDeleteTextures(1, &y_tex_);
    if (uv_tex_)       glDeleteTextures(1, &uv_tex_);
    if (vao_)          glDeleteVertexArrays(1, &vao_);
    if (program_)      glDeleteProgram(program_);
    if (hw_program_)   glDeleteProgram(hw_program_);
}

bool VideoRenderer::init(int width, int height, AVPixelFormat src_fmt,
                         AVColorSpace                  colorspace,
                         AVColorRange                  color_range,
                         AVColorTransferCharacteristic trc,
                         AVColorPrimaries              /*primaries*/)
{
    src_w_ = width;
    src_h_ = height;

    hdr_mode_ = detect_hdr_mode(trc);
    hdr_      = (hdr_mode_ >= 0);

    // For HDR, default colorspace to BT.2020 NCL when stream doesn't tag it.
    // For SDR, fall back to resolution-based heuristic (HD → BT.709, SD → BT.601).
    if (colorspace == AVCOL_SPC_UNSPECIFIED) {
        colorspace = hdr_
            ? AVCOL_SPC_BT2020_NCL
            : ((width >= 1280 || height >= 720) ? AVCOL_SPC_BT709 : AVCOL_SPC_BT470BG);
    }

    cs_    = colorspace;
    range_ = color_range;

    if (!build_sws(src_fmt)) return false;

    const AVPixelFormat dst_fmt = hdr_ ? AV_PIX_FMT_RGB48LE : AV_PIX_FMT_RGB24;

    // Staging frame, align=1 → tightly packed (no GL_UNPACK_ROW_LENGTH needed).
    converted_         = av_frame_alloc();
    converted_->format = dst_fmt;
    converted_->width  = width;
    converted_->height = height;
    if (av_frame_get_buffer(converted_, 1) < 0) {
        logger::error("VideoRenderer: av_frame_get_buffer failed");
        return false;
    }

    if (!compile_shaders(hdr_)) return false;

    glGenVertexArrays(1, &vao_);

    // Texture format depends on path:
    //   SDR → GL_RGB8  + GL_UNSIGNED_BYTE
    //   HDR → GL_RGB16 + GL_UNSIGNED_SHORT
    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    if (hdr_) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16, width, height, 0,
                     GL_RGB, GL_UNSIGNED_SHORT, nullptr);
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0,
                     GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glUseProgram(program_);
    glUniform1i(glGetUniformLocation(program_, "tex_rgb"), 0);
    if (hdr_)
        glUniform1i(glGetUniformLocation(program_, "u_hdr_mode"), hdr_mode_);

    const char* cs_name =
        (colorspace == AVCOL_SPC_BT709)                                  ? "BT.709"  :
        (colorspace == AVCOL_SPC_BT2020_NCL ||
         colorspace == AVCOL_SPC_BT2020_CL)                              ? "BT.2020" : "BT.601";
    const char* trc_name = av_color_transfer_name(trc);

    const bool src_full = (color_range == AVCOL_RANGE_JPEG);
    if (hdr_) {
        logger::info("VideoRenderer: {}×{} HDR ({} → RGB48 → GLSL tonemap)  trc={}  matrix={}  {}",
            width, height, pix_fmt_name(src_fmt),
            trc_name ? trc_name : "?", cs_name,
            src_full ? "full-range" : "limited-range");
    } else {
        logger::info("VideoRenderer: {}×{} SDR ({} → RGB24 sRGB)  matrix={}  {}",
            width, height, pix_fmt_name(src_fmt),
            cs_name, src_full ? "full-range" : "limited-range");
    }
    return true;
}

bool VideoRenderer::init_hw_lazy(bool ten_bit)
{
    if (hw_shader_built_) return true;

#ifdef __APPLE__
    // Compile HW shader program (separate from the SW program_).
    GLuint vert = compile_shader(GL_VERTEX_SHADER,   kVertSrc);
    GLuint frag = compile_shader(GL_FRAGMENT_SHADER, hdr_ ? kFragHdrHw : kFragSdrHw);
    if (!vert || !frag) return false;

    hw_program_ = glCreateProgram();
    glAttachShader(hw_program_, vert);
    glAttachShader(hw_program_, frag);
    glLinkProgram(hw_program_);
    GLint ok = 0;
    glGetProgramiv(hw_program_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(hw_program_, sizeof(log), nullptr, log);
        logger::error("HW shader link failed: {}", log);
        glDeleteShader(vert); glDeleteShader(frag);
        return false;
    }
    glDeleteShader(vert); glDeleteShader(frag);

    // Static uniforms: sampler bindings, YUV→RGB matrix + offset, HDR mode.
    glUseProgram(hw_program_);
    glUniform1i(glGetUniformLocation(hw_program_, "tex_y"),  0);
    glUniform1i(glGetUniformLocation(hw_program_, "tex_uv"), 1);

    YuvMat m = make_yuv_matrix(cs_, range_);
    glUniformMatrix3fv(glGetUniformLocation(hw_program_, "u_yuv2rgb"), 1, GL_FALSE, m.m);
    glUniform3fv(glGetUniformLocation(hw_program_, "u_yuv_offset"), 1, m.offset);
    if (hdr_)
        glUniform1i(glGetUniformLocation(hw_program_, "u_hdr_mode"), hdr_mode_);

    // Allocate rectangle texture handles. Storage is bound per-frame by
    // CGLTexImageIOSurface2D, so no glTexImage2D up front.
    glGenTextures(1, &y_tex_);
    glBindTexture(GL_TEXTURE_RECTANGLE, y_tex_);
    glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);

    glGenTextures(1, &uv_tex_);
    glBindTexture(GL_TEXTURE_RECTANGLE, uv_tex_);
    glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_RECTANGLE, 0);

    hw_shader_built_ = true;
    logger::info("VideoRenderer: HW zero-copy path ready ({}-bit, {} matrix, {}-range)",
        ten_bit ? 10 : 8,
        cs_ == AVCOL_SPC_BT709        ? "BT.709"  :
        cs_ == AVCOL_SPC_BT2020_NCL ||
        cs_ == AVCOL_SPC_BT2020_CL    ? "BT.2020" : "BT.601",
        range_ == AVCOL_RANGE_JPEG ? "full" : "limited");
    return true;
#else
    (void)ten_bit;
    return false;
#endif
}

bool VideoRenderer::upload_hw(AVFrame* frame)
{
#ifdef __APPLE__
    auto pixbuf = reinterpret_cast<CVPixelBufferRef>(frame->data[3]);
    if (!pixbuf) {
        logger::warn("upload_hw: frame->data[3] is null — cannot access CVPixelBuffer");
        return false;
    }
    OSType pf = CVPixelBufferGetPixelFormatType(pixbuf);
    IOSurfaceRef iosurf = CVPixelBufferGetIOSurface(pixbuf);
    if (!iosurf) {
        // Can happen if VT internally software-decoded (e.g. AV1 on M1/M2 without
        // AV1 HW block) — the CVPixelBuffer is CPU-backed, no IOSurface exists.
        // Fall back to SW upload path.
        const char* fourcc = reinterpret_cast<const char*>(&pf);
        logger::warn("upload_hw: no IOSurface (pixel format '{}{}{}{}') — VT may have SW-decoded; falling back",
            fourcc[3], fourcc[2], fourcc[1], fourcc[0]);
        return false;
    }
    // Two supported VT bi-planar YUV layouts:
    //   '420v' / '420f' = NV12       (8-bit, video/full range)
    //   'x420' / 'xf20' = P010 P016  (10-bit, video/full range)
    const bool ten_bit =
        (pf == kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange) ||
        (pf == kCVPixelFormatType_420YpCbCr10BiPlanarFullRange);

    if (!hw_shader_built_) {
        if (!init_hw_lazy(ten_bit)) return false;
    }

    // Take a ref so the CVPixelBuffer (and its IOSurface backing) stays alive
    // through draw(). Release the previous frame's ref first.
    if (hw_frame_ref_) av_frame_unref(hw_frame_ref_);
    else               hw_frame_ref_ = av_frame_alloc();
    if (av_frame_ref(hw_frame_ref_, frame) < 0) {
        logger::warn("upload_hw: av_frame_ref failed");
        return false;
    }

    CGLContextObj cgl = CGLGetCurrentContext();
    if (!cgl) {
        logger::warn("upload_hw: no current CGL context");
        return false;
    }

    const size_t w  = CVPixelBufferGetWidthOfPlane (pixbuf, 0);
    const size_t h  = CVPixelBufferGetHeightOfPlane(pixbuf, 0);
    const size_t wc = CVPixelBufferGetWidthOfPlane (pixbuf, 1);
    const size_t hc = CVPixelBufferGetHeightOfPlane(pixbuf, 1);

    const GLenum y_internal  = ten_bit ? GL_R16  : GL_R8;
    const GLenum uv_internal = ten_bit ? GL_RG16 : GL_RG8;
    const GLenum sample_type = ten_bit ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_RECTANGLE, y_tex_);
    CGLError err = CGLTexImageIOSurface2D(cgl, GL_TEXTURE_RECTANGLE, y_internal,
                                          (GLsizei)w, (GLsizei)h,
                                          GL_RED, sample_type, iosurf, 0);
    if (err != kCGLNoError) {
        logger::warn("CGLTexImageIOSurface2D(Y) failed: {}", CGLErrorString(err));
        return false;
    }
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_RECTANGLE, uv_tex_);
    err = CGLTexImageIOSurface2D(cgl, GL_TEXTURE_RECTANGLE, uv_internal,
                                 (GLsizei)wc, (GLsizei)hc,
                                 GL_RG, sample_type, iosurf, 1);
    if (err != kCGLNoError) {
        logger::warn("CGLTexImageIOSurface2D(CbCr) failed: {}", CGLErrorString(err));
        return false;
    }
    return true;
#else
    (void)frame;
    return false;
#endif
}

bool VideoRenderer::build_sws(AVPixelFormat src_fmt)
{
    if (sws_) {
        sws_freeContext(sws_);
        sws_ = nullptr;
    }

    // The decoder may not know its output format yet (a stream whose
    // parameters avformat_find_stream_info could not determine reports
    // AV_PIX_FMT_NONE, and swscale asserts on it). Defer: upload() builds
    // the context from the first decoded frame's format.
    if (src_fmt == AV_PIX_FMT_NONE) {
        sws_src_fmt_ = AV_PIX_FMT_NONE;
        return true;
    }

    const AVPixelFormat dst_fmt = hdr_ ? AV_PIX_FMT_RGB48LE : AV_PIX_FMT_RGB24;

    sws_ = sws_getContext(src_w_, src_h_, src_fmt,
                          src_w_, src_h_, dst_fmt,
                          SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_) {
        logger::error("VideoRenderer: sws_getContext failed for src_fmt={}",
            pix_fmt_name(src_fmt));
        return false;
    }

    const bool src_full = (range_ == AVCOL_RANGE_JPEG);
    int ret = sws_setColorspaceDetails(
        sws_,
        sws_getCoefficients(av_cs_to_sws(cs_)), src_full ? 1 : 0,
        sws_getCoefficients(hdr_ ? SWS_CS_BT2020 : SWS_CS_ITU709), 1,
        0, 1 << 16, 1 << 16);
    if (ret < 0)
        logger::warn("VideoRenderer: sws_setColorspaceDetails not supported for this format");

    sws_src_fmt_ = src_fmt;
    return true;
}

void VideoRenderer::upload(AVFrame* frame)
{
    // Zero-copy HW path: VT frame carries a CVPixelBuffer in data[3].
    // ScrubDecoder still returns SW frames (yuv420p/yuv420p10le), so the same
    // renderer must flip cleanly between the two paths.
    if (frame->format == AV_PIX_FMT_VIDEOTOOLBOX && upload_hw(frame)) {
        return;
    }
    if (frame->format == AV_PIX_FMT_VIDEOTOOLBOX) {
        // IOSurface bind failed — fall through to SW (won't happen in practice
        // unless the CoreVideo/CGL path is broken, but keeps the pipeline alive).
        logger::warn("VideoRenderer: HW upload failed, falling back to SW for this frame");
    }

    // SW path — release any stale HW ref so draw() dispatches to the SW program
    // and the previous frame's CVPixelBuffer isn't held longer than necessary.
    if (hw_frame_ref_) av_frame_free(&hw_frame_ref_);

    // Detect input format change (e.g., scrub returns yuv420p when the main
    // pipeline was NV12 from a previous HW-only session) and rebuild sws_.
    auto frame_fmt = static_cast<AVPixelFormat>(frame->format);
    if (frame_fmt != sws_src_fmt_ && frame_fmt != AV_PIX_FMT_NONE) {
        logger::info("VideoRenderer: source pixel format changed {} → {}, rebuilding sws",
            pix_fmt_name(sws_src_fmt_), pix_fmt_name(frame_fmt));
        if (!build_sws(frame_fmt)) return;
    }
    if (!sws_) return;   // no usable source format yet

    sws_scale(sws_,
              frame->data, frame->linesize, 0, src_h_,
              converted_->data, converted_->linesize);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_);
    if (hdr_) {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, src_w_, src_h_,
                        GL_RGB, GL_UNSIGNED_SHORT, converted_->data[0]);
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, src_w_, src_h_,
                        GL_RGB, GL_UNSIGNED_BYTE, converted_->data[0]);
    }
}

void VideoRenderer::draw()
{
    if (hw_frame_ref_) {
        glUseProgram(hw_program_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_RECTANGLE, y_tex_);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_RECTANGLE, uv_tex_);
    } else {
        glUseProgram(program_);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, tex_);
    }
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

// ── Shader helpers ────────────────────────────────────────────────────────────

bool VideoRenderer::compile_shaders(bool hdr_path)
{
    GLuint vert = compile_shader(GL_VERTEX_SHADER,   kVertSrc);
    GLuint frag = compile_shader(GL_FRAGMENT_SHADER, hdr_path ? kFragHdr : kFragSdr);
    if (!vert || !frag) return false;

    program_ = glCreateProgram();
    glAttachShader(program_, vert);
    glAttachShader(program_, frag);
    glLinkProgram(program_);

    GLint ok = 0;
    glGetProgramiv(program_, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(program_, sizeof(log), nullptr, log);
        logger::error("Shader link failed: {}", log);
        glDeleteShader(vert);
        glDeleteShader(frag);
        return false;
    }

    glDeleteShader(vert);
    glDeleteShader(frag);
    return true;
}

GLuint VideoRenderer::compile_shader(GLenum type, const char* src)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint ok = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        logger::error("Shader compile failed: {}", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}
