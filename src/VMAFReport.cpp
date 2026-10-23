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
    out += std::format("  \"frame_limit\": {},\n",
                       run.frame_limit > 0 ? std::to_string(run.frame_limit) : "null");
    out += std::format("  \"frames_scored\": {},\n", r.per_frame.size());
    out += std::format("  \"pooled\": {{\"mean\": {}, \"min\": {}}},\n",
                       number(r.vmaf_mean), number(r.vmaf_min));
    out += std::format("  \"ref\": {},\n", input_json(r.ref_url, r.ref_info));
    out += std::format("  \"dis\": {},\n", input_json(r.dis_url, r.dis_info));
    out += "  \"frames\": [";
    for (std::size_t i = 0; i < r.per_frame.size(); ++i) {
        out += i == 0 ? "\n    " : ",\n    ";
        out += std::format("{{\"index\": {}, \"ref_pts\": {}, \"dis_pts\": {}, \"vmaf\": {}}}",
                           i, pts_at(r.ref_pts, i), pts_at(r.dis_pts, i), number(r.per_frame[i]));
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
