// Headless replica of Mirage's HW zero-copy draw (no window: an offscreen CGL
// context and an FBO). Decodes one frame with VideoToolbox the way
// src/Decoder.cpp does, binds its IOSurface planes exactly as
// VideoRenderer::upload_hw does, compiles the shaders taken verbatim from
// src/VideoRenderer.cpp at run time, sets the uniforms from src/YuvMatrix.h as
// VideoRenderer::init_hw_lazy does, draws, and reads the bar centres back.
//
// Usage: gl_hw <VideoRenderer.cpp> <clip> <sdr|hdr> <cs: 709|2020> [force16] [fixed]
//   force16 — bind as R16/RG16 whatever the fourcc (what a fix of finding 3 would do)
//   fixed   — 16-bit-container limited-range constants instead of make_yuv_matrix's
#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>
#include <OpenGL/CGLIOSurface.h>
#include <OpenGL/gl3.h>
#include <CoreVideo/CoreVideo.h>
#include <IOSurface/IOSurface.h>
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
}
#include "YuvMatrix.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static std::string raw_string(const std::string& src, const char* name)
{
    const std::string open = std::string(name) + " = R\"glsl(";
    auto a = src.find(open);
    if (a == std::string::npos) { std::fprintf(stderr, "no %s\n", name); std::exit(1); }
    a += open.size();
    auto b = src.find(")glsl\"", a);
    return src.substr(a, b - a);
}

static AVPixelFormat pick_vt(AVCodecContext* c, const AVPixelFormat* p)
{
    for (; *p != AV_PIX_FMT_NONE; ++p) if (*p == AV_PIX_FMT_VIDEOTOOLBOX) return *p;
    return avcodec_default_get_format(c, p);
}

static AVFrame* decode_first_vt(const char* path)
{
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, path, nullptr, nullptr) < 0) return nullptr;
    avformat_find_stream_info(fmt, nullptr);
    const int vi = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const AVCodec* codec = avcodec_find_decoder(fmt->streams[vi]->codecpar->codec_id);
    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(ctx, fmt->streams[vi]->codecpar);
    AVBufferRef* dev = nullptr;
    av_hwdevice_ctx_create(&dev, AV_HWDEVICE_TYPE_VIDEOTOOLBOX, nullptr, nullptr, 0);
    ctx->hw_device_ctx = av_buffer_ref(dev);
    ctx->get_format    = pick_vt;
    avcodec_open2(ctx, codec, nullptr);
    AVPacket* pkt = av_packet_alloc();
    AVFrame* f = av_frame_alloc();
    bool got = false, eof = false;
    while (!got) {
        if (!eof) {
            if (av_read_frame(fmt, pkt) < 0) { avcodec_send_packet(ctx, nullptr); eof = true; }
            else { if (pkt->stream_index == vi) avcodec_send_packet(ctx, pkt); av_packet_unref(pkt); }
        }
        if (avcodec_receive_frame(ctx, f) == 0) got = true;
        else if (eof) break;
    }
    // (ctx/fmt intentionally leaked: the frame keeps the pool alive; a one-shot tool)
    return got ? f : nullptr;
}

static GLuint shader(GLenum t, const std::string& s)
{
    GLuint sh = glCreateShader(t);
    const char* p = s.c_str();
    glShaderSource(sh, 1, &p, nullptr);
    glCompileShader(sh);
    GLint ok = 0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[1024]; glGetShaderInfoLog(sh, sizeof log, nullptr, log); std::fprintf(stderr, "compile: %s\n", log); std::exit(1); }
    return sh;
}

static YuvMat fixed16(AVColorSpace cs)
{
    YuvMat m = make_yuv_matrix(cs, AVCOL_RANGE_JPEG);
    const double ys = 65535.0 / (219.0 * 256.0), uvs = 65535.0 / (224.0 * 256.0);
    for (int i = 0; i < 3; ++i) m.m[i] = (float)ys;
    for (int i = 3; i < 9; ++i) m.m[i] = (float)(m.m[i] * uvs);
    m.offset[0] = (float)(16.0 * 256.0 / 65535.0);
    m.offset[1] = m.offset[2] = (float)(128.0 * 256.0 / 65535.0);
    return m;
}

int main(int argc, char** argv)
{
    if (argc < 5) { std::fprintf(stderr, "usage\n"); return 2; }
    std::ifstream in(argv[1]); std::stringstream ss; ss << in.rdbuf();
    const std::string src = ss.str();
    const bool hdr = std::strcmp(argv[3], "hdr") == 0;
    const AVColorSpace cs = std::strcmp(argv[4], "2020") == 0 ? AVCOL_SPC_BT2020_NCL : AVCOL_SPC_BT709;
    bool force16 = false, fixed = false;
    for (int i = 5; i < argc; ++i) { if (!std::strcmp(argv[i], "force16")) force16 = true; if (!std::strcmp(argv[i], "fixed")) fixed = true; }

    CGLPixelFormatAttribute attrs[] = { kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core,
                                        kCGLPFAAccelerated, (CGLPixelFormatAttribute)0 };
    CGLPixelFormatObj pfo; GLint npf = 0;
    if (CGLChoosePixelFormat(attrs, &pfo, &npf) != kCGLNoError || !pfo) { std::fprintf(stderr, "no pixel format\n"); return 1; }
    CGLContextObj cgl; CGLCreateContext(pfo, nullptr, &cgl); CGLSetCurrentContext(cgl);
    std::printf("GL %s | %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));

    AVFrame* f = decode_first_vt(argv[2]);
    if (!f || f->format != AV_PIX_FMT_VIDEOTOOLBOX) { std::fprintf(stderr, "no VT frame\n"); return 1; }
    auto pixbuf = reinterpret_cast<CVPixelBufferRef>(f->data[3]);
    const OSType pf = CVPixelBufferGetPixelFormatType(pixbuf);
    IOSurfaceRef iosurf = CVPixelBufferGetIOSurface(pixbuf);
    char cc[5] = { (char)(pf >> 24), (char)(pf >> 16), (char)(pf >> 8), (char)pf, 0 };

    // ── VideoRenderer::upload_hw, as written (VideoRenderer.cpp:469-518) ──
    const bool ten_bit_code = (pf == kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange) ||
                              (pf == kCVPixelFormatType_420YpCbCr10BiPlanarFullRange);
    const bool ten_bit = ten_bit_code || force16;
    const size_t w  = CVPixelBufferGetWidthOfPlane (pixbuf, 0);
    const size_t h  = CVPixelBufferGetHeightOfPlane(pixbuf, 0);
    const size_t wc = CVPixelBufferGetWidthOfPlane (pixbuf, 1);
    const size_t hc = CVPixelBufferGetHeightOfPlane(pixbuf, 1);
    std::printf("frame '%s': upload_hw's ten_bit=%d%s, planes %zux%zu + %zux%zu\n", cc, ten_bit_code,
                force16 ? " (forced 16-bit binding)" : "", w, h, wc, hc);
    const GLenum y_internal  = ten_bit ? GL_R16  : GL_R8;
    const GLenum uv_internal = ten_bit ? GL_RG16 : GL_RG8;
    const GLenum sample_type = ten_bit ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE;
    GLuint tex[2]; glGenTextures(2, tex);
    for (GLuint t : tex) {
        glBindTexture(GL_TEXTURE_RECTANGLE, t);
        glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_RECTANGLE, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_RECTANGLE, tex[0]);
    CGLError e0 = CGLTexImageIOSurface2D(cgl, GL_TEXTURE_RECTANGLE, y_internal, (GLsizei)w, (GLsizei)h, GL_RED, sample_type, iosurf, 0);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_RECTANGLE, tex[1]);
    CGLError e1 = CGLTexImageIOSurface2D(cgl, GL_TEXTURE_RECTANGLE, uv_internal, (GLsizei)wc, (GLsizei)hc, GL_RG, sample_type, iosurf, 1);
    std::printf("CGLTexImageIOSurface2D: Y -> %d (%s), CbCr -> %d (%s)\n", e0, CGLErrorString(e0), e1, CGLErrorString(e1));
    if (e0 != kCGLNoError || e1 != kCGLNoError) {
        std::printf("=> upload_hw returns false; upload() falls back to the SW path with an AV_PIX_FMT_VIDEOTOOLBOX frame\n");
        return 0;
    }

    if (getenv("DUMPTEX")) {   // what the GPU reads from the bound IOSurface planes, at the bar centres (row h/2)
        std::vector<float> t(w * h);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_RECTANGLE, tex[0]);
        glGetTexImage(GL_TEXTURE_RECTANGLE, 0, GL_RED, GL_FLOAT, t.data());
        std::printf("Y texels (x255 / x65535):");
        for (int b = 0; b < 7; ++b) { const float v = t[(h / 2) * w + 64 * b + 32]; std::printf(" %.3f/%.1f", v * 255, v * 65535); }
        std::printf("\n");
        CVPixelBufferLockBaseAddress(pixbuf, kCVPixelBufferLock_ReadOnly);
        const uint8_t* yb = (const uint8_t*)CVPixelBufferGetBaseAddressOfPlane(pixbuf, 0);
        const size_t bpr = CVPixelBufferGetBytesPerRowOfPlane(pixbuf, 0);
        std::printf("CPU bytes row h/2:");
        for (int b = 0; b < 7; ++b) std::printf(" %u", yb[(h / 2) * bpr + 64 * b + 32]);
        std::printf("\n");
        unsigned hist[256] = {0};
        for (size_t yy = 0; yy < h; ++yy) for (size_t xx = 128; xx < 192; ++xx) ++hist[yb[yy * bpr + xx]];
        std::printf("CPU bar-2 histogram:");
        for (int i = 0; i < 256; ++i) if (hist[i]) std::printf(" %d:%u", i, hist[i]);
        std::printf("\n");
        CVPixelBufferUnlockBaseAddress(pixbuf, kCVPixelBufferLock_ReadOnly);
    }
    // ── program, as VideoRenderer::init_hw_lazy builds it ──
    const auto t0 = std::chrono::steady_clock::now();
    GLuint prog = glCreateProgram();
    glAttachShader(prog, shader(GL_VERTEX_SHADER, raw_string(src, "kVertSrc")));
    static const char* kRaw = R"(
#version 410 core
uniform sampler2DRect tex_y;
uniform sampler2DRect tex_uv;
in vec2 uv;
out vec4 frag_color;
void main() {
    float y = texture(tex_y, uv * vec2(textureSize(tex_y))).r;
    vec2 c  = texture(tex_uv, uv * vec2(textureSize(tex_uv))).rg;
    frag_color = vec4(y, c, 1.0);
}
)";
    glAttachShader(prog, shader(GL_FRAGMENT_SHADER, getenv("RAWSAMPLE") ? std::string(kRaw) : raw_string(src, hdr ? "kFragHdrHw" : "kFragSdrHw")));
    glLinkProgram(prog);
    GLint ok = 0; glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) { std::fprintf(stderr, "link failed\n"); return 1; }
    glUseProgram(prog);
    glUniform1i(glGetUniformLocation(prog, "tex_y"), 0);
    glUniform1i(glGetUniformLocation(prog, "tex_uv"), 1);
    const YuvMat m = fixed ? make_yuv_matrix(cs, AVCOL_RANGE_MPEG, ten_bit ? 16 : 8, ten_bit ? 10 : 8) : make_yuv_matrix(cs, AVCOL_RANGE_MPEG);
    glUniformMatrix3fv(glGetUniformLocation(prog, "u_yuv2rgb"), 1, GL_FALSE, m.m);
    glUniform3fv(glGetUniformLocation(prog, "u_yuv_offset"), 1, m.offset);
    if (hdr) glUniform1i(glGetUniformLocation(prog, "u_hdr_mode"), 0);   // PQ

    GLuint vao; glGenVertexArrays(1, &vao); glBindVertexArray(vao);
    for (GLenum ifmt : { (GLenum)GL_RGBA32F, (GLenum)GL_RGBA8 }) {
        GLuint fbo, ct; glGenFramebuffers(1, &fbo); glGenTextures(1, &ct);
        glBindTexture(GL_TEXTURE_2D, ct);
        glTexImage2D(GL_TEXTURE_2D, 0, ifmt, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, ifmt == GL_RGBA8 ? GL_UNSIGNED_BYTE : GL_FLOAT, nullptr);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ct, 0);
        glViewport(0, 0, (GLsizei)w, (GLsizei)h);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_RECTANGLE, tex[0]);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_RECTANGLE, tex[1]);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glFinish();
        if (ifmt == GL_RGBA32F) {
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            std::printf("compile + link + first draw: %.1f ms\n", ms);
        }
        std::vector<float> px(w * h * 4);
        glReadPixels(0, 0, (GLsizei)w, (GLsizei)h, GL_RGBA, GL_FLOAT, px.data());
        std::printf("%s %s matrix, %s target:\n", hdr ? "kFragHdrHw" : "kFragSdrHw", fixed ? "fixed" : "current",
                    ifmt == GL_RGBA8 ? "RGBA8" : "RGBA32F");
        for (int b = 0; b < 7; ++b) {
            const size_t x = 64 * b + 32; if (x >= w) break;
            const float* p = &px[((h / 2) * w + x) * 4];
            if (ifmt == GL_RGBA8) std::printf("  bar %d: %.0f %.0f %.0f\n", b, p[0] * 255, p[1] * 255, p[2] * 255);
            else                  std::printf("  bar %d: %.6f %.6f %.6f  (x255: %.3f %.3f %.3f)\n", b, p[0], p[1], p[2], p[0] * 255, p[1] * 255, p[2] * 255);
        }
    }
    return 0;
}
