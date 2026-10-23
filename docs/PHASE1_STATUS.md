# Phase 1 status

Phase 1 of the VMAF work order is its §5, §1 and §8, submitted as one PR (`docs/DECISIONS.md` D-001). This file says, for each of those sections, what the branch `vmaf/phase-1` has done, what its acceptance results are and which committed outputs they come from, and what remains. Every number comes from a committed output named next to it; where there is none, it says MEASUREMENT PENDING.

The open design decisions are in `docs/PHASE1_DECISIONS_PENDING.md`, each with its options and the measurements behind it. They are the owner's (hard rule 3 of the work order). Until the owner decides, every default is what it was before Phase 1: the bilinear scaler, the vmaf_v0.6.1 model, 8-bit yuv420p, frames paired in the order they are decoded, and libvmaf's pooled mean and min (D-003, D-004).

- **done**: the section's items are implemented and its acceptance, where the work order gives one, is met by committed outputs. A section can be done while the owner has still to pick one of its defaults.
- **partial**: some of its items are.
- **pending**: none is, or there is only a plan.

## Overview

| Section | Status | Acceptance result | Waits for |
|---|---|---|---|
| §5 Score consistency check | done for yuv420p references | `eval/consistency/acceptance-full/`: every frame of the four pairs of D-018 passes; the largest \|delta\| is 0.000000 at the 6 decimals of FFmpeg's log, and no frame is over 0.01. For a reference in another pixel format with other tags than the distorted input, Mirage's scores differ from FFmpeg's (`eval/consistency/format-cases-after/`) | the defaults of #3, #4, #5 and #7: with another default the report is made again; the package "D-016: references that aren't yuv420p" |
| §1.1 libvmaf 3 integration | done in `mirage_vmaf`; the player's own export lacks the new fields (difference 9) | `eval/consistency/models-300/`: each model changes the scores of every frame, and every report names its model | #4, the default model |
| §1.2 Two reference modes | partial | not run: it needs a ladder with a mezzanine (D-011) | the §1.2 package, D-011's clips |
| §1.3 Resolution alignment | done in `mirage_vmaf`; the player's own export doesn't state the scaler (difference 9) | the work order gives none; `eval/consistency/scalers-300/` measures the three scalers on both sides | #3, the default scaler |
| §1.4 Frame alignment | pending: a plan | not run | approval of `docs/plans/frame-alignment.md` and #7 |
| §1.5 Score pooling | partial | the work order gives none; `eval/pooling/` measures the candidate definitions | #6, the definitions |
| §8 Reproducible packaging | partial | a build in a clean worktree, which approximates the clean machine of the acceptance | #11a to #11c, the README package |

## §5 Score consistency check: done for yuv420p references

What the work order asks for, and where it is:

- **A tool** that compares Mirage's scores with those of `ffmpeg -i <distorted> -i <reference> -lavfi libvmaf=model=<same>` on the same inputs: `eval/consistency/run_delta.sh`, with `mirage_vmaf` (D-017) on Mirage's side (`eval/consistency/README.md`; P-008 to P-011). FFmpeg's libvmaf filter refuses inputs of different sizes, so FFmpeg's distorted input goes through `scale=W:H:flags=<scaler>` first, as in Phase 0's diagnostic comparison (difference 3 in `docs/PHASE1_DECISIONS_PENDING.md`).
- **A delta report** per frame and of the pooled values, with the largest and mean absolute difference and the frames over the threshold: `delta.md` and `delta.json` of every pair and every run.
- **The target**, a per-frame difference under 0.01 with the default configuration: D-019 fails a frame when |Mirage − FFmpeg| > 0.01.
- **The acceptance**, one command that makes the delta report, committed in `eval/`:

  ```
  eval/consistency/run_delta.sh build/macos-release/mirage_vmaf <out>/plain --jobs 4
  ```

**The result** over every frame of the four lower bipbop 16x9 rungs, each against gear5 (D-018), with the defaults (`eval/consistency/acceptance-full/plain/delta.md`; P-026):

| pair | status | frame pairs (Mirage / FFmpeg) | max \|delta\| | mean \|delta\| | frames over 0.01 | pooled mean, Mirage | pooled mean, FFmpeg | conversion, both sides |
|---|---|---|---|---|---|---|---|---|
| gear4_vs_gear5 | PASS | 53946 / 53946 | 0.000000 | 0.000000 | 0 | 74.563207 | 74.563207 | bt709 -> bt709 |
| gear3_vs_gear5 | PASS | 53946 / 53946 | 0.000000 | 0.000000 | 0 | 53.222012 | 53.222012 | bt709 -> bt709 |
| gear2_vs_gear5 | PASS | 53946 / 53946 | 0.000000 | 0.000000 | 0 | 27.868640 | 27.868640 | smpte170m -> bt709 |
| gear1_vs_gear5 | PASS | 53946 / 53946 | 0.000000 | 0.000000 | 0 | 0.000827 | 0.000827 | smpte170m -> bt709 |

The checks made on it afterwards (`eval/consistency/acceptance-full/checks.txt`; `eval/consistency/README.md`, "`acceptance-full/`"):

- Written with the 6 decimals of FFmpeg's log, Mirage's score of every one of the 53946 frames of every pair is FFmpeg's (check 2).
- Both tools convert gear2 and gear1 from smpte170m/tv to bt709/tv and change nothing for gear4 and gear3 (check 5, `plain/delta.md`).
- Every pair's two frames have the same PTS, and none lacks one (check 6), so comparing by position compared the same frames (P-009).
- The first 300 frames have the scores of `eval/consistency/scalers-300/bilinear`, made by the same program with `--frames 300` and one process at a time (check 3).
- The run was made at commit `3650f83` with a clean working tree at its start and end, with a `mirage_vmaf` whose SHA-256 builds of commits in clean worktrees reproduced (`build.txt`; P-026).
- A rerun by `eval/run_all.sh`, with a `mirage_vmaf` it built from commit `e0beb49`, reproduced every verdict and every score of both sides exactly (`eval/run_all/2026-10-23-full/rerun_check.md`; P-027), and so did a rerun of every step with the program of `9855319`, which has P-030's warning (`eval/run_all/2026-10-23-fix/rerun_check.md`; P-032).

**How it got there.** Phase 0 found Mirage scoring 640x360 higher than FFmpeg on every one of the first 300 frames, by 5.2798 on average (`eval/baseline/diagnostics/compare_first300.txt`: all 300 frames over 0.01, and the signed mean equal to the mean |delta|). The cause was FFmpeg's conversion of the distorted frames from the smpte170m color matrix to the reference's bt709, which Mirage didn't make (`eval/consistency/diag-before-300/`). Since D-016, Mirage converts the color matrix and range as FFmpeg's filter graph does for a yuv420p reference (`src/VMAFColor.h`, P-014), and the two agree on every frame of the 300-frame runs and of 18 synthetic color tag cases (`eval/consistency/diag-after-300/`, `color-cases-after/`; `eval/consistency/README.md`).

**Remaining.**

- The report was made with today's defaults. If the owner chooses another default scaler, model, bit depth or pairing (#3, #4, #5, #7), it has to be made again with it. Bicubic and lanczos pass on 300 frames when both sides use them (`eval/consistency/scalers-300/`); over every frame they are MEASUREMENT PENDING.
- **References that aren't yuv420p.** For such a reference FFmpeg's graph converts the reference to the distorted frames' color, which Mirage doesn't. On synthetic 10-bit, 4:2:2, 4:4:4 and full range H.264 references, Mirage's scores differ from FFmpeg's in every case whose tags differ, by up to 13.274013, and by up to 23.673382 in the full range case with the same tags; they agree in the other same-tag cases (`eval/consistency/format-cases-after/main/delta.md`). Since D-016 the full range case against a limited range distorted input fails, which passed before (`format-cases-before/`). `mirage_vmaf`'s report warns about such a reference (P-030); the fix is the owner's decision ("D-016: references that aren't yuv420p" in `docs/PHASE1_DECISIONS_PENDING.md`). No bipbop rung is affected: all are yuv420p.
- D-016's rules for untagged inputs and for matrices libswscale can't convert, and D-019's boundary against the work order's "< 0.01": `docs/PHASE1_DECISIONS_PENDING.md`.
- bipbop's rungs were rendered separately, so these scores compare the two tools, not the rungs' quality (D-005, D-011).

## §1.1 libvmaf 3 integration: done in mirage_vmaf

- **libvmaf's C API, no shell-out**: `mirage_vmaf` links FFmpeg's libraries, libvmaf and the system's C and C++ libraries, nothing else (`eval/build_check/macos-2026-10-23-6a6661f.txt`, "Built programs", `otool -L`). The `ffmpeg` program runs only in the eval tooling, as the reference §5 compares with.
- **Version and model identifier at run time**: `mirage_vmaf`'s report has libvmaf's version at run time and its package version (D-006), and `model`, `model_source` and `model_sha256` (`src/VMAFReport.h`; P-017).
- **Models**: `--model` takes any built-in model of the linked libvmaf, vmaf_v0.6.1, vmaf_v0.6.1neg and vmaf_4k_v0.6.1 among them, and `--model-file` a model file.
- **Acceptance**, changing the model changes the scores observably and the output names the model (`eval/consistency/models-300/`, gear2 against gear5, first 300 frames): with vmaf_v0.6.1neg every frame scores lower than with vmaf_v0.6.1, with vmaf_4k_v0.6.1 every frame higher; the pooled means are 19.498402 (vmaf_v0.6.1neg), 47.442375 (vmaf_4k_v0.6.1) and 28.552935 (vmaf_v0.6.1) (`model_effect.md`). Every report names its model, and Mirage matches FFmpeg at the 6 decimals of FFmpeg's log with every model (`checks.txt`, checks 3 and 4).
- **Remaining**: #4, the default model. The player has no model option, and its own JSON export (`VMAFAnalyzer::write_json`) still has its Phase 0 layout, without libvmaf's version or the model (difference 9 in `docs/PHASE1_DECISIONS_PENDING.md`).

## §1.2 Two reference modes: partial

- `mirage_vmaf`'s report has `reference_mode`, `explicit-pair`, since its command line names both inputs (P-017). The work order's §7 has only `mezzanine` and `top-rung` (difference 2).
- The player's ladder scoring is top-rung, the default of D-002, as before Phase 1 (`docs/VMAF_GAP_REPORT.md`, item 4); its JSON export has no `reference_mode`.
- Not built: mezzanine mode for a ladder, and ladder scoring in `mirage_vmaf` (P-019); the choices they need are in the §1.2 package.
- **Acceptance**, both modes on one ladder with the difference documented: MEASUREMENT PENDING. It needs a ladder with a mezzanine, D-011's self-encoded ladders.

## §1.3 Resolution alignment: done in mirage_vmaf, the default pending (#3)

- `mirage_vmaf --scaler bilinear|bicubic|lanczos`, and the report's `scaler` (P-017). Without it the scaler is bilinear, as before (D-003).
- **Measured** (`eval/consistency/scalers-300/`, first 300 frames of the four pairs, each scaler on both sides): Mirage matches FFmpeg with each of the three (`checks.txt`, check 3), and the scaler moves the scores: against bilinear, the mean shift per pair is 6.775801 to 11.113910 with bicubic and 8.874908 to 13.334836 with lanczos, and no frame scores lower (`scaler_effect.md`).
- The work order suggests bicubic so that §5 passes; §5 passes with each of the three (difference 1).
- **Remaining**: #3. The player has no scaler option, and its JSON export doesn't state the scaler (difference 9).

## §1.4 Frame alignment: pending, a plan

- `docs/plans/frame-alignment.md` is the plan D-007 asks for: pairing by timestamp, frame-rate mismatches, dropped and duplicated frames, discontinuities, the failure count, and #7, the threshold and whether exceeding it warns or refuses. Nothing of it is implemented.
- Today the analyzer pairs frames in the order they are decoded. `mirage_vmaf` records each pair's PTS, and the delta tool warns when a pair's two frames have different PTS. Over every frame of the four bipbop pairs, every pair's two frames have the same PTS (`eval/consistency/acceptance-full/checks.txt`, check 6): there, pairing by decode order and by timestamp pair the same frames.
- **Acceptance**, a stream with frames dropped on purpose for which the tool reports the failed frames instead of a wrong score: MEASUREMENT PENDING; the plan's tests include it.

## §1.5 Score pooling: partial

- `mirage_vmaf` reports libvmaf's pooled mean and min, and the player's analyzer also computes a p5. `harmonic_mean`, `p1`, `p10`, `p25` and `p50` are not reported: their definitions are #6.
- **Measured** with `eval/pooling/pooling.py` (P-018, P-027): on Phase 0's full-length series and the first 300 frames after D-016 (`eval/pooling/bipbop16x9/`), and on both sides of every frame of the committed §5 report (`eval/pooling/acceptance-full/`). Over every frame after D-016, the minimum is the first frame's score in every series, the three percentile definitions differ by at most 0.000276, libvmaf's harmonic mean is the shifted form `n / sum(1 / (x + 1)) - 1`, and gear1's frames at 0, 53790 of 53946, leave the plain harmonic mean undefined (`eval/pooling/README.md`).

## §8 Reproducible packaging: partial

| Item | Status | Where |
|---|---|---|
| `LICENSE` | done: GPL-3.0-or-later (D-008) | `LICENSE`, `README.md`; whether legal or the company's open-source process has to confirm it is D-008's open question |
| Pinned dependencies with their versions recorded | partial: every version is recorded and the FetchContent dependencies are pinned; FFmpeg, libvmaf and GLFW come from Homebrew, which can't pin them | `docs/BUILD.md` "Versions", `CMakeLists.txt`, P-021; difference 4 |
| Docker or Nix to reproduce the evaluation | pending | #11c |
| Build instructions for Linux and macOS, Apple Silicon included | partial: macOS on Apple Silicon only | `docs/BUILD.md`; #11b, difference 5 |
| `eval/run_all.sh`, one script that reruns the paper's numbers | done for the committed Phase 1 measurements, the full-length §5 run included; its three committed runs found everything as committed. There are no paper numbers yet (D-005) | `eval/README.md`, `eval/run_all/2026-10-23/`, `2026-10-23-full/` and `2026-10-23-fix/`, P-024, P-025, P-027, P-032; difference 7 |
| `eval/streams.md` and a fetch script | partial: the bipbop and advanced Apple streams; the fetch keeps playlists, no media; DASH-IF and Unified Streaming streams not examined | `eval/streams.md`, P-023; difference 8 |
| **Acceptance**: a clean machine builds by the README | partial: two builds in clean worktrees of committed HEADs on the machine of the measurements; on it the release preset stops at glad with the default Python, the glad step the README's `macos-debug` quick start runs too, and `docs/BUILD.md`'s venv builds both presets | `eval/build_check/macos-2026-10-23-6a6661f.txt`, P-022; difference 6 |

Also open: whether glad's generated loader is committed (#11a), the README beyond its install line, and the root `vcpkg.json`, which the build doesn't use and which lists FFmpeg and GLFW without libvmaf or versions (`docs/VMAF_GAP_REPORT.md`, "Specific gaps for §8"; `docs/PHASE1_DECISIONS_PENDING.md`).

## The work order's engineering constraints

- **C++20 in the style of the surrounding code**, with unit tests for every new feature: Catch2 tests in `mirage_tests` for the C++ (`tests/test_vmaf_analyzer.cpp`, `test_vmaf_cli.cpp`, `test_vmaf_color.cpp`, `test_vmaf_report.cpp`, `test_json_text.cpp`, `test_file_hash.cpp`), Python `unittest` suites for the eval scripts (`eval/tests/`, `eval/consistency/tests/`, `eval/pooling/tests/`, `eval/streams/tests/`). At `9855319`, after which no commit changes code or tests, `mirage_tests '~[audio]'` passes in the main working tree at the first run, 90866 assertions in 244 test cases, and so do the four Python suites, 20, 85, 7 and 16 tests (`eval/test_runs/macos-2026-10-23-9855319.txt`; P-028, P-032).
- **Library APIs only**: as in §1.1.
- **Every configurable item as a command line option and in a config file**: the options are on the command line; the config file is pending, its format being the owner's (`docs/PHASE1_DECISIONS_PENDING.md`).
- **No document edited to make the implementation and the work order agree**: the differences are listed at the end of `docs/PHASE1_DECISIONS_PENDING.md`.

Two unit tests of code outside Phase 1 can fail: in a clean checkout, `SidecarSubtitles — decodes data/sub_test/test.srt`, whose fixture `.gitignore` leaves out (`docs/BUILD.md`, step 4); and now and then `Demuxer: seamless video and audio switches on a local file`, which, run alone 650 times, failed 5 times in a build of `37472fd`, the commit Phase 1 started from, and 4 times in one of `855a71b`, each time at one of the same two assertions (`eval/test_runs/README.md`, "Repeated runs of one test"; P-031). Both are in `docs/PHASE1_DECISIONS_PENDING.md`, "Smaller items found in Phase 1".

## Before the Phase 1 PR can merge

- The owner's decisions in `docs/PHASE1_DECISIONS_PENDING.md`, and the reruns a changed default needs (§5 above).
- The owner's review of Phase 1's commits, and confirmation of the process choices P-006 to P-032 (`docs/DECISIONS.md`).
- The owner's confirmation of the Phase 1 rows of `docs/AI_CONTRIBUTIONS.md`.
- The PR itself, which the owner opens (hard rule 5 of the work order: one PR for the phase).
