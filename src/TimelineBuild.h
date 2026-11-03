#pragma once

// The merge of every source's events into a Timeline: filled in by WP09a.

#include <cstdint>
#include <string>
#include <vector>

namespace timeline {

// One segment of a variant as the timeline build's demux pass read it
// (DECISIONS.md D-074): its frames, by their positions in the input, and
// its bytes.
struct SegmentFrames {
    std::int64_t seq         = 0;   // media sequence number
    std::int64_t first_index = 0;   // its first frame's position in the input, from 0
    std::int64_t frames      = 0;
    std::int64_t bytes       = 0;   // as read
};

// What the demux pass gives of one variant (D-074): its segments' frames,
// the frames of its key and corrupt packets, and every frame's PTS. The
// adapter src/TimelineMedia.h returns it; a test can build one by hand.
struct VariantMedia {
    std::vector<SegmentFrames> segments;     // in the playlist's order
    std::vector<std::int64_t>  keyframes;    // positions in the input of the frames of key packets
    std::vector<std::int64_t>  corrupt;      // the same for corrupt packets (AV_PKT_FLAG_CORRUPT)
    std::vector<std::int64_t>  pts;          // per frame, ticks of the input's time base
    std::string                refused;      // why it gets no segment map (D-081 (c)), or empty
};

} // namespace timeline
