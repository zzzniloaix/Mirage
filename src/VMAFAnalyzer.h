#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <stop_token>
#include <atomic>

struct VariantStream;

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
};

// Offline VMAF analyzer.
//
// Runs decode + libvmaf scoring in a background jthread.
// For single-file mode: start(ref, dis)
// For manifest mode:    start_manifest(variants) — highest-bandwidth is reference.
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
                             float base, float span);
};
