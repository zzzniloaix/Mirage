#include "Sync.h"

#include <cmath>
#include <algorithm>

// Sync thresholds (wall-clock seconds)
static constexpr double kSyncThresholdMin = 0.04;   // 40ms — don't correct tiny diffs
static constexpr double kSyncThresholdMax = 0.10;   // 100ms — cap the correction window

double frame_duration(double frame_pts, double last_pts, double fallback)
{
    double d = frame_pts - last_pts;
    return (d <= 0.0 || d > 1.0 || std::isnan(d)) ? fallback : d;
}

double compute_video_delay(double frame_pts, double last_pts,
                            double last_frame_dur, double master_clock, double speed)
{
    if (!(speed > 0.0)) speed = 1.0;

    // Natural duration of this frame from its PTS delta, in wall time
    double delay = frame_duration(frame_pts, last_pts, last_frame_dur) / speed;

    // How far ahead (+) or behind (−) the video clock is relative to audio (media sec)
    double diff = frame_pts - master_clock;

    // Sync window scales with frame duration, clamped to [40ms, 100ms] wall time
    double threshold = std::clamp(delay, kSyncThresholdMin, kSyncThresholdMax);

    // Ignore huge diffs — they're seek artifacts, not real drift
    if (!std::isnan(diff) && std::abs(diff) < 10.0) {
        double diff_wall = diff / speed;
        if (diff_wall <= -threshold) {
            delay = 0.0;          // video too far behind: show immediately
        } else if (diff_wall >= threshold) {
            // Video ahead: hold this frame until audio catches up. (FFplay only
            // does this for long frames and doubles short ones, but at 2–4x a
            // frame is a few ms, so doubling barely slows video while audio
            // still plays out buffer rendered at the previous speed.)
            delay = delay + diff_wall;
        }
        // Within threshold: keep natural delay (no correction needed)
    }

    return delay;
}
