#pragma once

// Compute how long to wait before displaying the next video frame.
// Returns a delay in seconds (0.0 = show immediately, >0 = wait).
//
// Based on the FFplay sync algorithm:
//   - If video is behind audio by more than the sync threshold → return 0 (catch up)
//   - If video is ahead of audio by more than the sync threshold → double the delay (slow down)
//   - Otherwise → return the natural frame duration
double compute_video_delay(double frame_pts,   // PTS of the frame we're about to show
                            double last_pts,    // PTS of the frame we just showed
                            double last_delay,  // delay we used for the last frame
                            double master_clock // current audio clock value
);
