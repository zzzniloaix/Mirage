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

// Decodes a subtitle stream (text-based: SRT, ASS/SSA, WebVTT, MOV_TEXT) into
// SubtitleEvents and exposes the active event(s) at a given PTS to the renderer.
//
// Bitmap subtitles (DVD/HDMV PGS/DVB) are detected but not rendered — the
// decoder logs once and drops bitmap rects.
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

    [[nodiscard]] bool is_text_format() const { return is_text_; }

private:
    AVCodecContext* ctx_       = nullptr;
    AVRational      time_base_ = {1, 1};
    bool            is_text_   = false;

    mutable std::mutex          ev_mtx_;
    std::vector<SubtitleEvent>  events_;
    std::atomic<bool>           bitmap_warned_{ false };

    // Append non-overlapping event range; trims older events past a cap.
    void store_event(SubtitleEvent ev);
};
