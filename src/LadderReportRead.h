#pragma once

// A ladder report read back for the timeline (DECISIONS.md D-081 (b)): filled in by WP03.

extern "C" {
#include <libavutil/rational.h>
}

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace timeline {

// A frame of a pair's inputs that the aligner didn't pair (the report's
// alignment.failures; src/VMAFAlign.h).
struct Failure {
    std::string  input;                       // "reference" or "distorted"
    std::int64_t index = 0;                   // its position in its input, from 0
    std::optional<std::int64_t> pts;          // ticks of its input's time base; nullopt: none
    std::string  reason;                      // as the report names it: "unmatched_reference", ...
};

// One discontinuity sequence of a pair's input, as the report gives it in
// alignment.reference.sequences and alignment.distorted.sequences (D-046,
// D-057).
struct Sequence {
    std::int64_t first_index = 0;             // its first frame's position in its input
    std::optional<std::int64_t> first_pts;    // that frame's PTS, in ticks; nullopt: none
    std::int64_t frames = 0;                  // its frames the analysis read
};

// One complete pair of a ladder report: its inputs' timing, and each scored
// frame with the positions and PTS of its two frames. PTS are ticks of their
// input's time base, from the report's seconds.
struct PairScores {
    std::size_t index = 0;                        // the plan's, as pairs[].index gives it
    std::vector<std::size_t> entries;             // the video entries naming its distorted playlist
    std::string ref_url, dis_url;                 // ref.url, dis.url
    AVRational  ref_time_base = { 0, 1 };         // ref.time_base
    AVRational  dis_time_base = { 0, 1 };         // dis.time_base
    AVRational  ref_rate = { 0, 1 };              // alignment.reference.frame_rate; 0/1: unknown
    AVRational  dis_rate = { 0, 1 };              // alignment.distorted.frame_rate; 0/1: unknown
    std::vector<std::int64_t> ref_index, dis_index;   // per scored frame: positions in the inputs
    std::vector<std::int64_t> ref_pts, dis_pts;       // per scored frame: ticks
    std::vector<double>       vmaf;                   // per scored frame
    std::vector<std::int64_t> context_first;          // per libvmaf context, its first scored
                                                      // frame's index (alignment.vmaf_contexts)
    std::vector<Failure>      failures;               // in the report's order
    std::vector<Sequence>     ref_sequences, dis_sequences;
};

// One EXT-X-STREAM-INF entry of the ladder's master, as the report lists it
// (ladder.entries).
struct MasterEntry {
    std::size_t  index = 0;                       // among the master's entries, from 0
    std::size_t  line  = 0;                       // of its EXT-X-STREAM-INF, from 1
    std::string  uri;                             // its URI line, as the master gives it
    std::string  url;                             // resolved
    std::int64_t bandwidth = 0;                   // BANDWIDTH
    std::optional<std::string> average_bandwidth, resolution, codecs,
                               frame_rate;        // as the master gives them; nullopt: absent
    bool         video = false;
};

// A ladder report as the timeline reads it: what it was scored with, the
// master, the reference and the complete pairs. A timeline document names
// the report by two hashes and copies its provenance, not its scores (D-072).
struct LadderScores {
    std::string status;                           // "complete", "cancelled" or "failed"
    std::string reference_mode;                   // "top-rung" or "mezzanine"
    std::string model, model_source, model_sha256;   // model_sha256: empty for a built-in model
    std::string scaler;
    std::string libvmaf_version, libvmaf_package_version;
    std::string master_url, master_location, master_sha256;   // ladder.url, .location, .sha256
    std::vector<MasterEntry> entries;             // ladder.entries
    std::string reference_url;                    // reference.url: the top rung, or the mezzanine
    std::vector<std::size_t> reference_entries;   // reference.entries
    std::vector<PairScores> pairs;                // the complete pairs, in the plan's order
};

} // namespace timeline
