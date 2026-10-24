# Phase 1 status

Phase 1 of the VMAF work order is its §5, §1 and §8, submitted as one PR (`docs/DECISIONS.md` D-001). This file says, for each of those sections, what the branch `vmaf/phase-1` has done, what its acceptance results are and which committed outputs they come from, and what remains. Every number comes from a committed output named next to it; where there is none, it says MEASUREMENT PENDING.

The design decisions are the owner's (hard rule 3 of the work order). On 2026-10-23 the owner decided the open packages of `docs/PHASE1_DECISIONS_PENDING.md` (D-020 to D-033); what implementing them leaves open is at the end of that file, and the packages keep their options and the measurements behind them. The defaults: the bicubic scaler (D-024) and the pooled values of D-027, both implemented; the vmaf_v0.6.1 model (D-025) and 8-bit yuv420p (D-026), as before; and frames paired by timestamp (D-028), implemented in the analyzer, with the §5 delta tool comparing the two sides by timestamp.

- **done**: the section's items are implemented and its acceptance, where the work order gives one, is met by committed outputs. A section can be done while the owner has still to pick one of its defaults.
- **partial**: some of its items are.
- **pending**: none is, or there is only a plan.

## Overview

| Section | Status | Acceptance result | Waits for |
|---|---|---|---|
| §5 Score consistency check | done for yuv420p references, and for the synthetic references in other pixel formats since D-022 | `eval/consistency/acceptance-full/`: every frame of the four pairs of D-018 passes; the largest \|delta\| is 0.000000 at the 6 decimals of FFmpeg's log, and no frame is over 0.01. Since D-022 every reference format case passes as well (`eval/consistency/format-cases-d022/`) | the report made again with bicubic, the default since D-024, with the analyzer's pairing by timestamp of D-028 and the delta tool's comparison by timestamp (MEASUREMENT PENDING) |
| §1.1 libvmaf 3 integration | done in `mirage_vmaf`; the player's own export lacks the new fields (difference 9) | `eval/consistency/models-300/`: each model changes the scores of every frame, and every report names its model | nothing: D-025 keeps vmaf_v0.6.1 as the default |
| §1.2 Two reference modes | partial | not run: it needs a ladder with a mezzanine (D-011) | the §1.2 package, D-011's clips |
| §1.3 Resolution alignment | done: bicubic by default since D-024, in the player and `mirage_vmaf`; the player's own export doesn't state the scaler (difference 9) | the work order gives none; `eval/consistency/scalers-300/` measures the three scalers on both sides, and `defaults-300/` the default | the §5 report with bicubic |
| §1.4 Frame alignment | done: pairing by timestamp in the analyzer, with the failures counted and listed and the warning above 1% (D-028), and the delta tool's comparison by timestamp | `eval/consistency/align-cases-d028/`: on a stream with a frame dropped on purpose, `mirage_vmaf` reports the failed frame and warns, every pair's two frames at the same PTS; before D-028 it scored the pairs after the gap one picture apart, without a warning (`align-cases-before/`) | the owner's confirmation of P-043 to P-049, and the open items of D-028 in `docs/PHASE1_DECISIONS_PENDING.md` |
| §1.5 Score pooling | done: the pooled values of D-027 in `mirage_vmaf`'s report, and the player's p5 by D-027's definition | the work order gives none; on the first 300 frames with the defaults, every reported value is that of `eval/pooling/pooling.py`, and the mean and min those of libvmaf's `vmaf_score_pooled()` (`eval/consistency/defaults-300/checks.txt`) | nothing |
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

**How it got there.** Phase 0 found Mirage scoring 640x360 higher than FFmpeg on every one of the first 300 frames, by 5.2798 on average (`eval/baseline/diagnostics/compare_first300.txt`: all 300 frames over 0.01, and the signed mean equal to the mean |delta|). The cause was FFmpeg's conversion of the distorted frames from the smpte170m color matrix to the reference's bt709, which Mirage didn't make (`eval/consistency/diag-before-300/`). Since D-016, Mirage converts the color matrix and range as FFmpeg's filter graph does for a yuv420p reference (`src/VMAFColor.h`, P-014), and the two agree on every frame of the 300-frame runs and of 18 synthetic color tag cases (`eval/consistency/diag-after-300/`, `color-cases-after/`; `eval/consistency/README.md`). Since D-022 it converts a reference in another pixel format as FFmpeg's graph does too, and the two agree on every frame of the 12 synthetic reference format cases (`eval/consistency/format-cases-d022/main/delta.md`; P-033).

**Remaining.**

- The report was made with the defaults of its day, the bilinear scaler among them. D-024 made bicubic the default and has the report made again with it, the bilinear one staying committed: MEASUREMENT PENDING. Over the first 300 frames, the defaults pass on all four pairs, with the scores of `scalers-300/bicubic` on every frame (`eval/consistency/defaults-300/checks.txt`, check 3; P-041); lanczos passes there too when both sides use it (`eval/consistency/scalers-300/`).
- **References that aren't yuv420p** (D-022). For such a reference FFmpeg's graph converts the reference to the distorted frames' color. Until D-022 Mirage didn't: on synthetic 10-bit, 4:2:2, 4:4:4 and full range H.264 references, its scores differed from FFmpeg's in every case whose tags differ, by up to 13.274013, and by up to 23.673382 in the full range case with the same tags (`eval/consistency/format-cases-after/main/delta.md`). Since D-022 it converts such a reference as FFmpeg does (`src/VMAFColor.h`, P-033), and every one of these cases passes, every frame agreeing at the 6 decimals of FFmpeg's log, the full range case against a limited range distorted input (`j01`) with its scores from before D-016 (`eval/consistency/format-cases-d022/checks.txt`, checks 4 and 5; P-034). Real streams in these formats are MEASUREMENT PENDING; for frames in a pixel format whose conversion `tests/test_vmaf_color.cpp` doesn't check against FFmpeg's, `mirage_vmaf`'s report warns (P-033). No bipbop rung is affected: all are yuv420p.
- D-016's rules for untagged inputs and for matrices libswscale can't convert stay as implemented (D-021); `mirage_vmaf`'s report records the FFmpeg libraries' run-time versions (P-035). D-019's boundary against the work order's "< 0.01" stays (D-023).
- bipbop's rungs were rendered separately, so these scores compare the two tools, not the rungs' quality (D-005, D-011).
- **Inputs whose frames don't line up.** Since D-028 the delta tool compares the two sides' pairs by their frames and timestamps (P-047). On the synthetic alignment cases, Mirage's pairing (D-028) and FFmpeg's framesync score other pairs, or score the pairs both score differently through libvmaf's motion features, and §5 passes only for the control and the later start in a container from 0 (`eval/consistency/align-cases-d028/`; P-048): a reason §5 names, frame alignment (difference 12 in `docs/PHASE1_DECISIONS_PENDING.md`). The committed runs before them keep their verdicts and scores with this comparison (`eval/run_all/2026-10-23-align/`; P-049).

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

## §1.3 Resolution alignment: done, bicubic by default (D-024)

- `mirage_vmaf --scaler bilinear|bicubic|lanczos`, and the report's `scaler` (P-017). Without it the scaler is bicubic since D-024, in `mirage_vmaf` and in the player's analysis, and `run_delta.sh` gives both sides bicubic unless told otherwise (P-038).
- **Measured** (`eval/consistency/scalers-300/`, first 300 frames of the four pairs, each scaler on both sides): Mirage matches FFmpeg with each of the three (`checks.txt`, check 3), and the scaler moves the scores: against bilinear, the mean shift per pair is 6.775801 to 11.113910 with bicubic and 8.874908 to 13.334836 with lanczos, and no frame scores lower (`scaler_effect.md`).
- **The default**, over the first 300 frames of the four pairs (`eval/consistency/defaults-300/checks.txt`; P-041): `run_delta.sh` without `--scaler` gave both sides bicubic, and the scores are those of `scalers-300/bicubic` on every frame (checks 2 and 3); `mirage_vmaf` without `--scaler`, run on its own, scores every frame as the run did (check 4).
- The work order suggests bicubic so that §5 passes; §5 passes with each of the three (difference 1).
- **Remaining**: the §5 report over every frame with bicubic (MEASUREMENT PENDING). The player has no scaler option, and its JSON export doesn't state the scaler (difference 9).

## §1.4 Frame alignment: done, pairing by timestamp in the analyzer and the delta tool

What the work order asks for, and where it is:

- **Pairing by PTS**, not by position: the analyzer that the player and `mirage_vmaf` share pairs the two inputs' frames by timestamp, as `docs/plans/frame-alignment.md` plans it and D-028 approves it: rule A, the reference's timeline, with a tolerance of half the reference's frame duration; times measured from each input's start, as `ffmpeg` measures them without `-copyts`; each frame with the nearest frame of the other input (`src/VMAFAlign.h`, `src/VMAFAnalyzer.cpp`; P-043, P-044).
- **Frame-rate mismatches, dropped and duplicated frames**: a distorted input at a lower frame rate is scored on its own grid, one at a higher rate on the reference's, the frames between the grid's skipped without failing; a reference frame no distorted frame pairs with, a distorted frame that pairs with no reference frame, a duplicated timestamp and a frame without a timestamp fail. At a timestamp that goes back, or forward by more than 10 s, pairing stops and the rest fails (D-028's option (a)). After a gap in the reference's frames libvmaf starts a new context, so that no motion feature spans it.
- **Reporting the failures**: `mirage_vmaf`'s report has the plan's `alignment` object, with the counts by kind, `failed`, `failure_rate` = failed / (paired + failed), the threshold and the list of every failed frame with its input, position, PTS and reason, and the libvmaf contexts (`src/VMAFReport.h`; P-044).
- **A warning above 1%, never a silent score**: above the threshold the pair is still scored, with a warning in the report, on stderr and in the player's VMAF panels, and `mirage_vmaf` exits with status 0 (D-028). The player's warning was built, not seen on screen.
- **Acceptance**, a test stream with a frame dropped on purpose for which the tool reports the failed frame instead of a wrong score: the committed delta run of the alignment case `a01_dropped_distorted` (`eval/consistency/align-cases-d028/checks.txt`, check 4; P-048). `mirage_vmaf`'s report lists the reference's frame 5 as an unmatched reference frame, its failure rate of 0.1 is above the threshold, and the warning is in the report and on stderr, with exit status 0; every pair's two frames have the same PTS, and the pairs away from the gap have the scores of the same frames in the control `a00_aligned`. Before D-028 `mirage_vmaf` paired the 4 distorted frames after the gap each with the reference frame one picture earlier and scored them 22.609365 to 24.440811 without a warning, where since D-028 the same distorted frames score 72.711420 to 74.886821 (`align-cases-before/checks.txt`, checks 4 and 5). The analyzer test "a stream with a frame dropped on purpose" (`tests/test_vmaf_analyzer.cpp`; P-045) checks the same on a clip it generates; a committed record of the unit test run is MEASUREMENT PENDING.
- **The §5 comparison by timestamp**: the delta tool matches the two sides' pairs by their frames and timestamps, FFmpeg's from `showinfo` filters in its graph, and reports what each side counted as failed (`eval/consistency/vmafdelta.py`; P-047). On the 9 synthetic alignment cases, scored before and after D-028, the reports show each place where FFmpeg's framesync and Mirage's rule A pair other frames; 2 cases pass, the control and the later start in a container from 0 (`eval/consistency/README.md`, "`align-cases-before/`, `align-cases-d028/`"; difference 12 in `docs/PHASE1_DECISIONS_PENDING.md`).
- **Frames that line up keep their pairs and scores**: the rerun of the committed 300-frame and synthetic runs after D-028 found every verdict and every per-frame score as committed (`eval/run_all/2026-10-23-d028/rerun_check.md`; P-046), and so did the rerun with the delta tool's comparison by timestamp (`eval/run_all/2026-10-23-align/rerun_check.md`; P-049). Over every frame of the four bipbop pairs every pair's two frames have the same PTS (`eval/consistency/acceptance-full/checks.txt`, check 6); the full-length rerun with the new pairing is MEASUREMENT PENDING.

**Remaining.**

- Where the inputs' frames don't line up, §5 fails by design, and the open items "The delta tool's comparison by timestamp" and "What the alignment cases show where the two rules differ" in `docs/PHASE1_DECISIONS_PENDING.md` ask the owner whether that should stay.
- The values D-028 leaves open (the frame rate's source, the tolerance's bound, the coarser grid's failures, the discontinuity limits, the boundary of #7) are the agent's (P-043), listed for the owner in `docs/PHASE1_DECISIONS_PENDING.md`, "Open after D-020".

## §1.5 Score pooling: done (D-027)

- `mirage_vmaf`'s report gives, at full precision, the mean, harmonic mean and minimum as libvmaf pools them, the minimum without the first frame (`min_without_first_frame`), and p1, p5, p10, p25 and p50 by linear interpolation (`pooled`, `src/VMAFReport.h`). The analyzer that the player shares computes them, so the player's p5 is the interpolated one too; `write_json`'s layout is unchanged. They are computed from the per-frame scores, as a pair that D-028 scores in several libvmaf contexts needs (`src/VMAFPool.h`; P-039, P-044).
- **Checked** on the first 300 frames of the four pairs with the defaults (`eval/consistency/defaults-300/checks.txt`; P-041): each report's mean and min are those libvmaf's `vmaf_score_pooled()` gave for the same per-frame scores, at full precision (check 5); its mean, min and harmonic mean are those of FFmpeg's libvmaf log at its 6 decimals (check 6); and all nine values are those `eval/pooling/pooling.py` computes from the per-frame scores, at full precision (check 7; `eval/pooling/defaults-300/`). The unit tests compare the mean, min and harmonic mean with libvmaf's own pooling at full precision (`tests/test_vmaf_pool.cpp`).
- **Remaining**: the player's own export keeps its Phase 0 layout, with the mean, min and p5 only (difference 9).
- **Measured before D-027** with `eval/pooling/pooling.py` (P-018, P-027): on Phase 0's full-length series and the first 300 frames after D-016 (`eval/pooling/bipbop16x9/`), and on both sides of every frame of the committed §5 report (`eval/pooling/acceptance-full/`). Over every frame after D-016, the minimum is the first frame's score in every series, the three percentile definitions differ by at most 0.000276, libvmaf's harmonic mean is the shifted form `n / sum(1 / (x + 1)) - 1`, and gear1's frames at 0, 53790 of 53946, leave the plain harmonic mean undefined (`eval/pooling/README.md`).

## §8 Reproducible packaging: partial

| Item | Status | Where |
|---|---|---|
| `LICENSE` | done: GPL-3.0-or-later (D-008) | `LICENSE`, `README.md`; whether legal or the company's open-source process has to confirm it is D-008's open question |
| Pinned dependencies with their versions recorded | partial: every version is recorded and the FetchContent dependencies are pinned; FFmpeg, libvmaf and GLFW come from Homebrew, which can't pin them | `docs/BUILD.md` "Versions", `CMakeLists.txt`, P-021; difference 4 |
| Docker or Nix to reproduce the evaluation | pending | #11c |
| Build instructions for Linux and macOS, Apple Silicon included | partial: macOS on Apple Silicon only | `docs/BUILD.md`; #11b, difference 5 |
| `eval/run_all.sh`, one script that reruns the paper's numbers | done for the committed Phase 1 measurements, the full-length §5 run included; its seven committed runs found everything as committed. There are no paper numbers yet (D-005) | `eval/README.md`, `eval/run_all/2026-10-23/`, `2026-10-23-full/`, `2026-10-23-fix/`, `2026-10-23-d022/`, `2026-10-23-d027/`, `2026-10-23-d028/` and `2026-10-23-align/`, P-024, P-025, P-027, P-032, P-037, P-042, P-046, P-049; difference 7 |
| `eval/streams.md` and a fetch script | partial: the bipbop and advanced Apple streams; the fetch keeps playlists, no media; DASH-IF and Unified Streaming streams not examined | `eval/streams.md`, P-023; difference 8 |
| **Acceptance**: a clean machine builds by the README | partial: two builds in clean worktrees of committed HEADs on the machine of the measurements; on it the release preset stops at glad with the default Python, the glad step the README's `macos-debug` quick start runs too, and `docs/BUILD.md`'s venv builds both presets | `eval/build_check/macos-2026-10-23-6a6661f.txt`, P-022; difference 6 |

Also open: whether glad's generated loader is committed (#11a), the README beyond its install line, and the root `vcpkg.json`, which the build doesn't use and which lists FFmpeg and GLFW without libvmaf or versions (`docs/VMAF_GAP_REPORT.md`, "Specific gaps for §8"; `docs/PHASE1_DECISIONS_PENDING.md`).

## The work order's engineering constraints

- **C++20 in the style of the surrounding code**, with unit tests for every new feature: Catch2 tests in `mirage_tests` for the C++ (`tests/test_vmaf_align.cpp`, `test_vmaf_analyzer.cpp`, `test_vmaf_cli.cpp`, `test_vmaf_color.cpp`, `test_vmaf_pool.cpp`, `test_vmaf_report.cpp`, `test_json_text.cpp`, `test_file_hash.cpp`), Python `unittest` suites for the eval scripts (`eval/tests/`, `eval/consistency/tests/`, `eval/pooling/tests/`, `eval/streams/tests/`). At `9855319` `mirage_tests '~[audio]'` passed in the main working tree at the first run, 90866 assertions in 244 test cases, and so did the four Python suites, 20, 85, 7 and 16 tests (`eval/test_runs/macos-2026-10-23-9855319.txt`; P-028, P-032). The code and tests changed after D-020 have no committed test record yet (MEASUREMENT PENDING).
- **Library APIs only**: as in §1.1.
- **Every configurable item as a command line option and in a config file**: `mirage_vmaf`'s options are on its command line and, since D-030, in a config file, `--config FILE`, read through the same option table, with the file's path and SHA-256 in the report (P-040). The eval tooling is exempt (D-030): each run's `run.cmd.txt` records every option. The player has no options for the scoring settings (difference 9).
- **No document edited to make the implementation and the work order agree**: the differences are listed at the end of `docs/PHASE1_DECISIONS_PENDING.md`.

Two unit tests of code outside Phase 1 could fail: until `97f0098`, in a clean checkout, `SidecarSubtitles — decodes data/sub_test/test.srt`, whose fixture `.gitignore` leaves out (`docs/BUILD.md`, step 4), which since then writes its fixture at run time as `SidecarSubtitles — decodes an SRT file` (D-032, P-036); and now and then `Demuxer: seamless video and audio switches on a local file`, which, run alone 650 times, failed 5 times in a build of `37472fd`, the commit Phase 1 started from, and 4 times in one of `855a71b`, each time at one of the same two assertions (`eval/test_runs/README.md`, "Repeated runs of one test"; P-031). Both are in `docs/PHASE1_DECISIONS_PENDING.md`, "Smaller items found in Phase 1".

## Before the Phase 1 PR can merge

- The owner's decisions in `docs/PHASE1_DECISIONS_PENDING.md`, and the reruns a changed default needs (§5 above).
- The owner's review of Phase 1's commits, and confirmation of the process choices P-006 to P-032 (`docs/DECISIONS.md`).
- The owner's confirmation of the Phase 1 rows of `docs/AI_CONTRIBUTIONS.md`.
- The PR itself, which the owner opens (hard rule 5 of the work order: one PR for the phase).
