# Phase 1: decisions pending for the owner

The design decisions of Phase 1's §1 and §8 work that are still open, each as a package: the question, what the code does today, what has been measured and where the outputs are, the options, and, marked as such, the agent's recommendation. The decisions are the owner's (hard rule 3 of the work order, `docs/DECISIONS.md` D-003, D-004, D-007). Every number below comes from a committed output, named next to it; where there is none, it says MEASUREMENT PENDING. Numbered items (#3 to #7, #11) are those of the gap report's "Decisions needed from the owner" (`docs/VMAF_GAP_REPORT.md`).

What the same work added without needing a decision, keeping every default as it was: `mirage_vmaf --scaler` (bilinear, bicubic, lanczos), `--model` and `--model-file` with the model's identifier in the report, a `reference_mode` field, both libvmaf versions of both sides in the §5 delta reports (D-006), and the measurements below. The process choices behind them are P-016 to P-019 in `docs/DECISIONS.md`.

## #3 The default scaler (D-003)

**Question.** Which swscale scaler brings the distorted frames to the reference's size by default, in the GUI and in `mirage_vmaf`, and so in the paper's scores?

**Today.** Bilinear, as since before Phase 0; every committed §5 run used bilinear on both sides, except the bicubic and lanczos runs of `scalers-300/` below. `--scaler` now chooses bilinear, bicubic or lanczos, and the report records it.

**Measured** (`eval/consistency/scalers-300/`, first 300 frames of the four pairs of D-018, each scaler on both sides; P-016):

- Mirage matches FFmpeg with each of the three scalers: every frame of every pair agrees at the 6 decimals of FFmpeg's log (`checks.txt`, check 3), and all three runs pass D-019.
- The scaler changes the scores a lot, on both sides alike. Pooled means, Mirage's side, from `scaler_effect.md`:

  | pair | bilinear | bicubic | lanczos |
  |---|---|---|---|
  | gear4_vs_gear5 | 74.640607 | 84.258060 | 83.843581 |
  | gear3_vs_gear5 | 53.729092 | 64.843002 | 67.063929 |
  | gear2_vs_gear5 | 28.552935 | 36.817663 | 39.024426 |
  | gear1_vs_gear5 | 0.006117 | 6.781918 | 8.881025 |

  No frame of any pair scores lower with bicubic or lanczos than with bilinear; on average the scores rise by 6.775801 to 11.113910 points with bicubic and 8.874908 to 13.334836 with lanczos, depending on the pair (`scaler_effect.md`).
- FFmpeg's scale filter given no `flags=` uses the flags of `flags=bicubic` (`eval/consistency/ffmpeg-scale-default/checks.txt`). So a plain `ffmpeg -lavfi` graph that scales without naming a scaler scores as Mirage does with `--scaler bicubic`, and differs from today's default.
- Not measured: the effect on a ladder encoded from one source (D-011's clips), and over every frame (MEASUREMENT PENDING).

**A difference from the work order** (hard rule 4; nothing was edited to hide it): §1.3 suggests bicubic as the default "so that the §5 consistency check can pass". The runs show that §5 passes with each of the three scalers when both sides use the same one; the scaler decides the scores, not whether the two tools agree.

**Options.**

1. Bilinear: today's default, and the setting of the committed §5 runs other than the scaler runs.
2. Bicubic: the work order's suggestion, and what FFmpeg's scale filter does when no scaler is named.
3. Lanczos.

Whatever the choice, the §5 runs give FFmpeg the same `flags=` (`run_delta.sh --scaler`), and every report states the scaler. The full-length §5 report of D-018 runs with whatever the default is when it is made.

**Also open, with the scaler.** `--scaler` sets the distorted frames' scaler only; the reference keeps its size and is only converted to 8-bit yuv420p, with bilinear flags whatever `--scaler` says (P-017). Every reference so far is already yuv420p. For a reference in another format, FFmpeg's §5 graph converts it with an automatically inserted scale filter; whether Mirage's conversion gives the same pixels is part of #5 and untested.

**Agent's recommendation.** Bicubic. It is the work order's suggestion and FFmpeg's own default when a scale filter names no scaler, so the paper's scores would be what a reader gets from a plain `ffmpeg` command with a plain `scale` filter. The continuity argument for bilinear is weak, because no score so far is paper data (D-005). Whichever is chosen, the paper should say which scaler, since the measured effect is several points.

## #4 The default model (D-004)

**Question.** Which libvmaf model scores pairs by default? The gap report also asks whether choosing the model from the resolution counts as a default.

**Today.** The built-in vmaf_v0.6.1. `--model` takes any built-in model of the linked libvmaf (this one has vmaf_v0.6.1, vmaf_v0.6.1neg, vmaf_4k_v0.6.1 among them), `--model-file` a model file, and the report gives `model`, `model_source` and `model_sha256` (P-017).

**Measured** (`eval/consistency/models-300/`, gear2 against gear5, first 300 frames, bilinear, each model on both sides; P-016):

| model | pooled mean (Mirage's side, `model_effect.md`) | frames up / same / down against vmaf_v0.6.1 |
|---|---|---|
| vmaf_v0.6.1 (built-in) | 28.552935 | |
| vmaf_v0.6.1neg (built-in) | 19.498402 | 0 / 0 / 300 |
| vmaf_4k_v0.6.1 (built-in) | 47.442375 | 300 / 0 / 0 |
| the package's `vmaf_v0.6.1.json` (`--model-file`) | 28.552935 | 0 / 300 / 0 |

- Every frame agrees between Mirage and FFmpeg at FFmpeg's 6 decimals with every model (`checks.txt`, check 3), and the model file scores as the built-in model does (check 6). This meets §1.1's acceptance: changing the model gives observably different scores, and the output names the model.
- FFmpeg's libvmaf filter's own default model is `version=vmaf_v0.6.1` (`eval/consistency/ffmpeg-libvmaf-filter/checks.txt`).
- Not measured: the models on other rungs, other content, or every frame.

**Options.**

1. vmaf_v0.6.1: today's default and FFmpeg's libvmaf filter default.
2. vmaf_v0.6.1neg: the variant whose features limit enhancement gain (FFmpeg's log names its features with `_egl_1`, `models-300/checks.txt`, check 5); on gear2 it scores every frame lower.
3. vmaf_4k_v0.6.1.
4. A model chosen from the reference's resolution. That needs a rule, with a resolution limit the owner sets, and the report would have to state which model each pair got.

**Agent's recommendation.** vmaf_v0.6.1 as the default: FFmpeg's libvmaf filter uses it unless told otherwise, and every committed §5 run used it apart from the other models' runs in `models-300/`. The other models stay options, recorded in each report; a rule by resolution can come later as an option, once the owner has set its resolution limit.

## #5 Bit depth

**Question.** At what bit depth are frames handed to libvmaf?

**Today.** Every input becomes 8-bit yuv420p before scoring (`src/VMAFAnalyzer.cpp`, `kScorePixFmt` and `kScoreBpc`), and the report states it (`scored_as`). FFmpeg's §5 graph does the same with `format=yuv420p` on both inputs, so the §5 comparison is of 8-bit scoring on both sides. A 10-bit input would be reduced to 8 bits by swscale on both sides: the distorted frames in the same step as their scaling, and the reference in Mirage's own conversion call and on FFmpeg's side in an automatically inserted scale filter. Whether the two sides then give the same bytes is not tested: no input scored so far has more than 8 bits (bipbop 16x9 is yuv420p, `eval/baseline/diagnostics/ffprobe_rungs.txt`).

**What the libraries take.** FFmpeg 8.1.1's libvmaf filter accepts 8-, 10-, 12- and 16-bit planar 4:2:0, 4:2:2 and 4:4:4 (`libavfilter/vf_libvmaf.c`, its format list), and libvmaf's `vmaf_picture_alloc()` takes a bit depth.

**Options.**

1. 8 bits for every input (today).
2. The inputs' own bit depth when both have more than 8 bits (the §5 graph would then use the matching `format=` on both sides).
3. Configurable, with 1 or 2 as the default.

HDR content (PQ or HLG transfer) is a further question: the models above are SDR models, and nothing in this work handles HDR.

**What a measurement needs.** A ladder of 10-bit SDR rungs encoded from one 10-bit source, which D-011's self-encoded ladders can provide; each rung scored at 8 and at its own bit depth, on both sides; and, for the §5 comparison, synthetic 10-bit clips run through `run_delta.sh` the way `eval/consistency/color_cases.py` makes 8-bit ones, to check the reference's conversion. No suitable public stream was at hand tonight (the streams this repository uses are 8-bit), so nothing was measured (MEASUREMENT PENDING).

**Agent's recommendation.** Keep 8 bits until D-011's ladders exist, then measure option 2 against option 1 on them; add the synthetic 10-bit check to the §5 work before any 10-bit input is scored.

## #6 Pooling definitions (D-004)

**Question.** How are `mean`, `harmonic_mean`, `min` and `p1` / `p5` / `p10` / `p25` / `p50` (work order §1.5) defined?

**Today.** `mirage_vmaf` reports libvmaf's pooled mean and min (`vmaf_score_pooled`); the player's analyzer also computes `vmaf_p5` as `sorted[n * 5 / 100]`, which `mirage_vmaf` leaves out (P-006). Nothing was changed.

**Measured** (`eval/pooling/bipbop16x9/pooling.md`, made by `eval/pooling/pooling.py` from the Phase 0 baseline's full-length per-frame scores and from both sides of `eval/consistency/diag-after-300/plain`; P-018; summary in `eval/pooling/README.md`):

- **The minimum is the first frame's score** in every series, as the gap report observed. In 1280x720, 960x540 and 640x360 the first frame is the only frame at the minimum; in 416x234 the minimum is 0, which 5976 frames of the full length and 294 of the first 300 score. Without the first frame the full-length minima are 73.1728, 48.4413 and 22.1403, where the minima are 69.7469, 45.1244 and 20.0523. In FFmpeg's logs the first frame's `integer_motion2` and `integer_motion` are 0, and in each rung it is the only frame of the first 300 with `integer_motion2` 0.
- **Percentile definitions**: today's rule (`sorted[floor(n*p/100)]`), nearest rank (`sorted[ceil(n*p/100) - 1]`) and linear interpolation (Hyndman and Fan's definition 7) differ by at most 0.0003 on the full-length series (4 decimals), and by up to 0.093440 (gear2, p1), 0.082396 (gear3, p1) and 0.071134 (gear3, p5) on 300 frames.
- **Harmonic mean**: libvmaf's harmonic mean in FFmpeg's logs is `n / sum(1 / (x + 1)) - 1` for all four rungs, at the log's 6 decimals; the plain `n / sum(1 / x)` differs (gear2: 28.434599 against 28.428817), and it is undefined when a frame scores 0, as 416x234's frames do.
- The analyzer's recorded `vmaf_p5` is today's rule, and its recorded min is the series' minimum, on every rung.
- Not measured: every frame after D-016 (the full-length §5 run of D-018; MEASUREMENT PENDING; the same script runs on it).

**Options.**

- Percentiles: today's rule; nearest rank; linear interpolation. libvmaf has no percentile pooling, so Mirage computes them in every option.
- Harmonic mean: libvmaf's (`VMAF_POOL_METHOD_HARMONIC_MEAN`, the shifted form, which FFmpeg's log also reports); the plain form, with a rule for frames at 0.
- Minimum: libvmaf's, over every frame (then it is the first frame's score, as above); or also, as a separately named value, the minimum without the first frame.
- Mean: libvmaf's mean, which the reports already carry and FFmpeg's log shows too.

**Agent's recommendation.** Linear interpolation for the percentiles, the default of common analysis tools, so readers recomputing them from the per-frame scores get the same values; libvmaf's harmonic mean, which is defined with frames at 0 and is what FFmpeg reports; libvmaf's minimum for parity with FFmpeg, with the first-frame effect stated in the paper and, if the owner wants a minimum that means something, a second value without the first frame, under its own name.

## #7 Frame alignment, and approval of the plan (§1.4, D-007)

The plan is `docs/plans/frame-alignment.md`. It asks for approval of pairing by timestamp in the analyzer that the GUI and `mirage_vmaf` share (D-007), and lists the choices it needs: the matching rule (the reference's timeline, or FFmpeg's framesync rule, whose defaults are in `eval/consistency/ffmpeg-libvmaf-filter/checks.txt`), the timestamp origin, frame-rate mismatches, discontinuities, what a gap does to libvmaf's motion feature, the report's fields, and #7 itself: the failure-rate definition, the threshold (the work order suggests 1%) and whether exceeding it warns or refuses. The plan ends with the agent's recommendation. Nothing of it is implemented; its acceptance test, the work order's stream with dropped frames, is part of the plan.

## §1.2 Reference modes

- **`reference_mode` of `mirage_vmaf`**: `explicit-pair`, because the command line names both inputs (P-017). The work order's §7 lists only `mezzanine` and `top-rung`; confirm the name, or choose another, such as letting the user declare one of the two.
- **Ladder scoring in `mirage_vmaf`** (top rung by default per D-002, a mezzanine optionally) is not built (P-019). It needs the owner's or §2's choices: which `EXT-X-STREAM-INF` entries are scored (audio-only entries, entries that share a video playlist, equal bandwidths), what a failed rung does to the run and its exit status given P-006, and how one report holds several pairs while keeping the two modes apart before §7's schema. **Agent's recommendation**: score each video playlist once, skip entries without video and list them as skipped, let one failed rung fail the run as P-006 does for a pair, and write one report per reference mode with a `pairs` list, so that mode A and mode B never share a report.
- **§1.2's acceptance**, both modes on one ladder with the difference documented, needs a ladder with a mezzanine: D-011's source clips (MEASUREMENT PENDING).

## The config file (the work order's engineering constraints)

The work order asks for every configurable item as a command line option and in a config file. `mirage_vmaf`'s options are all on its command line: `--ref`, `--dis`, `--out`, `--frames`, `--scaler`, `--model`, `--model-file`. Fixed in the code, not configurable: libvmaf's threads and subsampling, the 8-bit yuv420p scoring format (#5), and pairing in decode order (#7). The player gets no new flags.

**A design for the owner to choose from** (not built: the format is the owner's call):

- `mirage_vmaf --config FILE`, with the same names as the long options, one setting per line (`scaler = bicubic`), read through the same option table as the command line, so that both accept exactly the same settings; or JSON; or TOML, which needs a parser the repository doesn't have.
- The command line overrides the file.
- The report records the file's path and SHA-256 next to the effective settings, which it already records.
- For `run_delta.sh`, the same, or a waiver for the eval tooling, since `run.cmd.txt` records every option of a run.

**Agent's recommendation**: the `name = value` lines through the option table, with the command line overriding the file and the file's SHA-256 in the report: no new dependency, and nothing that can be set one way but not the other.

## #11 The rest of the build strategy (§8, D-009)

D-009 standardized FFmpeg on ffmpeg-full and left two parts of #11 open: whether glad's generated output is committed, and the scope of Linux support. The work order's §8 also asks for a Docker or Nix environment to reproduce the evaluation, which needs both answers. Nothing of this is implemented. What the §8 work that needed no decision did is in `docs/BUILD.md`, `eval/streams.md` and `eval/README.md`; the process choices behind it are P-020 to P-025 in `docs/DECISIONS.md`.

### #11a Is glad's generated output committed?

**Today.** glad 2.0.8 (`CMakeLists.txt`, pinned by tag) generates the OpenGL 4.1 core loader while the player builds, with the Python interpreter CMake found when it configured (`glad_add_library(glad_gl_core_41 STATIC REPRODUCIBLE API gl:core=4.1)`). On the machine of the build checks, Homebrew's Python 3.14.7 can't import `pyexpat`, so the build stops at that step with the default Python, and the venv of `docs/BUILD.md` step 3 (Python 3.13 with jinja2 and MarkupSafe pinned) avoids it (P-003; both records in `eval/build_check/`). `mirage_vmaf` and `mirage_tests` don't use OpenGL and build without the venv.

**Measured** (`eval/build_check/macos-2026-10-23-6a6661f.txt`, "Generated glad files, release vs debug"):

- glad generates three files: `include/KHR/khrplatform.h` (11131 bytes), `include/glad/gl.h` (872844 bytes) and `src/gl.c` (623188 bytes), with their SHA-256 in the record.
- The debug and the release build generate identical files, and P-003 found them identical to the main working tree's build output.
- `gl.h` says `Loader generated by glad 2.0.8 on -` (no date, because of `REPRODUCIBLE`) and `SPDX-License-Identifier: (WTFPL OR CC0-1.0) AND Apache-2.0`.

**Options.**

1. Keep generating at build time, with the venv documented (today). Every build of the player needs Python with jinja2, and a Homebrew Python update can break the step again as Python 3.14.7 did.
2. Commit the three generated files, for example under `third_party/glad/`, build them as a plain static library, and keep a script that regenerates them with glad 2.0.8 in the pinned venv and compares the bytes. The build then runs no Python. The repository gains the three files (sizes above), and their license terms (WTFPL or CC0-1.0, and Apache-2.0 for what comes from the Khronos specification, as `gl.h` states) have to be stated next to them; how that sits with GPL-3.0-or-later belongs with D-008's open question, whether legal or the company's open-source process has to confirm the license.
3. Pin the generation environment instead: have CMake create the venv itself at configure time with the pinned packages, or refuse a Python newer than one known to work. The build stays dependent on Python and, for a venv, on PyPI at configure time.

**Agent's recommendation.** Option 2. The glad step is the only build step that failed in either build check, the files it generates are the same in every build so far, and committing them removes Python from the player's build.

### #11b How far does Linux support go?

**Today.** Only macOS on Apple Silicon is built and documented:

- Both presets of `CMakePresets.json` put `/opt/homebrew/opt/ffmpeg-full/lib/pkgconfig` on `PKG_CONFIG_PATH`.
- `CMakeLists.txt` configures every target at once: OpenGL and GLFW are required, and nativefiledialog-extended v1.2.1 requires GTK 3 on Linux, or D-Bus with its portal option (its `src/CMakeLists.txt`), even when only `mirage_vmaf` is to be built. Apple frameworks (CoreVideo, IOSurface) are linked under `if(APPLE)`.
- The player has macOS-specific code: VideoToolbox decoding (`src/Decoder.cpp`) and the IOSurface texture path (`src/VideoRenderer.cpp`, under `#ifdef __APPLE__`).
- The libvmaf package version that D-006 records is read from a Homebrew Cellar path (`CMakeLists.txt`, `eval/consistency/run_delta.py`); elsewhere it would read `unknown`.
- The eval scripts take the FFmpeg, ffprobe and model directory as options, with Homebrew paths as defaults, and `run_delta.py` finds linked libraries with `otool -L` or `ldd`.

Nothing has been built or run on Linux, so whether the code compiles there, which FFmpeg 8 and libvmaf 3 builds a distribution offers, and whether the scores equal those measured on Apple Silicon are all MEASUREMENT PENDING.

**Options.**

1. macOS only, stated in the paper. This differs from §8, which asks for build instructions for Linux and macOS.
2. Headless Linux: `mirage_vmaf`, `mirage_tests` and the eval tooling on Linux; the player stays macOS-only. Needs a CMake option that configures without the player's dependencies (OpenGL, GLFW, glad, Dear ImGui, nativefiledialog-extended), a Linux preset, Linux instructions in `docs/BUILD.md`, and a §5 run on Linux.
3. Full Linux support, the player included: an OpenGL 4.1 core context (for example through Mesa), GLFW, nativefiledialog-extended with GTK 3 or its portal backend, and software decoding where VideoToolbox is missing.

**Agent's recommendation.** Option 2. Every committed score comes from `mirage_vmaf` and the eval tooling, which use only FFmpeg and libvmaf; the player isn't needed to reproduce a number, and a headless build is what a Docker image (#11c) can run.

### #11c Docker or Nix?

**Today.** Neither exists. §8 asks for one of them to reproduce the evaluation.

**What the build checks show.** Homebrew can't install the versions the measurements used (`docs/BUILD.md`, step 1, and the help of `brew pin` and `brew extract` in `eval/build_check/macos-2026-10-23-6a6661f.txt`), and its current ffmpeg-full, libvmaf and GLFW are already newer than those (the same record, "installed vs current stable").

**Options.**

1. A Docker image for Linux: a base image pinned by digest; FFmpeg 8.1.1 and libvmaf built from release sources checked by SHA-256, the versions of the committed measurements; `mirage_vmaf` built from the repository; `eval/run_all.sh` as the command it runs, whose comparison says whether the container reproduces the committed scores. Needs #11b option 2 or 3, and doesn't cover the macOS build or the player.
2. A Nix flake: every dependency pinned through one nixpkgs revision, on macOS (aarch64-darwin) and Linux, so the macOS build could be reproduced as well. Needs a nixpkgs revision, or overrides, with the FFmpeg and libvmaf versions wanted. Not examined here.
3. Both.
4. Neither: the versions recorded in `docs/BUILD.md`, with `eval/run_all.sh` to show whether another environment reproduces the scores.

**Agent's recommendation.** Option 1, with #11b option 2. A container can build FFmpeg and libvmaf at exactly the versions of the measurements, which Homebrew can't, and `eval/run_all.sh` already reruns and compares everything without a display. Whether the container's scores equal those measured on Apple Silicon would be its first measurement.

## README.md beyond the brew line

**Question.** The §8 work changed `README.md` only where it had to: the brew line now installs ffmpeg-full (D-009), a note points to `docs/BUILD.md`, and a License section states GPL-3.0-or-later (D-008). Other parts describe an older state of the repository. Should they be brought up to date, and how?

**What differs from the repository today.**

- "Running tests" counts 34 tests covering `NetworkLogger` and `ManifestParser`, run with `ctest`; `mirage_tests` is now built from 26 test files (`CMakeLists.txt`), and `docs/BUILD.md` step 4 runs it without the `[audio]` tests, as the build checks do.
- "First configure takes ~1–2 minutes on a fast connection" rests on no committed record. The configures of the verification run of `eval/run_all.sh` took 83.3 s and 97.0 s ("cmake --preset macos-release" in `eval/run_all/2026-10-23/run_all.md`), on one machine and connection.
- "libvmaf model not found at runtime" says the model is loaded from a file under Homebrew's prefix; the analyzer loads built-in models by version (`vmaf_model_load` in `src/VMAFAnalyzer.cpp`) and reads a file only with `--model-file`.
- The Python note suggests `pip3 install jinja2 --break-system-packages`, which installs into the Python that Homebrew manages; the build checks used the venv of `docs/BUILD.md` step 3.
- The project tree lists two test files, and neither `tools/`, `docs/` nor `eval/`.

**Options.**

1. Correct these parts from the repository's files and the committed records.
2. Make the README's build part a short pointer to `docs/BUILD.md`, and correct the rest.
3. Leave the README as it is.

**Agent's recommendation.** Option 2: one place for the build steps that the build checks verify, and a README whose facts match the repository.

## Differences between the work order and the implementation found in this work (hard rule 4)

Reported, not edited:

1. §1.3 gives §5 passing as the reason for bicubic; §5 passes with all three scalers (#3).
2. §7's `reference_mode` has only `mezzanine` and `top-rung`; `mirage_vmaf` writes `explicit-pair` (§1.2 above).
3. §5's command, `ffmpeg -i <distorted> -i <reference> -lavfi libvmaf=model=<same>`, has no scale filter, and FFmpeg's libvmaf filter refuses inputs of different sizes (`input width must match`, `eval/consistency/ffmpeg-libvmaf-filter/checks.txt`, check 4). The §5 tool adds `scale=W:H:flags=<scaler>` to the distorted input, as Phase 0's diagnostic did (P-005, P-008).
4. §8 asks for FFmpeg 8.x, libvmaf 3.x, OpenGL 4.1, miniaudio and GLFW to be pinned, with their exact versions recorded. The versions are recorded (`docs/BUILD.md`, "Versions"), miniaudio is pinned by commit, and the generated loader fixes OpenGL at 4.1 core; FFmpeg, libvmaf and GLFW come from Homebrew, which can't pin them (#11c).
5. §8 asks for build instructions for Linux and macOS, Apple Silicon included. Only macOS on Apple Silicon is documented and checked (#11b).
6. §8's acceptance is a clean machine building as the README says. On the machine of the build checks, the README's quick start stops at glad with the default Python; the README now points to `docs/BUILD.md`, whose steps build both presets; and the check ran in a clean worktree, not on a clean machine (P-022).
7. §8 has `eval/run_all.sh` rerun all the paper's numbers and figures. There are none yet (D-005), so it reruns the committed Phase 1 measurements (P-024).
8. §8 asks for `eval/streams.md` with a fetch script, and lists DASH-IF test vectors and Unified Streaming demo streams. `eval/streams/fetch_streams.py` fetches the playlists and probes the streams but keeps no media (P-023), and the DASH-IF and Unified Streaming streams were not examined (`eval/streams.md`).
