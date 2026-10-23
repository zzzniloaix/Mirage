#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <stop_token>
#include <atomic>

struct VariantStream;

// The scoring settings an analysis used. They are fixed for now; every result
// records them so that a report can state what produced its scores.
struct VMAFSettings {
    std::string model;            // libvmaf built-in model version
    std::string scaler;           // swscale algorithm that brings both inputs to the reference size
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

// Limits and extra recording for one pair. Default-constructed it is what
// start() and start_manifest() use.
struct VMAFOptions {
    unsigned max_frames = 0;       // stop after this many frame pairs; 0 = until an input ends
    bool     record_pts = false;   // fill VMAFResult::ref_pts / dis_pts
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
