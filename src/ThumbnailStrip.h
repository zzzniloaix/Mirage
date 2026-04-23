#pragma once

#include <glad/gl.h>

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <thread>
#include <cstdint>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/rational.h>
}

// Async keyframe thumbnail strip rendered above the scrub bar while scrubbing.
//
// Background thread opens the file independently, seeks to each keyframe in the
// container index, decodes one frame per keyframe, downscales to kThumbH pixels
// tall (RGB24), and pushes to a mutex-protected queue.
//
// Main thread calls upload_pending() every frame to drain the queue and upload
// GL textures, then draw() to render the strip while scrubbing.
class ThumbnailStrip {
public:
    ThumbnailStrip() = default;
    ~ThumbnailStrip();
    ThumbnailStrip(const ThumbnailStrip&) = delete;
    ThumbnailStrip& operator=(const ThumbnailStrip&) = delete;

    // Start async decode.  Must be called after the OpenGL context is current.
    // Returns false if the container has no keyframe index (live streams, etc.)
    [[nodiscard]] bool init(const std::string& url,
                            int video_stream_idx,
                            AVRational time_base,
                            double duration,
                            int vid_w, int vid_h);

    // Upload any pending decoded thumbnails to GPU. Call every frame from main thread.
    void upload_pending();

    // Draw the strip above the scrub bar. Call only while scrubbing.
    // scrub_pos and duration are in seconds.
    // bottom_off: height in framebuffer pixels already occupied below (ImGui bar,
    //   timeline ticks, waveform) — the strip sits immediately above this.
    void draw(double scrub_pos, double duration, int fb_w, int fb_h,
              float bottom_off = 0.0f);

    // Keyframe navigation — available as soon as the container index is read
    // (before any thumbnails are decoded). Returns -1.0 if not yet ready or
    // no keyframe exists in that direction.
    double next_keyframe_pts(double current_pts) const;
    double prev_keyframe_pts(double current_pts) const;

    // Height in framebuffer pixels of the thumbnail strip (for layout / hit-testing).
    static constexpr int kStripPx = 54;

private:
    // Decoded thumbnail passed from background thread → main thread via pending_.
    struct Decoded {
        double               pts;
        int                  w, h;
        std::vector<uint8_t> rgb;  // RGB24, row-major, top-to-bottom
    };

    // Per-keyframe GPU resource.
    struct Thumb {
        double pts = 0.0;
        GLuint tex = 0;
        int    w   = 0;
        int    h   = 0;
    };

    std::vector<Thumb>  thumbs_;    // sorted by pts as they arrive

    mutable std::mutex  kf_pts_mtx_;
    std::vector<double> kf_pts_;   // all keyframe PTS in order, populated before decode starts

    std::mutex          pending_mtx_;
    std::queue<Decoded> pending_;

    std::jthread decode_thread_;

    double duration_ = 0.0;
    int    thumb_w_  = 0;   // decoded thumbnail dimensions
    int    thumb_h_  = 0;

    // GL — background quad (dark backing)
    GLuint bg_vao_     = 0;
    GLuint bg_program_ = 0;
    GLint  u_bg_rect_  = -1;
    GLint  u_bg_color_ = -1;

    // GL — textured thumbnail quads
    GLuint thumb_vao_       = 0;
    GLuint thumb_program_   = 0;
    GLint  u_th_rect_       = -1;
    GLint  u_th_tex_        = -1;
    GLint  u_th_highlight_  = -1;

    [[nodiscard]] bool compile_shaders();

    void draw_thumb(const Thumb& t, float cx_px, bool highlight,
                    int fb_w, int fb_h, float bottom_off);

    void decode_loop(std::stop_token st, std::string url,
                     int video_stream_idx, AVRational time_base);
};
