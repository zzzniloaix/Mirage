#pragma once

// Pure formatting / layout helpers for the player UI. No ImGui or GL here so
// they can be unit-tested in mirage_tests.

#include <string>
#include <vector>

namespace ui {

// Clock text: "MM:SS.mmm", or "H:MM:SS.mmm" when with_hours. Negative → 0.
std::string format_clock(double t, bool with_hours);

// Seek-bar timecode in one of three click-cycled modes:
//   0  "MM:SS.mmm / MM:SS.mmm"  (H:MM:SS.mmm when dur >= 3600)
//   1  "f 1234 / 98765"         frame = llround(t * fps); needs fps > 0,
//                               otherwise falls back to mode 0
//   2  "-MM:SS.mmm / MM:SS.mmm" remaining / duration
// Live or unknown duration (dur <= 0) always yields "LIVE 00:12.345".
std::string format_timecode(double t, double dur, int mode, double fps);

// Keyframe tick x-positions (px from the track's left edge, in [0, width])
// such that no two kept ticks are closer than min_px. Greedy left-to-right;
// the first and last keyframe are always kept (the last replaces the
// previously kept tick when the two collide). kf must be sorted ascending.
std::vector<float> decimate_ticks(const std::vector<double>& kf, double dur,
                                  float width, float min_px);

// Index of the last element <= t in the ascending `pts`, or -1 when there is
// none or when t - pts[idx] > max_gap.
int index_at_or_before(const std::vector<double>& pts, double t, double max_gap);

} // namespace ui
