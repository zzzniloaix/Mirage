#include "VMAFReport.h"
#include "JsonText.h"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <system_error>
#include <vector>

namespace vmafreport {

namespace {

using json::number;
using json::quote;

std::string input_json(const std::string& url, const VMAFInputInfo& in)
{
    return std::format(
        "{{\"url\": {}, \"width\": {}, \"height\": {}, \"pix_fmt\": {}, \"color_space\": {}, "
        "\"color_range\": {}, \"color_primaries\": {}, \"color_trc\": {}, \"time_base\": {}, "
        "\"ended\": {}}}",
        quote(url), in.width, in.height, quote(in.pix_fmt), quote(in.color_space),
        quote(in.color_range), quote(in.color_primaries), quote(in.color_trc),
        quote(std::format("{}/{}", in.time_base_num, in.time_base_den)), in.ended);
}

std::string pts_at(const std::vector<double>& pts, std::size_t i)
{
    return i < pts.size() ? number(pts[i]) : "null";
}

std::string index_at(const std::vector<int64_t>& index, std::size_t i)
{
    return i < index.size() ? std::to_string(index[i]) : "null";
}

std::string conversions_json(const std::vector<VMAFColorConversion>& list)
{
    std::string out = "[";
    for (std::size_t i = 0; i < list.size(); ++i) {
        const VMAFColorConversion& c = list[i];
        out += i == 0 ? "\n    " : ",\n    ";
        out += std::format(
            "{{\"from\": {{\"color_space\": {}, \"color_range\": {}}}, "
            "\"to\": {{\"color_space\": {}, \"color_range\": {}}}, "
            "\"matrix_changed\": {}, \"range_changed\": {}, \"first_index\": {}, \"pairs\": {}}}",
            quote(c.from_space), quote(c.from_range), quote(c.to_space), quote(c.to_range),
            c.matrix_changed, c.range_changed, c.first_index, c.pairs);
    }
    return out + (list.empty() ? "]" : "\n  ]");
}

std::string texts_json(const std::vector<std::string>& list)
{
    std::string out = "[";
    for (std::size_t i = 0; i < list.size(); ++i)
        out += (i == 0 ? "\n    " : ",\n    ") + quote(list[i]);
    return out + (list.empty() ? "]" : "\n  ]");
}

std::string clock_json(const std::string& rate, double start, const std::string& from)
{
    return std::format("{{\"frame_rate\": {}, \"start\": {}, \"start_from\": {}}}", quote(rate),
                       number(start), from.empty() ? "null" : quote(from));
}

std::string alignment_json(const VMAFAlignment& a)
{
    std::string out = std::format(
        "{{\"rule\": {}, \"timestamp_origin\": {}, \"grid\": {}, \"tolerance\": {},\n"
        "    \"reference\": {},\n    \"distorted\": {},\n"
        "    \"paired\": {}, \"failed\": {}, \"failure_rate\": {}, \"threshold\": {}, "
        "\"exceeded\": {},\n"
        "    \"unmatched_reference\": {}, \"extra_distorted\": {}, \"duplicate_distorted\": {}, "
        "\"no_timestamp\": {}, \"after_discontinuity\": {},\n"
        "    \"discontinuities\": {}, \"off_grid_reference\": {}, \"off_grid_distorted\": {},\n"
        "    \"vmaf_contexts\": [",
        quote(a.rule), quote(a.timestamp_origin), quote(a.grid), number(a.tolerance),
        clock_json(a.ref_frame_rate, a.ref_start, a.ref_start_from),
        clock_json(a.dis_frame_rate, a.dis_start, a.dis_start_from), a.paired, a.failed,
        number(a.failure_rate), number(a.threshold), a.exceeded, a.unmatched_reference,
        a.extra_distorted, a.duplicate_distorted, a.no_timestamp, a.after_discontinuity,
        a.discontinuities, a.off_grid_reference, a.off_grid_distorted);
    for (std::size_t i = 0; i < a.contexts.size(); ++i)
        out += std::format("{}{{\"first_index\": {}, \"pairs\": {}}}", i == 0 ? "" : ", ",
                           a.contexts[i].first_index, a.contexts[i].pairs);
    out += "],\n    \"failures\": [";
    for (std::size_t i = 0; i < a.failures.size(); ++i) {
        const VMAFAlignmentFailure& f = a.failures[i];
        out += i == 0 ? "\n      " : ",\n      ";
        out += std::format("{{\"input\": {}, \"index\": {}, \"pts\": {}, \"reason\": {}}}",
                           quote(f.input), f.index, number(f.pts), quote(f.reason));
    }
    return out + (a.failures.empty() ? "]}" : "\n    ]}");
}

} // namespace

std::string incomplete(const VMAFResult& r)
{
    if (!r.error.empty()) return r.error;
    if (!r.ref_info.end_error.empty())
        return std::format("reading the reference stopped on an error: {}", r.ref_info.end_error);
    if (!r.dis_info.end_error.empty())
        return std::format("reading the distorted input stopped on an error: {}",
                           r.dis_info.end_error);
    if (r.per_frame.empty()) return "no frame pair was scored";
    if (!r.score_error.empty()) return r.score_error;
    return {};
}

std::string to_json(const VMAFResult& r, const RunInfo& run)
{
    const VMAFSettings& s = r.settings;
    std::string out = "{\n";
    out += std::format("  \"tool\": {},\n", quote(run.tool));
    out += std::format("  \"reference_mode\": {},\n", quote(run.reference_mode));
    out += std::format("  \"libvmaf_version\": {},\n", quote(run.libvmaf_version));
    out += std::format("  \"libvmaf_package_version\": {},\n", quote(run.libvmaf_package_version));
    out += "  \"ffmpeg_libraries\": {";
    for (std::size_t i = 0; i < run.ffmpeg_libraries.size(); ++i)
        out += std::format("{}{}: {}", i == 0 ? "" : ", ", quote(run.ffmpeg_libraries[i].name),
                           quote(run.ffmpeg_libraries[i].version));
    out += "},\n";
    out += std::format("  \"config_file\": {},\n",
                       run.config_path.empty()
                           ? std::string("null")
                           : std::format("{{\"path\": {}, \"sha256\": {}}}",
                                         quote(run.config_path), quote(run.config_sha256)));
    out += std::format("  \"model\": {},\n", quote(s.model));
    out += std::format("  \"model_source\": {},\n", quote(s.model_source));
    out += std::format("  \"model_sha256\": {},\n",
                       s.model_sha256.empty() ? "null" : quote(s.model_sha256));
    out += std::format("  \"scaler\": {},\n", quote(s.scaler));
    out += std::format("  \"n_threads\": {},\n", s.n_threads);
    out += std::format("  \"n_subsample\": {},\n", s.n_subsample);
    out += std::format("  \"scored_as\": {{\"width\": {}, \"height\": {}, \"pix_fmt\": {}, "
                       "\"bit_depth\": {}}},\n",
                       r.width, r.height, quote(s.pix_fmt), s.bit_depth);
    out += std::format("  \"dis_conversion\": {},\n", conversions_json(r.dis_conversions));
    out += std::format("  \"ref_conversion\": {},\n", conversions_json(r.ref_conversions));
    out += std::format("  \"warnings\": {},\n", texts_json(r.warnings));
    out += std::format("  \"frame_limit\": {},\n",
                       run.frame_limit > 0 ? std::to_string(run.frame_limit) : "null");
    out += std::format("  \"frames_scored\": {},\n", r.per_frame.size());
    const VMAFPooled& p = r.pooled;
    out += std::format("  \"pooled\": {{\"mean\": {}, \"harmonic_mean\": {}, \"min\": {}, "
                       "\"min_without_first_frame\": {}, \"p1\": {}, \"p5\": {}, \"p10\": {}, "
                       "\"p25\": {}, \"p50\": {}}},\n",
                       number(p.mean), number(p.harmonic_mean), number(p.min),
                       number(p.min_without_first_frame), number(p.p1), number(p.p5),
                       number(p.p10), number(p.p25), number(p.p50));
    out += std::format("  \"alignment\": {},\n", alignment_json(r.alignment));
    out += std::format("  \"ref\": {},\n", input_json(r.ref_url, r.ref_info));
    out += std::format("  \"dis\": {},\n", input_json(r.dis_url, r.dis_info));
    out += "  \"frames\": [";
    for (std::size_t i = 0; i < r.per_frame.size(); ++i) {
        out += i == 0 ? "\n    " : ",\n    ";
        out += std::format("{{\"index\": {}, \"ref_index\": {}, \"dis_index\": {}, \"ref_pts\": {}, "
                           "\"dis_pts\": {}, \"vmaf\": {}}}",
                           i, index_at(r.ref_index, i), index_at(r.dis_index, i),
                           pts_at(r.ref_pts, i), pts_at(r.dis_pts, i), number(r.per_frame[i]));
    }
    out += r.per_frame.empty() ? "]\n" : "\n  ]\n";
    out += "}\n";
    return out;
}

bool write_file(const std::string& path, std::string_view text, std::string& err)
{
    namespace fs = std::filesystem;
    const fs::path target(path);
    fs::path tmp = target;
    tmp += ".partial";

    std::error_code ec;
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            err = std::format("cannot create {}: {}", tmp.string(), std::strerror(errno));
            return false;
        }
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
        f.close();
        if (f.fail()) {
            fs::remove(tmp, ec);
            err = std::format("cannot write {}", tmp.string());
            return false;
        }
    }
    fs::rename(tmp, target, ec);
    if (ec) {
        err = std::format("cannot move {} to {}: {}", tmp.string(), path, ec.message());
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

} // namespace vmafreport
