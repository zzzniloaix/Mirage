#pragma once

// Media keys and seconds on the reference's frames (DECISIONS.md D-073): filled in by WP01.

extern "C" {
#include <libavutil/rational.h>
}

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace timeline {

// A reference frame's media key: its discontinuity sequence in the ladder
// report (D-057) and its PTS. Together they name the frame where PTS alone
// repeat, as they do where an ad restarts its timestamps (D-073).
struct MediaKey {
    std::int32_t sequence = 0;            // the report's reference sequence, from 0
    std::optional<std::int64_t> pts;      // ticks of the reference's time base; nullopt: unknown
};

// What events are placed on: the reference's frames, compared as integers,
// and the seconds offsets are printed in (D-073). The seconds are the
// reference's playlist time where it has a segment map, the frame index
// times the frame duration where it has none; where the map exists the two
// are compared at every scored frame, and each frame's PTS with the frame
// grid of its playlist discontinuity sequence, each run of frames beyond
// half a frame a warning (eval/phase3-survey/design.md §4.2).
struct Axis {
    std::string  kind = "reference-frames";   // what events are placed and compared on
    std::string  clock;                       // the seconds: "playlist-time" or "frame-grid"
    AVRational   time_base      = { 0, 1 };   // the reference's: MediaKey::pts is in it
    AVRational   frame_duration = { 0, 1 };   // the reference's, in seconds
    std::int64_t frames = 0;                  // the reference's frames
    std::vector<MediaKey> keys;               // one per reference frame
    std::string  checked;                     // "segment-map" or "report-only" (design §4.2)
    std::vector<std::string> warnings;        // the check's runs of frames (design §4.2)
};

} // namespace timeline
