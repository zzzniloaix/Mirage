// Can a 'y416' (ProRes 4444 with alpha) IOSurface be bound as one RGBA16
// rectangle texture? Samples the bar centres raw (A, Y', Cb, Cr per the SDK).
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
}
#include <cstdio>
#include <vector>
static AVPixelFormat pick(AVCodecContext* c, const AVPixelFormat* p) { for (; *p != AV_PIX_FMT_NONE; ++p) if (*p == AV_PIX_FMT_VIDEOTOOLBOX) return *p; return avcodec_default_get_format(c, p); }
int main(int, char** argv) {
    AVFormatContext* fmt = nullptr; avformat_open_input(&fmt, argv[1], nullptr, nullptr); avformat_find_stream_info(fmt, nullptr);
    const int vi = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    const AVCodec* codec = avcodec_find_decoder(fmt->streams[vi]->codecpar->codec_id);
    AVCodecContext* ctx = avcodec_alloc_context3(codec); avcodec_parameters_to_context(ctx, fmt->streams[vi]->codecpar);
    AVBufferRef* dev = nullptr; av_hwdevice_ctx_create(&dev, AV_HWDEVICE_TYPE_VIDEOTOOLBOX, nullptr, nullptr, 0);
    ctx->hw_device_ctx = av_buffer_ref(dev); ctx->get_format = pick; avcodec_open2(ctx, codec, nullptr);
    AVPacket* pkt = av_packet_alloc(); AVFrame* f = av_frame_alloc(); bool got = false;
    while (!got && av_read_frame(fmt, pkt) >= 0) { if (pkt->stream_index == vi) avcodec_send_packet(ctx, pkt); av_packet_unref(pkt); got = avcodec_receive_frame(ctx, f) == 0; }
    if (!got) { avcodec_send_packet(ctx, nullptr); got = avcodec_receive_frame(ctx, f) == 0; }
    auto pb = reinterpret_cast<CVPixelBufferRef>(f->data[3]); IOSurfaceRef ios = CVPixelBufferGetIOSurface(pb);
    const size_t w = CVPixelBufferGetWidth(pb), h = CVPixelBufferGetHeight(pb);
    CGLPixelFormatAttribute attrs[] = { kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core, kCGLPFAAccelerated, (CGLPixelFormatAttribute)0 };
    CGLPixelFormatObj pfo; GLint n; CGLChoosePixelFormat(attrs, &pfo, &n); CGLContextObj cgl; CGLCreateContext(pfo, nullptr, &cgl); CGLSetCurrentContext(cgl);
    GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_RECTANGLE, t);
    CGLError e = CGLTexImageIOSurface2D(cgl, GL_TEXTURE_RECTANGLE, GL_RGBA16, (GLsizei)w, (GLsizei)h, GL_RGBA, GL_UNSIGNED_SHORT, ios, 0);
    std::printf("y416 %zux%zu as RGBA16: %d (%s)\n", w, h, e, CGLErrorString(e));
    if (e) return 0;
    const char* vs = "#version 410 core\nout vec2 uv; void main(){ vec2 p = vec2(float((gl_VertexID&1)*4-1), float((gl_VertexID>>1)*4-1)); uv = p*0.5+0.5; gl_Position = vec4(p,0,1);}";
    const char* fs = "#version 410 core\nuniform sampler2DRect t; in vec2 uv; out vec4 o; void main(){ o = texture(t, uv*vec2(textureSize(t))); }";
    GLuint p = glCreateProgram(); for (auto [ty, s] : { std::pair{GL_VERTEX_SHADER, vs}, std::pair{GL_FRAGMENT_SHADER, fs} }) { GLuint sh = glCreateShader(ty); glShaderSource(sh, 1, &s, nullptr); glCompileShader(sh); glAttachShader(p, sh); }
    glLinkProgram(p); glUseProgram(p);
    GLuint fbo, ct; glGenFramebuffers(1, &fbo); glGenTextures(1, &ct); glBindTexture(GL_TEXTURE_2D, ct);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, (GLsizei)w, (GLsizei)h, 0, GL_RGBA, GL_FLOAT, nullptr);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ct, 0);
    glViewport(0, 0, (GLsizei)w, (GLsizei)h); GLuint vao; glGenVertexArrays(1, &vao); glBindVertexArray(vao);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_RECTANGLE, t); glDrawArrays(GL_TRIANGLES, 0, 3); glFinish();
    std::vector<float> px(w * h * 4); glReadPixels(0, 0, (GLsizei)w, (GLsizei)h, GL_RGBA, GL_FLOAT, px.data());
    for (int b = 0; b < 7; ++b) { const float* q = &px[((h / 2) * w + 64 * b + 32) * 4]; std::printf("  bar %d: r=%.0f g=%.0f b=%.0f a=%.0f (x65535)\n", b, q[0]*65535, q[1]*65535, q[2]*65535, q[3]*65535); }
    return 0;
}
