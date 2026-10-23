# Phase 0 baseline

Runs Mirage's **existing** ladder VMAF scoring (`--vmaf`) on a public HLS ladder, without changing any code. In addition, FFmpeg's libvmaf filter was run on the first 300 frames of two of the rungs as a diagnostic comparison.

Every file here is either a raw output or a script that produced those outputs. Every number in `docs/VMAF_GAP_REPORT.md` comes from here.

## What was measured

| Item | Value | Source |
|---|---|---|
| Code | commit `a4e02895d2d9c51c620e6c140f5566944c80ec71` (after the 2026-10-23 history rewrite this commit is `1bab7a3104cf9149300eafd3d3deb7ae4a9c6f69`, with the same tree `61765e415978c5a9735a3a6a3e245c52d0e6be99`; see `docs/DECISIONS.md` D-012). Built with `cmake --preset macos-release` in a clean directory created by `git worktree add --detach` | `environment.txt`, `code_facts.txt` |
| Linked libraries | `libavformat` / `libavcodec` / `libswscale` from ffmpeg-full 8.1.1; libvmaf (Homebrew package 3.1.0, the library reports 3.0.0); GLFW 3.4 | `environment.txt` (`otool -L`, `brew list`, `vmaf_version()`) |
| Machine | Apple M3 Pro, macOS 26.2 | `environment.txt` |
| Ladder | `https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/bipbop_16x9_variant.m3u8` | `inputs/` (the fetched master and two variant playlists; the fetch time is in `inputs/fetched_utc.txt`) |

## How it was run

```
eval/baseline/run_baseline.sh <mirage> <ladder-url> <run-dir>       # the script used for this run (see the note below)
eval/baseline/finalize_baseline.sh <run-dir> eval/baseline/bipbop16x9
eval/baseline/collect_facts.sh <worktree> <mirage> eval/baseline     # environment.txt, code_facts.txt
```

What `run_baseline.sh` does:
- Launches `mirage <url> --vmaf --qa-no-focus --window 1280x720` with an empty `HOME`, wrapped in `/usr/bin/time -l`, with a 6-hour alarm as a safety net.
- Once "VMAF report written to" appears in the log, waits 2 more seconds and ends the player. The existing tool keeps playing after it writes the report and never exits on its own, so `rc.txt` is 143 (SIGTERM), as expected.
- `S` is a scratch directory, used only to hold the isolated `HOME`. For the 2026-10-21 run it was hard-coded to that session's scratch directory. Since the redaction (D-014) the script reads it from the `SCRATCH` environment variable; that line is the only change to the script since the run.

## Two deviations from running per the README as is

Both are recorded in `docs/DECISIONS.md`.

1. **P-003**: in the clean build, the glad generation step used Python 3.13 + jinja2 3.1.6 from a venv, because this machine's default Homebrew Python 3.14.7 fails to import `pyexpat`. The raw log is in `build_failure_python314.log`. The generated `gl.h` / `gl.c` / `khrplatform.h` are byte-for-byte identical to the main repository's build.
2. **P-004**: the existing code writes its report to `<URL>.vmaf.json`, relative to the working directory. So that it could be written, `https:/devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/` was created under the working directory beforehand. The path actually written is in `bipbop16x9/report_path_written_by_mirage.txt`.

## Files

| File | Contents |
|---|---|
| `bipbop16x9/mirage.log` | the player's stdout, verbatim |
| `bipbop16x9/time_and_stderr.log.xz` | the player's stderr (FFmpeg logs) plus the output of `time -l`, xz-compressed; it decompresses to the original file, and its SHA-256 is in `SHA256SUMS` |
| `bipbop16x9/time_l.txt` | the `time -l` part of the above (wall clock, CPU, peak memory) |
| `bipbop16x9/report.vmaf.json` | the JSON written by the existing code, copied verbatim |
| `bipbop16x9/summary.txt` | per-rung summary of the JSON by `tools/summarize_report.py` |
| `bipbop16x9/*_utc.txt`, `rc.txt` | start time, the time the report line was seen, end time, exit code |
| `diagnostics/ffmpeg_libvmaf_*_first300.*` | the commands (`.cmd.txt`), JSON logs and stderr of the FFmpeg libvmaf filter on the first 300 frames of the 640x360 and 416x234 rungs |
| `diagnostics/compare_first300.txt` | output of `tools/compare_first_frames.py`: per-frame differences between Mirage and FFmpeg on the same 300 frames |
| `diagnostics/ffprobe_rungs.txt` | each rung's stream parameters and the PTS of its first 12 frames |
| `diagnostics/frame_n300_*.png` | screenshots of frame 300 (counting from 0) after decoding from the start; the commands are in `frames_n300.cmd.txt` |
| `inputs/` | the original playlists, fetched on the day of the run |
| `build_failure_python314.log` | the raw log of the clean build failing with the default Python |
