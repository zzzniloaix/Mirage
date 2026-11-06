#include "VideoRenderer.h"
#include "Logger.h"
#include "YuvMatrix.h"

#include <cctype>
#include <string>

extern "C" {
#include <libswscale/swscale.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
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
// matrix + offset uniform (baked for the interpretation's colorspace + range
// and the bound buffers' depth), so the shader is agnostic to BT.601/709/2020
// or 8/10/16-bit.
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

static const char* matrix_name(AVColorSpace cs)
{
    return cs == AVCOL_SPC_BT709                                  ? "BT.709"  :
           cs == AVCOL_SPC_BT2020_NCL || cs == AVCOL_SPC_BT2020_CL ? "BT.2020" : "BT.601";
}

static const char* name_or(const char* n) { return n ? n : "?"; }

// An interpretation's tags, for the log: "matrix=bt709 range=tv trc=bt709 primaries=bt709".
static std::string interpretation_text(const ColourInterpretation& i)
{
    return std::format("matrix={} range={} trc={} primaries={}",
        name_or(av_color_space_name(i.cs)), name_or(av_color_range_name(i.range)),
        name_or(av_color_transfer_name(i.trc)), name_or(av_color_primaries_name(i.primaries)));
}

static const char* hdr_mode_name(int mode)
{
    return mode == 0 ? "HDR PQ" : mode == 1 ? "HDR HLG" : "SDR";
}

// A CVPixelBuffer pixel format as its four characters ('x420'), a byte
// that is not printable as '.' (the numeric formats: 32ARGB is 0x20).
static std::string fourcc_text(std::uint32_t pf)
{
    std::string s(4, '.');
    for (int i = 0; i < 4; ++i) {
        const auto c = static_cast<unsigned char>(pf >> (24 - 8 * i));
        if (std::isprint(c)) s[static_cast<std::size_t>(i)] = static_cast<char>(c);
    }
    return s;
}

// ── VideoRenderer ─────────────────────────────────────────────────────────────

VideoRenderer::~VideoRenderer()
{
    if (sws_)          sws_freeContext(sws_);
    if (converted_)    av_frame_free(&converted_);
    if (hw_frame_ref_) av_frame_free(&hw_frame_ref_);
    if (hw_copy_)      av_frame_free(&hw_copy_);
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
                         AVColorPrimaries              primaries)
{
    src_w_ = width;
    src_h_ = height;

    // The stream's tags; upload() follows each frame's own from there.
    // interpret() defaults an untagged matrix: BT.2020 NCL for HDR, and for
    // SDR a resolution-based heuristic (HD → BT.709, SD → BT.601).
    tags_     = { colorspace, color_range, trc, primaries };
    interp_   = interpret(tags_, width, height);
    hdr_mode_ = hdr_mode_of(interp_.trc);
    hdr_      = (hdr_mode_ >= 0);

    if (!build_sws(src_fmt)) return false;
    if (!alloc_sw_target())  return false;
    if (!compile_shaders(hdr_)) return false;

    glGenVertexArrays(1, &vao_);
    set_sw_uniforms();

    const char* trc_name = av_color_transfer_name(interp_.trc);
    const bool  src_full = (interp_.range == AVCOL_RANGE_JPEG);
    if (hdr_) {
        logger::info("VideoRenderer: {}×{} HDR ({} → RGB48 → GLSL tonemap)  trc={}  matrix={}  {}",
            width, height, pix_fmt_name(src_fmt),
            trc_name ? trc_name : "?", matrix_name(interp_.cs),
            src_full ? "full-range" : "limited-range");
    } else {
        logger::info("VideoRenderer: {}×{} SDR ({} → RGB24 sRGB)  matrix={}  {}",
            width, height, pix_fmt_name(src_fmt),
            matrix_name(interp_.cs), src_full ? "full-range" : "limited-range");
    }
    return true;
}

bool VideoRenderer::alloc_sw_target()
{
    // Staging frame, align=1 → tightly packed (no GL_UNPACK_ROW_LENGTH needed).
    av_frame_free(&converted_);
    converted_         = av_frame_alloc();
    converted_->format = hdr_ ? AV_PIX_FMT_RGB48LE : AV_PIX_FMT_RGB24;
    converted_->width  = src_w_;
    converted_->height = src_h_;
    if (av_frame_get_buffer(converted_, 1) < 0) {
        logger::error("VideoRenderer: av_frame_get_buffer failed");
        av_frame_free(&converted_);
        return false;
    }

    // Texture format depends on path:
    //   SDR → GL_RGB8  + GL_UNSIGNED_BYTE
    //   HDR → GL_RGB16 + GL_UNSIGNED_SHORT
    if (!tex_) {
        glGenTextures(1, &tex_);
        glBindTexture(GL_TEXTURE_2D, tex_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glBindTexture(GL_TEXTURE_2D, tex_);
    if (hdr_) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16, src_w_, src_h_, 0,
                     GL_RGB, GL_UNSIGNED_SHORT, nullptr);
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, src_w_, src_h_, 0,
                     GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    return true;
}

void VideoRenderer::set_sw_uniforms()
{
    glUseProgram(program_);
    glUniform1i(glGetUniformLocation(program_, "tex_rgb"), 0);
    if (hdr_)
        glUniform1i(glGetUniformLocation(program_, "u_hdr_mode"), hdr_mode_);
}

bool VideoRenderer::reconfigure(const ColourInterpretation& next, InterpretationChange change)
{
    const ColourInterpretation prev = interp_;
    const int prev_mode = hdr_mode_;
    interp_   = next;
    hdr_mode_ = hdr_mode_of(next.trc);
    hdr_      = (hdr_mode_ >= 0);
    logger::info("VideoRenderer: the frame's colour tags change the interpretation: {} → {} ({} → {}{})",
        interpretation_text(prev), interpretation_text(next), hdr_mode_name(prev_mode), hdr_mode_name(hdr_mode_),
        change == InterpretationChange::Program ? ": programs rebuilt" : "");

    if (change == InterpretationChange::Program) {
        // The other fragment shaders, and the staging frame and texture in
        // the other format (RGB24 / RGB48LE); the HW textures stay.
        if (!compile_shaders(hdr_)) return false;
        if (!alloc_sw_target())     return false;
        if (hw_program_ && !build_hw_program()) return false;
    }
    // sws's destination follows the HDR mode, its coefficients the matrix and
    // range: built again for the format it was built for.
    if (sws_src_fmt_ != AV_PIX_FMT_NONE && !build_sws(sws_src_fmt_))
        sws_src_fmt_ = AV_PIX_FMT_NONE;   // retried by the next SW frame
    set_sw_uniforms();
    set_hw_uniforms();
    return true;
}

bool VideoRenderer::init_hw_lazy()
{
#ifdef __APPLE__
    // Allocate rectangle texture handles. Storage is bound per-frame by
    // CGLTexImageIOSurface2D, so no glTexImage2D up front.
    if (!y_tex_) {
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
    }
    return hw_program_ || build_hw_program();
#else
    return false;
#endif
}

bool VideoRenderer::build_hw_program()
{
#ifdef __APPLE__
    if (hw_program_) {
        glDeleteProgram(hw_program_);
        hw_program_ = 0;
    }
    // Compile HW shader program (separate from the SW program_).
    GLuint vert = compile_shader(GL_VERTEX_SHADER,   kVertSrc);
    GLuint frag = compile_shader(GL_FRAGMENT_SHADER, hdr_ ? kFragHdrHw : kFragSdrHw);
    if (!vert || !frag) {
        if (vert) glDeleteShader(vert);
        if (frag) glDeleteShader(frag);
        return false;
    }

    hw_program_ = glCreateProgram();
    glAttachShader(hw_program_, vert);
    glAttachShader(hw_program_, frag);
    glLinkProgram(hw_program_);
    GLint ok = 0;
    glGetProgramiv(hw_program_, GL_LINK_STATUS, &ok);
    glDeleteShader(vert); glDeleteShader(frag);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(hw_program_, sizeof(log), nullptr, log);
        logger::error("HW shader link failed: {}", log);
        glDeleteProgram(hw_program_);
        hw_program_ = 0;
        return false;
    }

    // Sampler bindings; the matrix, offset and HDR mode are set_hw_uniforms()'.
    glUseProgram(hw_program_);
    glUniform1i(glGetUniformLocation(hw_program_, "tex_y"),  0);
    glUniform1i(glGetUniformLocation(hw_program_, "tex_uv"), 1);
    return true;
#else
    return false;
#endif
}

void VideoRenderer::set_hw_uniforms()
{
    if (!hw_program_ || !hw_container_bits_) return;   // no HW buffer bound yet
    glUseProgram(hw_program_);
    const YuvMat m = make_yuv_matrix(interp_.cs, interp_.range, hw_container_bits_, hw_sample_bits_);
    glUniformMatrix3fv(glGetUniformLocation(hw_program_, "u_yuv2rgb"), 1, GL_FALSE, m.m);
    glUniform3fv(glGetUniformLocation(hw_program_, "u_yuv_offset"), 1, m.offset);
    if (hdr_)
        glUniform1i(glGetUniformLocation(hw_program_, "u_hdr_mode"), hdr_mode_);
}

bool VideoRenderer::upload_hw(AVFrame* frame)
{
#ifdef __APPLE__
    auto pixbuf = reinterpret_cast<CVPixelBufferRef>(frame->data[3]);
    if (!pixbuf) {
        logger::warn("upload_hw: frame->data[3] is null — cannot access CVPixelBuffer");
        return false;
    }
    const OSType pf     = CVPixelBufferGetPixelFormatType(pixbuf);
    IOSurfaceRef iosurf = CVPixelBufferGetIOSurface(pixbuf);
    const size_t planes = CVPixelBufferGetPlaneCount(pixbuf);
    const size_t bpe_y  = iosurf ? IOSurfaceGetBytesPerElementOfPlane(iosurf, 0) : 0;
    const size_t bpe_uv = iosurf ? IOSurfaceGetBytesPerElementOfPlane(iosurf, 1) : 0;

    // The binding follows the planes' bytes per element: two planes of 1 and
    // 2 bytes ('420v', '422v', '444v') as R8 / RG8, of 2 and 4 bytes ('x420',
    // 'x422', 'x444', 'sv44', ...) as R16 / RG16 (hw_container_bits()). Any
    // other buffer is refused here, before the frame is referenced: a packed
    // one ('y416', ProRes 4444 with alpha), or one without an IOSurface,
    // which VT may give when it decoded in software (e.g. AV1 on M1/M2
    // without an AV1 HW block). upload() converts a CPU copy instead.
    const int bits = iosurf ? hw_container_bits(planes, bpe_y, bpe_uv) : 0;
    if (bits == 0) {
        if (pf != hw_refused_) {
            hw_refused_ = pf;
            if (!iosurf)
                logger::warn("VideoRenderer: '{}' buffers have no IOSurface (VT may have SW-decoded): "
                             "copied to the CPU and converted by sws", fourcc_text(pf));
            else
                logger::warn("VideoRenderer: the HW path refuses '{}' buffers ({} planes, {} and {} bytes per element): "
                             "copied to the CPU and converted by sws", fourcc_text(pf), planes, bpe_y, bpe_uv);
        }
        return false;
    }

    const bool new_program = (hw_program_ == 0);
    if (!init_hw_lazy()) return false;

    // The matrix follows the buffers' depths: uploaded again when their
    // format changes.
    if (new_program || pf != hw_format_) {
        const bool first = (hw_format_ == 0), changed = (pf != hw_format_);
        hw_format_         = pf;
        hw_container_bits_ = bits;
        hw_sample_bits_    = hw_sample_bits(frame->hw_frames_ctx
            ? reinterpret_cast<const AVHWFramesContext*>(frame->hw_frames_ctx->data)->sw_format
            : AV_PIX_FMT_NONE, bits);
        set_hw_uniforms();
        if (changed)
            logger::info("VideoRenderer: HW zero-copy path {}'{}' bound as {}, {}-bit samples ({} matrix, {}-range)",
                first ? "ready: " : "now ", fourcc_text(pf), bits == 16 ? "R16 / RG16" : "R8 / RG8",
                hw_sample_bits_, matrix_name(interp_.cs), interp_.range == AVCOL_RANGE_JPEG ? "full" : "limited");
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

    const GLenum y_internal  = bits == 16 ? GL_R16  : GL_R8;
    const GLenum uv_internal = bits == 16 ? GL_RG16 : GL_RG8;
    const GLenum sample_type = bits == 16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE;

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

AVFrame* VideoRenderer::copy_to_cpu(const AVFrame* frame)
{
    if (hw_copy_) av_frame_unref(hw_copy_);
    else          hw_copy_ = av_frame_alloc();
    if (!hw_copy_) return nullptr;
    hw_copy_->format = AV_PIX_FMT_NONE;   // the pool's sw_format ('y416' → ayuv64le)
    int ret = av_hwframe_transfer_data(hw_copy_, frame, 0);
    if (ret >= 0) ret = av_frame_copy_props(hw_copy_, frame);
    if (ret < 0) {
        if (!hw_copy_failed_) {
            char errbuf[AV_ERROR_MAX_STRING_SIZE];
            av_strerror(ret, errbuf, sizeof(errbuf));
            logger::warn("VideoRenderer: copying a VideoToolbox frame to the CPU failed: {}", errbuf);
        }
        hw_copy_failed_ = true;
        return nullptr;
    }
    hw_copy_failed_ = false;
    return hw_copy_;
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

    // SDR: full chroma interpolation, without which the RGB24 of 10-bit
    // sources falls short of the exact values (plan/engineer_tools_runs/
    // investigate-2026-11-03/render/results.txt [C4]). HDR goes without: no
    // flag corrects the RGB48LE output, and SWS_FULL_CHR_H_INT corrupts its
    // G and B ([C10]).
    const int flags = hdr_ ? SWS_BILINEAR : (SWS_BILINEAR | SWS_FULL_CHR_H_INT);
    sws_ = sws_getContext(src_w_, src_h_, src_fmt,
                          src_w_, src_h_, dst_fmt,
                          flags, nullptr, nullptr, nullptr);
    if (!sws_) {
        logger::error("VideoRenderer: sws_getContext failed for src_fmt={}",
            pix_fmt_name(src_fmt));
        return false;
    }

    const bool src_full = (interp_.range == AVCOL_RANGE_JPEG);
    int ret = sws_setColorspaceDetails(
        sws_,
        sws_getCoefficients(av_cs_to_sws(interp_.cs)), src_full ? 1 : 0,
        sws_getCoefficients(hdr_ ? SWS_CS_BT2020 : SWS_CS_ITU709), 1,
        0, 1 << 16, 1 << 16);
    if (ret < 0)
        logger::warn("VideoRenderer: sws_setColorspaceDetails not supported for this format");

    sws_src_fmt_ = src_fmt;
    return true;
}

void VideoRenderer::upload(AVFrame* frame)
{
    // The frame's own colour tags first: a specified tag wins over the one
    // followed so far, an unspecified one keeps it (follow_tags()), and a
    // matrix no tag gives keeps the current one but across an SDR/HDR flip
    // (next_interpretation()); a change of interpretation reconfigures the
    // renderer here, between two draws.
    tags_ = follow_tags(tags_, { frame->colorspace, frame->color_range, frame->color_trc, frame->color_primaries });
    const ColourInterpretation next = next_interpretation(interp_, tags_,
                                                          frame->width  > 0 ? frame->width  : src_w_,
                                                          frame->height > 0 ? frame->height : src_h_);
    if (const InterpretationChange change = classify_change(interp_, next);
        change != InterpretationChange::None && !reconfigure(next, change))
        return;

    // Zero-copy HW path: VT frame carries a CVPixelBuffer in data[3].
    // ScrubDecoder still returns SW frames (yuv420p/yuv420p10le), so the same
    // renderer must flip cleanly between the two paths.
    if (frame->format == AV_PIX_FMT_VIDEOTOOLBOX) {
        if (upload_hw(frame)) return;
        // Not bound (a buffer upload_hw refuses, or a Core Video / CGL
        // error): a CPU copy of the frame takes the SW path. sws accepts the
        // copy's format for every VideoToolbox buffer tried (results.txt [C8]).
        frame = copy_to_cpu(frame);
        if (!frame) {
            if (hw_frame_ref_) av_frame_free(&hw_frame_ref_);
            return;
        }
    }

    // SW path — release any stale HW ref so draw() dispatches to the SW program
    // and the previous frame's CVPixelBuffer isn't held longer than necessary.
    if (hw_frame_ref_) av_frame_free(&hw_frame_ref_);

    // Frame size changed (a video track switch to another resolution, or a
    // scrub frame from a different variant): resize the staging frame and
    // texture, and rebuild sws_ for the new geometry.
    auto frame_fmt = static_cast<AVPixelFormat>(frame->format);
    if (frame->width > 0 && frame->height > 0 && (frame->width != src_w_ || frame->height != src_h_)) {
        logger::info("VideoRenderer: frame size changed {}×{} → {}×{}", src_w_, src_h_, frame->width, frame->height);
        src_w_ = frame->width;
        src_h_ = frame->height;
        if (!alloc_sw_target()) return;
        sws_src_fmt_ = AV_PIX_FMT_NONE;   // force the rebuild below
    }
    if (!converted_) return;
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
    if (hw_frame_ref_ && hw_program_) {
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
    if (program_) {   // an SDR/HDR flip (reconfigure()) replaces the program
        glDeleteProgram(program_);
        program_ = 0;
    }
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
