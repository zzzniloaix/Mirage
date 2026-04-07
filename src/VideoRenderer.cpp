#include "VideoRenderer.h"
#include "Logger.h"

extern "C" {
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
}

// ── GLSL shaders ──────────────────────────────────────────────────────────────

static const char* kVertSrc = R"glsl(
#version 410 core
out vec2 uv;
void main() {
    // Single fullscreen triangle from vertex ID (no VBO needed)
    vec2 pos = vec2(
        float((gl_VertexID & 1) * 4 - 1),
        float((gl_VertexID >> 1) * 4 - 1)
    );
    uv = pos * 0.5 + 0.5;
    uv.y = 1.0 - uv.y;       // flip Y: OpenGL origin is bottom-left
    gl_Position = vec4(pos, 0.0, 1.0);
}
)glsl";

// Color conversion is done entirely by FFmpeg (sws_setColorspaceDetails).
// The shader is a pure RGB passthrough.
static const char* kFragSrc = R"glsl(
#version 410 core
uniform sampler2D tex_rgb;
in  vec2 uv;
out vec4 frag_color;
void main() {
    frag_color = vec4(texture(tex_rgb, uv).rgb, 1.0);
}
)glsl";

// ── Color space helpers ───────────────────────────────────────────────────────

// Map AVColorSpace to the SWS_CS_* id used by sws_getCoefficients().
static int av_cs_to_sws(AVColorSpace cs)
{
    switch (cs) {
        case AVCOL_SPC_BT709:
            return SWS_CS_ITU709;
        case AVCOL_SPC_BT2020_NCL:
        case AVCOL_SPC_BT2020_CL:
            return SWS_CS_BT2020;
        case AVCOL_SPC_SMPTE170M:
        case AVCOL_SPC_BT470BG:
        default:
            return SWS_CS_DEFAULT;   // ITU601
    }
}

// ── VideoRenderer ─────────────────────────────────────────────────────────────

VideoRenderer::~VideoRenderer()
{
    if (sws_)       sws_freeContext(sws_);
    if (converted_) av_frame_free(&converted_);
    if (tex_)       glDeleteTextures(1, &tex_);
    if (vao_)       glDeleteVertexArrays(1, &vao_);
    if (program_)   glDeleteProgram(program_);
}

bool VideoRenderer::init(int width, int height, AVPixelFormat src_fmt,
                         AVColorSpace colorspace, AVColorRange color_range)
{
    src_w_ = width;
    src_h_ = height;

    // ── swscale: decoded YUV (any format) → RGB24 ─────────────────────────────
    sws_ = sws_getContext(
        width, height, src_fmt,
        width, height, AV_PIX_FMT_RGB24,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!sws_) {
        logger::error("VideoRenderer: sws_getContext failed");
        return false;
    }

    // Guess colorspace from resolution when the stream doesn't tag it.
    if (colorspace == AVCOL_SPC_UNSPECIFIED)
        colorspace = (width >= 1280 || height >= 720) ? AVCOL_SPC_BT709
                                                       : AVCOL_SPC_BT470BG;

    const bool src_full = (color_range == AVCOL_RANGE_JPEG);

    // Apply YUV→RGB matrix + limited/full range on the FFmpeg side.
    // Output target: full-range BT.709 (≈ sRGB primaries + BT.709 gamma).
    // Note: sws does not handle HDR transfer functions (PQ/HLG);
    //       those require an avfilter tonemap stage (future work).
    int ret = sws_setColorspaceDetails(
        sws_,
        sws_getCoefficients(av_cs_to_sws(colorspace)), src_full ? 1 : 0,
        sws_getCoefficients(SWS_CS_ITU709),            1,   // output: BT.709 full range
        0, 1 << 16, 1 << 16);                               // brightness/contrast/sat neutral
    if (ret < 0)
        logger::warn("VideoRenderer: sws_setColorspaceDetails not supported for this format");

    // Allocate RGB24 staging frame with align=1 so linesize == 3*width (no padding).
    // This lets us upload directly to GL without GL_UNPACK_ROW_LENGTH arithmetic.
    converted_ = av_frame_alloc();
    converted_->format = AV_PIX_FMT_RGB24;
    converted_->width  = width;
    converted_->height = height;
    if (av_frame_get_buffer(converted_, 1) < 0) {
        logger::error("VideoRenderer: av_frame_get_buffer failed");
        return false;
    }

    // ── Shaders ───────────────────────────────────────────────────────────────
    if (!compile_shaders())
        return false;

    // ── VAO (fullscreen triangle, no vertex data needed) ─────────────────────
    glGenVertexArrays(1, &vao_);

    // ── Single RGB8 texture ───────────────────────────────────────────────────
    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, width, height, 0,
                 GL_RGB, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glUseProgram(program_);
    glUniform1i(glGetUniformLocation(program_, "tex_rgb"), 0);

    const char* cs_name =
        (colorspace == AVCOL_SPC_BT709)                               ? "BT.709"  :
        (colorspace == AVCOL_SPC_BT2020_NCL ||
         colorspace == AVCOL_SPC_BT2020_CL)                           ? "BT.2020" : "BT.601";

    logger::info("VideoRenderer: {}×{} ({} → RGB24 sRGB)  matrix={}  {}",
        width, height, av_get_pix_fmt_name(src_fmt),
        cs_name, src_full ? "full-range" : "limited-range");
    return true;
}

void VideoRenderer::upload(AVFrame* frame)
{
    sws_scale(sws_,
              frame->data, frame->linesize, 0, src_h_,
              converted_->data, converted_->linesize);

    // align=1 → linesize[0] == 3 * src_w_, tightly packed, no GL_UNPACK_ROW_LENGTH needed
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, src_w_, src_h_,
                    GL_RGB, GL_UNSIGNED_BYTE, converted_->data[0]);
}

void VideoRenderer::draw()
{
    glUseProgram(program_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

// ── Shader helpers ────────────────────────────────────────────────────────────

bool VideoRenderer::compile_shaders()
{
    GLuint vert = compile_shader(GL_VERTEX_SHADER,   kVertSrc);
    GLuint frag = compile_shader(GL_FRAGMENT_SHADER, kFragSrc);
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
