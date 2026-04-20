#pragma once

#include <glad/gl.h>
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>

struct DebugInfo {
    double  pts;           // video PTS (seconds)
    int64_t frame_number;  // frames displayed since file open
    double  av_diff_ms;    // video_pts − audio_clock (ms); NaN = paused or no audio
    bool    has_audio;     // false → show "N/A", true+NaN → show "--" (paused)
    double  decode_ms;     // most recent video decode time (ms/frame)
};

// Semi-transparent overlay in the top-left corner showing live playback stats.
// Toggle visibility with the D key.
//
// Layout (4 lines):
//   PTS     HH:MM:SS.mmm
//   Frame   NNNNN
//   A/V     ±N.N ms   (colour-coded: white < 10 ms, yellow < 50 ms, red ≥ 50 ms)
//   Decode  N.NN ms/f
class DebugHUD {
public:
    DebugHUD() = default;
    ~DebugHUD();
    DebugHUD(const DebugHUD&) = delete;
    DebugHUD& operator=(const DebugHUD&) = delete;

    [[nodiscard]] bool init();

    // Draw the live debug overlay (top-left).
    // scale = framebuffer_pixels / window_pixels (2.0 on retina displays).
    void draw(const DebugInfo& info, int fb_w, int fb_h, float scale = 1.0f);

    // One line of the stream inspector panel.
    struct InspectorLine {
        std::string text;
        bool        header = false;  // true → dimmed gray (section label)
    };

    // Draw the static stream inspector panel (top-right).
    void draw_inspector(const std::vector<InspectorLine>& lines,
                        int fb_w, int fb_h, float scale = 1.0f);

    // One entry in the track selector dropdown.
    struct TrackItem {
        int         stream_idx;  // Demuxer stream index
        std::string label;       // display string
    };

    // Result of draw_tracks(): which stream_idx was clicked this frame (-1 = none).
    struct TrackClick { int audio = -1; int video = -1; };

    // Draw the audio + video track selector panels (centered on screen).
    // fb_click_x/y: framebuffer-space coordinates of a mouse press this frame,
    //               or -1 if no click occurred.
    // fb_cursor_x/y: current cursor position in framebuffer space (for hover highlight).
    TrackClick draw_tracks(
        const std::vector<TrackItem>& audio, int cur_audio_stream,
        const std::vector<TrackItem>& video, int cur_video_stream,
        int fb_w, int fb_h, float scale,
        float fb_click_x = -1.0f, float fb_click_y = -1.0f,
        float fb_cursor_x = -1.0f, float fb_cursor_y = -1.0f);

    // Push one A/V drift sample. Call every frame (NaN = paused/no audio).
    // The ring buffer holds the last kDriftCap samples; older entries are overwritten.
    void push_drift(double av_diff_ms);

    // Draw the rolling A/V drift graph (bottom-left).
    // bottom_offset_px: framebuffer pixels already occupied at the bottom (scrub bar, etc.)
    void draw_drift_graph(int fb_w, int fb_h, float scale = 1.0f,
                          float bottom_offset_px = 0.0f);

    // ── Waveform (W key) ──────────────────────────────────────────────────────

    // Height of the waveform strip in framebuffer pixels.
    static constexpr int kWaveformPx = 48;

    // Draw the audio waveform strip spanning the full window width.
    // peaks: array of n downsampled absolute-amplitude values in [0,1],
    //        chronological order (oldest first).
    // bottom_offset_px: fb pixels already used at the bottom (scrub bar, etc.)
    void draw_waveform(const float* peaks, int n,
                       int fb_w, int fb_h, float scale,
                       float bottom_offset_px = 0.0f);

    // ── Network log + Manifest tags (N key) ───────────────────────────────────

    // One entry in the network request log.
    struct NetLogEntry {
        std::string time;   // "HH:MM:SS.mmm"
        std::string type;   // "manifest" | "variant" | "init" | "segment" | "other"
        std::string url;    // last ~60 chars of URL
        float r, g, b;      // colour coding
    };

    // Draw the network log panel (right side of screen).
    void draw_network_log(const std::vector<NetLogEntry>& entries,
                          int fb_w, int fb_h, float scale = 1.0f);

    // One manifest tag entry for display.
    struct TagEntry {
        double      pts;     // stream time in seconds
        std::string label;   // short display string, e.g. "DISCONTINUITY", "CUE-OUT 30s"
        float r, g, b;       // colour coding
    };

    // Draw the manifest tag inspector panel (left side of screen).
    void draw_manifest_tags(const std::vector<TagEntry>& tags,
                             double cur_pts,
                             int fb_w, int fb_h, float scale = 1.0f);

    // Draw a thin timeline strip above the scrub bar with coloured tick marks.
    // Returns the pts seeked to if a click lands in the strip, or -1.
    static constexpr int kTimelinePx = 10;  // strip height in framebuffer pixels

    double draw_timeline_ticks(const std::vector<TagEntry>& tags,
                                double duration,
                                int fb_w, int fb_h, float scale,
                                float bottom_offset_px,
                                float click_x = -1.0f, float click_y = -1.0f);

    // ── Help overlay (H key) ──────────────────────────────────────────────────

    // Draw a centered panel listing all keyboard controls.
    void draw_help(int fb_w, int fb_h, float scale = 1.0f);

private:
    // ── A/V drift ring buffer ─────────────────────────────────────────────────
    static constexpr int kDriftCap = 600;  // ≈10s at 60fps
    float drift_buf_[kDriftCap]{};
    int   drift_head_  = 0;
    int   drift_count_ = 0;
    // Background quad (NDC, gl_VertexID trick — no VBO needed)
    GLuint bg_vao_     = 0;
    GLuint bg_program_ = 0;
    GLint  u_rect_     = -1;  // vec4: xmin, xmax, ymin, ymax (NDC)
    GLint  u_bg_color_ = -1;  // vec4 rgba

    // Text (pixel-space VBO → NDC via uniforms)
    GLuint text_vao_     = 0;
    GLuint text_vbo_     = 0;
    GLuint text_program_ = 0;
    GLint  u_fb_size_    = -1;  // vec2 fb_w, fb_h
    GLint  u_text_color_ = -1;  // vec4 rgba

    [[nodiscard]] bool compile_shaders();

    void draw_bg(float ndc_xmin, float ndc_xmax, float ndc_ymin, float ndc_ymax,
                 float r, float g, float b, float a);

    // Renders one line of text; returns the pixel width of the rendered string.
    float draw_text(const char* text, float px, float py,
                    float r, float g, float b,
                    float scale, int fb_w, int fb_h);
};
