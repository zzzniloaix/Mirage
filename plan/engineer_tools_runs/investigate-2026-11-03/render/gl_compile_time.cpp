// Cost of (re)building each of VideoRenderer's programs on this machine, the
// way a tag-change re-init would: compile + link + uniforms + one draw +
// glFinish, in an offscreen CGL context. Shaders read from VideoRenderer.cpp.
// Usage: gl_compile_time <VideoRenderer.cpp> [rounds]
#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static std::string raw_string(const std::string& src, const char* name)
{
    const std::string open = std::string(name) + " = R\"glsl(";
    auto a = src.find(open) + open.size();
    return src.substr(a, src.find(")glsl\"", a) - a);
}
static GLuint shader(GLenum t, const std::string& s)
{
    GLuint sh = glCreateShader(t);
    const char* p = s.c_str();
    glShaderSource(sh, 1, &p, nullptr);
    glCompileShader(sh);
    GLint ok = 0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) { std::fprintf(stderr, "compile failed\n"); std::exit(1); }
    return sh;
}

int main(int argc, char** argv)
{
    std::ifstream in(argv[1]); std::stringstream ss; ss << in.rdbuf();
    const std::string src = ss.str();
    const int rounds = argc > 2 ? std::atoi(argv[2]) : 3;
    CGLPixelFormatAttribute attrs[] = { kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core,
                                        kCGLPFAAccelerated, (CGLPixelFormatAttribute)0 };
    CGLPixelFormatObj pfo; GLint npf = 0; CGLChoosePixelFormat(attrs, &pfo, &npf);
    CGLContextObj cgl; CGLCreateContext(pfo, nullptr, &cgl); CGLSetCurrentContext(cgl);

    // Inputs: 64x64 RGB16 2D texture (SW programs) and R16/RG16 rect textures (HW programs).
    std::vector<unsigned short> data(64 * 64 * 3, 30000);
    GLuint t2d, ty, tuv;
    glGenTextures(1, &t2d); glBindTexture(GL_TEXTURE_2D, t2d);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16, 64, 64, 0, GL_RGB, GL_UNSIGNED_SHORT, data.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glGenTextures(1, &ty); glBindTexture(GL_TEXTURE_RECTANGLE, ty);
    glTexImage2D(GL_TEXTURE_RECTANGLE, 0, GL_R16, 64, 64, 0, GL_RED, GL_UNSIGNED_SHORT, data.data());
    glGenTextures(1, &tuv); glBindTexture(GL_TEXTURE_RECTANGLE, tuv);
    glTexImage2D(GL_TEXTURE_RECTANGLE, 0, GL_RG16, 32, 32, 0, GL_RG, GL_UNSIGNED_SHORT, data.data());
    GLuint fbo, ct; glGenFramebuffers(1, &fbo); glGenTextures(1, &ct);
    glBindTexture(GL_TEXTURE_2D, ct);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ct, 0);
    glViewport(0, 0, 64, 64);
    GLuint vao; glGenVertexArrays(1, &vao); glBindVertexArray(vao);

    const char* names[] = { "kFragSdr", "kFragHdr", "kFragSdrHw", "kFragHdrHw" };
    for (int r = 0; r < rounds; ++r) {
        for (const char* n : names) {
            const bool hw = std::string(n).find("Hw") != std::string::npos;
            const auto t0 = std::chrono::steady_clock::now();
            GLuint prog = glCreateProgram();
            GLuint vs = shader(GL_VERTEX_SHADER, raw_string(src, "kVertSrc"));
            GLuint fs = shader(GL_FRAGMENT_SHADER, raw_string(src, n));
            glAttachShader(prog, vs); glAttachShader(prog, fs);
            glLinkProgram(prog);
            glDeleteShader(vs); glDeleteShader(fs);
            glUseProgram(prog);
            if (hw) {
                glUniform1i(glGetUniformLocation(prog, "tex_y"), 0);
                glUniform1i(glGetUniformLocation(prog, "tex_uv"), 1);
                glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_RECTANGLE, ty);
                glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_RECTANGLE, tuv);
            } else {
                glUniform1i(glGetUniformLocation(prog, "tex_rgb"), 0);
                glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, t2d);
            }
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glFinish();
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            std::printf("round %d %-10s compile+link+first draw %.2f ms\n", r, n, ms);
            glDeleteProgram(prog);
        }
    }
    // A uniform-only change (what a matrix/range/PQ<->HLG switch needs on an existing program).
    GLuint prog = glCreateProgram();
    glAttachShader(prog, shader(GL_VERTEX_SHADER, raw_string(src, "kVertSrc")));
    glAttachShader(prog, shader(GL_FRAGMENT_SHADER, raw_string(src, "kFragHdrHw")));
    glLinkProgram(prog); glUseProgram(prog);
    glDrawArrays(GL_TRIANGLES, 0, 3); glFinish();
    const float m[9] = { 1, 1, 1, 0, -0.2f, 2.1f, 1.8f, -0.5f, 0 };
    const auto t0 = std::chrono::steady_clock::now();
    glUniformMatrix3fv(glGetUniformLocation(prog, "u_yuv2rgb"), 1, GL_FALSE, m);
    glUniform1i(glGetUniformLocation(prog, "u_hdr_mode"), 1);
    glDrawArrays(GL_TRIANGLES, 0, 3); glFinish();
    std::printf("uniform update + draw on an existing program: %.3f ms\n",
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
    return 0;
}
