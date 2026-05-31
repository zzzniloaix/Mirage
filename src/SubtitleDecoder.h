#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/rational.h>
}

// One displayable subtitle line with absolute stream PTS bounds.
struct SubtitleEvent {
    double      start_pts = 0.0;
    double      end_pts   = 0.0;
    std::string text;        // multi-line; '\n'-separated
};

// One bitmap rectangle inside a bitmap subtitle event. Pre-decoded to RGBA8
// during decode_packet so the render thread doesn't touch palettes.
struct BitmapSubRect {
    int                  x = 0, y = 0;   // top-left in authored coordinate space
    int                  w = 0, h = 0;
    std::vector<uint8_t> rgba;            // w * h * 4 bytes
};

struct BitmapSubEvent {
    double                     start_pts  = 0.0;
    double                     end_pts    = 0.0;
    int                        authored_w = 0;     // resolution the rects were authored for
    int                        authored_h = 0;     //   (0 → fallback to playback dims)
    std::vector<BitmapSubRect> rects;
};

// Decodes a subtitle stream (text or bitmap) and exposes events active at a
// given PTS. Text codecs: SRT, ASS/SSA, WebVTT, MOV_TEXT — the ASS Dialogue
// payload is reduced to plain text via extract_ass_text(). Bitmap codecs:
// HDMV PGS, DVD, DVB — palette-decoded to RGBA8 at decode time.
class SubtitleDecoder {
public:
    SubtitleDecoder() = default;
    ~SubtitleDecoder();

    SubtitleDecoder(const SubtitleDecoder&) = delete;
    SubtitleDecoder& operator=(const SubtitleDecoder&) = delete;

    [[nodiscard]] bool open(AVCodecParameters* par, AVRational time_base);
    void close();

    [[nodiscard]] bool reopen(AVCodecParameters* par, AVRational time_base);

    // Decode one packet (caller frees it). pkt may be nullptr (drain).
    void decode_packet(AVPacket* pkt);

    // Drop all stored events (call after seek / track switch).
    void flush();

    // Return the text active at `pts`, or empty if none. Joined by '\n' if
    // multiple events overlap (rare — usually one).
    [[nodiscard]] std::string at(double pts) const;

    // Snapshot of bitmap events active at `pts`. Returns a copy because the
    // rendering path runs concurrently with decode_packet().
    [[nodiscard]] std::vector<BitmapSubEvent> active_bitmaps(double pts) const;

    [[nodiscard]] bool is_text_format() const { return is_text_; }

    // Convert a paletted (FFmpeg AV_PIX_FMT_PAL8 layout) bitmap to RGBA8.
    // palette: 256 BGRA8 entries (1024 bytes). Exposed for unit testing.
    static void palette_to_rgba(const std::uint8_t* indices, int stride,
                                const std::uint8_t* palette,
                                int w, int h,
                                std::uint8_t* out_rgba);

private:
    AVCodecContext* ctx_       = nullptr;
    AVRational      time_base_ = {1, 1};
    bool            is_text_   = false;

    mutable std::mutex                ev_mtx_;
    std::vector<SubtitleEvent>        events_;
    std::vector<BitmapSubEvent>       bitmap_events_;

    // Append non-overlapping event range; trims older events past a cap.
    void store_event(SubtitleEvent ev);
    void store_bitmap_event(BitmapSubEvent ev);
};
