#pragma once

// JSON reports as written by mirage_vmaf: one scored pair (to_json), or the
// pairs of a ladder run in one reference mode (ladder_to_json). No FFmpeg /
// libvmaf here so it can be unit-tested in mirage_tests.
//
// Layout of a pair's report (numbers at full precision; null for NaN or a
// missing value):
//   tool,
//   reference_mode         how the reference was chosen: "explicit-pair" when
//                          the command line names both inputs
//   libvmaf_version, libvmaf_package_version,
//   ffmpeg_libraries { libavutil, libavcodec, libavformat, libswscale,
//                      libavfilter }  each library's version at run time
//   config_file { path, sha256 }   the --config file the settings were read
//                          from (D-030), by its path as given and the SHA-256 of
//                          its contents; null without one
//   model                  built-in model version, or model file path as given
//   model_source           "built-in" | "file"
//   model_sha256           SHA-256 of the model file; null for a built-in model
//   scaler, n_threads, n_subsample,
//   scored_as { width, height, pix_fmt, bit_depth }   what libvmaf was given
//   dis_conversion [ { from { color_space, color_range },
//                      to { color_space, color_range },
//                      matrix_changed, range_changed,
//                      first_index, pairs } ]       the distorted frames' color
//                                                    conversions (VMAFColorConversion)
//   ref_conversion [ ... ]  the reference's, the same way; empty for a yuv420p
//                           reference, which is used as it is
//   warnings [ text ]      what can make the scores differ from FFmpeg's
//                          (VMAFResult::warnings); empty when nothing is known
//   frame_limit (null = none), frames_scored,
//   pooled { mean, harmonic_mean, min, min_without_first_frame,
//            min_without_context_first_frames,
//            p1, p5, p10, p25, p50 }   VMAFResult::pooled (src/VMAFPool.h): the
//                                      mean, harmonic mean and min as libvmaf
//                                      pools them, the min of frames 1 on
//                                      (null for one frame), the min without
//                                      the first pair of each libvmaf context
//                                      (D-037; null when every pair starts
//                                      one), and percentiles by linear
//                                      interpolation
//   alignment {            how the frames were paired by timestamp (D-028,
//                          src/VMAFAlign.h; VMAFResult::alignment)
//     rule, timestamp_origin, grid, tolerance (seconds),
//     reference / distorted { frame_rate, start (seconds), start_from },
//     paired, failed, failure_rate, threshold, exceeded,
//     unmatched_reference, extra_distorted, duplicate_distorted,
//     no_timestamp, after_discontinuity, discontinuities,
//     discontinuity_jump (seconds), off_grid_reference, off_grid_distorted,
//     vmaf_contexts [ { first_index, pairs } ],
//     failures [ { input, index, pts, reason } ] }
//   ref / dis { url, width, height, pix_fmt, color_space, color_range,
//               color_primaries, color_trc, time_base, ended }
//   frames [ { index, ref_index, dis_index, ref_pts, dis_pts, vmaf } ]
//                          index: the pair's; ref_index, dis_index: its frames'
//                          positions in their inputs; PTS in seconds
//
// Layout of a ladder run's report (DECISIONS.md D-029, D-047, src/VMAFLadder.h),
// one reference mode per report:
//   status ("complete" | "cancelled" | "failed"), error (null when complete)
//                          how the run ended (vmafladder::Result)
//   pool { jobs, memory_limit, max_running, pair_memory, peak_resident, warnings [ text ] }
//                          the pool's size, the memory limit in bytes (null =
//                          none), the most pairs scored at once, what the limit
//                          measured a pair to use, at its largest, in bytes
//                          (null = never; D-053), the process's peak resident
//                          memory in bytes (null = unknown)
//   tool, reference_mode ("top-rung" | "mezzanine"), libvmaf_version,
//   libvmaf_package_version, ffmpeg_libraries, config_file, model,
//   model_source, model_sha256, scaler, n_threads, n_subsample, frame_limit
//                          as in a pair's report, once for every pair: the
//                          first complete pair's settings, or with none
//                          complete those the run asked for
//                          (vmafladder::Result::settings; model_sha256 null)
//   ladder { url, location, sha256,     the master playlist as given, where
//                                       it was read from, and its SHA-256
//            entries [ { index, line, uri, url, bandwidth, average_bandwidth,
//                        resolution, codecs, frame_rate, video } ] }
//                          its EXT-X-STREAM-INF entries: their attributes as
//                          the master gives them (null when absent), each URI
//                          resolved (url), and whether the entry has video
//   reference { url, entries [ index ] }   the reference: the top rung and
//                          the entries naming it, or the mezzanine as given
//                          and no entry
//   skipped [ { entry, reason } ]   entries not scored: those without video
//   warnings [ text ]      about the plan, such as a second playlist with the
//                          top rung's BANDWIDTH
//   pairs [ { index, entries [ index ],  the complete pairs, in the plan's
//                                        order: index, the plan's; entries,
//                                        the video entries naming its
//                                        distorted playlist
//             started_s, ended_s,        when it was scored, seconds from
//                                        the run's start
//             scored_as, dis_conversion, ref_conversion, warnings,
//             frames_scored, pooled, alignment, ref, dis, frames } ]
//                          each with the keys of a pair's report that are its
//                          own, as a pair given with --ref and --dis has them
//   not_scored [ { index, url, entries [ index ], status, why } ]
//                          the plan's other pairs: status "failed",
//                          "stopped" or "not started"; why, a failed pair's
//                          reason (null for the others)

#include "VMAFAnalyzer.h"
#include "VMAFLadder.h"

#include <string>
#include <string_view>

namespace vmafreport {

// What a report states beyond the analysis result.
struct RunInfo {
    std::string tool;                     // program that wrote the report
    std::string reference_mode;           // see the layout above
    std::string libvmaf_version;          // vmaf_version() at run time
    std::string libvmaf_package_version;  // found at configure time; "unknown" if not derivable
    std::vector<VMAFLibraryVersion> ffmpeg_libraries;   // vmaf_ffmpeg_libraries() (D-021)
    std::string config_path;              // --config as given; empty = no config file
    std::string config_sha256;            // SHA-256 of the config file's contents
    unsigned    frame_limit = 0;          // --frames; 0 = none
};

// Why `r` must not be reported as a complete run: the analysis failed, an
// input stopped on a read or decode error, no pair was scored, or a libvmaf
// call failed while scoring. "" when the run is complete.
std::string incomplete(const VMAFResult& r);

// The report for `r`, which must hold a finished analysis.
std::string to_json(const VMAFResult& r, const RunInfo& run);

// The report of a ladder run that scored (vmafladder::Result::status not
// empty), complete or not. Its reference mode is the plan's;
// run.reference_mode isn't used.
std::string ladder_to_json(const vmafladder::Result& ladder, const RunInfo& run);

// Writes `text` to `path` through a temporary file next to it that is renamed
// over `path` only once everything was written and closed, so a failed write
// leaves no partial report behind. On failure returns false and sets `err`.
bool write_file(const std::string& path, std::string_view text, std::string& err);

} // namespace vmafreport
