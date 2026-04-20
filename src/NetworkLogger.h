#pragma once

#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <cstdarg>

// Hooks FFmpeg's av_log to capture every HTTP URL that FFmpeg opens.
// Parses lines of the form:  Opening 'https://...' for reading
// Classifies each URL as manifest / variant / init / segment / other.
//
// Usage:
//   NetworkLogger net_log;
//   net_log.install();         // before avformat_open_input
//   ...
//   auto entries = net_log.get_recent(20);
class NetworkLogger {
public:
    NetworkLogger() = default;
    ~NetworkLogger();
    NetworkLogger(const NetworkLogger&) = delete;
    NetworkLogger& operator=(const NetworkLogger&) = delete;

    // Install as the global av_log callback.
    // The default callback (av_log_default_callback) is still called for all log lines.
    void install();
    void uninstall();

    struct Entry {
        std::string time;   // "HH:MM:SS.mmm"
        std::string type;   // "manifest" | "variant" | "init" | "segment" | "other"
        std::string url;    // truncated to last 60 chars if longer
        float r, g, b;      // colour for display
    };

    // Return the most recent (up to n) entries, oldest-first.
    std::vector<Entry> get_recent(int n) const;

    int total_count() const;

    // Classify an HTTP URL into a type string and assign a display colour.
    // Public so it can be unit-tested directly.
    static std::string classify(const std::string& url, float& r, float& g, float& b);

    // Ingest a pre-formatted log line as if it came from av_log.
    // Public so unit tests can drive it without installing a real av_log hook.
    void ingest_line(const char* line);

private:
    static void av_log_cb(void* avcl, int level, const char* fmt, va_list vl);

    bool installed_ = false;
    mutable std::mutex mtx_;
    std::deque<Entry>  entries_;

    static constexpr int kMaxEntries = 200;
    static NetworkLogger* instance_;
};
