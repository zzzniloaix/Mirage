#pragma once

// Pure formatting / layout helpers for the player UI. No ImGui or GL here so
// they can be unit-tested in mirage_tests.

#include <string>
#include <vector>

namespace ui {

// ── Positions ────────────────────────────────────────────────────────────────
// The UI shows positions: seconds from the media's start, PTS − start, so the
// seek bar spans [0, duration] and the first frame is at 0. The rest of the
// player (demuxer, decoders, clocks, logs, the HUD's and the frame
// inspector's PTS rows) keeps the absolute PTS; these convert between them.

// Where positions count from: the video stream's start time, else the
// container's, else 0 (NaN: not known). Live or unknown duration
// (duration <= 0): 0, so the display stays the stream's own clock.
double timeline_start(double video_start, double container_start, double duration);

// Position of a PTS, and the PTS at a position. pts_at never returns less
// than 0: a seek target below 0 means "none".
double position(double pts, double start);
double pts_at(double pos, double start);

// Frame number counted from start, the first frame 0 (as the frame inspector
// numbers frames): llround((pts - start) * fps); 0 before start.
long long frame_number(double pts, double start, double fps);

// The seek bar spans PTS [start, start + dur]: the fraction of it at a PTS,
// clamped to [0, 1] (0 when dur <= 0), and the PTS at a fraction (0 → start,
// 1 → start + dur; the fraction clamped to [0, 1]).
double bar_fraction(double pts, double start, double dur);
double pts_at_fraction(double frac, double start, double dur);

// Clock text: "MM:SS.mmm", or "H:MM:SS.mmm" when with_hours. Negative → 0.
std::string format_clock(double t, bool with_hours);

// Seek-bar timecode of the frame at `pts`, in one of four click-cycled modes:
//   0  "MM:SS.mmm / MM:SS.mmm"  position / duration (H:MM:SS.mmm when dur >= 3600)
//   1  "f 1234 / 98765"         frame_number(pts, start, fps) / llround(dur * fps);
//                               needs fps > 0, otherwise falls back to mode 0
//   2  "-MM:SS.mmm / MM:SS.mmm" remaining / duration
//   3  "PTS 1234.567"           the absolute PTS in seconds
// Positions before start read 0. Live or unknown duration (dur <= 0) always
// yields "LIVE 00:12.345", the stream's own clock (pts).
std::string format_timecode(double pts, double start, double dur, int mode, double fps);

// Keyframe tick x-positions (px from the track's left edge, in [0, width]) for
// keyframe PTS on a bar spanning [start, start + dur], such that no two kept
// ticks are closer than min_px. Greedy left-to-right; the first and last
// keyframe are always kept (the last replaces the previously kept tick when
// the two collide). kf must be sorted ascending.
std::vector<float> decimate_ticks(const std::vector<double>& kf, double start, double dur,
                                  float width, float min_px);

// Index of the last element <= t in the ascending `pts`, or -1 when there is
// none or when t - pts[idx] > max_gap.
int index_at_or_before(const std::vector<double>& pts, double t, double max_gap);

} // namespace ui
