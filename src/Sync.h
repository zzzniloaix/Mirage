#pragma once

// Compute how long (wall-clock seconds) to wait before displaying the next video frame.
// Returns 0.0 = show immediately, >0 = wait.
//
// Based on the FFplay sync algorithm:
//   - If video is behind audio by more than the sync threshold → return 0 (catch up)
//   - If video is ahead of audio by more than the sync threshold → add the lead to the delay
//     (hold the frame until audio catches up)
//   - Otherwise → return the natural frame duration, less a tenth of the
//     drift, so an offset inside the window doesn't stay for good
//
// PTS / clock inputs are media time; the result and the sync threshold are wall
// time (media / speed). At 4x a 60 fps frame lasts ~4 ms of wall time, so a
// media-time threshold would be crossed by ordinary clock jitter every frame.
double compute_video_delay(double frame_pts,      // PTS of the frame we're about to show
                            double last_pts,       // PTS of the frame we just showed
                            double last_frame_dur, // last known frame duration (media sec)
                            double master_clock,   // current audio clock value
                            double speed = 1.0     // playback speed (media sec per wall sec)
);

// Media duration of the frame at frame_pts: its PTS delta from last_pts, or
// fallback when the delta is missing/bogus (<= 0 or > 1 s — seek, discontinuity).
double frame_duration(double frame_pts, double last_pts, double fallback);
