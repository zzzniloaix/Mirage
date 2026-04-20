#include "NetworkLogger.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cctype>

extern "C" {
#include <libavutil/log.h>
}

NetworkLogger* NetworkLogger::instance_ = nullptr;

// ── Public API ────────────────────────────────────────────────────────────────

void NetworkLogger::install()
{
    instance_  = this;
    av_log_set_callback(av_log_cb);
    installed_ = true;
}

void NetworkLogger::uninstall()
{
    if (installed_) {
        av_log_set_callback(av_log_default_callback);
        instance_  = nullptr;
        installed_ = false;
    }
}

NetworkLogger::~NetworkLogger()
{
    uninstall();
}

std::vector<NetworkLogger::Entry> NetworkLogger::get_recent(int n) const
{
    std::lock_guard<std::mutex> lock(mtx_);
    int start = std::max(0, (int)entries_.size() - n);
    return { entries_.begin() + start, entries_.end() };
}

int NetworkLogger::total_count() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return static_cast<int>(entries_.size());
}

// ── av_log callback ───────────────────────────────────────────────────────────

void NetworkLogger::av_log_cb(void* avcl, int level, const char* fmt, va_list vl)
{
    // Always call the default handler (stderr output).
    // We need a separate copy of vl since default_callback will advance it.
    va_list vl_default;
    va_copy(vl_default, vl);
    av_log_default_callback(avcl, level, fmt, vl_default);
    va_end(vl_default);

    // Only inspect INFO/VERBOSE level where network open lines appear.
    if (level > AV_LOG_VERBOSE || !instance_) return;

    char buf[512];
    vsnprintf(buf, sizeof(buf), fmt, vl);
    instance_->ingest_line(buf);
}

void NetworkLogger::ingest_line(const char* line)
{
    // Match: Opening 'URL' for reading
    static constexpr const char* kPrefix = "Opening '";
    static constexpr const char* kSuffix = "' for reading";

    const char* p = strstr(line, kPrefix);
    if (!p) return;
    p += strlen(kPrefix);

    const char* q = strstr(p, kSuffix);
    if (!q) return;

    std::string url(p, q);
    if (url.find("http") != 0) return;  // only HTTP/HTTPS

    // Wall-clock timestamp
    auto now    = std::chrono::system_clock::now();
    auto t      = std::chrono::system_clock::to_time_t(now);
    int  ms_val = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count() % 1000);
    struct tm tm_info{};
#ifdef _WIN32
    localtime_s(&tm_info, &t);
#else
    localtime_r(&t, &tm_info);
#endif
    char tsbuf[20];
    snprintf(tsbuf, sizeof(tsbuf), "%02d:%02d:%02d.%03d",
             tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec, ms_val);

    // Classify and colour
    float r, g, b;
    std::string type = classify(url, r, g, b);

    // Truncate URL for display (keep tail — most informative part)
    std::string short_url = url;
    constexpr int kMaxUrlDisplay = 60;
    if ((int)short_url.size() > kMaxUrlDisplay)
        short_url = "..." + short_url.substr(short_url.size() - (kMaxUrlDisplay - 3));

    std::lock_guard<std::mutex> lock(mtx_);
    entries_.push_back({ tsbuf, type, short_url, r, g, b });
    if ((int)entries_.size() > kMaxEntries)
        entries_.pop_front();
}

// ── URL classification ────────────────────────────────────────────────────────

std::string NetworkLogger::classify(const std::string& url, float& r, float& g, float& b)
{
    // Extract just the path component (after host)
    std::string path;
    {
        auto slash = url.find('/', url.find("//") + 2);
        path = (slash != std::string::npos) ? url.substr(slash) : url;
    }

    // Get lower-case file extension
    std::string ext;
    {
        auto dot = path.rfind('.');
        if (dot != std::string::npos) {
            ext = path.substr(dot);
            for (auto& c : ext) c = static_cast<char>(std::tolower(c));
        }
    }

    auto path_has = [&](const char* needle) {
        return path.find(needle) != std::string::npos;
    };

    if (ext == ".mpd") {
        r = 0.35f; g = 0.90f; b = 0.35f;  // bright green — DASH manifest
        return "manifest";
    }
    if (ext == ".m3u8") {
        bool is_master = path_has("master") || url.find("master") != std::string::npos
                      || path_has("index") || path_has("playlist");
        if (is_master) {
            r = 0.35f; g = 0.90f; b = 0.35f;
            return "manifest";
        }
        r = 0.40f; g = 0.70f; b = 0.40f;  // dimmer green — variant playlist
        return "variant";
    }
    if (ext == ".mp4" && (path_has("init") || path_has("header") || path_has("Init"))) {
        r = 0.90f; g = 0.70f; b = 0.20f;  // orange — init segment
        return "init";
    }
    if (ext == ".m4s" || ext == ".ts" || ext == ".aac" || ext == ".m4a" ||
        (ext == ".mp4" && (path_has("seg") || path_has("frag") || path_has("chunk")))) {
        r = 0.50f; g = 0.55f; b = 0.90f;  // blue-purple — media segment
        return "segment";
    }
    if (ext == ".mp4" || ext == ".webm" || ext == ".mkv" || ext == ".mov") {
        r = 0.70f; g = 0.70f; b = 0.90f;  // light blue — plain media file
        return "media";
    }

    r = 0.60f; g = 0.60f; b = 0.60f;  // gray — other
    return "other";
}
