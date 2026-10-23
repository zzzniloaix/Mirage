// mirage_vmaf — score a distorted video against a reference with Mirage's
// VMAF analyzer (src/VMAFAnalyzer.cpp) without the player: no window, GL or
// ImGui. Writes a JSON report (src/VMAFReport.h). See --help.
//
// Exit codes:
//   0 — report written
//   1 — the analysis failed or was incomplete, or the report could not be
//       written; no report is left at --out
//   2 — bad arguments

#include "VMAFAnalyzer.h"
#include "VMAFCli.h"
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

int main(int argc, char** argv)
{
    vmafcli::Args args;
    const std::vector<std::string_view> argl(argv + 1, argv + argc);
    if (const std::string err = vmafcli::parse(argl, args); !err.empty()) {
        const std::string usage = vmafcli::usage();
        std::fprintf(stderr, "mirage_vmaf: %s\n%sRun mirage_vmaf --help for details.\n",
                     err.c_str(), usage.substr(0, usage.find('\n') + 1).c_str());
        return 2;
    }
    if (args.help) {
        std::fputs(vmafcli::usage().c_str(), stdout);
        return 0;
    }

    // FFmpeg's warnings and errors only: no per-segment "Opening ..." lines.
    av_log_set_level(AV_LOG_WARNING);
    avformat_network_init();

    const VMAFResult r = VMAFAnalyzer::analyze(
        args.ref, args.dis,
        { .max_frames = args.frames, .record_pts = true, .scaler = args.scaler,
          .model = args.model });
    if (const std::string why = vmafreport::incomplete(r); !why.empty()) {
        std::fprintf(stderr, "mirage_vmaf: %s\n", why.c_str());
        return 1;
    }

    // Both inputs come from the command line: the reference is neither picked
    // from a ladder (top-rung) nor known to be the source (mezzanine).
    const vmafreport::RunInfo run{
        .tool                    = "mirage_vmaf",
        .reference_mode          = "explicit-pair",
        .libvmaf_version         = vmaf_version(),
        .libvmaf_package_version = MIRAGE_LIBVMAF_PACKAGE_VERSION,
        .ffmpeg_libraries        = vmaf_ffmpeg_libraries(),
        .frame_limit             = args.frames,
    };
    std::string err;
    if (!vmafreport::write_file(args.out, vmafreport::to_json(r, run), err)) {
        std::fprintf(stderr, "mirage_vmaf: %s\n", err.c_str());
        return 1;
    }
    std::printf("mirage_vmaf: %zu frame pairs scored, report written to %s\n",
                r.per_frame.size(), args.out.c_str());
    return 0;
}
