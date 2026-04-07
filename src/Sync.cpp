#include "Sync.h"

#include <cmath>
#include <algorithm>

// Sync thresholds (seconds)
static constexpr double kSyncThresholdMin = 0.04;   // 40ms — don't correct tiny diffs
static constexpr double kSyncThresholdMax = 0.10;   // 100ms — cap the correction window

double compute_video_delay(double frame_pts, double last_pts,
                            double last_delay, double master_clock)
{
    // Natural duration of this frame from its PTS delta
    double delay = frame_pts - last_pts;
    if (delay <= 0.0 || delay > 1.0)
        delay = last_delay;  // bad/missing PTS — reuse last known frame duration

    // How far ahead (+) or behind (−) the video clock is relative to audio
    double diff = frame_pts - master_clock;

    // Sync window scales with frame duration, clamped to [40ms, 100ms]
    double threshold = std::clamp(delay, kSyncThresholdMin, kSyncThresholdMax);

    // Ignore huge diffs — they're seek artifacts, not real drift
    if (!std::isnan(diff) && std::abs(diff) < 10.0) {
        if (diff <= -threshold) {
            delay = 0.0;          // video too far behind: show immediately
        } else if (diff >= threshold) {
            delay = 2.0 * delay;  // video ahead: hold this frame an extra duration
        }
        // Within threshold: keep natural delay (no correction needed)
    }

    return delay;
}
