#pragma once

#include <string>
#include <string_view>
#include <optional>
#include <vector>
#include <mutex>
#include <thread>
#include <stop_token>
#include <atomic>

struct VariantStream;

// The swscale algorithms the distorted frames can be scaled to the
// reference's size with (work order section 1.3). Bilinear is what the
// analyzer uses unless told otherwise; the default is the owner's to decide
// (DECISIONS.md D-003).
enum class VMAFScaler { Bilinear, Bicubic, Lanczos };

// The name of `s` as FFmpeg's scale filter takes it in flags=, e.g. "bicubic".
constexpr std::string_view vmaf_scaler_name(VMAFScaler s)
{
    switch (s) {
    case VMAFScaler::Bilinear: return "bilinear";
    case VMAFScaler::Bicubic:  return "bicubic";
    case VMAFScaler::Lanczos:  return "lanczos";
    }
    return "";
}

// The scaler vmaf_scaler_name() calls `name`, if there is one.
constexpr std::optional<VMAFScaler> vmaf_scaler_named(std::string_view name)
{
    for (VMAFScaler s : { VMAFScaler::Bilinear, VMAFScaler::Bicubic, VMAFScaler::Lanczos })
        if (vmaf_scaler_name(s) == name) return s;
    return std::nullopt;
}

// The libvmaf model the pairs are scored with (work order section 1.1): one
// of the linked libvmaf's built-in models, by version, or a model file.
// vmaf_v0.6.1 is what the analyzer uses unless told otherwise; the default
// is the owner's to decide (DECISIONS.md D-004).
struct VMAFModelChoice {
    std::string version = "vmaf_v0.6.1";   // built-in model; used when `path` is empty
    std::string path;                      // a libvmaf model file (JSON) instead
};

// The scoring settings an analysis used. Every result records them so that
// a report can state what produced its scores.
struct VMAFSettings {
    std::string model;            // the built-in model's version, or the model file's path as given
    std::string model_source;     // "built-in" or "file"
    std::string model_sha256;     // SHA-256 of the model file, lowercase hex; empty for a built-in model
    std::string scaler;           // swscale algorithm that scales the distorted frames to the reference size
    std::string pix_fmt;          // format both inputs are converted to before scoring
    int         bit_depth   = 0;  // bits per sample handed to libvmaf
    int         n_threads   = 0;  // libvmaf worker threads
    int         n_subsample = 0;  // libvmaf scores every n-th frame
};

// One input of a scored pair as the analyzer's decoder saw it. The stream
// fields are the decoder's right after opening, under FFmpeg's names
// (av_get_pix_fmt_name, av_color_space_name, ...; "unknown" when unnamed).
struct VMAFInputInfo {
    int         width  = 0;
    int         height = 0;
    std::string pix_fmt;
    std::string color_space;
    std::string color_range;
    std::string color_primaries;
    std::string color_trc;
    int         time_base_num = 0;   // stream time base the frame timestamps are in
    int         time_base_den = 0;
    bool        ended = false;       // decoding reached the end of the input
    std::string end_error;           // FFmpeg error that ended it; empty = clean end or not ended
};

// How the distorted frames' color was converted before scoring, as FFmpeg's
// filter graph converts it (DECISIONS.md D-016, src/VMAFColor.h): the frames'
// matrix and range tags, the tags they were converted to, and whether that
// changed the matrix or the range of the pixels. One entry per distinct
// conversion, in the order the pairs first used it. Tags under FFmpeg's names.
struct VMAFColorConversion {
    std::string from_space;
    std::string from_range;
    std::string to_space;
    std::string to_range;
    bool        matrix_changed = false;
    bool        range_changed  = false;
    unsigned    first_index    = 0;   // first frame pair converted this way
    unsigned    pairs          = 0;   // frame pairs converted this way
};

// Settings, limits and extra recording for one pair. Default-constructed it
// is what start() and start_manifest() use.
struct VMAFOptions {
    unsigned        max_frames = 0;       // stop after this many frame pairs; 0 = until an input ends
    bool            record_pts = false;   // fill VMAFResult::ref_pts / dis_pts
    VMAFScaler      scaler     = VMAFScaler::Bilinear;   // scales the distorted frames
    VMAFModelChoice model;
};

// Per-variant (or single-file) VMAF result.
struct VMAFResult {
    std::string  ref_url;
    std::string  dis_url;
    std::string  label;          // human-readable: "720p  2.0 Mbps" or "Analysis"
    int          width     = 0;
    int          height    = 0;
    int64_t      bandwidth = 0;  // bits/sec from manifest; 0 for single-file mode
    double       vmaf_mean = -1.0;
    double       vmaf_min  = -1.0;
    double       vmaf_p5   = -1.0;   // 5th-percentile score
    std::vector<double> per_frame;   // per-frame scores; empty until done
    bool         done  = false;
    std::string  error;              // non-empty on failure

    VMAFSettings  settings;
    VMAFInputInfo ref_info;
    VMAFInputInfo dis_info;
    std::string   score_error;       // first libvmaf call that failed while scoring; empty = none
    std::vector<VMAFColorConversion> dis_conversions;
    // What can make these scores differ from those of FFmpeg's section 5
    // graph although the analysis succeeded, such as a reference that isn't
    // yuv420p (vmafcolor::converts_reference). Empty = nothing known.
    std::vector<std::string> warnings;
    // With VMAFOptions::record_pts: the best-effort PTS (seconds) of the
    // reference and distorted frame scored at each index of per_frame.
    // NaN = the frame had no timestamp.
    std::vector<double> ref_pts;
    std::vector<double> dis_pts;
};

// Offline VMAF analyzer.
//
// Runs decode + libvmaf scoring in a background jthread.
// For single-file mode: start(ref, dis)
// For manifest mode:    start_manifest(variants) — highest-bandwidth is reference.
// Headless (no thread): analyze(ref, dis, opts)
//
// Thread-safe: results() may be called at any time from any thread.
// JSON report is written via write_json() only when all pairs are done.
class VMAFAnalyzer {
public:
    VMAFAnalyzer()  = default;
    ~VMAFAnalyzer() { cancel(); }

    VMAFAnalyzer(const VMAFAnalyzer&)            = delete;
    VMAFAnalyzer& operator=(const VMAFAnalyzer&) = delete;

    // Start analysis of one ref+dis pair.
    void start(std::string ref_url, std::string dis_url, int64_t bandwidth_hint = 0);

    // Start analysis of HLS manifest variants vs the highest-bandwidth one.
    void start_manifest(const std::vector<VariantStream>& variants);

    // Score one pair on the calling thread and return its result: the same
    // decoding and scoring as start(), plus what `opts` asks for.
    static VMAFResult analyze(std::string ref_url, std::string dis_url,
                              const VMAFOptions& opts = {});

    // Request cancellation and block until the worker exits.
    void cancel();

    bool  running()  const { return running_.load(); }
    float progress() const { return progress_.load(); }

    // Thread-safe snapshot of all results accumulated so far.
    std::vector<VMAFResult> results() const;

    // Write a JSON report to path.  Returns true on success.
    // Only call after all results are done (running() == false).
    bool write_json(const std::string& path) const;

private:
    std::jthread            worker_;
    std::atomic<bool>       running_{ false };
    std::atomic<float>      progress_{ 0.0f };
    mutable std::mutex      mtx_;
    std::vector<VMAFResult> results_;

    // Decode and score one pair; updates result in-place.
    // progress_total is updated from base to base+span during the analysis.
    static void analyze_pair(VMAFResult& result, std::stop_token st,
                             std::atomic<float>& progress_total,
                             float base, float span, const VMAFOptions& opts);
};
