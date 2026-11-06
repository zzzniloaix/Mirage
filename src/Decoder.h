#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
}

class Decoder {
public:
    Decoder() = default;
    ~Decoder();

    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;

    // try_hw=true attempts VideoToolbox HW decode on macOS; silently falls back
    // to software if the codec/OS doesn't support it. When HW is active, pull()
    // returns AVFrames with format=AV_PIX_FMT_VIDEOTOOLBOX and data[3] pointing
    // at a CVPixelBufferRef — the caller is responsible for zero-copy upload
    // via CGLTexImageIOSurface2D. Frames that fell back to SW inside the codec
    // arrive as regular yuv420p/nv12/etc.
    // False when no decoder is found or it fails to open: no codec context is
    // kept, and push(), pull() and flush() do nothing until an open() succeeds.
    [[nodiscard]] bool open(AVCodecParameters* par, bool try_hw = false);
    void close();               // free context so open() can be called again
    // avcodec_send_packet; returns its result. Errors are counted (errors()),
    // except EAGAIN and the EOF of a packet sent after the drain (null) one.
    // With frame threads an error can be an earlier packet's.
    int  push(AVPacket* pkt);
    // avcodec_receive_frame; true = got frame. An error is counted and the
    // codec asked again, up to kMaxErrorsPerPull times, as FFmpeg's command
    // line does: the drain at the end then goes on past a frame that failed
    // in one frame thread to the frames of the others.
    bool pull(AVFrame* frame);
    void flush();               // avcodec_flush_buffers (call after seek)
    // H.264 / HEVC: don't decode the frames nothing refers to (B-frames,
    // mostly) — for packets whose frames are dropped unseen anyway (before a
    // seek's / switch's target). Other codecs: no-op. A reopen clears it.
    void skip_unreferenced(bool on);

    // More errors than a frame-threaded decoder holds frames: only a codec
    // that keeps failing without taking input reaches it.
    static constexpr int kMaxErrorsPerPull = 64;

    int                            width()                const;
    int                            height()               const;
    AVPixelFormat                  pixel_format()         const;
    AVColorSpace                   colorspace()           const;
    AVColorRange                   color_range()          const;
    AVColorPrimaries               color_primaries()      const;
    AVColorTransferCharacteristic  color_transfer()       const;
    AVRational                     sample_aspect_ratio()  const;  // {0,1} means "unspecified / square"

    // HW decode status (populated after the first pull() call — before then,
    // is_hw_active() reflects only whether we *asked* for HW).
    [[nodiscard]] bool        is_hw_active()  const { return hw_active_; }
    [[nodiscard]] const char* hw_type_name()  const;   // "VideoToolbox" or "Software"
    // The last open() succeeded. Any thread.
    [[nodiscard]] bool        is_open()       const { return open_.load(std::memory_order_relaxed); }

    // What went wrong in decoding, counted on the decode thread, read on any
    // thread. Kept across open() / close(): a track switch adds to them.
    struct Errors {
        std::int64_t errors     = 0;   // push() / pull() errors, as counted there
        int          last       = 0;   // the latest of them (AVERROR), 0 = none
        std::int64_t corrupt    = 0;   // frames with decode_error_flags or AV_FRAME_FLAG_CORRUPT
        std::int64_t hw_run     = 0;   // errors in a row under VideoToolbox, now (0 once a frame decodes)
        std::int64_t hw_run_max = 0;   // the longest such run
    };
    [[nodiscard]] Errors errors() const;

private:
    AVCodecContext* ctx_            = nullptr;
    AVBufferRef*    hw_device_ctx_  = nullptr;
    bool            hw_requested_   = false;
    std::atomic<bool> hw_active_    { false };   // set true after first HW frame (decode thread; read on main)
    std::atomic<bool> open_         { false };
    // get_format picked VideoToolbox: the errors from then on are its own
    // (it can also fall back to software, when the hwaccel fails to start).
    bool            hw_chosen_      = false;
    bool            draining_       = false;     // the null packet was sent; until flush() / open()
    std::string     name_;                       // the codec's, for the log

    std::atomic<std::int64_t> errors_     { 0 };
    std::atomic<int>          last_error_ { 0 };
    std::atomic<std::int64_t> corrupt_    { 0 };
    std::atomic<std::int64_t> hw_run_     { 0 };
    std::atomic<std::int64_t> hw_run_max_ { 0 };

    // One log line per kErrorLogInterval at most, while there is something
    // new to say (FFmpeg's own messages, one per error, go to stderr).
    static constexpr std::chrono::seconds kErrorLogInterval{ 1 };
    std::int64_t logged_errors_  = 0;
    std::int64_t logged_corrupt_ = 0;
    std::chrono::steady_clock::time_point last_log_{};

    void note_error(int err);
    void log_errors(bool now);
    static AVPixelFormat get_hw_format(AVCodecContext* ctx, const AVPixelFormat* pix_fmts);
};

// av_strerror's text for an AVERROR code.
std::string av_error_text(int err);

// The counts in words, "" when there is nothing to report: "3 decode errors
// (last: Invalid data found when processing input), 12 corrupt frames"; with
// errors under VideoToolbox, the longest run of them and that --no-hw
// decodes in software.
std::string describe_decode_errors(const Decoder::Errors& e);
