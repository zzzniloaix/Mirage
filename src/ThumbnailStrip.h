#pragma once

#include <glad/gl.h>

#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <thread>
#include <cstdint>
#include <atomic>
#include <optional>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/rational.h>
}

// Async keyframe thumbnails: the seek-bar hover preview card (PlayerUI) and the
// "Keyframe filmstrip" (K) drawn above the seek bar while scrubbing.
//
// Background thread opens the file independently and publishes every keyframe
// PTS from the container index (keyframe navigation uses the full list). It
// then decodes a thumbnail for a sampled subset (at most kMaxThumbs, spaced
// >= duration/kMaxThumbs apart) in coarse-to-fine order — every kCoarseStep-th
// sample first so the whole timeline gets previews quickly, then the rest —
// downscales each to kDecodeH pixels tall (RGB24, display aspect), and pushes
// to a mutex-protected queue.
//
// Main thread calls upload_pending() every frame to drain the queue (a few
// textures per frame) and upload GL textures. thumbs_ is main-thread only, so
// nearest_at_or_before() / draw() need no locking.
class ThumbnailStrip {
public:
    ThumbnailStrip() = default;
    ~ThumbnailStrip();
    ThumbnailStrip(const ThumbnailStrip&) = delete;
    ThumbnailStrip& operator=(const ThumbnailStrip&) = delete;

    // Start async decode.  Must be called after the OpenGL context is current.
    // Returns false if the container has no keyframe index (live streams, etc.)
    // Thumbnail width follows the source's display aspect (sample aspect
    // ratio applied, rotation not), read from the decoded frames.
    [[nodiscard]] bool init(const std::string& url,
                            int video_stream_idx,
                            AVRational time_base,
                            double duration);

    // Upload pending decoded thumbnails to GPU (at most kUploadPerFrame per call,
    // to avoid hitches). Call every frame from the main thread.
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

    // ── Hover-preview accessors (main thread only) ────────────────────────────

    struct ThumbRef { GLuint tex; int w, h; double pts; };

    // Uploaded thumbnail with the greatest pts <= t, or nullopt when there is
    // none or it is more than max_gap seconds before t. "At or before" matches
    // what a seek to t shows first (decode starts at the preceding keyframe).
    std::optional<ThumbRef> nearest_at_or_before(double t, double max_gap) const;

    // Default max_gap for nearest_at_or_before(): max(10 s, 3 × sample stride).
    double preview_max_gap() const;

    // Seconds between sampled keyframes (0 = every keyframe is decoded).
    double sample_stride() const { return sample_stride_s_; }

    // Copy of all keyframe PTS (locks kf_pts_mtx_). Empty until the index is read.
    std::vector<double> keyframes_copy() const;

    int  thumb_count() const { return static_cast<int>(thumbs_.size()); }
    // Number of keyframes chosen for thumbnail decode (0 until the index is read).
    int  sampled_count() const { return sampled_count_.load(); }
    // True once the background decode thread has finished (or given up).
    bool ready() const { return done_.load(); }
    // ready() and every decoded thumbnail has been uploaded (QA capture gate).
    bool all_uploaded() const;

    // Height in framebuffer pixels of the thumbnail strip (for layout / hit-testing).
    static constexpr int kStripPx = 54;
    // Decoded thumbnail height in framebuffer pixels (never upscaled past the
    // source height). Decoupled from kStripPx: the strip draws them scaled down.
    static constexpr int kDecodeH = 160;
    // Cap on decoded thumbnails (~0.14 MB each at 284×160 RGB24).
    static constexpr int kMaxThumbs = 400;
    // Coarse pass decodes every kCoarseStep-th sampled keyframe first.
    static constexpr int kCoarseStep = 8;
    static constexpr int kUploadPerFrame = 8;

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
    std::vector<double> thumb_pts_; // parallel to thumbs_ (for index_at_or_before)
    std::atomic<bool>   done_{ false };

    mutable std::mutex  kf_pts_mtx_;
    std::vector<double> kf_pts_;   // all keyframe PTS in order, populated before decode starts

    mutable std::mutex  pending_mtx_;
    std::queue<Decoded> pending_;

    std::atomic<int>    sampled_count_{ 0 };

    std::jthread decode_thread_;

    double duration_        = 0.0;
    double sample_stride_s_ = 0.0;   // set in init(), read-only afterwards

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
