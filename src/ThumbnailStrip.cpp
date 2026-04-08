#include "ThumbnailStrip.h"
#include "ScrubBar.h"
#include "Logger.h"

#include <algorithm>
#include <cmath>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

// ── Shaders ───────────────────────────────────────────────────────────────────

// Background backing quad (gl_VertexID trick, NDC uniforms, no VBO).
static const char* kBgVert = R"glsl(
#version 410 core
uniform vec4 u_bg_rect;
void main() {
    float x = (gl_VertexID & 1) != 0 ? u_bg_rect.y : u_bg_rect.x;
    float y = (gl_VertexID & 2) != 0 ? u_bg_rect.w : u_bg_rect.z;
    gl_Position = vec4(x, y, 0.0, 1.0);
}
)glsl";

static const char* kBgFrag = R"glsl(
#version 410 core
uniform vec4 u_bg_color;
out vec4 frag_color;
void main() { frag_color = u_bg_color; }
)glsl";

// Thumbnail textured quad (gl_VertexID, NDC uniforms).
// v is flipped so row 0 of the uploaded RGB data maps to the top of the quad.
static const char* kThVert = R"glsl(
#version 410 core
uniform vec4 u_th_rect;  // xmin, xmax, ymin, ymax (NDC)
out vec2 v_uv;
void main() {
    float x = (gl_VertexID & 1) != 0 ? u_th_rect.y : u_th_rect.x;
    float y = (gl_VertexID & 2) != 0 ? u_th_rect.w : u_th_rect.z;
    float u = (gl_VertexID & 1) != 0 ? 1.0 : 0.0;
    float v = (gl_VertexID & 2) != 0 ? 0.0 : 1.0;  // flip v: row 0 = top
    v_uv = vec2(u, v);
    gl_Position = vec4(x, y, 0.0, 1.0);
}
)glsl";

static const char* kThFrag = R"glsl(
#version 410 core
in vec2 v_uv;
uniform sampler2D u_th_tex;
uniform float     u_th_highlight;  // 1.0 = nearest cursor, 0.0 = dimmed
out vec4 frag_color;
void main() {
    vec4 c = texture(u_th_tex, v_uv);
    c.rgb *= mix(0.55, 1.0, u_th_highlight);
    frag_color = c;
}
)glsl";

// ── Helpers ───────────────────────────────────────────────────────────────────

static GLuint compile_shader(GLenum type, const char* src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512]; glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        logger::error("ThumbnailStrip shader: {}", log);
        glDeleteShader(s); return 0;
    }
    return s;
}

static GLuint link_program(GLuint vert, GLuint frag)
{
    GLuint p = glCreateProgram();
    glAttachShader(p, vert); glAttachShader(p, frag);
    glLinkProgram(p);
    glDeleteShader(vert); glDeleteShader(frag);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512]; glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        logger::error("ThumbnailStrip program: {}", log);
        glDeleteProgram(p); return 0;
    }
    return p;
}

// ── ThumbnailStrip ────────────────────────────────────────────────────────────

ThumbnailStrip::~ThumbnailStrip()
{
    // Stop the background thread first (it may be pushing to pending_).
    decode_thread_ = {};

    for (auto& t : thumbs_)
        if (t.tex) glDeleteTextures(1, &t.tex);

    if (bg_program_)    glDeleteProgram(bg_program_);
    if (thumb_program_) glDeleteProgram(thumb_program_);
    if (bg_vao_)        glDeleteVertexArrays(1, &bg_vao_);
    if (thumb_vao_)     glDeleteVertexArrays(1, &thumb_vao_);
}

bool ThumbnailStrip::init(const std::string& url,
                           int video_stream_idx,
                           AVRational time_base,
                           double duration,
                           int vid_w, int vid_h)
{
    duration_ = duration;

    // Thumbnail height matches the strip; width is aspect-correct.
    thumb_h_ = kStripPx;
    thumb_w_ = (vid_h > 0) ? (thumb_h_ * vid_w / vid_h) : thumb_h_;

    if (!compile_shaders()) return false;

    glGenVertexArrays(1, &bg_vao_);
    glGenVertexArrays(1, &thumb_vao_);

    decode_thread_ = std::jthread([this, url, video_stream_idx, time_base]
                                  (std::stop_token st) {
        decode_loop(st, url, video_stream_idx, time_base);
    });

    return true;
}

void ThumbnailStrip::upload_pending()
{
    // Drain the queue and upload each decoded thumbnail as a GL texture.
    std::queue<Decoded> batch;
    {
        std::lock_guard lock(pending_mtx_);
        std::swap(batch, pending_);
    }

    while (!batch.empty()) {
        Decoded d = std::move(batch.front());
        batch.pop();

        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, d.w, d.h, 0,
                     GL_RGB, GL_UNSIGNED_BYTE, d.rgb.data());

        // Insert sorted by pts.
        Thumb t{ d.pts, tex, d.w, d.h };
        auto it = std::lower_bound(thumbs_.begin(), thumbs_.end(), t,
                                   [](const Thumb& a, const Thumb& b) {
                                       return a.pts < b.pts;
                                   });
        thumbs_.insert(it, t);
    }
}

void ThumbnailStrip::draw(double scrub_pos, double duration, int fb_w, int fb_h)
{
    if (thumbs_.empty() || duration <= 0.0 || fb_w <= 0 || fb_h <= 0) return;

    glViewport(0, 0, fb_w, fb_h);

    // ── Backing panel ─────────────────────────────────────────────────────────
    // Sits just above the scrub bar backing.
    const float gap_px   = 2.0f;
    const float ymin_px  = static_cast<float>(ScrubBar::kBackingPx) + gap_px;
    const float ymax_px  = ymin_px + static_cast<float>(kStripPx);

    auto px2x = [&](float px) { return px / static_cast<float>(fb_w) * 2.0f - 1.0f; };
    auto px2y = [&](float py) { return py / static_cast<float>(fb_h) * 2.0f - 1.0f; };

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(bg_program_);
    glUniform4f(u_bg_rect_,  -1.0f, 1.0f, px2y(ymin_px), px2y(ymax_px));
    glUniform4f(u_bg_color_,  0.0f, 0.0f, 0.0f, 0.65f);
    glBindVertexArray(bg_vao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisable(GL_BLEND);

    // ── Thumbnails ────────────────────────────────────────────────────────────
    // Find the thumb nearest the scrub cursor for highlighting.
    int nearest_idx = 0;
    double best_dist = std::abs(thumbs_[0].pts - scrub_pos);
    for (int i = 1; i < static_cast<int>(thumbs_.size()); ++i) {
        double d = std::abs(thumbs_[i].pts - scrub_pos);
        if (d < best_dist) { best_dist = d; nearest_idx = i; }
    }

    // Draw highlighted thumb last so it renders on top.
    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < static_cast<int>(thumbs_.size()); ++i) {
            bool highlight = (i == nearest_idx);
            if (pass == 0 && highlight) continue;  // highlighted drawn in pass 1
            if (pass == 1 && !highlight) continue;

            float cx_px = static_cast<float>(thumbs_[i].pts / duration)
                          * static_cast<float>(fb_w);
            draw_thumb(thumbs_[i], cx_px, highlight, fb_w, fb_h);
        }
    }
}

void ThumbnailStrip::draw_thumb(const Thumb& t, float cx_px, bool highlight,
                                 int fb_w, int fb_h)
{
    const float gap_px  = 2.0f;
    const float ymin_px = static_cast<float>(ScrubBar::kBackingPx) + gap_px;
    const float ymax_px = ymin_px + static_cast<float>(kStripPx);

    float half_w = static_cast<float>(thumb_w_) * 0.5f;
    auto px2x = [&](float px) { return px / static_cast<float>(fb_w) * 2.0f - 1.0f; };
    auto px2y = [&](float py) { return py / static_cast<float>(fb_h) * 2.0f - 1.0f; };

    glUseProgram(thumb_program_);
    glUniform4f(u_th_rect_,
                px2x(cx_px - half_w), px2x(cx_px + half_w),
                px2y(ymin_px),        px2y(ymax_px));
    glUniform1f(u_th_highlight_, highlight ? 1.0f : 0.0f);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, t.tex);
    glUniform1i(u_th_tex_, 0);
    glBindVertexArray(thumb_vao_);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

// ── Background decode thread ──────────────────────────────────────────────────

void ThumbnailStrip::decode_loop(std::stop_token st, std::string url,
                                  int video_stream_idx, AVRational time_base)
{
    // Open our own AVFormatContext — completely independent of the playback pipeline.
    AVFormatContext* fmt_ctx = nullptr;
    if (avformat_open_input(&fmt_ctx, url.c_str(), nullptr, nullptr) < 0) {
        logger::error("ThumbnailStrip: failed to open '{}'", url);
        return;
    }
    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
        avformat_close_input(&fmt_ctx);
        return;
    }

    AVStream* stream = fmt_ctx->streams[video_stream_idx];

    // Collect keyframe timestamps from the container index.
    // AVStream::index_entries was made opaque in FFmpeg 5; use the accessor API.
    std::vector<int64_t> keyframe_ts;
    int n_entries = avformat_index_get_entries_count(stream);
    for (int i = 0; i < n_entries; ++i) {
        const AVIndexEntry* e = avformat_index_get_entry(stream, i);
        if (e && (e->flags & AVINDEX_KEYFRAME))
            keyframe_ts.push_back(e->timestamp);
    }

    if (keyframe_ts.empty()) {
        logger::warn("ThumbnailStrip: no keyframe index for stream {}", video_stream_idx);
        avformat_close_input(&fmt_ctx);
        return;
    }

    // Publish all keyframe PTS immediately — before any decode — so main thread
    // can use next/prev_keyframe_pts() as soon as the user presses a key.
    {
        std::lock_guard lock(kf_pts_mtx_);
        kf_pts_.reserve(keyframe_ts.size());
        for (int64_t ts : keyframe_ts)
            kf_pts_.push_back(static_cast<double>(ts) * av_q2d(time_base));
    }

    logger::info("ThumbnailStrip: {} keyframes found, decoding thumbnails {}×{}",
                 keyframe_ts.size(), thumb_w_, thumb_h_);

    // Open decoder.
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    AVCodecContext* codec_ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codec_ctx, stream->codecpar);
    // Use fewer threads to avoid starving the playback decoder.
    codec_ctx->thread_count = 2;
    if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
        avcodec_free_context(&codec_ctx);
        avformat_close_input(&fmt_ctx);
        return;
    }

    AVPacket* pkt   = av_packet_alloc();
    AVFrame*  frame = av_frame_alloc();
    SwsContext* sws = nullptr;

    for (int64_t target_ts : keyframe_ts) {
        if (st.stop_requested()) break;

        // Seek to this keyframe.
        avformat_seek_file(fmt_ctx, video_stream_idx,
                           INT64_MIN, target_ts, target_ts, 0);
        avcodec_flush_buffers(codec_ctx);

        // Decode until we get the first frame at or after the target.
        bool got = false;
        while (!got && !st.stop_requested()) {
            int ret = av_read_frame(fmt_ctx, pkt);
            if (ret < 0) break;

            if (pkt->stream_index == video_stream_idx) {
                avcodec_send_packet(codec_ctx, pkt);
                if (avcodec_receive_frame(codec_ctx, frame) == 0) {
                    got = true;
                }
            }
            av_packet_unref(pkt);
        }

        if (!got) continue;

        // Lazy-init sws context on first frame (gets actual pixel format).
        if (!sws) {
            sws = sws_getContext(frame->width, frame->height,
                                 static_cast<AVPixelFormat>(frame->format),
                                 thumb_w_, thumb_h_, AV_PIX_FMT_RGB24,
                                 SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (!sws) continue;
        }

        // Scale to thumbnail size.
        Decoded d;
        d.pts = (frame->pts != AV_NOPTS_VALUE)
            ? static_cast<double>(frame->pts) * av_q2d(time_base)
            : static_cast<double>(target_ts)  * av_q2d(time_base);
        d.w   = thumb_w_;
        d.h   = thumb_h_;
        d.rgb.resize(static_cast<size_t>(thumb_w_ * thumb_h_ * 3));

        uint8_t* dst_data[1]    = { d.rgb.data() };
        int      dst_stride[1]  = { thumb_w_ * 3 };
        sws_scale(sws, frame->data, frame->linesize, 0, frame->height,
                  dst_data, dst_stride);

        av_frame_unref(frame);

        {
            std::lock_guard lock(pending_mtx_);
            pending_.push(std::move(d));
        }
    }

    sws_freeContext(sws);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&codec_ctx);
    avformat_close_input(&fmt_ctx);

    logger::info("ThumbnailStrip: decode complete ({} thumbnails)", thumbs_.size());
}

// ── Keyframe navigation ───────────────────────────────────────────────────────

double ThumbnailStrip::next_keyframe_pts(double current_pts) const
{
    std::lock_guard lock(kf_pts_mtx_);
    if (kf_pts_.empty()) return -1.0;
    // First keyframe strictly after current_pts (1 ms tolerance for fp rounding).
    auto it = std::upper_bound(kf_pts_.begin(), kf_pts_.end(), current_pts + 0.001);
    return (it != kf_pts_.end()) ? *it : -1.0;
}

double ThumbnailStrip::prev_keyframe_pts(double current_pts) const
{
    std::lock_guard lock(kf_pts_mtx_);
    if (kf_pts_.empty()) return -1.0;
    // Last keyframe strictly before current_pts (1 ms tolerance).
    auto it = std::lower_bound(kf_pts_.begin(), kf_pts_.end(), current_pts - 0.001);
    if (it == kf_pts_.begin()) return -1.0;
    return *std::prev(it);
}

// ── Shader compilation ────────────────────────────────────────────────────────

bool ThumbnailStrip::compile_shaders()
{
    // Background program
    GLuint bv = compile_shader(GL_VERTEX_SHADER,   kBgVert);
    GLuint bf = compile_shader(GL_FRAGMENT_SHADER, kBgFrag);
    if (!bv || !bf) { glDeleteShader(bv); glDeleteShader(bf); return false; }
    bg_program_ = link_program(bv, bf);
    if (!bg_program_) return false;
    u_bg_rect_  = glGetUniformLocation(bg_program_, "u_bg_rect");
    u_bg_color_ = glGetUniformLocation(bg_program_, "u_bg_color");

    // Thumbnail program
    GLuint tv = compile_shader(GL_VERTEX_SHADER,   kThVert);
    GLuint tf = compile_shader(GL_FRAGMENT_SHADER, kThFrag);
    if (!tv || !tf) { glDeleteShader(tv); glDeleteShader(tf); return false; }
    thumb_program_ = link_program(tv, tf);
    if (!thumb_program_) return false;
    u_th_rect_      = glGetUniformLocation(thumb_program_, "u_th_rect");
    u_th_tex_       = glGetUniformLocation(thumb_program_, "u_th_tex");
    u_th_highlight_ = glGetUniformLocation(thumb_program_, "u_th_highlight");

    return true;
}
