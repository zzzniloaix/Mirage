// mirage_vmaf — score a distorted video against a reference with Mirage's
// VMAF analyzer (src/VMAFAnalyzer.cpp) without the player: no window, GL or
// ImGui. Writes a JSON report (src/VMAFReport.h). With --ladder, scores the
// video playlists of an HLS master playlist one after another
// (src/VMAFLadder.h) and writes one report of them all. See --help.
//
// Exit codes:
//   0 — report written, also when more frames than the threshold allows
//       failed to pair by timestamp: that is a warning, on stderr and in the
//       report (DECISIONS.md D-028)
//   1 — the analysis failed or was incomplete, with --ladder the master
//       playlist couldn't be read or one pair's analysis failed or was
//       incomplete (D-029), or the report could not be written; no report is
//       left at --out
//   2 — bad arguments, or a --config file that can't be read or has an error

#include "FileHash.h"
#include "VMAFAnalyzer.h"
#include "VMAFCli.h"
#include "VMAFLadder.h"
#include "VMAFReport.h"

#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/log.h>
}

#include <libvmaf/libvmaf.h>

// Set by CMake from the real path of the linked libvmaf (DECISIONS.md D-006).
#ifndef MIRAGE_LIBVMAF_PACKAGE_VERSION
#define MIRAGE_LIBVMAF_PACKAGE_VERSION "unknown"
#endif

namespace {

// The pairs of the ladder `args` names, scored and reported (D-029).
int score_ladder(const vmafcli::Args& args, const VMAFOptions& opts, vmafreport::RunInfo run)
{
    const vmafladder::Result ladder = vmafladder::score(
        args.ladder, args.mezzanine, opts,
        [](std::size_t k, std::size_t n, const VMAFResult& r) {
            // Too many frames that failed to pair by timestamp: the pair is
            // still scored and reported, with this warning (D-028).
            if (!r.alignment.warning.empty())
                std::fprintf(stderr, "mirage_vmaf: warning: pair %zu of %zu, %s: %s\n", k + 1, n,
                             r.dis_url.c_str(), r.alignment.warning.c_str());
            std::printf("mirage_vmaf: pair %zu of %zu, %s: %zu frame pairs scored\n", k + 1, n,
                        r.dis_url.c_str(), r.per_frame.size());
            std::fflush(stdout);
        });
    for (const std::string& w : ladder.plan.warnings)
        std::fprintf(stderr, "mirage_vmaf: warning: %s\n", w.c_str());
    if (!ladder.error.empty()) {
        std::fprintf(stderr, "mirage_vmaf: %s\n", ladder.error.c_str());
        return 1;
    }

    run.reference_mode = ladder.plan.reference_mode;
    std::string err;
    if (!vmafreport::write_file(args.out, vmafreport::ladder_to_json(ladder, run), err)) {
        std::fprintf(stderr, "mirage_vmaf: %s\n", err.c_str());
        return 1;
    }
    const std::size_t pairs = ladder.results.size(), skipped = ladder.plan.skipped.size();
    std::printf("mirage_vmaf: %zu %s of the ladder scored against %s (%s), %zu %s skipped, "
                "report written to %s\n",
                pairs, pairs == 1 ? "pair" : "pairs", ladder.plan.reference_url.c_str(),
                ladder.plan.reference_mode.c_str(), skipped, skipped == 1 ? "entry" : "entries",
                args.out.c_str());
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    vmafcli::Args args;
    // The SHA-256 of the --config file's contents, of the bytes parse() reads.
    std::string config_sha256;
    const auto load_config = [&](const std::string& path) {
        vmafcli::ConfigFile file = vmafcli::read_config(path);
        if (file.error.empty()) config_sha256 = filehash::sha256_hex(file.text);
        return file;
    };
    const std::vector<std::string_view> argl(argv + 1, argv + argc);
    if (const std::string err = vmafcli::parse(argl, args, load_config); !err.empty()) {
        const std::string usage = vmafcli::usage();
        std::fprintf(stderr, "mirage_vmaf: %s\n%sRun mirage_vmaf --help for details.\n",
                     err.c_str(), usage.substr(0, usage.find("\n\n") + 1).c_str());
        return 2;
    }
    if (args.help) {
        std::fputs(vmafcli::usage().c_str(), stdout);
        return 0;
    }

    // FFmpeg's warnings and errors only: no per-segment "Opening ..." lines.
    av_log_set_level(AV_LOG_WARNING);
    avformat_network_init();

    const VMAFOptions opts{ .max_frames = args.frames, .record_pts = true,
                            .scaler = args.scaler, .model = args.model };
    vmafreport::RunInfo run{
        .tool                    = "mirage_vmaf",
        .libvmaf_version         = vmaf_version(),
        .libvmaf_package_version = MIRAGE_LIBVMAF_PACKAGE_VERSION,
        .ffmpeg_libraries        = vmaf_ffmpeg_libraries(),
        .config_path             = args.config,
        .config_sha256           = config_sha256,
        .frame_limit             = args.frames,
    };
    if (!args.ladder.empty()) return score_ladder(args, opts, run);

    const VMAFResult r = VMAFAnalyzer::analyze(args.ref, args.dis, opts);
    // Too many frames that failed to pair by timestamp: the pair is still
    // scored and reported, with this warning (DECISIONS.md D-028).
    if (!r.alignment.warning.empty())
        std::fprintf(stderr, "mirage_vmaf: warning: %s\n", r.alignment.warning.c_str());
    if (const std::string why = vmafreport::incomplete(r); !why.empty()) {
        std::fprintf(stderr, "mirage_vmaf: %s\n", why.c_str());
        return 1;
    }

    // Both inputs are named, on the command line or in its config file: the
    // reference is neither picked from a ladder (top-rung) nor known to be
    // the source (mezzanine).
    run.reference_mode = "explicit-pair";
    std::string err;
    if (!vmafreport::write_file(args.out, vmafreport::to_json(r, run), err)) {
        std::fprintf(stderr, "mirage_vmaf: %s\n", err.c_str());
        return 1;
    }
    std::printf("mirage_vmaf: %zu frame pairs scored, report written to %s\n",
                r.per_frame.size(), args.out.c_str());
    return 0;
}
