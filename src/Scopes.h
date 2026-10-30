#pragma once

// Video scopes (S) from the code values of the frame on screen
// (PixelView.h): luma waveform, R′G′B′ parade, vectorscope and luma
// histogram, with legal-range statistics. compute() is pure; Worker runs it
// on a thread of its own, the newest submitted frame winning, so a 4K frame
// never holds up the render loop. The panel (ScopesPanel.h) uploads the
// images and draws the graticules.

#include "PixelView.h"

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace scopes {

enum class Mode : std::uint8_t { Waveform, Parade, Vectorscope, Histogram };
inline constexpr int kModes = 4;
[[nodiscard]] const char* mode_name(Mode m);

// Bins of the waveform and parade images: one row per 8-bit code value
// (deeper sources are binned by their top 8 bits), row 0 = code 255; at
// most kWaveCols / kParadeCols columns, fewer for a narrower frame (one per
// sampled column).
inline constexpr int kRows        = 256;
inline constexpr int kWaveCols    = 512;
inline constexpr int kParadeCols  = 256;   // per component
inline constexpr int kVectorSize  = 256;   // Cb × Cr, by their top 8 bits

struct Image {
    int w = 0, h = 0;
    std::vector<std::uint32_t> rgba;   // row 0 at the top; R in the lowest byte (IM_COL32 order)
};

struct Stats {
    int    depth   = 8;
    bool   limited = true;                 // legality counts apply
    int    y_min = 0, y_max = 0;           // code values
    double y_avg = 0.0;
    double below_black = 0.0, above_white = 0.0, chroma_out = 0.0;   // fractions of the samples
    int    codes_used = 0, codes_total = 0;   // distinct Y′ codes seen, of 2^depth
    // Deeper than 8 bits, but every Y′ code a multiple of 2^(depth−8): the
    // low bits carry nothing (an 8-bit source padded).
    bool          lsb_unused = false;
    std::uint64_t samples    = 0;
};

struct Params {
    Mode         mode  = Mode::Waveform;
    AVColorSpace cs    = AVCOL_SPC_BT709;     // matrix for the parade
    AVColorRange range = AVCOL_RANGE_MPEG;
};

struct Result {
    bool        ok = false;
    std::string error;                 // !ok: why there is nothing to show
    Mode        mode = Mode::Waveform;
    Image       image;                 // waveform, parade, vectorscope
    std::vector<float> histogram;      // Histogram mode: Y′ count per code, normalised to the largest
    Stats       stats;
    double        pts    = 0.0;        // of the frame measured
    std::uint64_t serial = 0;          // the submit() it answers
};

// Large frames are sampled on a grid (every 2nd pixel and row of 4K, ...)
// so a measure stays near 2 million samples.
[[nodiscard]] Result compute(const PixelView& v, const Params& p);

class Worker {
public:
    Worker();
    ~Worker();
    Worker(const Worker&)            = delete;
    Worker& operator=(const Worker&) = delete;

    // Render thread: measure `f` (a reference is taken) with p; replaces a
    // job that has not started yet.
    void submit(const AVFrame* f, const Params& p, std::uint64_t serial, double pts);
    // Render thread: the newest result, if one arrived since the last take.
    bool take(Result& out);

private:
    void run(std::stop_token st);

    std::mutex                  mtx_;
    std::condition_variable_any cv_;
    AVFrame*                    job_ = nullptr;   // pending (under mtx_)
    Params                      job_params_;
    std::uint64_t               job_serial_ = 0;
    double                      job_pts_    = 0.0;
    std::optional<Result>       done_;            // under mtx_
    std::jthread                thread_;          // last: joins before the rest goes
};

} // namespace scopes
