#pragma once

// A ladder report read back for the timeline (DECISIONS.md D-081 (b)), and
// the canonical text of its scores digest (D-072).
//
// What is read. A report that mirage_vmaf --ladder writes
// (vmafreport::ladder_to_json, src/VMAFReport.h) in the layout of 75cf6e0 or
// a later one: every key of that layout in each object the reader opens must
// be there, and keys a later layout adds are passed over. An older layout is
// refused naming the first key it lacks (the discontinuity sequences of
// D-057, or pool.baseline of D-058, the last key 75cf6e0's layout gained); so
// is a pair's report of --ref/--dis, the player's report, and text that
// isn't JSON (src/JsonRead.h). URLs and paths are kept as the report gives
// them: a relative one is taken from the current directory by whatever opens
// it, as mirage_vmaf takes its inputs.
//
// What is checked, beyond the layout: the kind of each value read; a ladder's
// reference mode, "top-rung" or "mezzanine"; each frame's index is its
// position, and frames_scored their number; a pair's reference and distorted
// positions both rise from frame to frame, as the aligner pairs them
// (src/VMAFAlign.h), and each is below the frames of its input's sequences
// in all, since the aligner counts every frame the analysis read of an input
// in one of them (src/VMAFAlign.cpp, Aligner::push); each libvmaf context
// starts at a frame of its pair, after the one before; pairs come in the
// plan's order; every entry named is one of the master's, whose entries are
// listed in its order; a failure's input is the reference or the distorted
// one; time bases are positive; a scored frame has both PTS, as only frames
// with a timestamp pair. A report that fails one is refused with the value
// named.
//
// PTS. The report gives seconds, which the analyzer computes as the ticks
// times the time base (src/VMAFAnalyzer.cpp, DecodeCtx::seconds); they are
// turned back into ticks by rounding seconds / time base to the nearest
// integer (eval/phase3-survey/design.md P-A). The two conversions round a
// double four times, each by at most 2^-53 of it, so a PTS under 2^49 ticks
// in magnitude comes back exactly. A score the report writes as null, NaN in
// the analysis, is NaN.
//
// No FFmpeg but AVRational here; unit-tested in tests/test_ladder_report_read.cpp.

extern "C" {
#include <libavutil/rational.h>
}

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
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
    // The other scoring settings of the report's head, which hold no path:
    // the scores digest covers them (D-072).
    int n_threads   = 0;                          // n_threads
    int n_subsample = 0;                          // n_subsample
    std::optional<std::int64_t> frame_limit;      // frame_limit; nullopt: none (null)
};

// The ladder report `text`, read as above. On a refusal, `err` says why,
// naming the key or value, and the result is empty; otherwise `err` is empty.
LadderScores read_ladder_report(std::string_view text, std::string& err);

// The version of scores_text()'s layout, which is part of its text.
inline constexpr int kScoresTextVersion = 1;

// The canonical text of the report's scores digest (D-072, Q06 (c)): the
// caller hashes it (filehash::sha256_hex) for the timeline document's
// scores_sha256. It holds what the timeline reads from the report, apart
// from every field that holds a path: those are what the eval scripts
// replace with placeholders before a report is committed
// (eval/ladder/pool_check.py, eval/consistency/ladder_check.py;
// eval/phase3-survey/design.md P-Z), so a committed copy has the digest of
// the report it was made from. Left out: the master's URL and location, its
// entries' URIs and URLs, the reference's URL, each pair's two URLs, and the
// model unless model_source is "built-in", since a model file is named by
// its path; its SHA-256 stays. In: status, reference_mode, the model as
// above, model_source, model_sha256, scaler, n_threads, n_subsample,
// frame_limit, libvmaf_version, libvmaf_package_version, the master's
// SHA-256, each entry's index, line, bandwidth, average_bandwidth,
// resolution, codecs, frame_rate and video, the reference's entries, and for
// each pair its index, entries, time bases, frame rates, sequences, libvmaf
// contexts, failures and frames.
//
// One line of JSON with no white space between its tokens and a line feed at
// its end; keys in this order, strings as json::quote and numbers as
// json::number writes them (src/JsonText.h), PTS in ticks, rationals as
// "num/den", null for a value that is absent:
//   {"digest":"mirage-ladder-scores","version":1,"status":…,"reference_mode":…,
//    "model":<its name when built in, else null>,"model_source":…,"model_sha256":…,
//    "scaler":…,"n_threads":…,"n_subsample":…,"frame_limit":…,
//    "libvmaf_version":…,"libvmaf_package_version":…,"master_sha256":…,
//    "entries":[{"index":…,"line":…,"bandwidth":…,"average_bandwidth":…,
//                "resolution":…,"codecs":…,"frame_rate":…,"video":…},…],
//    "reference_entries":[…],
//    "pairs":[{"index":…,"entries":[…],"ref_time_base":…,"dis_time_base":…,
//              "ref_rate":…,"dis_rate":…,
//              "ref_sequences":[[first_index,first_pts,frames],…],"dis_sequences":[…],
//              "context_first":[…],"failures":[[input,index,pts,reason],…],
//              "frames":[[ref_index,dis_index,ref_pts,dis_pts,vmaf],…]},…]}
std::string scores_text(const LadderScores& scores);

// A PTS in seconds, as a report writes it, in ticks of `time_base`: the
// nearest integer to seconds / time base, halves away from zero; nullopt
// when `seconds` isn't finite, the time base isn't positive, or the ticks
// don't fit in 64 bits.
std::optional<std::int64_t> pts_ticks(double seconds, AVRational time_base);

} // namespace timeline
