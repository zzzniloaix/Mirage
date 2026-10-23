# Decision Log

Purpose: material for the paper's design-rationale section, and the owner's basis when defending the design (see hard rule 4 of the VMAF work order).

Each entry records the decision, the alternatives, the rationale, who decided and the date.

**Only the owner makes design decisions** (hard rule 3 of the work order). The agent only makes measurement and process choices. Those entries are listed separately below and stay marked as pending until the owner confirms them.

---

## Design decisions (owner)

The remaining open items are listed in the "Decisions needed from the owner" section of `docs/VMAF_GAP_REPORT.md`.

### D-001 Order within Phase 1: the §5 consistency check comes first

- **Decision**
  - The work order's Phase 1 = §8 + §1 + §5, submitted as one PR as the work order requires.
  - Order within Phase 1:
    1. §5: build a per-frame delta tool comparing Mirage with FFmpeg's libvmaf filter, and find the cause of the discrepancy Phase 0 found (640x360, first 300 frames, mean |Δ| 5.28, Mirage higher on every frame). Alongside it, do only the minimal §8 needed to reproduce runs: document the Python 3.13 venv approach to glad generation (see P-003).
    2. §1: the VMAF core (PTS pairing, configurable model / scaler, etc.).
    3. §8: full reproducible packaging.
    4. Re-run §5 and commit the delta report to `eval/`.
  - Phase 2 onward follows the work order's dependency order. Phase 2 doesn't start until §5 passes.
- **Alternatives**
  - §8 first: pin the build environment before any measurement. The cost is that VMAF correctness is delayed, and #10 (license) and #11 (build strategy) have to be decided first.
  - §1 first: finish the analyzer core, then verify it with §5. The cost is building on an unexplained 5-point discrepancy, and #2–#7 have to be decided first.
  - Split Phase 1 into three PRs, 1a / 1b / 1c. Not chosen: it conflicts with hard rule 5 of the work order (one PR per phase).
- **Rationale**
  - Every score in the paper depends on §5 passing, and the work order also asks for §5 to be done early.
  - The cause of the discrepancy may directly decide #3 (the default scaler).
- **Known cost**: §1's changes may shift the scores again, so §5 has to be re-run after §1. The Phase 1 PR will be large.
- **Correction**: the first draft of this entry (2026-10-22) listed §1 and §8 as Phase 2 and Phase 3, which doesn't match the work order's phases. The cause was the agent's wording of the options when asking, not the owner's decision. After reading the work order, the owner confirmed the current wording.
- **Who**: owner (Jiayu Lin)
- **Date**: 2026-10-22

### D-002 Default reference mode: top-rung (mode B)

- **Decision**: §1.2's default reference is top-rung, i.e. the highest-bandwidth variant. The mezzanine reference (mode A) is also implemented, as the work order requires, as an optional mode.
- **Alternatives**: default to mezzanine (mode A), which scores every variant against the external source material.
- **Rationale**: the owner gave no reason when choosing; to be added by the owner.
- **Implications** (recorded by the agent for the owner to check)
  - Default scores measure relative degradation within the ladder, not absolute quality. The paper has to make this clear (work order §1.2).
  - Consistent with the current code's behavior (the existing ladder scoring only supports top-rung).
  - Public streams rarely come with a mezzanine. With top-rung as the default, they can be scored directly.
  - The output still has to carry a `reference_mode` field, and scores from the two modes must not be mixed in one report.
  - In the default mode, the other rungs must be scaled encodes of the same source as the top rung, or the scores mean nothing. bipbop 16x9 doesn't satisfy this (see "Baseline" in the gap report), which makes the ladder choice in #14 more important.
- **Who**: owner (Jiayu Lin)
- **Date**: 2026-10-22

### D-003 Default scaler deferred until the §5 diagnosis

- **Decision**: §1.3's default scaler is not decided yet. During the §5 diagnosis, bilinear and bicubic are each measured on both the Mirage side and the FFmpeg side, and the owner decides after seeing the measurements. Until then the code keeps its current bilinear.
- **Alternatives**
  - Decide bicubic now (the suggestion in work order §1.3).
  - Decide bilinear now (the current code's behavior).
- **Rationale**: the 5-point discrepancy measured in Phase 0 was obtained with bilinear on both sides, so it doesn't necessarily come from the scaler itself. Choosing a default before seeing the scaler's actual effect on scores would have no data behind it.
- **Who**: owner (Jiayu Lin), adopting the agent's suggestion
- **Date**: 2026-10-23

### D-004 #4–#8 deferred until their sections start

- **Decision**
  - #4 default model, #5 bit-depth handling, #6 pooling definitions, #7 frame-alignment failure threshold: decided when §1 starts.
  - #8 default backpressure policy: decided when Phase 4 (§4) starts.
  - During the §5 comparison the current settings (`vmaf_v0.6.1`, 8-bit YUV420P) stay, and the code isn't changed for the comparison.
- **Alternatives**: decide each one now.
- **Rationale**: all of these change scores. As with D-003, they are decided once there's measured data.
- **Who**: owner (Jiayu Lin), adopting the agent's suggestion
- **Date**: 2026-10-23

### D-005 Phase 0 baseline scores are a record of the current state only, not paper data

- **Decision**: the scores in `eval/baseline/` serve only as Phase 0's record of the current state. Every number in the paper comes from runs made after §5 passes.
- **Alternatives**: put the baseline scores in the paper, e.g. as a before-the-fix comparison, with the reason stated.
- **Rationale**: under equivalent conditions Mirage and FFmpeg libvmaf differ by about 5 points, and the cause hasn't been found (see "Baseline" in the gap report).
- **Who**: owner (Jiayu Lin), adopting the agent's suggestion
- **Date**: 2026-10-23

### D-006 libvmaf version: record both the package version and the runtime version

- **Decision**: the JSON output and the §5 delta report record both libvmaf's package version (3.1.0 from Homebrew on this machine) and the value returned by `vmaf_version()` at runtime (3.0.0 on this machine).
- **Alternatives**
  - Record only the runtime version (the literal requirement of §1.1).
  - Record only the package version.
- **Rationale**: the two disagree on this machine. Recording both is the only way to tell which build was used when reproducing.
- **Who**: owner (Jiayu Lin), adopting the agent's suggestion
- **Date**: 2026-10-23

### D-007 Any refactor touching the render or decode path needs a plan first

- **Decision**: for any refactor that changes the render or decode path (e.g. §4's scoring of played frames, or moving the analyzer out of `MediaSession`), the agent first writes a plan for the owner to approve, and changes code only after approval.
- **Alternatives**: do small refactors directly and explain them in the PR; only large ones need a plan first.
- **Rationale**: hard rule 3 of the work order.
- **Who**: owner (Jiayu Lin), adopting the agent's suggestion
- **Date**: 2026-10-23

### D-008 Mirage is licensed GPL-3.0-or-later

- **Decision**: Mirage's license is GPL-3.0-or-later. The LICENSE file is added in the §8 part of Phase 1.
- **Alternatives**
  - GPL-2.0-or-later (the CFP's example is GPLv2; when distributed together with the GPLv3 FFmpeg, GPLv3 applies).
  - BSD / MIT (permissive source, but a binary that includes ffmpeg-full is still GPLv3 as a whole).
  - Wait for legal or the company's open-source process before deciding.
  - GPLv2-only isn't an option: it's incompatible with the linked GPLv3 FFmpeg build.
- **Rationale**: the ffmpeg-full 8.1.1 that Mirage links is GPL-3.0-or-later (`--enable-gpl --enable-version3`, see item 9 of the gap report). Choosing the same license keeps the terms for source and binary distribution simplest.
- **Open**: whether legal or the company's open-source process has to confirm this is for the owner to check.
- **Who**: owner (Jiayu Lin)
- **Date**: 2026-10-23

### D-009 FFmpeg is standardized on ffmpeg-full

- **Decision**: the build standardizes on Homebrew's ffmpeg-full. `CMakePresets.json` already points to it. The `brew install ffmpeg` in README / CLAUDE.md is corrected in the §8 part of Phase 1.
- **Alternatives**
  - Standardize on `ffmpeg`: change the presets to match the README. The §5 comparison would then need a separately installed FFmpeg with libvmaf, and the baseline would have to be re-run.
  - Defer the decision to §8.
- **Rationale**
  - ffmpeg-full has the libvmaf filter, which the §5 comparison needs.
  - The Phase 0 baseline was run with ffmpeg-full.
  - It's consistent with D-008's GPL-3.0-or-later.
- **Not decided**: the other two parts of #11 (whether glad's generated output is committed, and the scope of Linux support) are decided when §8 starts.
- **Who**: owner (Jiayu Lin)
- **Date**: 2026-10-23

### D-010 Merge fix/high-speed-playback into main first; VMAF work goes to main through PRs

- **Decision**
  - Fast-forward `main` to `fix/high-speed-playback` (`a4e0289`).
  - Every VMAF phase branch gets a PR into `main`. `vmaf/phase-0` is already based on `a4e0289` and needs no rebase.
  - The FairPlay changes stay on the local branch `feat/fairplay-drm` and are not pushed (the owner's instruction of 2026-10-22).
- **Alternatives**
  - PR into `fix/high-speed-playback` and leave `main` alone.
  - Open a long-lived vmaf branch and merge it into `main` before submission.
- **Rationale**: `main` was 6 commits behind `a4e0289` and could fast-forward. With `main` as the target, a VMAF PR's diff shows only VMAF changes.
- **Who**: owner (Jiayu Lin)
- **Date**: 2026-10-23

### D-011 The evaluation uses self-encoded ladders

- **Decision**: the evaluation encodes its own HLS ladders from public source clips, with the source clip serving as the mezzanine. The encoding scripts and parameters go in `eval/`. The owner picks the specific clips and encoding parameters when encoding starts.
- **Alternatives**
  - Pick existing public streams (DASH-IF, Unified Streaming demos, etc.): no mezzanine is available, and the streams may change or go offline.
  - Use both: self-encoded ladders for absolute quality and the §3 attribution tests, public streams to demonstrate applicability.
- **Rationale**
  - All rungs come from one source clip. That meets the precondition of D-002 (top-rung reference by default) and also allows mezzanine mode.
  - Controllable and reproducible, and test cases such as dropped frames and discontinuities can be built deliberately.
  - The rungs of bipbop 16x9 used in Phase 0 were rendered separately and are unsuitable for measuring quality (see "Baseline" in the gap report).
- **Who**: owner (Jiayu Lin)
- **Date**: 2026-10-23

### D-012 Rewrite all history; commit emails become the GitHub noreply address

- **Decision**
  - Change the author / committer email of every commit to `217987193+zzzniloaix@users.noreply.github.com`, and force-push `main` and `fix/high-speed-playback`.
  - Future commits in this repository use the same address (repository-level git config).
- **Alternatives**: change only future commits and leave the pushed history alone.
- **Rationale**: keep personal email addresses out of the public history. The repository was public from 2026-04-03, was made private on 2026-10-23, and will be made public again for the submission.
- **Implications**
  - Every commit SHA changed. Each of the 25 pushed commits was checked by tree hash, and no file tree changed at all.
  - Old → new: the baseline commit `a4e02895d2d9c51c620e6c140f5566944c80ec71` → `1bab7a3104cf9149300eafd3d3deb7ae4a9c6f69`, with tree `61765e415978c5a9735a3a6a3e245c52d0e6be99` unchanged. Every `a4e0289` in the gap report, in this log (P-001, D-010) and in the raw outputs in `eval/` refers to this commit. The raw outputs are kept as they are and are not changed to the new SHA.
  - The 7 commits of `vmaf/phase-0` were also rewritten per D-013 and D-014, so their trees did change.
- **Who**: owner (Jiayu Lin)
- **Date**: 2026-10-23

### D-013 The work order stays out of the repository

- **Decision**: the work order (`mirage-vmaf-spec-agent.md`, "Mirage VMAF Subsystem — Agent Work Order") is kept only locally by the owner and isn't committed. The copy that was under `docs/spec/` has been removed from the entire history of `vmaf/phase-0`. Documents in the repository refer to it by title and file name.
- **Alternatives**
  - Commit it with the passages unsuitable for publication removed, noting that it was edited.
  - Commit it as is.
- **Rationale**: the work order contains the owner's personal information and isn't suitable for a repository that will be public.
- **Who**: owner (Jiayu Lin)
- **Date**: 2026-10-23

### D-014 Local machine details redacted in the Phase 0 raw outputs

- **Decision**: local machine details in `eval/baseline/` and in the gap report are replaced with placeholders, and nothing else changes.
  - `<worktree>`: the path of the clean worktree used to build the baseline in Phase 0 (P-001).
  - `<repo>`: the path of this repository on the owner's machine.
  - `<tmp>`: the per-user temporary directory the system generates.
  - `<hostname>`: the machine name (in the output of `uname -a`).
  - The scratch directory hard-coded in `run_baseline.sh` is now read from the environment variable `SCRATCH`.
  - Item 9 of the gap report dropped a hostname that shouldn't be public.
- **Files affected**: `environment.txt`, `code_facts.txt`, `run_baseline.sh`, `diagnostics/ffmpeg_libvmaf_gear1_vs_gear5_first300.cmd.txt`, `diagnostics/ffmpeg_libvmaf_gear2_vs_gear5_first300.cmd.txt`, `docs/VMAF_GAP_REPORT.md`. No score, log or other output changed.
- **Checksums**: in `diagnostics/SHA256SUMS`, only the lines for the two cmd files were updated; every other line is byte-for-byte unchanged. After the rewrite, `shasum -a 256 -c` passes in both directories.
  - `ffmpeg_libvmaf_gear1_vs_gear5_first300.cmd.txt`: `92ae786fad941303c5a0128c7a4cda0d14948e039f628663def57e3adbcae66f` (before redaction) → `555e64e875918da3a237b6f790b579d049acfe05158566c50f7a024bab197b3e`
  - `ffmpeg_libvmaf_gear2_vs_gear5_first300.cmd.txt`: `13001dca0964b86eb7651cfa171649d15b4a27f4d2ec47ca1fc6ff6af14bd318` (before redaction) → `2050a5d83277c54a5bd386326cc272dddc50bf3fe47073d0c5bd144f3b9ce939`
- **Originals**: the unredacted originals are kept in the owner's private local backup and aren't committed.
- **Alternatives**: keep the outputs as they are.
- **Rationale**: the machine name and local paths are irrelevant to reproduction and shouldn't be in a public repository.
- **Who**: owner (Jiayu Lin)
- **Date**: 2026-10-23

### D-015 Phase 0 signed off

- **Decision**
  - The owner accepts Phase 0's gap report and baseline, and confirms P-001–P-005.
  - Phase 1 can start. For its scope and order, see D-001.
  - Phase 0 is submitted to `main` as one PR, per hard rule 5 of the work order (D-010).
- **Alternatives**: read the whole gap report before signing off.
- **Who**: owner (Jiayu Lin)
- **Date**: 2026-10-23

### D-016 Color matrix and range mismatches: convert like FFmpeg

- **Decision**: when the distorted stream's color matrix or range differs from the reference's, Mirage converts the distorted frames to the reference's color matrix and range before scoring, as FFmpeg's filter graph does. Untagged streams are handled the way FFmpeg handles them. The agent reports what that means in practice for the owner to confirm.
- **Alternatives**
  - Keep scoring the decoded YUV values unchanged (the behavior until now; libvmaf's own `vmaf` tool does the same with raw files). The §5 comparison would then relabel FFmpeg's input with `setparams` so that FFmpeg skips its conversion, and document that step.
  - Make it configurable, with a default chosen after more measurement.
- **Rationale**: Mirage then matches a plain `ffmpeg -lavfi libvmaf` run without workarounds.
- **Background**: on 2026-10-23 a scratch probe located the cause of the Phase 0 discrepancy. bipbop's 640x360 and 416x234 rungs are tagged `smpte170m` (BT.601), while the gear5 reference is tagged `bt709` (`eval/baseline/diagnostics/ffprobe_rungs.txt`). FFmpeg's verbose log shows its `scale` filter converting the distorted frames (`csp:smpte170m` → `csp:bt709`); Mirage didn't convert. The probe's outputs weren't committed, so its numbers aren't recorded here. The §5 work reproduces the effect with committed outputs (MEASUREMENT PENDING).
- **Implication**: Mirage's scores change for every pair whose tags differ, such as bipbop's 640x360 and 416x234 rungs.
- **Who**: owner (Jiayu Lin)
- **Date**: 2026-10-23

### D-017 Headless scoring through a separate mirage_vmaf tool

- **Decision**: Mirage's scorer runs without the GUI through a separate small executable, `mirage_vmaf`, built from the same `VMAFAnalyzer.cpp`. The player isn't changed for this.
- **Alternatives**: a `--vmaf-score <ref> <dis>` mode in the main `mirage` binary. It would have to branch before GLFW starts, because `main()` creates the window before it parses most flags.
- **Rationale**: it leaves the player untouched, §6's bench / sweep modes can live there later, and the repository already has a standalone of this kind (`snapshot_diff`).
- **Who**: owner (Jiayu Lin)
- **Date**: 2026-10-23

### D-018 Inputs of the committed §5 delta report

- **Decision**: the committed §5 delta report covers the four video rungs of Apple bipbop 16x9 (1280x720, 960x540, 640x360, 416x234), each scored against gear5 (1920x1080), over every frame.
- **Alternatives**
  - The same rungs, first 300 frames only.
  - Both: 300 frames while iterating, full length as the acceptance output.
- **Rationale**: it matches the Phase 0 baseline and gives the most complete evidence.
- **Who**: owner (Jiayu Lin)
- **Date**: 2026-10-23

### D-019 §5 pass threshold

- **Decision**: a frame fails the §5 check if |Mirage − FFmpeg| > 0.01. The delta report also gives the maximum and mean per-frame |Δ| and the number of frames over the threshold, as the work order asks.
- **Alternatives**: a stricter threshold, e.g. 0.001. That is measurable, because Mirage will write scores at full precision.
- **Rationale**: it is the work order's §5 target.
- **Who**: owner (Jiayu Lin)
- **Date**: 2026-10-23

---

## Measurement / process choices (proposed by the agent, confirmed by the owner)

### P-001 The Phase 0 baseline builds the committed HEAD in a clean worktree

- **Choice**
  - In the directory created by `git worktree add --detach <scratch>/wt-head HEAD`, build commit `a4e0289` with `cmake --preset macos-release`.
  - Run the existing scoring feature with the resulting binary.
- **Alternatives**
  - Use the debug build of the main working tree directly.
  - `git stash` first, then build.
- **Rationale**
  - The main working tree has uncommitted FairPlay/AVFoundation changes that aren't part of the survey. A worktree guarantees that what's measured is a citable commit, and it leaves the working tree alone.
  - Release was chosen only to shorten the run: libvmaf and FFmpeg are prebuilt libraries, and Mirage's own code only moves and orders frames without taking part in computing scores. This is an inference from reading the code, not verified by measurement.
- **Who**: agent (Claude)
- **Date**: 2026-10-21
- **Status**: confirmed by the owner (2026-10-23)

### P-002 Apple bipbop 16x9 as the baseline ladder

- **Choice**: `https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/bipbop_16x9_variant.m3u8`
- **Alternatives**
  - Apple's advanced sample `img_bipbop_adv_example_ts/master.m3u8`. It has 24 `EXT-X-STREAM-INF` entries (bipbop 16x9 has 6) pointing to 8 video playlists, at two frame rates, 30 and 60. The existing scorer is serial and scores every entry one by one, including duplicate entries that point to the same playlist. It wasn't chosen as the Phase 0 baseline; it's better suited to later, dedicated tests of frame-rate mismatches and duplicate entries. The original text of both masters is in `eval/baseline/inputs/`.
  - Other public streams, left for Phase 1's `eval/streams.md`.
- **Rationale**
  - Hosted by Apple, at a stable address.
  - 5 video variants plus 1 audio-only variant, which shows how the existing tool behaves when it meets an audio-only variant.
  - The variant counts and durations above all come from actually fetching the master and variant playlists (see `eval/baseline/README.md`).
- **Who**: agent
- **Date**: 2026-10-21
- **Status**: confirmed by the owner (2026-10-23)

### P-003 glad code generation through a scratch venv (Python 3.13 + jinja2 3.1.6)

- **Choice**: configure the worktree with `-DPython_EXECUTABLE=<scratch>/venv-glad/bin/python`.
- **Alternatives**
  - Fix Homebrew's Python 3.14. That changes the machine's environment and needs the owner's decision.
  - Copy the glad files already generated in the main repository. Tried, and it didn't work: ninja still insisted on re-running the generation step.
- **Rationale**
  - On this machine, importing `pyexpat` in Homebrew Python 3.14.7 fails (`Symbol not found: _XML_SetAllocTrackerActivationThreshold`), so a clean build fails at the glad generation step.
  - The venv doesn't change the system environment. The generated `gl.h` / `gl.c` / `khrplatform.h` are byte-for-byte identical to the main repository's build output (checked with `cmp`).
- **Who**: agent
- **Date**: 2026-10-21
- **Status**: confirmed by the owner (2026-10-23). This is itself a §8 reproducibility issue; see the gap report.

### P-004 Letting the existing code write its JSON report: pre-creating URL-shaped directories

- **Choice**: pre-create `https:/devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/` under the run directory.
- **Alternatives**
  - Don't create them. The existing code's write to the relative path `<URL>.vmaf.json` would fail, and the baseline would have only logs.
  - Change the code. Forbidden in Phase 0.
- **Rationale**: get real output from the existing export feature without changing code.
- **Who**: agent
- **Date**: 2026-10-21
- **Status**: confirmed by the owner (2026-10-23). The export-path problem is listed separately in the gap report.

### P-005 Diagnostic comparison of the baseline scores: FFmpeg libvmaf filter, first 300 frames, two rungs

- **Choice**
  - Use the `libvmaf` filter of ffmpeg-full 8.1.1 to score the first 300 frames of the 640x360 and 416x234 rungs, then compare with Mirage's per-frame scores.
  - Keep the conditions as close to Mirage's as possible: model `vmaf_v0.6.1`, `n_threads=4`, the distorted video upscaled with `scale=1920:1080:flags=bilinear`, the reference used as is.
- **Alternatives**
  - No comparison, only the baseline numbers. The drawback is that there would be no way to judge whether 416x234's 1.1455 is credible.
  - Compare the whole clip. Too slow, and beyond Phase 0's scope; that belongs to §5.
- **Rationale**
  - Whether the baseline scores can be trusted is a question the gap report has to answer.
  - These two rungs were chosen because one is anomalous and the other is normal.
  - At 30000/1001 fps, 300 frames is a little longer than the first segment: in both rungs' playlists the first segment's `EXTINF` is 9.9766 (see `eval/baseline/inputs/`), so the compared range runs into the second segment. The pairing was checked with the frame at index 300 (counting from 0, right after the compared range): the frame counters on both rungs show 000300.
- **Who**: agent
- **Date**: 2026-10-21
- **Status**: confirmed by the owner (2026-10-23). This step is **not** the §5 implementation. §5's check tool, parameters and thresholds are still the owner's to decide.

### P-006 What mirage_vmaf records, and when it writes no report

- **Choice**
  - **PTS**: each paired frame's `best_effort_timestamp`, in seconds (`ts × num / den` with the stream's time base, which the report also gives), as the stream has it, not shifted to start at 0. `null` when the frame has none.
  - **Input parameters** (size, `pix_fmt`, color space, range, primaries, transfer): the decoder's values right after opening, the stream parameters ffprobe also shows, under FFmpeg's names. Not those of the first decoded frame.
  - **Pooled values**: only libvmaf's pooled mean and min, as the analyzer computes them today. The analyzer's p5 stays out of the report while its definition is open (#6); every per-frame score is in the report.
  - **No report, and exit status 1**, when an input can't be opened, an input stops on a read or decode error instead of reaching its end, no pair is scored, or a libvmaf call fails while scoring. Until now the analyzer took a read or decode error for the end of the input, and a failed score stayed 0. The report is written to a temporary file next to `--out` and renamed over it once complete.
  - **FFmpeg's log level** in `mirage_vmaf`: warnings and errors only. That drops FFmpeg's informational lines, such as the "Opening … for reading" line for every HLS segment.
  - The libvmaf package version is read at configure time, as the stage's task asked. The commit is recorded by the eval tooling when it runs, not by `mirage_vmaf`.
  - `mirage_tests` links libvmaf and runs the analyzer on clips it generates.
- **Alternatives**
  - PTS: the frame's `pts`.
  - Input parameters: the first decoded frame's values, or both.
  - Failures: write a report with an error field, or report whatever was scored, as the analyzer did before.
  - Package version: read at run time from the library that was actually loaded (`dladdr()` and `realpath()`), which can't go stale when libvmaf is upgraded without a new configure.
  - Keep `mirage_tests` free of libvmaf and test only the helpers.
- **Rationale**
  - `best_effort_timestamp` is the decoder's timestamp for the frame. It is the frame's `pts` unless FFmpeg's heuristics find the stream's `pts` unreliable.
  - The decoder's values exist even when decoding fails, and can be compared with `eval/baseline/diagnostics/ffprobe_rungs.txt`. If a stream's frames ever carry other tags than its parameters, the report needs the frames' values too; D-016's conversion is where that would show.
  - With these rules a report exists only for a complete run, so later tooling can't take a truncated run for a full one.
  - A commit read when the tooling runs can't go stale the way a value fixed at configure time can.
  - The analyzer's new features (frame limit, PTS, input parameters, how inputs ended) need tests that run the analyzer.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: pending owner confirmation

### P-007 The build check behind docs/BUILD.md

- **Choice**
  - `eval/build_check/run_build_check.sh` builds a committed HEAD in a clean worktree, as P-001 did. First the release preset with the default Python, which stops at glad on this machine; then `mirage_vmaf` and `mirage_tests` alone; then both presets with a glad venv the script creates itself (Homebrew's Python 3.13, `jinja2==3.1.6` from PyPI). It runs the unit tests without `[audio]` and writes a record with local paths replaced by placeholders.
  - `docs/BUILD.md` states only what that record shows or what the repository's files say.
- **Alternatives**
  - Describe the build from the build directories already on the machine.
  - Reuse the venv prepared earlier on 2026-10-23, which has the same Python and packages (its `pyvenv.cfg` and `pip list`).
- **Rationale**
  - A worktree of a commit is the nearest this machine gets to a fresh clone: FetchContent downloads every dependency again, and nothing uncommitted takes part.
  - Creating the venv in the check verifies the commands `BUILD.md` gives, not only that some venv works.
  - The record backs the version numbers in `BUILD.md` (hard rule 2 of the work order).
- **Limits**: the check reuses this machine's Homebrew packages, so it isn't the clean-machine build that §8's acceptance asks for. The player's GUI isn't started.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: pending owner confirmation

### P-008 FFmpeg's side of the §5 delta tool

- **Choice**
  - `eval/consistency/run_delta.sh` runs FFmpeg with the filter graph of Phase 0's diagnostic comparison (P-005, `eval/baseline/diagnostics/ffmpeg_libvmaf_gear2_vs_gear5_first300.cmd.txt`), with these differences:
    - `scale=W:H` takes the reference's size from ffprobe instead of the fixed `1920:1080`;
    - with `--frames N`, `trim=end_frame=N` starts both inputs' chains, instead of `-frames:v N`;
    - `-an`: no audio output;
    - `-v verbose` and `-nostats`.
  - Mirage's side is `mirage_vmaf [--frames N]` on the same two inputs. Both sides get model `vmaf_v0.6.1`, the bilinear scaler and 4 libvmaf threads by default. `mirage_vmaf` is given `--scaler` and `--model` only when its `--help` lists them; today it has neither (P-009 makes a pair fail on any settings mismatch).
- **Alternatives**
  - Phase 0's `-frames:v N`.
  - FFmpeg's default log level, with the color space lines taken from a separate verbose run.
  - The reference's size from `mirage_vmaf`'s report.
- **Rationale**
  - `-frames:v` limits the output, after the filter graph, so the command doesn't fix how many pairs libvmaf has read when FFmpeg stops. With `trim`, libvmaf reads exactly N pairs, and the last of them has no successor, as in `mirage_vmaf --frames N`. That matters because libvmaf's motion2 feature for a frame also uses the next frame (`mirage_vmaf --help`; the test "the last pair of a limited run has no successor" in `tests/test_vmaf_analyzer.cpp`).
  - `-an`: only video is scored, and an audio output stream would not end where `trim` ends the video.
  - `-v verbose` makes FFmpeg log the color space and range of each filter graph input and of each scale filter's input and output, which is what D-016 is about. `-nostats` keeps the progress line out of the logs.
  - ffprobe doesn't depend on the program being checked.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: pending owner confirmation

### P-009 How the delta tool compares, and when a pair passes

- **Choice**
  - Frame i of `mirage_vmaf`'s report is compared with `frameNum` i of FFmpeg's libvmaf log; delta = Mirage − FFmpeg.
  - A pair passes only if both programs exit with status 0 and leave their outputs; `mirage_vmaf`'s report has the settings FFmpeg was given (model, scaler, n_threads, n_subsample 1, the frame limit, yuv420p at 8 bits, the reference's size); both sides scored the same number of frames, more than none; and no frame has |delta| > the threshold (D-019; a frame exactly at the threshold passes). Anything else fails the pair, with the reason in the report.
  - The reports also give the largest and mean |delta|, the mean delta, the frames over the threshold as index runs, the five frames with the largest |delta|, both sides' pooled mean and min as libvmaf computes them and their difference, and both sides' mean over the compared frames. Pooled values aren't part of the verdict.
  - Warnings, which don't fail a pair: different libvmaf versions on the two sides; `mirage_vmaf` pairs whose two frames have different PTS; `mirage_vmaf`'s input parameters differing from ffprobe's; FFmpeg's log lacking the scale filter's line, or showing another size or other scaler flags than requested; FFmpeg's filter graph inputs having other tags than ffprobe found.
- **Alternatives**
  - Pair frames by PTS in the delta tool.
  - Also fail a pair on its pooled mean difference.
  - Make the warnings failures.
- **Rationale**
  - Mirage pairs frames in decode order, FFmpeg's libvmaf filter by timestamp. Comparing by position is right only while both pair the same frames, which the PTS warning checks from Mirage's side; FFmpeg's log has no PTS. PTS pairing in Mirage waits for the owner (§1.4, D-007).
  - With different frame counts the two sides haven't scored the same frames, so the pair can't pass. The reports flag it in capitals.
  - FFmpeg's libvmaf log gives scores with 6 decimals (for example `eval/baseline/diagnostics/ffmpeg_libvmaf_gear2_vs_gear5_first300.json`), while `mirage_vmaf` writes full precision, so differences below that resolution mean nothing.
  - The threshold rule is D-019's. The work order states the target as a per-frame difference "< 0.01"; the two differ only for a frame at exactly 0.01.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: pending owner confirmation

### P-010 The delta tool's diagnostic relabel variant

- **Choice**: with `--diagnostic-relabel`, FFmpeg's distorted input gets `setparams` before its scale filter, setting `colorspace` and `range` to the reference's values for each of the two tags that differs between the inputs in ffprobe's stream parameters. FFmpeg's scale filter then has nothing to convert but the size. Mirage's side doesn't change. A tag the reference lacks isn't relabeled, with a warning. The reports call the variant a diagnostic, not the §5 comparison.
- **Alternatives**
  - Relabel the color space only.
  - Take the tags from `mirage_vmaf`'s report, or from FFmpeg's log.
  - Relabel the reference instead.
- **Rationale**
  - It leaves FFmpeg's color conversion as the only difference between a plain run and this one, so the two runs side by side show what the conversion changes.
  - The range is included because D-016 covers the matrix and the range, and FFmpeg's scale filter states both in its log. No bipbop 16x9 rung differs in range (`eval/baseline/diagnostics/ffprobe_rungs.txt`), so for those pairs only `colorspace` is set.
  - ffprobe's tags are the stream parameters, the same values as Phase 0's `ffprobe_rungs.txt` and `mirage_vmaf`'s input fields (P-006). The report warns if FFmpeg's filter graph inputs show other tags.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: pending owner confirmation

### P-011 What a delta run records

- **Choice**
  - Every command it runs, in the pair's `cmd.txt` and the run's `run.cmd.txt`, with the programs shown as `<mirage_vmaf>`, `<ffmpeg>` and `<ffprobe>`. The programs run in the pair's directory with relative output paths.
  - Local paths in all outputs replaced by `<out_dir>`, `<repo>`, `<home>` and `<tmp>`, with the number of replacements per file in `delta.json`. A path that would still show local details, such as a pairs file elsewhere, appears as a placeholder only. Finally all outputs, logs decompressed, are searched for the home, temporary, output and repository paths, the system's temporary directory, the user name and the host name; a find ends the run with exit status 3.
  - Both programs' stderr logs, always xz-compressed, and their JSON outputs when larger than 1 MiB, as full-length runs make them. Empty outputs aren't kept.
  - `environment.txt`: git HEAD and `git status --porcelain` at the start and end of the run; OS, CPU, kernel, Python; `ffmpeg -version`, `ffprobe -version`; the libvmaf library FFmpeg's libavfilter links and the Homebrew package version in its real path; `mirage_vmaf`'s path, SHA-256, size, linked libvmaf and the first line of its `--help`.
  - `SHA256SUMS` over every file.
  - One scoring process at a time unless `--jobs` says more; each is stopped after `--timeout` seconds, 6 hours by default, the alarm of the Phase 0 run (`eval/baseline/README.md`).
- **Alternatives**
  - Absolute paths in the outputs.
  - Uncompressed logs, or compressing only large ones.
  - JSON outputs uncompressed whatever their size.
  - No final search of the outputs.
- **Rationale**
  - Local paths and personal details stay out of the repository (D-013, D-014), and the placeholders keep the commands readable.
  - Committed measurements must come from a citable commit (P-001): the git state and the program's SHA-256 tie the outputs to one.
  - The search catches anything the placeholders miss, such as a path an input or a program prints.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: pending owner confirmation

### P-012 The diagnosis before D-016: first 300 frames, plain and relabeled

- **Choice**
  - Before Mirage converts color (D-016), run `eval/consistency/run_delta.sh --frames 300` twice on the four pairs of D-018: plain, and with `--diagnostic-relabel` (P-010). The outputs are in `eval/consistency/diag-before-300/plain/` and `relabel/`, with `build.txt` and `checks.txt` beside them.
  - The `mirage_vmaf` is the release build in the main working tree, at commit `ff98a13`, after `cmake --build --preset macos-release --target mirage_vmaf` reported no work to do. Its sources haven't changed since `83c8ed6`, where stage 1 built it (`build.txt`).
  - One scoring process at a time, with the inputs fetched from Apple's CDN in each run rather than from a local copy.
- **Alternatives**
  - Every frame now, which is D-018's input for the committed report. Phase 0's full-length run of the existing scorer over the ladder took 3172.96 s of wall time (`eval/baseline/bipbop16x9/time_l.txt`), with FFmpeg's side on top of that.
  - Fewer frames, such as 30.
  - A build in a clean worktree, as P-001 did.
- **Rationale**
  - 300 frames are the range of Phase 0's diagnostic comparison (P-005), so both sides could be checked against Phase 0's outputs (`checks.txt`). At 30000/1001 fps they run into the second segment (P-005).
  - Short enough to rerun while D-016 is implemented. The committed §5 report remains the full-length one of D-018, after D-016.
  - The main working tree was clean and at HEAD, and ninja's no-op shows the binary is up to date with it, so the outputs rest on a committed, clean tree, as P-001's did, without fetching every dependency again for a worktree.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: pending owner confirmation

### P-013 The color tag cases: synthetic clips, scored before and after D-016

- **Choice**
  - `eval/consistency/color_cases.py` makes clips of FFmpeg's `testsrc2` pattern: references at 1920x1080, distorted inputs scaled to 640x360, 10 frames at 30000/1001 fps, 8-bit yuv420p, uncompressed in Matroska, each given its tags with `setparams`. ffprobe reads every clip's tags back, and the MD5 of the decoded frames is the same for all clips of one size, so the cases differ only in their tags (`eval/consistency/color-cases-before/clips.txt`).
  - 18 cases that FFmpeg's §5 graph scores: matrices (different, equal, missing on either input or both, two names of BT.601, a BT.2020 reference), ranges (full on either input, missing on either input), both at once, and tags FFmpeg leaves alone (primaries and transfer, chroma siting, interlacing). 2 cases it refuses: a YCgCo matrix on either input.
  - Both sets go through `run_delta.sh --frames 10` in separate runs, with `TMPDIR` set to the directory that holds the clips, so that their paths show as `<tmp>`. Before D-016 with stage 1's `mirage_vmaf` (`color-cases-before/`); after it with the new one, on the same clips, their MD5s checked again (`color-cases-after/`).
  - The FFmpeg 8.1.1 sources (`ffmpeg-8.1.1.tar.xz` from ffmpeg.org, checked against the SHA-256 in Homebrew's ffmpeg-full formula) were read to explain the logs. The evidence is the logs.
- **Alternatives**
  - `setparams` on the bipbop streams inside FFmpeg's graph. That sets the tags the scale filter sees, but bypasses what FFmpeg's buffer source does with an untagged stream (for a missing range tag it also offers limited range), and `mirage_vmaf` would still see the streams' real tags.
  - H.264 clips encoded without color tags.
  - Reading the sources only, without runs.
- **Rationale**
  - With the same pixels in every case, the tags are the only difference between cases, so a difference in the scores comes from the conversion.
  - Files that carry the tags, or none, reach both programs the way decoded streams do, through FFmpeg's buffer source on FFmpeg's side.
  - Generated clips need no network, come out the same on every run of the same FFmpeg, and both sides score them in seconds.
- **Limits**: 8-bit yuv420p at two sizes only, the libvmaf graph of the §5 comparison only, and FFmpeg 8.1.1 only; another FFmpeg version may negotiate differently.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: pending owner confirmation

### P-014 How Mirage converts the distorted frames (D-016)

- **Choice**
  - The distorted frames go through `sws_scale_frame()`, the call FFmpeg's scale filter makes, with a context from `sws_alloc_context()` given the bilinear flag, and with the scale filter's handling of the frames under its default options: the input frame loses its chroma location and its interlaced flag, and the output frame gets the input's properties and side data, then the negotiated matrix and range (`src/VMAFColor.h`). The reference frames keep the `sws_scale()` call they had, which converts only the pixel format.
  - The tags come from the decoded frames, pair by pair: the distorted frame's own tags, and the target that `vmafcolor::target()` derives from the reference frame's and the distorted frame's tags by FFmpeg's negotiation rules (`eval/consistency/README.md`, on `color-cases-before/`).
  - One swscale thread for the distorted frames. FFmpeg's scale filter uses the filter graph's threads, by default as many as `ffmpeg -lavfi` finds CPUs.
  - An input whose matrix libswscale has no coefficients for (`sws_test_colorspace()`) ends the analysis with an error, so `mirage_vmaf` writes no report and exits with status 1 (P-006).
  - The report's `dis_conversion` lists each distinct conversion once, with the pairs it applied to and whether swscale changed the matrix or the range in the pixels, decided as swscale decides it: different `sws_getCoefficients()` tables, full range on one side only.
  - `tests/test_vmaf_color.cpp` runs the §5 graph through libavfilter on generated frames and compares the bytes; it is skipped where libavfilter has no libvmaf filter.
- **Alternatives**
  - The legacy `sws_getContext()` with `sws_setColorspaceDetails()`: the same conversion inside swscale, but the scale filter's frame handling (chroma location, interlacing, side data) would have to be imitated by hand.
  - Mirage's own conversion matrices, which would not produce FFmpeg's bytes.
  - Tags from the decoders' stream parameters (P-006's input fields), or fixed at the first pair, as FFmpeg configures its graph from each input's first frame.
  - As many swscale threads as FFmpeg's scale filter uses.
  - For a matrix libswscale lacks: score without conversion, with a warning, as before D-016.
- **Rationale**
  - The same library call with the same parameters is how Mirage gets FFmpeg's bytes, not merely close values. `tests/test_vmaf_color.cpp` checks all three planes, with libavfilter on its default threads and Mirage on one.
  - Tags taken pair by pair are the streams' tags while those don't change, and follow a change as FFmpeg does, since `ffmpeg` reconfigures its filter graph when a frame's tags change. In every committed run they don't change (each `dis_conversion` has one entry).
  - FFmpeg refuses such a pair, so there is no FFmpeg score to match; an error says so, where a score would match nothing.
  - One thread keeps the GUI's background analysis light, and the comparison test shows the bytes do not depend on it.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: pending owner confirmation. What this means for untagged streams, and the refusal, are the owner's to confirm under D-016.

### P-015 The checks after D-016: 300 frames of four rungs, plain, and the color tag cases again

- **Choice**
  - Build the release `mirage_vmaf` in the main working tree at a clean HEAD, `5263061`, as P-012 did; `eval/consistency/diag-after-300/build.txt` records the build's output, a second build with nothing to do, and the SHA-256.
  - Run `run_delta.sh --frames 300` on the four pairs of D-018 with plain FFmpeg (`diag-after-300/plain/`), and the color tag cases again with the same program (`color-cases-after/`, P-013).
  - Afterwards, `checks.txt` compares both sides at the 6 decimals of FFmpeg's log, Mirage's scores with those before D-016 at full precision, and FFmpeg's side with that of the run before D-016, and lists the conversions.
  - The delta tool shows `mirage_vmaf`'s `dis_conversion` next to the lines of FFmpeg's scale filter, and warns when their tags differ; a warning, not a failure, like the other checks of P-009.
- **Alternatives**
  - A build in a clean worktree, as P-001 and P-007 did.
  - The full-length run of D-018 now.
  - Fail a pair when the two sides' conversions differ.
- **Rationale**
  - 300 frames are the range of the diagnosis before D-016 (P-012), so the two runs compare frame by frame. The full-length report remains D-018's acceptance output.
  - The main working tree was clean and at the commit the run records, and the build log shows what was rebuilt.
  - A difference in the conversions shows in the scores anyway; the warning names its likely cause.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: pending owner confirmation
