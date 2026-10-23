#pragma once

// JSON report of one scored pair, as written by mirage_vmaf. No FFmpeg /
// libvmaf here so it can be unit-tested in mirage_tests.
//
// Layout (numbers at full precision; null for NaN or a missing value):
//   tool,
//   reference_mode         how the reference was chosen: "explicit-pair" when
//                          the command line names both inputs
//   libvmaf_version, libvmaf_package_version,
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
//   warnings [ text ]      what can make the scores differ from FFmpeg's
//                          (VMAFResult::warnings); empty when nothing is known
//   frame_limit (null = none), frames_scored,
//   pooled { mean, min }                             libvmaf's vmaf_score_pooled
//   ref / dis { url, width, height, pix_fmt, color_space, color_range,
//               color_primaries, color_trc, time_base, ended }
//   frames [ { index, ref_pts, dis_pts, vmaf } ]      PTS in seconds

#include "VMAFAnalyzer.h"

#include <string>
#include <string_view>

namespace vmafreport {

// What a report states beyond the analysis result.
struct RunInfo {
    std::string tool;                     // program that wrote the report
    std::string reference_mode;           // see the layout above
    std::string libvmaf_version;          // vmaf_version() at run time
    std::string libvmaf_package_version;  // found at configure time; "unknown" if not derivable
    unsigned    frame_limit = 0;          // --frames; 0 = none
};

// Why `r` must not be reported as a complete run: the analysis failed, an
// input stopped on a read or decode error, no pair was scored, or a libvmaf
// call failed while scoring. "" when the run is complete.
std::string incomplete(const VMAFResult& r);

// The report for `r`, which must hold a finished analysis.
std::string to_json(const VMAFResult& r, const RunInfo& run);

// Writes `text` to `path` through a temporary file next to it that is renamed
// over `path` only once everything was written and closed, so a failed write
// leaves no partial report behind. On failure returns false and sets `err`.
bool write_file(const std::string& path, std::string_view text, std::string& err);

} // namespace vmafreport
