#pragma once

// External subtitle file (.srt/.vtt/.ass/.ssa) decoded once, in full, into a
// persistent SubtitleDecoder store. Lookups are by media time and are not
// affected by seeks, subtitle-track switches or demuxer EOF — the store
// never flushes.

#include "SubtitleDecoder.h"

#include <atomic>
#include <memory>
#include <stop_token>
#include <string>
#include <thread>

class SidecarSubtitles {
public:
    SidecarSubtitles() = default;
    ~SidecarSubtitles();

    SidecarSubtitles(const SidecarSubtitles&) = delete;
    SidecarSubtitles& operator=(const SidecarSubtitles&) = delete;

    // Start loading `path` on a short-lived background thread. Cue times are
    // file-relative; video_start_s (the video stream's start_time in seconds)
    // maps them onto the stream PTS timeline used by at().
    void load(const std::string& path, double video_start_s);

    // Text active at stream PTS `pts`, or empty (also before ready()).
    [[nodiscard]] std::string at(double pts) const;

    [[nodiscard]] bool ready()  const { return ready_.load(); }
    [[nodiscard]] bool failed() const { return failed_.load(); }
    [[nodiscard]] const std::string& path() const { return path_; }
    [[nodiscard]] std::size_t event_count() const { return decoder_.event_count(); }

    // Synchronous load of every cue in `path` into `dec` (set persistent
    // first). Returns false if the file can't be opened / has no subtitle
    // stream / no decoder. Exposed for tests.
    static bool decode_file(const std::string& path, SubtitleDecoder& dec,
                            const std::stop_token& st = {});

private:
    std::string       path_;
    double            video_start_s_ = 0.0;
    SubtitleDecoder   decoder_;
    std::atomic<bool> ready_{ false };
    std::atomic<bool> failed_{ false };
    std::jthread      worker_;   // last member: joined before the rest is destroyed
};
