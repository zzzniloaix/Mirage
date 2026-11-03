#pragma once

// Frame sizes and picture types of one video stream, for a frame size / GOP
// view (a bar per frame coloured by type, GOP boundaries, rolling bitrate,
// click to seek): every packet of the stream as the demuxer reads it, in
// read (decode) order, with nothing decoded.
//
// FrameScan::start() opens the input on its own thread — its own
// AVFormatContext, through ffopen::open_input, so a stop request aborts any
// blocking FFmpeg call — discards every other stream and reads to the end
// with av_read_frame. Each packet becomes one framescan::Frame.
//
// Picture types come from libavcodec's parser for the codec: each packet is
// parsed as one complete frame (PARSER_FLAG_COMPLETE_FRAMES) against a codec
// context filled from the stream's parameters and never opened (it carries
// the extradata, so length-prefixed avcC / hvcC packets parse). What the
// parsers give (FFmpeg 8.1, checked on generated clips and local media):
//  * H.264, HEVC, MPEG-2, MPEG-4 Part 2: I / P / B, the frame's first slice
//    (picture) type — an I-frame need not be a keyframe (a non-IDR I without
//    a recovery point), so `type` and `key` are separate.
//  * VP8, VP9, AV1: I for key frames (and AV1 intra-only frames), P for all
//    others: these formats have no B-frames. A VP9 superframe (a hidden
//    alt-ref frame packed with the next shown frame) is one packet, typed by
//    its first frame. An AV1 temporal unit holds exactly one shown frame: the
//    hidden frames (show_frame = 0: alt-refs, pyramid levels) travel in the
//    unit of the next shown frame and swell its size, and showing one of them
//    later (show_existing_frame) is a unit of its own of a few bytes; all P.
//  * ProRes, FFV1: I. MJPEG and codecs without a parser (raw video): '?' —
//    StreamInfo::intra_only says when every frame is intra-coded all the same.
// The keyframe flag is the demuxer's (the container's sync-sample flag) or
// the parser's (IDR / IRAP picture, recovery point SEI, key frame header) —
// what libavformat itself reports for a stream it parses, so the same
// bitstream gives the same flags in any container.
//
// Thread-safe: start() and stop() from the owner's thread; everything else
// from any thread (the render thread polls version() and copies what is new).

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <limits>
#include <mutex>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace framescan {

inline constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// One packet of the stream: one frame (an AV1 temporal unit, a VP9
// superframe). 40 bytes: a two-hour 60 fps stream lists 432k of them (17 MB).
struct Frame {
    double        pts      = kNaN;    // seconds on the stream's timeline (as playback seeks); NaN = none
    double        dts      = kNaN;    // seconds; NaN = none (Matroska's first frames with B-frames)
    double        duration = 0.0;     // seconds; 0 = unknown
    std::int64_t  pos      = -1;      // byte position as the demuxer reports it (-1 = unknown;
                                      //   HLS: inside its segment)
    std::uint32_t size     = 0;       // bytes
    char          type     = '?';     // 'I', 'P', 'B' (also VC-1 BI), 'S' (SI / SP, MPEG-4 S-VOP,
                                      //   AV1 switch frame), '?' = unknown
    bool          key      = false;   // keyframe: a random access point
    bool          discard  = false;   // decoded but never shown: before an MP4 edit list's start
                                      //   (a file cut inside a GOP), its pts before StreamInfo::start
    std::int8_t   ref      = -1;      // H.264: 1 = a reference picture (nal_ref_idc > 0), 0 = not;
                                      //   -1 = unknown (other codecs, no slice found)
};
static_assert(sizeof(Frame) == 40, "a scan may list millions of frames");

// ── Pure helpers (no FFmpeg; unit-tested in tests/test_frame_scan.cpp) ───────

// A copy of `frames` (read order) in display order: by pts, ties in read
// order. A frame without a pts stays right behind the frame read before it
// (raw H.264 has no timestamps at all: read order is kept).
std::vector<Frame> display_order(std::span<const Frame> frames);

// Per frame, in the order given (read or display): the bits of the frames
// shown in the `window` seconds ending at it — time in (t - window, t], up to
// and including it — over `window`, in bits per second. A frame's time is its
// pts, else its dts; NaN for a frame with neither (it counts nowhere). The
// first frames' windows reach back before the stream, so values ramp up over
// the first `window` seconds. window <= 0: all NaN.
std::vector<double> rolling_bitrate(std::span<const Frame> frames, double window);

// The highest rolling_bitrate(frames, window): the peak over every window of
// that length (a window's sum only grows when a frame enters it, so the
// maximum is at one ending on a frame). 0 when there is none.
double peak_bitrate(std::span<const Frame> frames, double window = 1.0);

struct GopSummary {
    std::size_t   frames = 0;
    std::size_t   i = 0, p = 0, b = 0, s = 0, unknown = 0;   // by type
    std::size_t   keyframes = 0;
    // Keyframe to keyframe, in display_order(): `intervals` of them (one less
    // than the keyframes), in frames; all 0 when there is none. In seconds
    // pts to pts, else dts to dts, over the intervals that have either at
    // both ends (0 when none has).
    std::size_t   intervals = 0;
    int           min_frames = 0, max_frames = 0;
    double        avg_frames = 0.0;
    double        min_s = 0.0, avg_s = 0.0, max_s = 0.0;
    std::uint64_t bytes    = 0;
    // First to last frame time (pts, else dts) plus the last one's duration
    // (unknown: the average frame interval).
    double        seconds  = 0.0;
    double        mean_bps = 0.0;   // bytes * 8 / seconds (0 when seconds is 0)
    double        peak_bps = 0.0;   // peak_bitrate(frames, peak_window)
};

// Frames in any order (sorted internally where it matters).
GopSummary summarize(std::span<const Frame> frames, double peak_window = 1.0);

// nal_ref_idc of the first slice NAL unit (type 1 or 5) of an H.264 access
// unit: length-prefixed NAL units when nal_length_size is 1..4 (avcC — and
// Annex B after all when the lengths don't add up), else Annex B start
// codes. -1 when no slice is found.
int h264_ref_idc(const std::uint8_t* data, std::size_t size, int nal_length_size);

} // namespace framescan

// The background scan: start() one input's stream, poll, copy what is new.
//
// Network inputs are read like local files — every byte of the stream:
// scanning an HLS variant downloads all of its segments (the master copy
// `master` as for ffopen::open_input, so stream indices match playback's),
// and a live input reads on until stop(). Progress is by byte position
// against the file size where both are known (not HLS / DASH), else by pts
// against the duration (0 while that is unknown: live).
class FrameScan {
public:
    enum class State : std::uint8_t {
        Idle,       // never started
        Running,    // opening, or reading
        Done,       // read to the end of the media: every packet is listed
        Failed,     // could not open / not a video stream / read error (error() says which);
                    //   what was read before a read error stays listed
        Stopped,    // stop() before the end
    };
    // The stream, once the input is open.
    struct StreamInfo {
        int          stream     = -1;
        int          codec_id   = 0;        // AVCodecID
        std::string  codec;                 // its name ("h264")
        bool         parser     = false;    // libavcodec has a parser for it (see the top)
        bool         intra_only = false;    // every frame is intra-coded (codec descriptor)
        double       frame_rate = 0.0;      // frames per second (av_guess_frame_rate); 0 = unknown
        double       start      = framescan::kNaN;   // first pts (seconds)
        double       duration   = 0.0;      // seconds; 0 = unknown
        std::int64_t file_size  = -1;       // bytes progress is measured against; -1 = by time
    };

    FrameScan() = default;
    ~FrameScan();   // stop()
    FrameScan(const FrameScan&)            = delete;
    FrameScan& operator=(const FrameScan&) = delete;

    // Scan stream `stream` of `url`: the index playback uses for it (opened
    // the same way; `master`: as for ffopen::open_input). It must be a video
    // stream. Stops a scan still running and drops its records first (a copy
    // made of them is stale: copy again from 0). Returns at once; opening
    // happens on the scan thread, a failure shows in state() / error().
    void start(const std::string& url, int stream, const std::string& master = {});

    // Ends the scan and joins its thread — promptly, even mid-file or while
    // a network read blocks. The records read so far stay. No-op when idle.
    void stop();

    // Bumped whenever records are appended (in batches) and with every
    // change of state() / info(). Lock-free.
    std::uint64_t version() const { return version_.load(); }
    State state() const { return state_.load(); }
    // No more records will come: Done, Failed or Stopped.
    bool  done() const;
    // Why it Failed (empty otherwise).
    std::string error() const;
    // 0..1; 1 once Done.
    double progress() const { return progress_.load(); }
    std::optional<StreamInfo> info() const;

    // Records listed so far. Lock-free.
    std::size_t count() const { return count_.load(); }
    // Appends the records from index `from` on (read order) to `out` and
    // returns count() — incremental polling: copy(mine, mine.size()).
    std::size_t copy(std::vector<framescan::Frame>& out, std::size_t from = 0) const;

    // Records appended per batch, at most (a batch also goes out after
    // kFlushMs with fewer, so a slow network read shows progress).
    static constexpr std::size_t kBatch   = 1024;
    static constexpr int         kFlushMs = 50;

private:
    void run(std::stop_token st, std::string url, int stream, std::string master);
    // Lists `batch` (and clears it); `progress`: where reading has got to.
    void append(std::vector<framescan::Frame>& batch, double progress);
    void finish(State s, std::string why = {});

    mutable std::mutex              mtx_;
    std::deque<framescan::Frame>    frames_;   // never relocated: appending costs no copy of the rest
    std::string                     error_;
    std::optional<StreamInfo>       info_;

    std::atomic<std::uint64_t> version_{ 0 };
    std::atomic<std::size_t>   count_{ 0 };
    std::atomic<State>         state_{ State::Idle };
    std::atomic<double>        progress_{ 0.0 };

    std::jthread thread_;
};
