# Decision Log

Purpose: material for the paper's design-rationale section, and the owner's basis when defending the design (see hard rule 4 of the VMAF work order).

Each entry records the decision, the alternatives, the rationale, who decided and the date.

**Only the owner makes design decisions** (hard rule 3 of the work order). The agent only makes measurement and process choices. Those entries are listed separately below and stay marked as pending until the owner confirms them.

---

## Design decisions (owner)

The remaining open items are listed in the "Decisions needed from the owner" section of `docs/VMAF_GAP_REPORT.md`. Those still open after Phase 1, each with its options and measurements, are in `docs/PHASE1_DECISIONS_PENDING.md`.

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
- **Resolved by**: D-024 (2026-10-23), bicubic.
- **Who**: owner (Jiayu Lin), adopting the agent's suggestion
- **Date**: 2026-10-23

### D-004 #4–#8 deferred until their sections start

- **Decision**
  - #4 default model, #5 bit-depth handling, #6 pooling definitions, #7 frame-alignment failure threshold: decided when §1 starts.
  - #8 default backpressure policy: decided when Phase 4 (§4) starts.
  - During the §5 comparison the current settings (`vmaf_v0.6.1`, 8-bit YUV420P) stay, and the code isn't changed for the comparison.
- **Alternatives**: decide each one now.
- **Rationale**: all of these change scores. As with D-003, they are decided once there's measured data.
- **Resolved by** (2026-10-23): D-025 (#4), D-026 (#5), D-027 (#6) and D-028 (#7). #8 stays with Phase 4.
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
- **Correction** (2026-10-23, D-032): the first alternative says that standardizing on `ffmpeg` would need a separately installed FFmpeg with libvmaf. On the machine of the measurements that isn't so: Homebrew's plain `ffmpeg` 8.1.1 there is built with `--enable-libvmaf` as well (`eval/baseline/environment.txt`, line 163, in the output of its `-buildconf`). The decision stands on its other reasons.
- **Resolved by**: D-031 (2026-10-23), for the other two parts of #11.
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
- **Measured** (note added 2026-10-23, D-032): the committed runs reproduce the effect: before the conversion in `eval/consistency/diag-before-300/`, after it in `eval/consistency/diag-after-300/` and, over every frame, `eval/consistency/acceptance-full/`, with the color tag cases in `eval/consistency/color-cases-before/` and `color-cases-after/`. What D-016 means in practice is D-021 for a yuv420p reference and D-022 for other references.
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

### D-020 Phase 1's open packages: the agent's recommendations adopted

- **Decision**: the owner adopts every recommendation labeled as the agent's in `docs/PHASE1_DECISIONS_PENDING.md` as of `63b40e2`, and those of `docs/plans/frame-alignment.md`. The choices those recommendations left to the owner, #7's failure-rate definition, threshold and action, the owner made from the options the agent gave. D-021 to D-033 record each decision.
- **Owner's words**: "Yea I see you have recommendations, then we can run with your recommendations"; for #7, "All failures", "1%" and "Warn".
- **Not covered, still open**: the source clips and encoding parameters of D-011; D-008's question whether legal or the company's open-source process has to confirm the license; the player's own JSON export (difference 9 of the pending document, which had no firm recommendation).
- **Alternatives**: deciding each package separately.
- **Who**: owner (Jiayu Lin)
- **Date**: 2026-10-23

### D-021 D-016 in practice for a yuv420p reference: FFmpeg 8.1.1's rules, and a refusal for matrices libswscale can't convert

- **Decision**
  - For a yuv420p reference, D-016 means what the code does since `0444859` (option 1 of the package "D-016: untagged streams, and matrices libswscale can't convert"): the distorted frames get the reference's matrix and range tags, a missing tag included; when the reference has no range tag, they keep a limited or missing range and a full range becomes unspecified; in the pixels a missing matrix is taken as BT.601's and a missing range as limited; the primaries and the transfer are never converted, the chroma siting is dropped and interlaced frames are scaled as progressive.
  - A pair in which either input has a matrix libswscale has no coefficients for, such as YCgCo, is refused, in `mirage_vmaf` and in the player, as FFmpeg refuses it (option (a)).
  - `mirage_vmaf`'s report records the run-time versions of the FFmpeg libraries, libswscale and libavfilter among them, next to libvmaf's (D-006).
  - After any FFmpeg upgrade, the color tag cases are run through the delta tool again before new scores are used.
  - The paper states that an untagged input is taken as BT.601 with limited range.
- **Alternatives**: converting nothing when either input lacks a tag, or making the handling of untagged inputs configurable; for a matrix libswscale lacks, scoring without converting and warning, as before D-016.
- **Rationale**: D-016's rationale is matching a plain `ffmpeg -lavfi libvmaf` run. For yuv420p references the committed runs show Mirage doing so on every frame (`eval/consistency/color-cases-after/`, `diag-after-300/`, `acceptance-full/`), and FFmpeg gives no score to match for a pair it refuses (`eval/consistency/color-cases-before/refused/`, `format-cases-before/refused/`).
- **Who**: owner (Jiayu Lin), adopting the agent's recommendation (D-020)
- **Date**: 2026-10-23

### D-022 References that aren't yuv420p: convert the reference, as FFmpeg's graph does

- **Decision**: when the reference's frames aren't yuv420p (10-bit, 4:2:2, 4:4:4, yuvj420p and the other formats FFmpeg's §5 graph has to convert), Mirage converts the reference to the distorted frames' color matrix and range, with `sws_scale_frame()` and the flags of the scale filter FFmpeg inserts for it (bicubic), and leaves the distorted frames' color as it is; the distorted frames are still scaled with the chosen scaler (D-024). With D-026, this conversion is also what brings a 10-bit reference to 8 bits. The acceptance test is the reference format cases (`eval/consistency/format-cases-before/`, `format-cases-after/`): after the change every case is expected to pass, `j01` with its scores from before D-016. Until it is implemented, P-030's warning stays.
- **Alternatives**: following D-016's wording, converting the distorted frames and only keeping a yuvj420p reference's range; refusing such references; today's behavior with the warning.
- **Rationale**: D-016's rationale is parity with plain FFmpeg, and for these references FFmpeg's graph converts the reference (`eval/consistency/format-cases-before/checks.txt`, check 2). Since D-016, Mirage's scores differ from FFmpeg's for every such reference whose tags differ from the distorted input's, and for a full range reference with the same tags (`eval/consistency/format-cases-after/main/delta.md`).
- **Who**: owner (Jiayu Lin), adopting the agent's recommendation (D-020)
- **Date**: 2026-10-23

### D-023 A frame exactly at the §5 threshold passes

- **Decision**: D-019 stands: a frame fails if |Mirage − FFmpeg| > 0.01, so a frame at exactly 0.01 passes. The paper's method text states this boundary, which differs from the work order's "< 0.01".
- **Alternatives**: failing a frame when |Mirage − FFmpeg| ≥ 0.01, as the work order words it.
- **Rationale**: the two differ only for a frame at exactly 0.01.
- **Who**: owner (Jiayu Lin), adopting the agent's recommendation (D-020)
- **Date**: 2026-10-23

### D-024 The default scaler is bicubic (#3)

- **Decision**: bicubic scales the distorted frames to the reference's size by default, in the player and in `mirage_vmaf`, where `--scaler` keeps bilinear and lanczos as options. The §5 delta tool's default becomes bicubic as well, for both sides. This resolves D-003. The full-length §5 report of D-018 is made again with it; the bilinear report stays committed as the record it is. The paper states the scaler.
- **Alternatives**: bilinear, the default until now and the setting of the committed §5 runs other than the scaler runs; lanczos.
- **Rationale**: it is the work order's suggestion (§1.3) and what FFmpeg's scale filter uses when no scaler is named (`eval/consistency/ffmpeg-scale-default/checks.txt`), so the scores are what a reader gets from a plain `ffmpeg` command with a plain `scale` filter. Each of the three scalers passes §5 when both sides use it (`eval/consistency/scalers-300/checks.txt`, check 3), and no score so far is paper data (D-005). Against bilinear, bicubic raises the scores by 6.775801 to 11.113910 on average per pair over the first 300 frames (`eval/consistency/scalers-300/scaler_effect.md`).
- **Who**: owner (Jiayu Lin), adopting the agent's recommendation (D-020)
- **Date**: 2026-10-23

### D-025 The default model stays vmaf_v0.6.1 (#4)

- **Decision**: libvmaf's built-in vmaf_v0.6.1 stays the default model. The other built-in models and model files stay options, recorded in each report (`--model`, `--model-file`). A model chosen from the reference's resolution may come later as an option, once the owner sets its resolution limit.
- **Alternatives**: vmaf_v0.6.1neg; vmaf_4k_v0.6.1; a rule by resolution now.
- **Rationale**: FFmpeg's libvmaf filter uses it unless told otherwise (`eval/consistency/ffmpeg-libvmaf-filter/checks.txt`), and every committed §5 run used it apart from the other models' runs in `eval/consistency/models-300/`.
- **Who**: owner (Jiayu Lin), adopting the agent's recommendation (D-020)
- **Date**: 2026-10-23

### D-026 Frames are scored at 8 bits for now (#5)

- **Decision**: every input stays converted to 8-bit yuv420p before scoring. Once D-011's ladders exist, a 10-bit SDR ladder is scored at 8 bits and at its own bit depth, on both sides, and the owner decides again with that measurement.
- **Alternatives**: the inputs' own bit depth when both have more than 8 bits; configurable.
- **Rationale**: FFmpeg's §5 graph scores at 8 bits as well (`format=yuv420p` on both inputs), and no ladder of 10-bit SDR rungs encoded from one source is at hand to measure the difference (MEASUREMENT PENDING). How a 10-bit reference reaches 8 bits is D-022's conversion.
- **Who**: owner (Jiayu Lin), adopting the agent's recommendation (D-020)
- **Date**: 2026-10-23

### D-027 Pooling definitions (#6)

- **Decision**
  - Mean: libvmaf's pooled mean.
  - Harmonic mean: libvmaf's (`VMAF_POOL_METHOD_HARMONIC_MEAN`), the shifted form `n / sum(1 / (x + 1)) - 1` that FFmpeg's log reports too.
  - Minimum: libvmaf's, over every frame; the paper states that it is the first frame's score, whose motion features are 0. Also reported, under its own name: the minimum without the first frame.
  - Percentiles p1, p5, p10, p25 and p50: computed by Mirage with linear interpolation (Hyndman and Fan's definition 7), in the analyzer that the player and `mirage_vmaf` share, so the player's p5 changes from `sorted[n * 5 / 100]` to this definition too.
- **Alternatives**: for the percentiles, today's `sorted[floor(n * p / 100)]` or nearest rank; the plain harmonic mean, with a rule for frames at 0; the minimum over every frame only.
- **Rationale**: the three percentile definitions differ by at most 0.000276 over every frame of the four pairs, and by up to 0.093440 over 300 frames (`eval/pooling/acceptance-full/pooling.md`, `eval/pooling/bipbop16x9/pooling.md`); with linear interpolation, the default of common analysis tools, readers recomputing the percentiles from the per-frame scores get the same values. libvmaf's harmonic mean is defined when frames score 0 and is what FFmpeg reports. libvmaf's minimum keeps parity with FFmpeg, and the minimum without the first frame is the one that says something about the encode.
- **Who**: owner (Jiayu Lin), adopting the agent's recommendation (D-020)
- **Date**: 2026-10-23

### D-028 Frames are paired by timestamp (§1.4, #7)

- **Decision**
  - `docs/plans/frame-alignment.md` is approved (D-007), with its recommended choices:
    - matching rule A, the reference's timeline, with the plan's tolerance of half the reference's frame duration;
    - timestamps measured from each input's start, as `ffmpeg` does without `-copyts`;
    - for a frame-rate mismatch, scoring on the coarser of the two grids (option (b));
    - at a discontinuity, pairing stops and the rest of the input counts as failed (option (a)), until D-011's ladders show what real discontinuities look like;
    - a new libvmaf context after each gap, so that no motion feature spans one;
    - the report's `alignment` fields and the list of failures as the plan proposes, ahead of §7's schema;
    - the §5 delta tool compares the two sides by timestamp.
  - #7, chosen by the owner from the options given:
    - the failure rate is failed / (paired + failed), counting unmatched reference frames, extra and duplicate distorted frames and frames without a timestamp;
    - the threshold is 1%, the work order's suggestion;
    - above it the pair is still scored, with a warning in the report, on stderr and in the player's VMAF panel, and `mirage_vmaf` exits with status 0.
  - Failures are always counted and listed.
- **Alternatives**: rule B (FFmpeg's framesync rule), or A with B's pairing reported next to it; absolute timestamps; options (a) or (c) for frame-rate mismatches; (b), (c) or (d) for discontinuities; motion across gaps, or stand-in frames; for #7, unmatched reference frames over all reference frames, a threshold of 0.1% or any failure, and refusing or flagging the report above the threshold.
- **Rationale**: rule A follows the reference variant's timeline, which §3.1 uses for attribution, and never scores a pair whose frames don't match; where it differs from FFmpeg's rule B, the delta tool reports the difference instead of hiding it. The work order rules out silent scores; with a warning every score stays available and the failures are visible.
- **Who**: owner (Jiayu Lin): the plan's choices adopting the agent's recommendation (D-020); #7's definition, threshold and action chosen by the owner
- **Date**: 2026-10-23

### D-029 Reference modes in mirage_vmaf (§1.2)

- **Decision**
  - `explicit-pair` stays the `reference_mode` of a pair given on the command line; §7's enumeration gains it when the schema is written.
  - `mirage_vmaf` gets ladder scoring as the §1.2 package designs it: top-rung by default (D-002), a mezzanine optionally; each video playlist scored once; entries without video skipped and listed as skipped; one failed rung fails the run, as P-006 does for a pair; one report per reference mode, with a `pairs` list, so that the two modes never share a report.
  - §1.2's acceptance, both modes on one ladder with the difference documented, waits for D-011's clips.
- **Alternatives**: another name, or letting the user declare `top-rung` or `mezzanine` for a pair; ladder scoring that mirrors the player's `start_manifest`, every entry with its errors recorded; ladder scoring deferred to Phase 2.
- **Rationale**: a pair given on the command line is neither of §7's two modes; the design keeps the two modes' results apart and gives a failed rung the exit status a failed pair has.
- **Who**: owner (Jiayu Lin), adopting the agent's recommendation (D-020)
- **Date**: 2026-10-23

### D-030 A config file for mirage_vmaf

- **Decision**: `mirage_vmaf --config FILE` reads settings as `name = value` lines with the long options' names, through the same option table as the command line, which overrides the file; the report records the file's path and SHA-256 next to the effective settings. The eval tooling is exempt from the work order's config-file constraint: each run's `run.cmd.txt` records every option.
- **Alternatives**: JSON; TOML, which needs a parser the repository doesn't have; a config file for the eval tooling as well.
- **Rationale**: no new dependency, and nothing can be set in one place but not the other.
- **Who**: owner (Jiayu Lin), adopting the agent's recommendation (D-020)
- **Date**: 2026-10-23

### D-031 The rest of the build strategy (§8, #11), the README and vcpkg.json

- **Decision**
  - #11a: glad's generated OpenGL loader, three files, is committed, for example under `third_party/glad/`, and built as a plain static library, with a script that regenerates it with glad 2.0.8 in the pinned venv and compares the bytes, and with its license terms (WTFPL or CC0-1.0, and Apache-2.0) stated next to it. The player's build then runs no Python.
  - #11b: headless Linux. `mirage_vmaf`, `mirage_tests` and the eval tooling build and run on Linux; the player stays macOS-only. A CMake option configures without the player's dependencies, with a Linux preset, Linux instructions in `docs/BUILD.md` and a §5 run on Linux.
  - #11c: a Docker image for Linux: a base image pinned by digest; FFmpeg 8.1.1 and libvmaf built from release sources checked by SHA-256, at the versions of the committed measurements; `mirage_vmaf` built from the repository; `eval/run_all.sh` as its command. Whether its scores equal those measured on Apple Silicon is its first measurement.
  - `README.md`: its build part becomes a short pointer to `docs/BUILD.md`, and its other stale facts are corrected from the repository's files and committed records.
  - The root `vcpkg.json`, which the build doesn't use, is removed.
- **Alternatives**: generating glad at build time as now, or pinning its environment; macOS only, or full Linux support with the player; a Nix flake, both, or neither; correcting the README in place, or leaving it; keeping `vcpkg.json`, or making it a pinned second route.
- **Rationale**: the glad step is the only build step that failed in either build check, and its output is the same in every build so far (`eval/build_check/macos-2026-10-23-6a6661f.txt`); every committed score comes from `mirage_vmaf` and the eval tooling, which need only FFmpeg and libvmaf; Homebrew can't install the measured versions, and a container can; the build steps belong in one place that the build checks verify.
- **Known limit**: the machine of the measurements has no container runtime, so the image and the Linux build can be written there but not run; their runs are MEASUREMENT PENDING until one is available.
- **Who**: owner (Jiayu Lin), adopting the agent's recommendation (D-020)
- **Date**: 2026-10-23

### D-032 Smaller Phase 1 items

- **Decision**
  - The subtitle test writes its SRT fixture to a temporary file at run time, as the other media tests generate their inputs; a change to the test only.
  - For the intermittent `Demuxer: seamless video and audio switches on a local file`, the agent writes a plan to find out whether the test's timing or the track switch is at fault (D-007); nothing on the decode path changes before the owner approves it.
  - Two findings of the review are fixed before merge: the analyzer's leak when an input opens but has no video stream or its decoder fails to open, and the tests' fixed file names in the shared temporary directory. The other low findings are left for later.
  - The committed delta runs keep FFmpeg's verbose logs raw, the CDN servers' public addresses included.
  - The paper's method text states that the §5 tool adds `scale=W:H:flags=<scaler>` to FFmpeg's distorted input, since FFmpeg's libvmaf filter refuses inputs of different sizes (difference 3 of the pending document).
  - D-009 gets a correction note and D-016 a note citing the committed runs, as D-001 has its correction.
- **Alternatives**: as the packages list them.
- **Who**: owner (Jiayu Lin), adopting the agent's recommendation (D-020)
- **Date**: 2026-10-23

### D-033 The process choices P-006 to P-032 are confirmed

- **Decision**: the agent's measurement and process choices P-006 to P-032 are confirmed as their entries state them.
- **Who**: owner (Jiayu Lin), adopting the agent's recommendation (D-020)
- **Date**: 2026-10-23

### D-034 What implementing D-020 to D-033 left open: the agent's recommendations adopted

- **Decision**: the owner adopts every recommendation labeled as the agent's in `docs/PHASE1_DECISIONS_PENDING.md` as of `b2d2824`, as D-020 adopted those of `63b40e2`. D-035 to D-041 record each decision.
- **Owner's words**: "yea just go with the recommendations", in answer to the agent's summary of the open items and its recommendations.
- **Not covered, still open**: D-011's source clips and encoding parameters, for which the agent's recommendation is that the owner choose them; D-008's question whether legal or the company's open-source process has to confirm the license, which has no recommendation, and with it the license texts next to glad's loader (P-053).
- **Alternatives**: deciding each item separately.
- **Who**: owner (Jiayu Lin)
- **Date**: 2026-10-26

### D-035 D-022 where FFmpeg changes the distorted frames' color: FFmpeg's negotiation

- **Decision**: where D-022's wording and FFmpeg 8.1.1's negotiation differ, for a distorted input without a range tag, or not in plain YUV, against a reference that isn't yuv420p, Mirage follows FFmpeg's negotiation, as the code does since `22e2874` (option 1 of "D-022's wording and FFmpeg's negotiation"). A committed delta run of both kinds of input shows what the two tools do with them.
- **Alternatives**: D-022's wording, with a rule for distorted frames that aren't YCbCr; refusing such pairs.
- **Rationale**: D-022's rationale is parity with a plain `ffmpeg -lavfi libvmaf` run, and its wording describes the committed cases, in which the two agree.
- **Who**: owner (Jiayu Lin), adopting the agent's recommendation (D-034)
- **Date**: 2026-10-26

### D-036 Network reads with a time limit in the analyzer

- **Decision**: the analyzer that `mirage_vmaf` and the player share sets FFmpeg's `timeout` and `rw_timeout` to 5 s on network inputs, the values of `src/FfOpen.cpp`, and gives each input an interrupt callback on its stop token, so that a read that waits longer fails and a cancel interrupts a read; `src/VMAFLadder.cpp` reads a master playlist with the same limits (option 2 of "A network read without a time limit").
- **Alternatives**: no limit, as before; the player's reconnect and segment retry options as well; the limit as an option of `mirage_vmaf` and its config file.
- **Rationale**: a read that waits longer becomes a failure that the report and the §5 check show, instead of a process that waits until the §5 tool's 6 h limit (P-062, P-065), and the player's cancel interrupts a read. A limit changes no score of a run whose reads succeed.
- **Who**: owner (Jiayu Lin), adopting the agent's recommendation (D-034)
- **Date**: 2026-10-26

### D-037 The minimum without the first frame, and a second minimum without the first pair of every libvmaf context

- **Decision**: the report keeps D-027's minimum without the first frame, and adds, under a name of its own, the minimum without the first pair of every libvmaf context; for a pair scored in one context the two are the same (option 4 of "D-027: the minimum without the first frame").
- **Alternatives**: D-027's value only; another name for it; the first pair of every context left out instead.
- **Rationale**: in `eval/consistency/align-cases-d028/main/a01_dropped_distorted/` D-027's value is the score of the first pair of the second context, which has no motion to a frame before it; the second value carries D-027's reason, a minimum that says something about the encode, to every such pair.
- **Who**: owner (Jiayu Lin), adopting the agent's recommendation (D-034)
- **Date**: 2026-10-26

### D-038 D-006 outside Homebrew: the libvmaf package version from the build

- **Decision**: where the library's path names no version, a build gives the libvmaf package version as a CMake cache variable, which the Dockerfile sets to the release it builds, and `run_delta.py` takes the same as an option for FFmpeg's side (option 2 of "D-006 outside Homebrew").
- **Alternatives**: `unknown`, with the run-time version, as before; pkg-config's version, which is the run-time version.
- **Rationale**: whoever builds libvmaf knows the release, and D-006 asks for both versions.
- **Who**: owner (Jiayu Lin), adopting the agent's recommendation (D-034)
- **Date**: 2026-10-26

### D-039 The plan for the intermittent Demuxer test is approved

- **Decision**: experiments 1 to 3 of `docs/plans/demuxer-switch-test.md` run on the machine of the measurements, as the plan describes them; experiment 4, scripted runs of the player, only if experiments 1 or 2 point at the switch.
- **Rationale**: the experiments change no code under `src/`; what they show decides whether the test or `Demuxer` would change, each by a later decision.
- **Who**: owner (Jiayu Lin), adopting the agent's recommendation (D-034)
- **Date**: 2026-10-26

### D-040 Items kept as they are

- **Decision**: these stay as the code and the documents have them, each the agent's recommendation in `docs/PHASE1_DECISIONS_PENDING.md`:
  - the warning for frames in a pixel format outside `vmafcolor::verified_formats()` (option 1 of "The warning that remains of P-030");
  - a reference whose frames change size ends the analysis with an error (option 1 of "A reference whose frames change size", P-063);
  - what a config file accepts (D-030; P-040);
  - the values D-028's pairing leaves open (P-043, P-044) and the delta tool's comparison by timestamp (P-047);
  - §5 as FFmpeg's plain graph where the inputs' frames don't line up (option 1 of "What the alignment cases show where the two rules differ"), with the timestamp jumps of case (4) decided again with D-011's ladders;
  - the values D-029's ladder scoring leaves open (P-050);
  - the headless configuration and the Docker image as they are (P-054, P-055), Debian's packages to be pinned from `snapshot.debian.org` once a container runtime shows that the image builds;
  - the player's own JSON export until §7's schema (difference 9);
  - the commit messages that state a wrong count or rest on no record, left as they are and disclosed in `docs/AI_CONTRIBUTIONS.md`.
- **Who**: owner (Jiayu Lin), adopting the agent's recommendation (D-034)
- **Date**: 2026-10-26

### D-041 The process choices P-033 to P-065 are confirmed

- **Decision**: the agent's measurement and process choices P-033 to P-065 are confirmed as their entries state them.
- **Who**: owner (Jiayu Lin), adopting the agent's recommendation (D-034)
- **Date**: 2026-10-26

### D-042 Phase 2's survey: the agent's recommendations adopted

- **Decision**: the owner starts Phase 2 (§2, ladder orchestration) on the branch `vmaf/phase-2`, made from `vmaf/phase-1` before Phase 1 is merged, and adopts every recommendation of `docs/PHASE2_SURVEY.md` (P2-1 to P2-11). D-043 to D-049 record each decision.
- **Owner's words**: "yep go on", to starting the Phase 2 survey on a separate branch; "yep go with recommendations", to the survey's recommendations.
- **Who**: owner (Jiayu Lin)
- **Date**: 2026-10-26

### D-043 The pool scores whole pairs, in mirage_vmaf's ladder mode only

- **Decision**: the pool of §2 runs in `mirage_vmaf --ladder` only; the player's VMAF panel keeps scoring a manifest's pairs one after another, until §4 decides scoring next to playback (P2-1). Each worker scores one pair, as a pair is scored today, so nothing within a pair changes (P2-2).
- **Alternatives**: the pool in the player as well; frames of one pair spread across workers.
- **Who**: owner (Jiayu Lin), adopting the agent's recommendation (D-042)
- **Date**: 2026-10-26

### D-044 The pool's default size, and libvmaf's threads

- **Decision**: by default the pool scores the machine's physical cores minus one pairs at a time, at least one, the work order's default; each pair's libvmaf keeps its 4 threads, so that Phase 1's §5 parity holds as measured (P2-3). How the wall time changes with the pool's size is a measurement of Phase 5's benchmarks.
- **Alternatives**: libvmaf on 1 thread per pair; a default that counts libvmaf's threads.
- **Who**: owner (Jiayu Lin), adopting the agent's recommendation (D-042)
- **Date**: 2026-10-26

### D-045 The memory limit: the process's resident memory, no default limit yet

- **Decision**: with a memory limit, a pair starts only while the process's resident memory, read from the system during the run, plus what a running pair uses stays under the limit; running pairs aren't stopped, and a pair that needs more than the limit on its own runs alone, with a warning. There is no default limit until the measurements of one pair's memory and of the ladder before the pool (the survey's M2 and M3) show what a pair uses (P2-4).
- **Alternatives**: an estimate per pair from its frame sizes and queue depths; a default limit as a share of the machine's memory.
- **Who**: owner (Jiayu Lin), adopting the agent's recommendation (D-042)
- **Date**: 2026-10-26

### D-046 Discontinuities: each discontinuity sequence paired on its own

- **Decision**: after the survey's M5 shows what the analyzer gets at an `EXT-X-DISCONTINUITY`, the pairing pairs the k-th discontinuity sequence of the reference with the k-th of the distorted input, each measured from its own start, with a new libvmaf context for each, the sequences taken from the playlists' `EXT-X-DISCONTINUITY` tags where there are any, else from the timestamp jumps (P2-5). This changes D-028's option (a), which D-040 kept until discontinuities were measured. Rungs whose segments differ in duration need nothing of their own unless M5 shows timestamps that don't line up (P2-6).
- **Alternatives**: D-028's rule as it is; `ffmpeg`'s correction of timestamp jumps.
- **Who**: owner (Jiayu Lin), adopting the agent's recommendation (D-042)
- **Date**: 2026-10-26

### D-047 A cancel or a failed pair leaves a report of the complete pairs

- **Decision**: `mirage_vmaf --ladder` stops on SIGINT or SIGTERM, or at a pair that fails; the pairs still running are stopped; it writes a report of the pairs that were complete, marked as cancelled or failed, with the pairs not scored listed, and exits with status 1 (P2-7). The report lists the pairs in the plan's order, whatever order they finish in, and records the pool's size, the memory limit and each pair's start and end times, so that it is the same report at every pool size apart from the times (P2-8).
- **Alternatives**: no report unless every pair is complete, as P-006 and D-029 have it until now.
- **Who**: owner (Jiayu Lin), adopting the agent's recommendation (D-042)
- **Date**: 2026-10-26

### D-048 The pool's options

- **Decision**: `--jobs N` and `--memory-limit SIZE`, on the command line and in a config file (D-030) (P2-9).
- **Who**: owner (Jiayu Lin), adopting the agent's recommendation (D-042)
- **Date**: 2026-10-26

### D-049 Phase 2's acceptance on Apple's advanced HLS sample, from a local copy

- **Decision**: §2's acceptance runs on Apple's advanced HLS sample: the ladder at the pool sizes 1, 2, 4 and the default, the first 300 frames of each pair, every per-frame score compared across them at full precision, and one run over every frame at the default size for the peak memory against the limit; D-011's ladders, once chosen, as a second acceptance (P2-10). The runs across pool sizes read a local copy of the sample's playlists and segments, kept outside the repository with their SHA-256s committed, and one run reads Apple's CDN as a check (P2-11).
- **Alternatives**: waiting for D-011's ladders; every run from the CDN.
- **Who**: owner (Jiayu Lin), adopting the agent's recommendation (D-042)
- **Date**: 2026-10-26

### D-050 Discontinuity sequences pair only where their jumps are at the same place

- **Decision**: the k-th discontinuity sequences of the two inputs pair only where the jump that starts them is at the same place in both: where both inputs' (k − 1)-th sequences end within a frame of the coarser input of each other, in their own times. Where they don't, pairing stops at that sequence and every frame after it counts as failed, as D-028's rule counts the frames after a discontinuity (option 2 of "Discontinuity sequences that don't line up" in `docs/PHASE2_DECISIONS_PENDING.md`). Jumps at the same place pair as D-046 has them, and the sequences stay those of P-068, found where the timestamps jump; the playlists' tags (option 5) aren't read.
- **Owner's words**: "Pair only aligned jumps (Recommended)", the owner's answer to the agent's question of 2026-10-27.
- **Alternatives**: D-046 as P-068 implements it; a jump forward taken as a gap, not a sequence (options 3 and 4); the sequences from the playlists' tags.
- **Rationale**: RFC 8216 (§6.2.4) has matching content in variant streams carry matching timestamps and discontinuity sequence numbers, so a tag's jump is at the same place in every rung of a ladder that follows it. Jumps at different places come from frames missing in one input, as when segments are skipped, and D-046's pairing paired those inputs' later sequences 12.012 s apart in M5's gaps (P-070).
- **Who**: owner (Jiayu Lin), adopting the agent's recommendation
- **Date**: 2026-10-27

### D-051 The memory limit counts each pair at what a pair uses plus 10%

- **Decision**: with a memory limit, a pair starts only while the memory before the run, plus what a pair uses and 10% more for each running pair and the new one, stays within the limit; the rest of D-045's rule and P-072's warm-up stay as they are (option 2 of "The memory limit's peak"). The acceptance's runs check that the peak then stays under the limit.
- **Owner's words**: "10% margin (Recommended)".
- **Alternatives**: D-045's rule with P-072's warm-up as it is; a pair counted at the most one pair has used in the run; an estimate from the inputs' frame sizes and the queues' depths (D-045's alternative).
- **Rationale**: one pair gains about 3% after its first 30 frame pairs (P-071, `warm_up.txt`), and with the rule as it was the peak at 1 GiB was 5.4% over the limit (P-072).
- **Who**: owner (Jiayu Lin), adopting the agent's recommendation
- **Date**: 2026-10-27

### D-052 No default memory limit

- **Decision**: `mirage_vmaf --ladder` has a memory limit only when `--memory-limit` gives one; there is no default. This settles what D-045 left until M2 and M3 were measured.
- **Owner's words**: "No default (Recommended)".
- **Alternatives**: a default limit as a share of the machine's memory.
- **Rationale**: by M2 one pair of the advanced sample with two 1080p60 inputs peaked at 292995072 bytes over 300 frame pairs, and at the default size 7 pairs at once peaked at 2009759744 bytes, on a machine with 38654705664 bytes (P-071).
- **Who**: owner (Jiayu Lin), adopting the agent's recommendation
- **Date**: 2026-10-27

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
  - The libvmaf package version is read at configure time, from the real path of the libvmaf the build links (`CMakeLists.txt`). The commit is recorded by the eval tooling when it runs, not by `mirage_vmaf`.
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
- **Status**: confirmed by the owner (2026-10-23, D-033)

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
- **Status**: confirmed by the owner (2026-10-23, D-033)

### P-008 FFmpeg's side of the §5 delta tool

- **Choice**
  - `eval/consistency/run_delta.sh` runs FFmpeg with the filter graph of Phase 0's diagnostic comparison (P-005, `eval/baseline/diagnostics/ffmpeg_libvmaf_gear2_vs_gear5_first300.cmd.txt`), with these differences:
    - `scale=W:H` takes the reference's size from ffprobe instead of the fixed `1920:1080`;
    - with `--frames N`, `trim=end_frame=N` starts both inputs' chains, instead of `-frames:v N`;
    - `-an`: no audio output;
    - `-v verbose` and `-nostats`.
  - Mirage's side is `mirage_vmaf [--frames N]` on the same two inputs. Both sides get model `vmaf_v0.6.1`, the bilinear scaler and 4 libvmaf threads by default. `mirage_vmaf` is given `--scaler` and `--model` only when its `--help` lists them; when this entry was written it had neither (P-009 makes a pair fail on any settings mismatch). It has both since `6a9aca2` and `44cd4b5` (P-017).
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
- **Status**: confirmed by the owner (2026-10-23, D-033)

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
- **Status**: confirmed by the owner (2026-10-23, D-033)

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
- **Status**: confirmed by the owner (2026-10-23, D-033)

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
- **Status**: confirmed by the owner (2026-10-23, D-033)

### P-012 The diagnosis before D-016: first 300 frames, plain and relabeled

- **Choice**
  - Before Mirage converts color (D-016), run `eval/consistency/run_delta.sh --frames 300` twice on the four pairs of D-018: plain, and with `--diagnostic-relabel` (P-010). The outputs are in `eval/consistency/diag-before-300/plain/` and `relabel/`, with `build.txt` and `checks.txt` beside them.
  - The `mirage_vmaf` is the release build in the main working tree, at commit `ff98a13`, after `cmake --build --preset macos-release --target mirage_vmaf` reported no work to do. Its sources haven't changed since `83c8ed6`, at which it was built (`build.txt`).
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
- **Status**: confirmed by the owner (2026-10-23, D-033)

### P-013 The color tag cases: synthetic clips, scored before and after D-016

- **Choice**
  - `eval/consistency/color_cases.py` makes clips of FFmpeg's `testsrc2` pattern: references at 1920x1080, distorted inputs scaled to 640x360, 10 frames at 30000/1001 fps, 8-bit yuv420p, uncompressed in Matroska, each given its tags with `setparams`. ffprobe reads every clip's tags back, and the MD5 of the decoded frames is the same for all clips of one size, so the cases differ only in their tags (`eval/consistency/color-cases-before/clips.txt`).
  - 18 cases that FFmpeg's §5 graph scores: matrices (different, equal, missing on either input or both, two names of BT.601, a BT.2020 reference), ranges (full on either input, missing on either input), both at once, and tags FFmpeg leaves alone (primaries and transfer, chroma siting, interlacing). 2 cases it refuses: a YCgCo matrix on either input.
  - Both sets go through `run_delta.sh --frames 10` in separate runs, with `TMPDIR` set to the directory that holds the clips, so that their paths show as `<tmp>`. Before D-016 with the `mirage_vmaf` built at `83c8ed6` (`color-cases-before/`); after it with the new one, on the same clips, their MD5s checked again (`color-cases-after/`).
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
- **Status**: confirmed by the owner (2026-10-23, D-033)

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
- **Status**: confirmed by the owner (2026-10-23, D-033); D-021 confirms the untagged rule and the refusal

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
- **Status**: confirmed by the owner (2026-10-23, D-033)

### P-016 The scaler and model measurements: 300 frames, both sides, one setting changed at a time

- **Choice**
  - Build the release `mirage_vmaf` in the main working tree at a clean HEAD, `8cc06a7`, as P-015 did; `eval/consistency/scalers-300/build.txt` records it, and `models-300/build.txt` is the same record.
  - D-003: `run_delta.sh --frames 300` on the four pairs of D-018, plain FFmpeg, once with each scaler of work order §1.3, `--scaler bilinear`, `bicubic` and `lanczos`, which `run_delta.sh` gives both sides (`eval/consistency/scalers-300/`). Lanczos is included because the work order lists it and it costs one more run of the same kind.
  - Work order §1.1: the same on one pair, gear2 against gear5 (`pairs/bipbop16x9_gear2.txt`), bilinear, once with each built-in model the work order names, `--model vmaf_v0.6.1`, `vmaf_v0.6.1neg` and `vmaf_4k_v0.6.1`, and once with `--model-file` and the libvmaf package's `vmaf_v0.6.1.json` (`eval/consistency/models-300/`).
  - Commit the runs, then make the comparisons and checks from the clean tree: `compare_runs.py` against the bilinear run and against the vmaf_v0.6.1 run (`scaler_effect.*`, `model_effect.*`), and `checks.txt` in each directory, each command with its output.
  - One check run of FFmpeg's scale filter without `flags=`, on FFmpeg's own test pattern, for which scaler it then uses (`eval/consistency/ffmpeg-scale-default/checks.txt`); and FFmpeg's description of its libvmaf filter, with a run on inputs of different sizes (`eval/consistency/ffmpeg-libvmaf-filter/checks.txt`).
  - The inputs are fetched from Apple's CDN in every run, as in P-012; no local mirror.
- **Alternatives**
  - Every frame, D-018's length, for the scaler runs.
  - The model runs on all four pairs.
  - The scalers measured on Mirage's side only, against Phase 0's bilinear FFmpeg outputs.
  - A build in a clean worktree, as P-001 and P-007 did.
- **Rationale**
  - 300 frames are the range of `diag-before-300/` and `diag-after-300/`, so the bilinear run can be checked frame by frame against them (`scalers-300/checks.txt`, check 4). The full-length §5 report of D-018 is a separate run.
  - D-003 asks for each scaler on both sides; with both, a scaler's effect on the scores and a disagreement between the tools can't be confused.
  - §1.1's acceptance needs observable differences and the model identifier, which one pair shows; the model file run shows `--model-file` and its SHA-256 on a file whose content is a built-in model's.
  - The main working tree was clean and at the commit every run records, as in P-012 and P-015.
- **Limits**: bipbop's rungs were rendered separately (D-011), so the size of a scaler's or model's effect on a ladder encoded from one source isn't measured here.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: confirmed by the owner (2026-10-23, D-033)

### P-017 How mirage_vmaf takes the scaler and the model, and what its report says about them

- **Choice**
  - `--scaler NAME` takes the three scalers of work order §1.3 by the names FFmpeg's scale filter takes in `flags=`: `bilinear`, `bicubic`, `lanczos`. It sets the swscale flags of the distorted frames' scaler only, with swscale's default parameters for that scaler; the reference keeps the flags of its pixel format conversion. Without `--scaler` the scaler is bilinear, as before (D-003).
  - `--model NAME` takes any built-in model of the linked libvmaf by its version, and libvmaf says which it has: an unknown name ends the run with status 1 and no report (P-006). `--help` lists the work order's three. `--model-file FILE` loads a libvmaf model file instead; the two exclude each other. Without either the model is vmaf_v0.6.1, as before (D-004).
  - The report names the model by `model` (the version, or the file's path as given), `model_source` (`built-in` or `file`) and `model_sha256` (SHA-256 of the file's contents, read right before libvmaf reads it; `null` for a built-in model). The analyzer records them, so the GUI's results have them too.
  - The report's `reference_mode` is `explicit-pair`: `mirage_vmaf` scores the one pair its command line names, and doesn't know whether that reference is a ladder's top rung or a mezzanine.
  - `run_delta.sh --model-file FILE` gives FFmpeg `model=path=FILE` and `mirage_vmaf` `--model-file FILE`, and fails a pair unless the report has `model_source` `file` and the SHA-256 the tool computes; a path with characters that mean something in a filter graph, or white space, is refused before anything runs. For D-006, every pair report states both sides' libvmaf package versions and run time versions, and the run's report FFmpeg's run time version next to its package version.
- **Alternatives**
  - `--scaler` for the reference's conversion too, or other swscale algorithms.
  - A fixed list of model names checked by the command line.
  - One `--model` option that takes a name or a path.
  - The model file's SHA-256 computed by `mirage_vmaf` rather than recorded by the analyzer.
  - `reference_mode` left out until §7's schema, or a `--reference-mode` option with which the user declares `top-rung` or `mezzanine`.
- **Rationale**
  - FFmpeg's `flags=` names make the two sides' settings the same words, which `run_delta.sh` passes on unchanged. The reference keeps its size; in the §5 graph its format conversion isn't the scale filter that `flags=` sets, so applying `--scaler` to it wouldn't match FFmpeg either. Every reference so far is yuv420p and kept at its own size, so its conversion has no format or size to change; what the flags do to a reference in another format belongs to #5.
  - Which built-in models a libvmaf has depends on how it was built, so libvmaf is the one to ask; FFmpeg's libvmaf filter has `model=version=` and `model=path=` too.
  - A path doesn't identify a file's contents; the SHA-256 does, and `models-300/checks.txt` (check 4) shows it is the value `shasum -a 256` computes.
  - Work order §1.2 asks every output for a `reference_mode`. Its two values describe how a ladder's reference is chosen; for a pair given by hand neither is known. The work order's §7 lists only `mezzanine` and `top-rung`, which is a difference the owner decides.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: confirmed by the owner (2026-10-23, D-033)

### P-018 The pooling measurement: candidate definitions on per-frame scores already committed

- **Choice**
  - `eval/pooling/pooling.py` computes, without changing Mirage's pooling (#6), on the full-length per-frame scores of Phase 0's baseline (`eval/baseline/bipbop16x9/report.vmaf.json`, 4 decimals) and on both sides of `eval/consistency/diag-after-300/plain`: the minimum and where it falls, the first frame's score and the minimum without the first frame; p1, p5, p10, p25 and p50 by the analyzer's current rule (`sorted[floor(n*p/100)]`), by nearest rank, and by linear interpolation (Hyndman and Fan's definition 7); the harmonic mean as `n / sum(1 / (x + 1)) - 1` and as `n / sum(1 / x)`; and, from FFmpeg's logs, libvmaf's own pooled values and the first frame's motion features.
  - It checks its own reading: the analyzer's recorded `vmaf_p5` against the current rule, libvmaf's harmonic mean against both forms, and the recorded minima. The outputs are in `eval/pooling/bipbop16x9/`, made from a clean tree, with the inputs' SHA-256.
- **Alternatives**
  - Wait for the full-length §5 run of D-018, whose scores are all after D-016.
  - More percentile definitions, such as Hyndman and Fan's definition 6.
  - Add libvmaf's harmonic mean to `mirage_vmaf`'s report now.
- **Rationale**
  - The baseline has full-length series, which show how much the definitions differ on long runs, and `diag-after-300/` has full precision and libvmaf's harmonic mean in FFmpeg's logs. The baseline's scores of 640x360 and 416x234 are from before D-016; the definitions are applied to them only as series (`eval/pooling/README.md`). The script runs on D-018's run unchanged once it is committed.
  - The three definitions are the current one, the textbook nearest rank, and the interpolation that common analysis tools use by default (NumPy's `percentile`, R's `quantile`).
  - Any new pooled value in the report would pick a definition, which is #6.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: confirmed by the owner (2026-10-23, D-033)

### P-019 Not yet in mirage_vmaf: ladder scoring and a config file

- **Choice**: `mirage_vmaf` still scores one pair. It gets neither a ladder mode (work order §1.2: the top rung of a master playlist by default, D-002, or a mezzanine) nor a config file (the work order's engineering constraints). `docs/PHASE1_DECISIONS_PENDING.md` describes both as pending items.
- **Alternatives**
  - A ladder mode now, with the player's current behavior: every `EXT-X-STREAM-INF` entry but the top one, audio-only and repeated playlists included, as `start_manifest()` does.
  - A config file now, in a format of the agent's choosing.
- **Rationale**
  - A ladder mode could be built without touching the player and without new dependencies (`ManifestParser` and pugixml are in the tree), but it needs choices that are the owner's or §2's: which entries are scored (audio-only entries, entries that share a playlist, equal bandwidths), what a failed rung does to the run and its exit status given P-006, and how one report holds several pairs and keeps the two reference modes apart before §7's schema exists. Its acceptance, both modes on one ladder, also needs a ladder with a mezzanine, which waits for D-011's clips.
  - The config file's format is the owner's to choose (`docs/PHASE1_DECISIONS_PENDING.md`).
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: confirmed by the owner (2026-10-23, D-033); D-029 and D-030 decide the ladder scoring and the config file

### P-020 The LICENSE text, and where it was checked

- **Choice**
  - `LICENSE` is the GNU General Public License, version 3, 29 June 2007, in the revision with `https` web addresses, copied from the `COPYING` file of Homebrew's GNU m4 package.
  - It is checked against every GPLv3 text that the Homebrew packages on the machine ship: byte for byte the same as those of GnuTLS, libidn2, libtasn1, libunistring and XZ Utils as well as m4's; the texts of FFmpeg, GMP, readline and siege are an older revision that differs from it in four web addresses, and gettext's differs in one (`eval/build_check/macos-2026-10-23-6a6661f.txt`, "LICENSE", with the `diff`s).
  - `README.md` states `GPL-3.0-or-later` and points to `LICENSE` (D-008), because the license text alone doesn't say "or any later version".
- **Alternatives**
  - FFmpeg's `COPYING.GPLv3`, the older revision with `http` addresses and the same terms.
  - The text downloaded from gnu.org.
  - A license notice with an SPDX identifier at the top of every source file as well.
- **Rationale**
  - It is the revision most of the copies on the machine share: 6 of the 12 GPLv3 texts in the record, shipped by six different upstream projects, are byte for byte `LICENSE`. That checks the text against independent copies without fetching anything.
  - Notices in every source file touch every file of the player, and the work order asks only for the license file; that is left to the owner.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: confirmed by the owner (2026-10-23, D-033). Whether legal or the company's open-source process has to confirm the license remains D-008's open question.

### P-021 How the FetchContent dependencies and the glad venv are pinned

- **Choice**
  - miniaudio and stb, which followed their `master` branch, are pinned by full commit hash to the commits the Phase 0 build fetched (`eval/baseline/environment.txt`): miniaudio `9634bedb5b5a2ca38c1ee7108a9358a4e233f14d`, stb `a7d4c2175630e1ea5b6f8dd00ab96fed0b97b96d`.
  - glad, pugixml, Catch2, Dear ImGui and nativefiledialog-extended keep their release tags. In both build checks each tag fetched the commit Phase 0 fetched (`eval/build_check/macos-2026-10-23.txt`, `macos-2026-10-23-6a6661f.txt`).
  - The glad venv of `docs/BUILD.md` step 3 and of `run_build_check.sh` installs `markupsafe==3.0.4` with `jinja2==3.1.6`: the version pip chose in the first build check.
- **Alternatives**
  - Pin the five tagged dependencies by commit hash as well, with the tag in a comment.
  - miniaudio by its tag `0.11.25`, which names the same commit (`macos-2026-10-23-6a6661f.txt`).
  - stb at `003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5`, the commit the first build check and the main working tree's builds of Phase 1 fetched; it differs from the pinned one only in stb's `CONTRIBUTING.md` (the same record).
  - Copy the two single-header libraries into the repository.
  - Leave MarkupSafe to pip.
- **Rationale**
  - The commits of Phase 0's build are the ones a committed record names for the baseline; a hash can't be moved the way a tag or branch can.
  - Release tags say which version a dependency is, and the records show what they named when each build ran, so a moved tag would show in the next build check.
  - No committed score depends on either pin: `mirage_vmaf` compiles neither miniaudio nor stb (`CMakeLists.txt`), and the release `mirage_vmaf` built after the pins has the SHA-256 of the one that made the scaler and model runs (`macos-2026-10-23-6a6661f.txt`, `eval/consistency/scalers-300/build.txt`).
  - With MarkupSafe named, the venv has exactly the packages of both build checks.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: confirmed by the owner (2026-10-23, D-033)

### P-022 The §8 build check: the clean worktree of 6a6661f, and what it stands for

- **Choice**
  - `eval/build_check/run_build_check.sh` (P-007), extended to record the dependency pins and what was fetched, the files glad generates, the SHA-256 of `mirage_vmaf`, how `LICENSE` compares with Homebrew's GPLv3 texts, and the help of `brew pin` and `brew extract`; run on a clean worktree of `6a6661f`, the commit after the pins and the `docs/BUILD.md` update, with the steps of `docs/BUILD.md`: both presets, first with the default Python, then with the venv (`eval/build_check/macos-2026-10-23-6a6661f.txt`).
  - `docs/BUILD.md` and `eval/build_check/README.md` cite the record, and say that a clean worktree on a machine that has the Homebrew packages only approximates the clean machine of §8's acceptance.
- **Alternatives**
  - A new macOS user account, a virtual machine or a hosted CI runner, starting without the Homebrew packages.
  - Following `README.md`'s quick start on its own as well.
- **Rationale**
  - Installing or removing Homebrew packages changes the owner's machine, and a hosted runner needs the repository pushed; the worktree still fetches every FetchContent dependency anew and builds nothing uncommitted (P-001, P-007).
  - The README's quick start (`macos-debug`, default Python) runs the glad step at which the record's default-Python build of the release preset stops on this machine; the README points to `docs/BUILD.md` for the rest.
- **Limits**: one machine, with its Homebrew packages, Xcode Command Line Tools and network; the player's window wasn't opened.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: confirmed by the owner (2026-10-23, D-033)

### P-023 The stream list and its fetch: playlists only, no media kept

- **Choice**
  - `eval/streams.md` lists bipbop 16x9, which every committed run used, and Apple's advanced TS sample, which P-002 considered, with only what committed files say about them.
  - `eval/streams/fetch_streams.py` fetches each master playlist of `eval/streams/streams.txt` and every playlist its `EXT-X-STREAM-INF` entries name, keeps them with four response headers (`Last-Modified`, `ETag`, `Content-Length`, `Content-Type`), says whether each is byte for byte the copy Phase 0 kept in `eval/baseline/inputs/`, and with `--probe` records ffprobe's parameters of each video playlist's first video stream. No media is kept.
  - One run, `--probe`, from a clean tree at `a1dec9f` (`eval/streams/fetched-2026-10-23/`).
- **Alternatives**
  - Mirror the media too (for example `ffmpeg -c copy`), for runs without network.
  - Keep every response header.
  - Also fetch the audio, subtitle and I-frame playlists.
  - List the DASH-IF and Unified Streaming streams the work order names, unexamined.
- **Rationale**
  - Every committed run read the streams from Apple's CDN (P-012, P-016); scoring a local copy instead would first need a check, on one pair, that it scores exactly as the URLs do, which no run needed.
  - The four headers identify the content; the others name CDN edge servers and request times.
  - The video playlists are what the scorers read; the advanced sample's URL, its streams' parameters and its playlists' segments were in no committed file before.
  - A stream nobody examined has no committed property to list.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: confirmed by the owner (2026-10-23, D-033)

### P-024 eval/run_all.sh: what it reruns, with which program, and what "as committed" means

- **Choice**
  - `eval/run_all.sh` reruns every committed Phase 1 measurement into a new directory outside the repository: the stream fetch, the color tag clips, the §5 runs before and after D-016, the scaler and model runs with their `compare_runs.py` summaries, the two FFmpeg default records, and the pooling measurement on the rerun of `diag-after-300`. Each `run_delta.sh` run gets the options its committed `run.cmd.txt` records, which `eval/tests/test_run_all.py` checks, together with a test that fails when a committed run has no step.
  - The runs made before D-016 use the `mirage_vmaf` of `83c8ed6`, the commit their `build.txt` names; all the others use that of the repository's HEAD. Unless given, each is built in a temporary worktree of its commit, release preset, only the `mirage_vmaf` target.
  - It compares the rerun with the committed outputs: Mirage's per-frame scores at full precision and every libvmaf metric of FFmpeg's log, frame by frame, and each pair's and run's verdict; the pairs of `compare_runs.py`'s JSON, its `settings` left out because they name a model file by its path; the series and skipped entries of `pooling.py`'s JSON; every playlist's SHA-256 and every probed stream's parameters; `clips.txt` as text; and the FFmpeg records' outputs, heap addresses in FFmpeg's error lines left out. It writes `run_all.md` and `rerun_check.md` (with JSON), and fails when anything differs.
  - The `checks.txt` of the committed run directories aren't rerun: they compare committed outputs with each other, and the comparison with the committed outputs covers the rerun. The build checks of `eval/build_check/` aren't part of it: they need their own empty directory and Python 3.13, and build everything.
  - One run from a clean tree, with both programs built by the script, verifies it (P-025).
- **Alternatives**
  - The after-D-016 runs with the programs they were made with, built at `5263061` and `8cc06a7`.
  - Leave the runs before D-016 out: only the current code makes paper data.
  - A plain shell script that reruns without comparing, or a comparison at the 6 decimals of FFmpeg's log only.
  - Rerun the commands of every `checks.txt` as well.
- **Rationale**
  - HEAD's `mirage_vmaf` is the program the paper's numbers will come from. The bilinear run of `scalers-300`, made with the program built at `8cc06a7`, has the scores of `diag-after-300`, made at `5263061`, at full precision on every frame (`eval/consistency/scalers-300/checks.txt`, check 4), so one current program can reproduce all the runs after D-016.
  - The runs before D-016 are part of the committed evidence for D-016, and only the code that made them can reproduce them.
  - Comparing at full precision shows whether the rerun reproduces the committed outputs exactly, not only within D-019's threshold.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: confirmed by the owner (2026-10-23, D-033)

### P-025 The verification run of eval/run_all.sh, and what of it is committed

- **Choice**
  - One run of `eval/run_all.sh` with no program given, so that it built both from their commits, from a clean tree at `e2169f5`, with its output directory and `TMPDIR` in a scratch directory outside the repository.
  - Committed in `eval/run_all/2026-10-23/`: `run_all.md` and `.json`, `rerun_check.md` and `.json`, each build's `build.txt` and logs, and the output of every command, with a `SHA256SUMS`. Not committed: the rerun outputs and the two programs.
  - Two earlier runs aren't cited: the first's configure logs showed the temporary worktree's path in the form CMake gives check variables, and the second kept its build records in a directory `.gitignore` leaves out. Both were fixed before this run (`cf27231`, `e2169f5`).
- **Alternatives**
  - Commit the rerun outputs too.
  - Run with the programs already built in the main working tree.
- **Rationale**
  - The record says what was rerun, with which programs, how it compared and how long it took; the rerun outputs would duplicate the committed ones wherever they match, which the record says they all do, and most of their size is the color cases' uncompressed clips.
  - Building inside the run shows both programs come from commits, and their SHA-256s can be compared with those of the committed runs.
- **Result** (`eval/run_all/2026-10-23/rerun_check.md`, `run_all.md`): every step ran and everything compared is as committed. The program built from `83c8ed6` has the SHA-256 that the `build.txt` of `diag-before-300` and `color-cases-before` records, and the one built from HEAD that of `scalers-300` and `models-300`. 544.3 s of wall time, both builds included.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: confirmed by the owner (2026-10-23, D-033)

### P-026 The full-length §5 run of D-018: every frame of the four pairs, four scoring processes at a time

- **Choice**
  - Build the release `mirage_vmaf` in the main working tree at a clean HEAD, `3650f83`, as P-015 and P-016 did. `eval/consistency/acceptance-full/build.txt` records the commit, the clean working tree, two builds that had nothing to do, and the program's SHA-256.
  - Run `eval/consistency/run_delta.sh` without `--frames` on the four pairs of D-018 with the defaults: plain FFmpeg, model vmaf_v0.6.1, scaler bilinear, 4 libvmaf threads, threshold 0.01 (D-019). With `--jobs 4`, four scoring processes run at a time, the two sides of two pairs (`acceptance-full/plain/`).
  - The inputs are fetched from Apple's CDN during the run, as in every committed run (P-012, P-016); no local mirror.
  - After committing the run, make `acceptance-full/checks.txt` from the clean tree, each command with its output: both sides at the 6 decimals of FFmpeg's log; the first 300 frames against `scalers-300/bilinear`, made by the same program with `--frames 300` and one process at a time; Mirage's scores against Phase 0's run of the player's analyzer at its 4 decimals; the conversions; the pairs' PTS; the checksums.
- **Alternatives**
  - `--jobs 1`, one scoring process at a time, as in every earlier committed run.
  - A build in a clean worktree, as P-007 and P-022 did.
  - A local mirror of the streams, scored once a run on one pair has shown that it scores as the URLs do.
- **Rationale**
  - How many processes run at once doesn't enter the scores: each side's libvmaf has its fixed 4 threads, and Mirage pairs frames in the order they are decoded and FFmpeg's libvmaf filter by their timestamps, neither by when they arrive; only the reports' wall times depend on the load. The run's first 300 frames have the scores of `scalers-300/bilinear`, made one process at a time (`checks.txt`, check 3).
  - The program has the SHA-256 of the one behind `scalers-300/` and `models-300/`, which builds of commits in clean worktrees reproduced (`eval/build_check/macos-2026-10-23-6a6661f.txt`, "Built programs"; `eval/run_all/2026-10-23/builds/head/build.txt`), and the main working tree was clean and at the commit the run records, as in P-012, P-015 and P-016.
  - D-018 names the streams. With a mirror, the report's inputs would be local files, and showing that a mirror scores as the URLs do takes a run on the URLs as well.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: confirmed by the owner (2026-10-23, D-033)

### P-027 The full-length run in eval/run_all.sh, and the pooling definitions over every frame

- **Choice**
  - `eval/run_all.py` gets two steps (`8aca01a`): `acceptance-full`, which reruns `eval/consistency/acceptance-full/plain` with the options of its `run.cmd.txt` and HEAD's `mirage_vmaf` and compares it with the committed run as the other `run_delta.sh` steps do; and `pooling-full`, which runs `eval/pooling/pooling.py` on that rerun and compares it with `eval/pooling/acceptance-full/`. Both run by default; `--skip-full` leaves them out, as `--skip-before` leaves out the runs made before D-016. `--jobs` stays 1 by default.
  - `eval/pooling/acceptance-full/`: `pooling.py --delta-run eval/consistency/acceptance-full/plain`, made from the clean tree at `dfadf66` after the run was committed: P-018's measurement over every frame after D-016.
  - One run of the two steps verifies them, from a clean worktree of `e0beb49` rather than the main working tree, in which documents were committed while it ran; its record is committed as P-025's is (`eval/run_all/2026-10-23-full/`).
- **Alternatives**
  - Leave the full-length run out of `run_all.sh`, or rerun it only when an option asks for it.
  - Add the full-length run as a second source of `pooling/bipbop16x9/` instead of a directory of its own.
- **Rationale**
  - `run_all.sh` reruns every committed Phase 1 measurement (P-024), and its test fails for a committed run without a step. The full-length run is the §5 acceptance output, the one run that most needs to be reproducible.
  - Each side of each pair took longer than the whole `run_all.sh` run of 2026-10-23: from 1134.6 to 1396.7 s (the pairs' `delta.md` in `acceptance-full/plain/`), against 544.3 s (`eval/run_all/2026-10-23/run_all.md`). So rerunning the other steps alone should take one option, not a list of eleven steps.
  - A directory of its own leaves `pooling/bipbop16x9/` as committed, and each pooling step reruns from the outputs of the steps it needs.
- **Result** (`eval/run_all/2026-10-23-full/rerun_check.md`, `run_all.md`): one run, `eval/run_all.sh <out_dir> --only pooling-full --jobs 4` from a clean worktree of `e0beb49`, which built HEAD's `mirage_vmaf` itself, found everything of both steps as committed: every verdict, Mirage's per-frame scores at full precision, every libvmaf metric of FFmpeg's logs and every pooling series. The program had the SHA-256 of `acceptance-full/build.txt`. 2707.1 s of wall time, the build included.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: confirmed by the owner (2026-10-23, D-033)

### P-028 The record of the unit test runs behind the Phase 1 PR description

- **Choice**
  - `eval/test_runs/macos-2026-10-23-8aca01a.txt`: from the clean main working tree at `8aca01a`, each command with its output and exit status: the changes to the C++ sources and CMake files since the build check at `6a6661f` (none), a debug build (nothing to do), whether the gitignored subtitle fixture is present, `mirage_tests '~[audio]'` in the form of `docs/BUILD.md` step 4 (a second run only if the first fails), and the four Python suites of `eval/`. The PR description quotes it.
- **Alternatives**
  - Paste the outputs into the PR description only.
  - A new build check in a clean worktree (P-022), whose record has the unit tests too.
- **Rationale**
  - Hard rule 5 of the work order has a PR's description show the actual output of its tests, and as a report its numbers come from committed outputs (hard rule 2).
  - The main working tree is where the measurements' programs were built. Its subtitle fixture, which `.gitignore` leaves out, lets that test pass there; the build check at `6a6661f`, whose C++ sources and CMake files are those of `8aca01a`, shows the one failure of a clean checkout (`docs/BUILD.md`, step 4).
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: confirmed by the owner (2026-10-23, D-033)

### P-029 The reference format cases: references that aren't yuv420p, scored before and after D-016

- **Choice**
  - `eval/consistency/color_cases.py --set formats` makes clips as P-013's are made, from FFmpeg's `testsrc2` pattern, 10 frames, references at 1920x1080 and distorted inputs at 640x360, tagged with `setparams`: references in yuv420p10le, yuv422p and yuv444p, losslessly in FFV1, and a full range reference losslessly in H.264 (libx264 with `-qp 0`), which FFmpeg's decoder gives as yuvj420p, each against a yuv420p distorted input with another matrix and with the same tags; a 10-bit and a full range H.264 distorted input against a yuv420p reference; and YCgCo on both inputs, as a refused case. The script checks every clip's tags and that all clips of one size made from one pixel format have the same pixels (`eval/consistency/format-cases-before/clips.txt`).
  - Both pairs files go through `run_delta.sh --frames 10`, with `TMPDIR` above the clips, from a clean worktree of `d993686`, twice: with the `mirage_vmaf` of `83c8ed6`, which converts no color (`eval/consistency/format-cases-before/`), and with that of `d993686` (`format-cases-after/`), each built in a new worktree of its commit, release preset, the `mirage_vmaf` target only (`build.txt`). `checks.txt` was made afterwards from the clean tree. `eval/run_all.sh` reruns them (steps `format-clips`, `format-cases-before`, `format-cases-after`).
- **Alternatives**
  - Real streams in these formats, such as a 10-bit HLS rung or a full range H.264 file; none is in the repository's stream list (`eval/streams.md`).
  - MJPEG for a yuvj420p reference, which is lossy.
  - More formats and tags: RGB, gray, 12-bit, 4:4:4 distorted inputs, distorted inputs without a range tag.
  - Only the current program, without a run before D-016.
  - The release build of the main working tree, as P-015, P-016 and P-026 did.
- **Rationale**
  - Lossless coding keeps the pattern's pixels, so within one pixel format the cases differ only in their tags, and the lossless H.264 clips decode to the bytes of the uncompressed yuv420p ones (the MD5s in `clips.txt`).
  - The run before D-016 shows what D-016's implementation changed for these references (`j01`, `d01`), which only the program of the other runs before D-016 can show.
  - New worktrees tie both programs to their commits; the program of `83c8ed6` has the SHA-256 of `diag-before-300/build.txt`. Running from a separate worktree kept the runs' git state clean while documents were edited in the main working tree.
- **Limits**: synthetic clips, 10 frames, and the §5 graph of FFmpeg 8.1.1 only; the formats and tags above only.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: confirmed by the owner (2026-10-23, D-033)

### P-030 A warning for a reference that isn't yuv420p, and a corrected --help

- **Choice**
  - Until the owner decides the package "D-016: references that aren't yuv420p" (`docs/PHASE1_DECISIONS_PENDING.md`), the analyzer scores such pairs as before and records a warning in `VMAFResult::warnings` whenever the reference's decoder gives another pixel format than yuv420p (`vmafcolor::converts_reference()`), also logged as a warning. `mirage_vmaf`'s report has a `warnings` list, empty when there is none. The delta tool lists each warning of the report among its own, prefixed `mirage_vmaf: `; like those, it doesn't fail a pair (P-009). The player shows nothing new.
  - `mirage_vmaf --help` no longer says that the reference frame keeps its color: it keeps its size and color matrix, its conversion to yuv420p makes a yuvj420p reference limited range, and for a reference that isn't yuv420p FFmpeg's graph converts the reference instead.
- **Alternatives**
  - Refuse such pairs, as matrices libswscale lacks are refused (P-014).
  - No warning until the decision.
  - A narrower condition: only when the two inputs' tags differ, or when the reference is yuvj420p, the cases that fail in `eval/consistency/format-cases-after/`.
  - The warning in the player's VMAF panel as well.
- **Rationale**
  - Refusing would change what the player and `mirage_vmaf` do with such inputs, which is the owner's choice (option 3 of the package); the warning changes no score and no default, and keeps scores that can differ from FFmpeg's from passing silently.
  - The condition is the one under which FFmpeg's graph has to convert the reference's pixel format, its `format` filter taking yuv420p only; with a reference in each of four such formats, `tests/test_vmaf_color.cpp` shows the graph leaving the distorted frame's tags alone. The cases in which Mirage still agrees (`f04`, `f06`, `f08`) are synthetic, so "can differ" is what the evidence supports for any such reference.
  - The player's own code wasn't changed in Phase 1; a warning in its panel would be a change to its interface.
  - The help text has to say what the program does.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: confirmed by the owner (2026-10-23, D-033); D-022 replaces the warning with FFmpeg's conversion of the reference once that is implemented

### P-031 Repeated runs of the intermittent Demuxer test, at the commit Phase 1 started from and at the branch's head

- **Choice**
  - Build `mirage_tests` of `37472fd`, the first commit of Phase 1, whose code is `main`'s (`a102e80`), and of `855a71b`, the branch's head when the runs were made, after which no commit changes C++ code, each in a new worktree of its commit, debug preset, the `mirage_tests` target only, configured with the glad venv of `docs/BUILD.md` step 3 as Python. Run `Demuxer: seamless video and audio switches on a local file` alone, with the alarm of `docs/BUILD.md` step 4, from each worktree, one run at a time, the two builds alternating, while nothing else scores or builds on the machine. Record each run's exit status and wall time, each failed assertion with its expansion, both builds' SHA-256, the miniaudio commit each fetched, and that the test and `src/Demuxer.*` don't differ between the two commits.
  - Two series: 150 runs of each build (`eval/test_runs/demuxer-switch-150-2026-10-23.txt`), then, because one failure in 150 at the head and none at the base couldn't tell the two builds apart, 500 more of each (`eval/test_runs/demuxer-switch-500-2026-10-23.txt`). Both are committed.
- **Alternatives**
  - No record, and the documents reduced to "reported as intermittent; no committed record".
  - The whole suite repeated, or runs in the main working tree.
  - One series only.
- **Rationale**
  - The documents said the test failed now and then on both commits, from runs whose outputs weren't committed; the owner's decision on the test needs a committed measurement.
  - The test and `src/Demuxer.*` are the same at both commits, so a difference between the builds would point at what else they link or at timing, not at the test's code.
  - Alternating the builds spreads any change in the machine's load over both; one run at a time, because the test writes its media file under a fixed name in the temporary directory.
- **Result** (the two records): of 150 runs of each build, none failed at `37472fd` and 1 at `855a71b`; of 500 more of each, 5 and 3. Every failure is one assertion, at line 441 or 491 of `tests/test_demuxer_switch.cpp`, and both lines failed at both commits.
- **Limits**: one machine and one kind of load. A few failures in some hundred runs only roughly bound a failure rate.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: confirmed by the owner (2026-10-23, D-033)

### P-032 The records after the code changes of the review fixes: the unit tests, and a rerun of every measurement

- **Choice**
  - The unit test record of P-028 again, by the same commands, from the clean main working tree at `9855319`, after which no commit changes code or tests (`eval/test_runs/macos-2026-10-23-9855319.txt`). The PR description quotes it instead of the record at `8aca01a`.
  - `eval/run_all.sh` with no program given and `--jobs 4`, every step, the full-length ones included, from a new clean worktree of the same commit, with its output directory and `TMPDIR` outside the repository; its record is committed as P-025's and P-027's are (`eval/run_all/2026-10-23-fix/`).
- **Alternatives**
  - Keep citing the record at `8aca01a` and the earlier `run_all.sh` records, whose `mirage_vmaf` is the program before P-030's warning.
  - `--skip-full`, or `--jobs 1`.
- **Rationale**
  - P-030 changed `src/` and `tests/`, so the program at the head is not the one the committed runs after D-016 were made with; rerunning every committed measurement with it shows whether the change moved any score, and the PR's test record has to be of the code it merges.
  - `--jobs` doesn't enter the scores (P-026), and with 4 the full-length step takes about as long as in P-027's rerun.
- **Result** of the rerun (`eval/run_all/2026-10-23-fix/rerun_check.md`, `run_all.md`): every step ran, and everything compared is as committed. The program built from `9855319` has the SHA-256 of `eval/consistency/format-cases-after/build.txt`, and the one built from `83c8ed6` that of the runs before D-016. 2968.8 s of wall time, both builds included.
- **Result** of the unit tests (`eval/test_runs/macos-2026-10-23-9855319.txt`): `mirage_tests '~[audio]'` passed at the first run, 90866 assertions in 244 test cases, and the four Python suites passed, 20, 85, 7 and 16 tests.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: confirmed by the owner (2026-10-23, D-033)

### P-033 How D-022 is implemented, and what remains of P-030's warning

- **Choice**
  - **The tags.** `vmafcolor::negotiated()` (`src/VMAFColor.h`) derives the color matrix and range that FFmpeg 8.1.1's libavfilter negotiates for both inputs of the §5 graph's libvmaf filter, from both frames' pixel formats and tags, as the FFmpeg 8.1.1 sources do it (`ffmpeg-8.1.1.tar.xz`, whose SHA-256 is the one in the installed ffmpeg-full formula; `libavfilter/buffersrc.c`, `formats.c`, `avfiltergraph.c`, `vf_scale.c`). A buffer source offers its frames' matrix and range, limited as well when the range is untagged, full range only for a yuvj format, and every matrix and range for a format that isn't plain YUV (RGB, gray, palette, XYZ, floating point). For a yuv420p reference the reference's own lists reach the libvmaf filter. For any other reference, the scale filter libavfilter inserts cuts the reference's lists off, and libavfilter reduces the shared lists to the distorted input's single entry, else the reference's, else unspecified. For a yuv420p reference the analyzer keeps `target()`, D-016's rule as implemented (D-021).
  - **The conversion.** A reference that isn't yuv420p goes through `vmafcolor::convert()`, the `sws_scale_frame()` call and frame handling of FFmpeg's scale filter, with a context from `sws_alloc_context()` given `SWS_BICUBIC` (`vmafcolor::kInsertedScaleFlags`, the inserted filter's `flags:0x00000004` in the logs of `eval/consistency/format-cases-before/`) and one thread, as the distorted frames' conversion has (P-014). A yuv420p reference keeps the plain copy it had. The distorted frames get the same tags, scaled with the chosen scaler.
  - **The report.** `ref_conversion` lists the reference's conversions in the form of `dis_conversion`, empty for a yuv420p reference. The delta tool shows it next to FFmpeg's scale filter lines and warns when its tags differ from those of the scale filters FFmpeg inserts (`auto_scale_N`), as it warns for the distorted frames (P-015).
  - **Where FFmpeg's negotiation and D-022's wording part.** D-022 leaves the distorted frames' color as it is. FFmpeg 8.1.1's graph does so whenever the distorted input's buffer source offers one matrix and one range, as in every committed case. For a distorted input without a range tag against a reference that has one, or is yuvj, both inputs get the reference's range, so a full range reference makes the distorted frames full range; for a distorted input in a format that isn't plain YUV, such as gray or RGB, both get the reference's matrix and range. The analyzer follows FFmpeg there too, as D-022's rationale, parity with plain FFmpeg, has it (the negotiation test in `tests/test_vmaf_color.cpp`). That departs from D-022's wording, and it is provisional until the owner decides between FFmpeg's negotiation and the wording for these inputs (`docs/PHASE1_DECISIONS_PENDING.md`, "A departure from a decision's wording"). The code has no switch for the wording: for these inputs it would first need the owner's definition, such as which matrix a reference gets against distorted frames that aren't YCbCr. No committed delta run has such an input (MEASUREMENT PENDING).
  - **The warning.** P-030's warning about a reference that isn't yuv420p is gone. The analyzer warns instead, once per input, when frames it converts, the distorted frames always and the reference when it isn't yuv420p, are in a pixel format outside `vmafcolor::verified_formats()`: the formats that `tests/test_vmaf_color.cpp` compares byte for byte with the graph run through libavfilter on its default threads, as reference and as distorted input, against tagged, untagged and full range inputs. NV12 and NV21 are left out: with a change of matrix, libswscale 9.5 converts them differently on four threads than on one (the same test), and FFmpeg's scale filters run on as many threads as `ffmpeg` finds CPUs. The player shows the warnings nowhere, as before.
  - **Kept as implemented (D-021).** For a yuv420p reference without a range tag and distorted frames tagged limited in a yuvj format or one that isn't plain YUV, `target()` keeps limited where FFmpeg's graph tags the frames unspecified. libswscale converts both alike, so the pixels are the same (`tests/test_vmaf_color.cpp`), and there is no warning; only `dis_conversion`'s `to` range differs from FFmpeg's scale filter line, which the delta tool would show as a warning.
- **Alternatives**
  - D-022's wording where FFmpeg changes the distorted frames' range or matrix: convert the reference to the distorted frames' tags as they are.
  - No warning at all; a warning only for formats that aren't plain YUV, which no committed delta run covers; refusing unverified formats.
  - As many swscale threads as FFmpeg's scale filters use, which might give NV12 and NV21 FFmpeg's bytes on the same machine (not measured).
  - `negotiated()` for a yuv420p reference too, which changes no pixel, only that tag.
- **Rationale**
  - The same library call with the same frames, tags and flags gives FFmpeg's bytes; the rule names the source files it comes from, and the test checks it against libavfilter for every combination of five kinds of pixel format on both inputs, with five tag sets on the reference and four on the distorted input.
  - A warning where the conversion is checked against FFmpeg's would say nothing; where it isn't checked, or is known to depend on the thread count, the scores can differ.
  - D-021 keeps a yuv420p reference's rules as implemented, and the difference there is a tag, not a pixel.
- **Limits**: the rule is FFmpeg 8.1.1's; synthetic frames in the test, and the committed runs' clips (P-034).
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: confirmed by the owner (2026-10-26, D-041); the behavior for the two kinds of distorted input under "Where FFmpeg's negotiation and D-022's wording part", which departs from D-022's wording, is decided by D-035

### P-034 The reference format cases after D-022, and how eval/run_all.sh reruns all three runs of them

- **Choice**
  - Build the release `mirage_vmaf` in a new worktree of `3ea0281`, the commit after the code of D-021, D-022 and the delta tool's changes, release preset, the `mirage_vmaf` target only, as P-029 did (`eval/consistency/format-cases-d022/build.txt`). From that worktree, make the clips with `color_cases.py --set formats` and run `run_delta.sh` on both pairs files with the options of `format-cases-after/`'s `run.cmd.txt`, `TMPDIR` above the clips (`format-cases-d022/main/`, `refused/`). `checks.txt` afterwards from the clean tree at `5135d58`.
  - `eval/run_all.sh`: a step `format-cases-d022` with HEAD's program; `format-cases-after` with the program of `d993686`, the commit its `build.txt` names (`BEFORE_D022`, `--mirage-vmaf-before-d022`), because HEAD's converts a reference that isn't yuv420p since D-022 and no longer reproduces those scores. The other runs made after D-016 keep HEAD's program and the options their `run.cmd.txt` records: their references are all yuv420p, which D-022 leaves as it was, so HEAD's program should reproduce them, and a rerun with it shows that. The runs before D-016 keep the program of `83c8ed6`.
- **Alternatives**
  - `format-cases-after` with HEAD's program, its rerun no longer as committed.
  - The program of `9855319`, which has `format-cases-after`'s SHA-256 too (`eval/run_all/2026-10-23-fix/rerun_check.md`), instead of `d993686`'s.
  - A build in the main working tree, as P-015 and P-016 did.
- **Rationale**
  - A new directory keeps the committed runs as records (hard rule of this work), and the same options and clips make the three runs compare case by case.
  - `d993686` is the commit the record itself names, as `83c8ed6` is for the runs before D-016.
- **Result** (`eval/consistency/format-cases-d022/checks.txt`): the clips are those of `format-cases-before` (check 2); every case of `main/` passes, every frame agreeing at the 6 decimals of FFmpeg's log (check 4); `j01` has its scores from before D-016 at full precision on all 10 frames (check 5); every report's `ref_conversion` has the tags of FFmpeg's `auto_scale_0` and its `dis_conversion` those of `Parsed_scale_1` (checks 3 and 6), and the delta reports have no warning (check 8); FFmpeg's side has every libvmaf metric of every frame of both earlier runs (check 7); both programs refuse YCgCo on both inputs (check 9).
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: confirmed by the owner (2026-10-26, D-041)

### P-035 The FFmpeg libraries' versions in mirage_vmaf's report (D-021)

- **Choice**
  - The report's `ffmpeg_libraries`, next to libvmaf's versions: libavutil, libavcodec, libavformat, libswscale and libavfilter, each with the version its `*_version()` function reports at run time, as `major.minor.micro` (`vmaf_ffmpeg_libraries()`). libswresample and libavdevice, which the analyzer doesn't use, are left out.
  - The delta tool reads the run-time column of `ffmpeg -version`'s library lines (`environment.txt`), shows both sides' versions of each library in the pair reports and both sets in the run report, and warns, without failing the pair, when a library that both sides name has other versions, as for libvmaf's versions (P-009).
- **Alternatives**
  - The versions the build compiled against as well; FFmpeg's version string (`av_version_info()`); every library the program links.
  - A failure when the versions differ.
- **Rationale**
  - D-021 names the run-time versions, libswscale and libavfilter among them; these five decode the inputs, convert the frames, and give the negotiation the analyzer follows.
  - Both sides link the same Homebrew libraries on the machine of the measurements, and a difference elsewhere is what the warning is for.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: confirmed by the owner (2026-10-26, D-041)

### P-036 D-032's fixes in the tests and the analyzer

- **Choice**
  - `tests/TestTemp.h`: one directory per run of `mirage_tests`, made by `mkdtemp()` as `mirage_tests.XXXXXX` under the system's temporary directory on first use, and removed with its contents when the process exits normally. Every test that writes a file writes it there: the tests of `filehash`, `Demuxer` switching, `PipelineState`, HLS variants, scrub following, the network logger, subtitle selection, `VMAFReport` and `VMAFAnalyzer`.
  - The subtitle test writes its three cues to an SRT file in that directory and reads it back; data/ is left alone. The test is renamed `SidecarSubtitles — decodes an SRT file`.
  - The analyzer's decode context frees its demuxer, decoder and packet in its destructor, so every early return of `analyze_pair()` leaks nothing; a test scores an input that opens without a video stream and one whose FFV1 decoder rejects its extradata, on either side.
- **Alternatives**
  - A directory per test case; unique file names in the shared directory; Catch2 listeners for the cleanup.
  - Committing the SRT file with a `.gitignore` exception (the package's other options).
  - Closing both inputs before each early return instead of a destructor.
- **Rationale**
  - One directory per process is what makes two runs at the same time safe, and removing it at exit leaves nothing behind; a crash leaves it, under a name that says what it is.
  - A destructor covers early returns added later as well.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: confirmed by the owner (2026-10-26, D-041)

### P-037 The rerun of the committed measurements after D-021, D-022 and D-032, and what of it is committed

- **Choice**
  - One run of `eval/run_all.sh --skip-full --jobs 4` with no program given, so that it built all three programs from their commits, from a new clean worktree of `5135d58`, the commit after this round's changes to the code and to the eval tooling, with its output directory and `TMPDIR` outside the repository. After `5135d58` the code changes in comments only (`efaa2ef`).
  - Committed in `eval/run_all/2026-10-23-d022/` as P-025's record is: `run_all.md` and `.json`, `rerun_check.md` and `.json`, the three builds' `build.txt` and logs, and the output of every command, with a `SHA256SUMS`.
- **Alternatives**
  - Every step, the two over every frame included, as P-032 did.
  - `--only` the steps of the reference format cases.
- **Rationale**
  - D-021, D-022 and D-032 must leave every committed run as it is: the runs with yuv420p references for HEAD's program, and the others for the programs that made them; a rerun of every step shows that, the two over every frame aside. Those are to be made again with the decided defaults (D-024), and the first 300 frames of `acceptance-full` have the scores of `scalers-300/bilinear` (`eval/consistency/acceptance-full/checks.txt`, check 3), which this run reruns.
- **Result** (`eval/run_all/2026-10-23-d022/rerun_check.md`, `run_all.md`): every step ran, and everything compared is as committed. The program built from `5135d58` has the SHA-256 of `eval/consistency/format-cases-d022/build.txt`, the one built from `d993686` that of `format-cases-after/build.txt`, and the one built from `83c8ed6` that of the runs before D-016. 521.0 s of wall time, the three builds included.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: confirmed by the owner (2026-10-26, D-041)

### P-038 How D-024 is implemented, and how the runs made with bilinear stay reproducible

- **Choice**
  - `VMAFOptions::scaler` defaults to bicubic, so the player's analysis (`start()`, `start_manifest()`) and `mirage_vmaf` without `--scaler` scale the distorted frames with `SWS_BICUBIC`. `mirage_vmaf`'s command line takes the analyzer's default (`vmafcli::Args::scaler` is `VMAFOptions{}.scaler`), and `--help` says so. The reference keeps its own flags, as before (P-017, P-033).
  - `run_delta.sh`'s `--scaler` defaults to bicubic: FFmpeg's scale filter gets `flags=bicubic`, and a `mirage_vmaf` whose `--help` lists `--scaler` gets `--scaler bicubic`, the default included. A `mirage_vmaf` without the option, such as the one of `83c8ed6`, scores bilinear, so its runs fail with `SETTINGS MISMATCH` unless they name bilinear. `run.cmd.txt` records the scaler either way.
  - `eval/run_all.sh` gives every rerun the scaler its `run.cmd.txt` records, as it did; a test now checks that every delta step names its scaler, model and threads. Every committed run made before D-024 recorded the scaler it ran with, bilinear unless it named another, so the reruns keep it, `acceptance-full` included.
  - The command blocks of `eval/consistency/README.md` for the runs made before D-024 stay as they were run, with a note that those naming no scaler ran with bilinear, the default then.
  - In the delta tool's tests, FFmpeg's stand-in states the flags of its graph's `flags=`, and `mirage_vmaf`'s stand-in without `--scaler`, which stands for the programs before that option, is given `--scaler bilinear`.
- **Alternatives**
  - Leave `run_delta.sh`'s default at bilinear and name bicubic where wanted. D-024 makes bicubic the delta tool's default for both sides.
  - Give `mirage_vmaf` no `--scaler` when the scaler is the default, so that every run exercises its own default; the settings check would still catch a difference.
  - Add `--scaler bilinear` to the earlier runs' command blocks.
- **Rationale**
  - One default in one place, so the command line can't drift from the analyzer, the player's included.
  - A scaler given explicitly shows in every pair's `cmd.txt`; `mirage_vmaf`'s own default is checked directly instead (P-041).
  - The command blocks are records of how the runs were made, and each run's `run.cmd.txt` has its full command.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: confirmed by the owner (2026-10-26, D-041)

### P-039 How D-027 is implemented: the pooled values from the per-frame scores

- **Choice**
  - `vmafpool::pool()` (`src/VMAFPool.h`) computes every pooled value from the per-frame scores:
    - the mean, the harmonic mean and the minimum with libvmaf's formulas, `sum / n`, `n / sum(1 / (x + 1)) - 1` and the smallest score, summed frame by frame in index order;
    - the minimum of frames 1 to n − 1 under the name `min_without_first_frame`, the name `eval/pooling/pooling.py` gives it, `null` for a pair of one frame;
    - p1, p5, p10, p25 and p50 by linear interpolation as `pooling.py`'s `linear` definition computes it: `h = (n − 1) p / 100`, `k = floor(h)`, the fraction the double nearest `((n − 1) p mod 100) / 100`, and the difference, the product and the sum each rounded on their own. `src/VMAFPool.cpp` is compiled with `-ffp-contract=off`: Apple clang fuses a multiplication and an addition into one rounding by default, which can change the last bit (`tests/test_vmaf_pool.cpp` has such a case).
  - The analyzer no longer asks libvmaf's `vmaf_score_pooled()` for the mean and the min. `VMAFResult::pooled` holds every value; `vmaf_mean`, `vmaf_min` and `vmaf_p5` stay, filled from it, for the player's VMAF panel and `write_json`, whose layout doesn't change, so the player's p5 is the interpolated one.
  - `mirage_vmaf`'s report: `pooled { mean, harmonic_mean, min, min_without_first_frame, p1, p5, p10, p25, p50 }` at full precision.
  - Checks: known values, the same in `tests/test_vmaf_pool.cpp` and `eval/pooling/tests/test_pooling.py`; the mean, minimum and harmonic mean against libvmaf's own pooling, of scores imported with `vmaf_import_feature_score()` (`vmaf_feature_score_pooled()`) and of a context the test scores 24 frame pairs in (`vmaf_score_pooled()`), compared with `==`; and a committed cross-check (P-041).
  - The delta reports show both sides' harmonic means next to their means and minima, outside the verdict (P-009). `pooling.py --check-pooled` computes the values as `mirage_vmaf` does, `POOLED_D027`, and checks them against the report's; without the option its output is as before.
- **Alternatives**
  - Keep `vmaf_score_pooled()` for the mean, the minimum and the harmonic mean, and compute only the percentiles: libvmaf's values by construction, but undefined over the several libvmaf contexts in which D-028 scores a pair with gaps.
  - Also call `vmaf_score_pooled()` on a pair scored in one context and warn when it differs.
  - Exactly rounded sums (`math.fsum`), as `pooling.py`'s own mean and shifted harmonic mean are; they can differ from libvmaf's in the last bits (`eval/pooling/README.md`).
  - Another name for the minimum without the first frame; leaving out the first frame of each libvmaf context once D-028 restarts them.
- **Rationale**
  - One definition for a pair scored in one context and in several; for one context the values are libvmaf's to the last bit (the tests; `eval/consistency/defaults-300/checks.txt`, check 5).
  - The same arithmetic as `pooling.py`'s gives a reader who recomputes the values from the per-frame scores the same doubles, which the committed cross-check shows.
  - The name says what the value is; with one frame no frame is left.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: confirmed by the owner (2026-10-26, D-041)

### P-040 How D-030 is implemented: what a config file accepts, and how the command line replaces it

- **Choice**
  - `--config FILE` is a row of the option table. The file has one `name = value` per line: `name` a long option's name without the leading `--`, `value` the rest of the line after the first `=`, both without the spaces and tabs around them and taken as they are, with no quotes, escapes or comments after a value. Blank lines and lines whose first character other than white space is `#` are skipped; a UTF-8 byte order mark at the start and a carriage return at a line's end are ignored.
  - Every option but `--config` can be set in the file, the required `--ref`, `--dis` and `--out` included, each once; each value is checked by the option's own setter, also where the command line replaces it.
  - An option on the command line replaces the file's setting. Options that exclude each other, `--model` and `--model-file`, are one setting, the model: either on the command line replaces either in the file, and both in the file, or both on the command line, are an error.
  - Values mean what they mean on the command line: relative paths from the current directory.
  - An unreadable file, an unknown or repeated name, a name written with `--`, `config` in the file, a line without `=`, and an empty or bad value are errors that name the file and the line; `mirage_vmaf` exits with status 2, as for bad arguments.
  - The report's `config_file` gives the path as given and the SHA-256 of the bytes that were parsed, read once and hashed with `filehash`; `null` without a config file.
- **Alternatives**
  - Comments after a value, or quoted values.
  - Only the scoring settings in the file, not the inputs and the output.
  - Relative paths from the file's directory.
  - Leaving the settings the command line replaces unchecked.
- **Rationale**
  - D-030's rationale: nothing can be set in one place but not the other, so every option of the table is allowed, and each is checked by the same code.
  - Without quoting rules, paths and URLs with `#`, `=` or spaces are written as they are.
  - Hashing the bytes that were parsed makes the recorded SHA-256 that of the settings the run used.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: confirmed by the owner (2026-10-26, D-041)

### P-041 The run with the defaults of D-024 and D-027, and the cross-check of its pooled values

- **Choice**
  - Build the release `mirage_vmaf` in a new worktree of `02ebed5`, the commit after this round's changes to the code and to the delta tool, release preset, the `mirage_vmaf` target only, as P-034 did (`eval/consistency/defaults-300/build.txt`), and from that worktree run `run_delta.sh --frames 300 --jobs 1` on the four pairs of D-018, given no `--scaler`, so with the defaults on both sides, bicubic and vmaf_v0.6.1 (`defaults-300/plain/`). Its `run.cmd.txt` has `--scaler bicubic --model vmaf_v0.6.1`, since `run_delta.py` writes every option a run had, the defaults included; that the command gave neither is the agent's account, which no committed output records (P-064).
  - Run `pooling.py --delta-run eval/consistency/defaults-300/plain --check-pooled` from the clean tree at `a0a9058` (`eval/pooling/defaults-300/`), and make `defaults-300/checks.txt` from the clean tree at `9d8c173`, each command with its output. Check 4 runs the same program directly, with `--frames 300` and no `--scaler`, on each pair, and on gear2 with its settings in a config file.
  - `eval/run_all.sh` steps `defaults-300`, HEAD's program with the options of its `run.cmd.txt`, bicubic named, and `pooling-defaults-300`, with `--check-pooled`.
- **Alternatives**
  - The checks in a scratch directory only, uncommitted.
  - Every frame: the full-length report with bicubic that D-024 asks for is a run of its own.
  - `--scaler bicubic` named in the run.
- **Rationale**
  - 300 frames are the range of `scalers-300/`, whose bicubic run named `--scaler bicubic`, so the two runs compare frame by frame, and its reports' pooled mean and min came from `vmaf_score_pooled()` for the same per-frame scores.
  - A run given no `--scaler` has `run_delta.sh`'s defaults, which it passes to both sides explicitly; `mirage_vmaf`'s own default is what the direct runs of check 4, given no `--scaler`, show in a committed output.
- **Result** (`eval/consistency/defaults-300/checks.txt`): every pair passes, every frame agreeing at the 6 decimals of FFmpeg's log, with no warning (check 8). Both sides used bicubic: `mirage_vmaf` was given `--scaler bicubic`, FFmpeg's scale filter `flags=bicubic` and states `flags:0x00000004` (check 2). Mirage's per-frame scores and FFmpeg's every libvmaf metric are those of `scalers-300/bicubic` on all 300 frames of every pair (check 3). The direct runs without `--scaler`, and the run with a config file, report bicubic and score all 300 frames as the run did; the config file's SHA-256 in the report is the one `shasum` computes (check 4). The reports' pooled mean and min equal those of `scalers-300/bicubic` at full precision (check 5); their mean, min and harmonic mean those of FFmpeg's log at 6 decimals (check 6); and all nine pooled values those `pooling.py` computes, at full precision (check 7; `eval/pooling/defaults-300/pooling.md`).
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: confirmed by the owner (2026-10-26, D-041)

### P-042 The rerun of the committed measurements after D-024, D-027 and D-030, and what of it is committed

- **Choice**
  - One run of `eval/run_all.sh --skip-full --jobs 4` with no program given, so that it built all three programs from their commits, from a new clean worktree of `0496b21`, the commit after this round's changes to the code, the eval tooling and the committed runs, with its output directory and `TMPDIR` outside the repository. After `0496b21` only documents change.
  - Committed in `eval/run_all/2026-10-23-d027/` as P-037's record is: `run_all.md` and `.json`, `rerun_check.md` and `.json`, the three builds' `build.txt` and logs, and the output of every command, with a `SHA256SUMS`.
- **Alternatives**
  - Every step, the two over every frame included, as P-032 did.
  - `--only` the steps this round's changes touch.
- **Rationale**
  - Rerun with the settings they recorded, the committed runs must keep every score: a rerun of every step but the two over every frame shows that for the programs at hand. Its `pooling` step also compares the pooled mean and min of HEAD's reports, which the analyzer pools itself since D-027, with those that the program of `diag-after-300` took from `vmaf_score_pooled()`, since `pooling.py` records them. The steps over every frame are left to the full-length report with bicubic that D-024 asks for.
- **Result** (`eval/run_all/2026-10-23-d027/rerun_check.md`, `run_all.md`): every step ran, and everything compared is as committed, `defaults-300` and `pooling-defaults-300` included. The program built from `0496b21` has the SHA-256 of `eval/consistency/defaults-300/build.txt`, the one built from `d993686` that of `format-cases-after/build.txt`, and the one built from `83c8ed6` that of the runs before D-016. 529.5 s of wall time, the three builds included.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: confirmed by the owner (2026-10-26, D-041)

### P-043 How D-028's pairing by timestamp is implemented, and the values it leaves open

- **Choice**
  - **A pure module.** `vmafalign::Aligner` (`src/VMAFAlign.h`) is told each input's decoded frames in the order its decoder outputs them, with their `best_effort_timestamp` (P-006), and says step by step what the analyzer does: decode a frame of one input, score the frames at the heads of both queues as a pair, or take a head frame off its queue unscored. It has no FFmpeg type but `AVRational` and is unit-tested on timestamp sequences, among them every case of the plan's "Tests" (`tests/test_vmaf_align.cpp`).
  - **Times.** A frame's timestamp minus its input's start, as D-028 has it: the container's `start_time`, rescaled to the stream's time base with `av_rescale_q()` as `ffmpeg` rescales the offset it shifts an input by without `-copyts` (`fftools/ffmpeg_demux.c` of FFmpeg 8.1.1, `ts_offset`); without a container start time, the input's first timestamped frame, as the plan says. Times are compared exactly, as whole ticks of 1/L, L the least common multiple of the denominators of both time bases, the reference's frame duration and the grid input's, the tolerance and 10 s; when L would exceed 2^40, the analysis ends with an error.
  - **The frame rate** of each input: `av_guess_frame_rate()`, the rate `ffmpeg` gives its filter graph for a stream when its command line sets none (`fftools/ffmpeg_demux.c`). A reference without one ends the analysis with an error, since D-028's tolerance is half the reference's frame duration; a distorted input without one is taken to have the reference's.
  - **Rule A.** A reference frame and a distorted frame pair when their times differ by at most the tolerance, the bound included. In time order, each frame of the grid input takes the nearest frame of the other input within the tolerance that is neither paired nor dropped, the earlier of two equally near; a frame at or after the grid frame's time is taken at once.
  - **The coarser grid** (option (b)). The two frame rates are compared exactly (`av_cmp_q()`): a distorted input at a lower rate is the grid, otherwise the reference is. A frame of the other input that pairs with nothing fails only when it stands for a frame missing from the grid: for each point of the grid that no frame of the grid input takes (between two consecutive grid frames a and b, a + k·d for k = 1 to round((b − a) / d) − 1; before the first and after the last grid frame, steps of d; d the grid input's frame duration), the nearest such frame nearer to it than the tolerance. The others are off the grid: counted (`off_grid_reference`, `off_grid_distorted`), not failures. A frame halfway between two points counts for neither, so the in-between frames of an input at exactly twice the other's rate are off the grid, also past the other input's last frame. A frame that repeats the timestamp of its input's frame before it is never off the grid: it is a duplicate distorted frame, or an unmatched reference frame. With equal rates every distorted frame that pairs with nothing fails, as in the plan's rule A.
  - **Discontinuities** (option (a)). A timestamp lower than that of its input's previous timestamped frame, or higher by more than 10 s, the default of `ffmpeg`'s `dts_delta_threshold` (`fftools/ffmpeg_opt.c`); an equal one is a duplicate. From the first, in either input, every frame of either input that isn't paired or counted yet, those still to come included, counts as `after_discontinuity`, and both inputs are decoded to their ends to count them. Later discontinuities are counted too.
  - **Frames without a timestamp** fail (`no_timestamp`) when they reach the head of their queue.
  - **#7.** The failure rate is failed / (paired + failed), failed the sum of `unmatched_reference`, `extra_distorted`, `duplicate_distorted`, `no_timestamp` and `after_discontinuity`, and undefined (`null`) when both are 0. It exceeds the threshold of 1% (`vmafalign::kFailureThreshold`) when it is higher than 0.01; a rate of exactly 0.01 doesn't warn.
  - **libvmaf contexts.** A pair before which a reference frame that counts as a failure was dropped since the previous pair starts a new libvmaf context. Reference frames off the grid start none.
- **Alternatives**
  - The frame rate from the stream's `avg_frame_rate`, which the analyzer's progress estimate uses, or its `r_frame_rate`; from the frames' durations, or from the first timestamps.
  - Without a container start time, no shift at all, which is what `ffmpeg` does then.
  - A tolerance without its bound; the nearest frame chosen over both inputs at once rather than for each grid frame in turn.
  - Frame rates taken as equal within a margin; every frame of the finer input that pairs with nothing a failure, as plain rule A has it; the grid's points counted from each input's start rather than from the grid input's frames.
  - `ffmpeg`'s whole discontinuity rule, which takes a backward step for one only beyond 0.1 s; another forward limit.
  - A warning at a failure rate of exactly 1%.
  - Pairing in decode order for a reference without a frame rate.
- **Rationale**
  - Where D-028 and the plan leave a value open, it is the one FFmpeg uses: the frame rate `ffmpeg` gives its filter graph, its rounding of the start offset, its discontinuity threshold.
  - Exact comparison makes equal times in different time bases compare equal, and decides a frame at the tolerance's bound without floating point (the tests "inputs with other time bases and origins that line up" and "the nearest frame within the tolerance, and ties").
  - Taking a frame at or after the grid frame at once pairs inputs whose frames line up frame by frame without decoding a frame more than pairing in decode order did, so a run with `--frames` reads the frames it read before (the test "equal streams pair frame for frame, without decoding ahead").
  - Option (b) counts only frames missing from the coarser grid as failures, and the halfway rule keeps an input at exactly twice the other's rate free of them (the double-rate tests).
  - The plan has a timestamp that goes backwards be a discontinuity; `ffmpeg`'s 0.1 s margin is for packets' decoding timestamps before decoding, where the aligner gets decoded frames in presentation order.
  - D-019 and D-023 let a frame exactly at the §5 threshold pass; the same boundary here.
  - An error rather than a pairing D-028 doesn't define: without the reference's frame rate there is no tolerance, and without a common time base no exact comparison. FFmpeg 8.1.1's `avformat_find_stream_info()` falls back to the inverse of the stream's time base when it finds no other rate (`libavformat/demux.c`), so `av_guess_frame_rate()` (`libavformat/avformat.c`) is expected to give one for every video stream; read from the sources, not measured.
- **Limits**: an input of variable frame rate is paired with its nominal rate's tolerance and grid; a picture repeated under a new timestamp is scored, as the plan says.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: confirmed by the owner (2026-10-26, D-041)

### P-044 The analysis with the pairing: queues, libvmaf contexts, reading to the ends, the warning and the report

- **Choice**
  - `analyze_pair`, which the player's `start()` and `start_manifest()` and `mirage_vmaf` share, keeps the decoded frames the aligner holds in one queue per input and does what the aligner says (`src/VMAFAnalyzer.cpp`). The aligner looks at most one frame ahead in an input and decides on the frames at the heads, so each queue holds a frame or two; no input is decoded ahead on another thread.
  - Both inputs are read to their ends unless the frame limit stops the run, so that the frames of the longer input past the shorter's end are counted, as unmatched reference frames or extra distorted frames. Before D-028 pairing stopped at the shorter input's end and the rest of the other wasn't read; the test of that changes.
  - A new libvmaf context is `vmaf_init()` and `vmaf_use_features_from_model()` with the model already loaded. The context before it is flushed and its pairs' scores read with `vmaf_score_at_index()` before it is closed; each context counts its pictures from 0. The pooled values are computed from all the pairs' scores in pair order (P-039).
  - `--frames N` and `VMAFOptions::max_frames` count pairs.
  - Above the threshold the warning text is `VMAFResult::alignment.warning` and joins `VMAFResult::warnings`. `mirage_vmaf` prints it on stderr, `mirage_vmaf: warning: ...`, writes the report with it among the report's `warnings` and exits with status 0. In the player, the compact VMAF panel shows it under the pair's row, shortened as it shortens errors, and the VMAF results window shows it in full under its table, the pair's label in yellow. The warnings about pixel formats stay out of the player, as before (P-033).
  - The report (`src/VMAFReport.h`) gets `alignment` after `pooled`: the plan's `rule`, `tolerance`, `timestamp_origin`, `paired`, `unmatched_reference`, `extra_distorted`, `duplicate_distorted`, `no_timestamp`, `discontinuities`, `failed`, `failure_rate`, `threshold`, `exceeded` and `failures`, each with its input, position in that input, PTS in seconds and reason; and beyond them `grid`, each input's `frame_rate`, `start` and `start_from`, `after_discontinuity`, `discontinuity_jump` (the 10 s of P-043), `off_grid_reference`, `off_grid_distorted` and `vmaf_contexts`. Each pair of `frames` gains `ref_index` and `dis_index`, its frames' positions in their inputs. PTS stay as the streams have them (P-006). `write_json`'s layout doesn't change (difference 9).
  - `mirage_vmaf --help` gets a paragraph on the pairing.
- **Alternatives**
  - Each input decoded on its own thread, with queues bounded by a number of frames, as the plan's wording allows.
  - Pairing stopped at the shorter input's end as before, the rest not counted.
  - Times relative to each input's start in the report.
  - Every warning of `warnings` in the player's panels; the warning in the report's `alignment` object as well.
- **Rationale**
  - A loop that decodes what the aligner asks for needs no thread and no bound of its own: the aligner decides on the frames it has, with one frame of look-ahead.
  - D-028 counts every unmatched reference frame and every extra distorted frame, those past the other input's end included, and always counts and lists the failures; that needs the rest of the longer input.
  - Every value that decides which frames fail is in the report, as the work order's §7 asks of every setting that changes scores: the tolerance, the threshold and the discontinuity limit. The pairing has no option: like libvmaf's threads, its values are fixed in the code, so neither the command line nor the config file sets them.
  - The fields beyond the plan's say what option (b), option (a) and the restarted contexts did: which grid, what was skipped without failing, what failed once pairing stopped, and which pairs start a context and so have no motion to a frame before them (OD-5). The frame positions place each pair and each failure in its input.
  - D-028 asks for the warning in the report, on stderr and in the player's VMAF panel, with exit status 0; P-033 kept the other warnings out of the player.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: confirmed by the owner (2026-10-26, D-041)

### P-045 The analyzer tests of D-028 on generated clips, the §1.4 acceptance among them

- **Choice**
  - `tests/test_vmaf_analyzer.cpp` generates its clips at run time, in P-036's directory: Matroska clips of uncompressed frames at given timestamps, each frame showing the tests' moving ramp as it is at its own time, so that a frame dropped, repeated or retimed leaves the other frames as `dis_clip()` has them; and an MPEG-TS clip of 12 frames, coded by FFmpeg's own MPEG-2 encoder with every frame a key frame, written as two TS files one after the other, the second restarting its timestamps at 0 and marking its first packets as discontinuous (the muxer's `initial_discontinuity`).
  - The cases:
    - the work order's stream with a frame dropped on purpose, frame 5 of 12 (§1.4's acceptance): the failed frame is reported, with its input, position, PTS and reason; every pair's two frames have the same timestamp; the failure rate is above the threshold and warns; a new libvmaf context starts after the gap; and the pairs away from the gap score exactly as the full clip's do against the same reference;
    - a duplicated timestamp, whose pairs score as the full clip's;
    - half the frame rate, scored on its grid without a failure;
    - double the frame rate, whose pairs and scores are the full clip's;
    - a later start: a clip cut with its timestamps kept, which pairs from its own start as `ffmpeg` pairs it, so with reference frames three frames earlier in the picture; and the same frames with an audio track from 0, whose first three reference frames are unmatched;
    - MPEG-TS timestamps that restart midway, at which pairing stops, the rest of both inputs failed;
    - inputs whose frames line up, which keep their pairs; and the player's analysis, `start()`, with the warning.
  - Three tests change: the shorter input's now expects the reference read to its end and its last four frames unmatched; the D-022 test and the unverified format test use inputs of equal length, so that their warnings are only those they test.
- **Alternatives**
  - Clips made by the `ffmpeg` program's `select`, `setpts` and `fps` filters, which the plan has for delta runs scored by both tools.
  - Cuts of D-011's ladders, whose clips aren't chosen yet.
  - Committed fixture files.
  - H.264 in the MPEG-TS clip.
- **Rationale**
  - Mirage's C++ uses library APIs only; generated clips need no fixture and no network, and uncompressed frames keep every picture exact, so scores compare with `==`.
  - MPEG-TS carries no uncompressed video that FFmpeg reads back; the MPEG-2 encoder is FFmpeg's own and in every build, where libx264 is an option of the build.
  - The cut clip shows what D-028's timestamp origin means for a clip cut with its timestamps kept.
- **Limits**: synthetic clips of 12 frames at 25 fps, one container per case. The delta runs on synthetic clips, scored by both tools, belong with the delta tool's comparison by timestamp and are not made here.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: confirmed by the owner (2026-10-26, D-041)

### P-046 The rerun of the committed measurements after D-028's pairing, and how eval/run_all.sh treats the runs it changes

- **Choice**
  - The analyzer's pairing by timestamp changes what HEAD's `mirage_vmaf` does with inputs whose frames don't line up. In the committed runs the frames of each pair line up, so `eval/run_all.sh` keeps rerunning the runs made after D-016 with HEAD's program and the options their `run.cmd.txt` records, `format-cases-after` with the program of `d993686` and the runs before D-016 with that of `83c8ed6` (P-034, P-038): no step and no program changes, and the pairing has no option to pass.
  - One run of `eval/run_all.sh --skip-full --jobs 4` with no program given, so that it built all three programs from their commits, from a new clean worktree of `713431b`, the commit after this round's changes to the code, with its output directory and `TMPDIR` outside the repository. After `713431b` only documents and the record change.
  - Committed in `eval/run_all/2026-10-23-d028/` as P-042's record is: `run_all.md` and `.json`, `rerun_check.md` and `.json`, the three builds' `build.txt` and logs, and the output of every command, with a `SHA256SUMS`.
  - An earlier run of the same kind, from a clean worktree of `8f20f46`, before the report gained `discontinuity_jump` (`713431b`), isn't cited: the code changed after it.
- **Alternatives**
  - A program row for the code before D-028, such as the program of `0496b21` that made `defaults-300`, so that the runs are rerun with a program that pairs in decode order.
  - Every step, the two over every frame included, as P-032 did.
- **Rationale**
  - HEAD's program reproducing the runs is what shows that the pairing moved no score where frames line up; a program from before D-028 would show nothing about the new pairing.
  - The steps over every frame are left to the full-length report with bicubic that D-024 asks for, as in P-042.
- **Result** (`eval/run_all/2026-10-23-d028/rerun_check.md`, `run_all.md`): every step ran, and everything compared is as committed: every verdict, Mirage's per-frame scores at full precision and every libvmaf metric of FFmpeg's logs, in the 300-frame runs and in the synthetic color tag and reference format cases. The program built from `713431b` has the SHA-256 `7fb455e9e1349c85e25afe54d5fd47d714c463ecebea1b03b84ca1b594d27741`, which no committed run's `build.txt` names; it reproduces the scores of runs made by four other builds, `420b8776…`, `2220ce3a…`, `3633c43e…` and `85535c5c…`. The one built from `d993686` has the SHA-256 of `format-cases-after/build.txt`, and the one built from `83c8ed6` that of the runs before D-016. 520.5 s of wall time, the three builds included.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: confirmed by the owner (2026-10-26, D-041)

### P-047 How the §5 delta tool compares the two sides by timestamp (D-028)

- **Choice**
  - **FFmpeg's pairs from its log.** FFmpeg's graph gets `showinfo=checksum=0` at the end of both inputs' chains and after libvmaf (`vmafdelta.SHOWINFO`, `filtergraph()`): filters that change no frame and log each frame's pts in its link's time base. The tool runs FFmpeg 8.1.1's framesync rule on the frames of the two chains (`framesync_events()`, from `libavfilter/framesync.c` and `vf_libvmaf.c`): in time order each input's frames become its current frame; each distorted frame is scored with the current reference frame, the one at or before its time, none before the reference's first frame, the reference's last once it has ended; once the distorted input has ended, each later reference frame is scored against the last distorted frame. Times are compared in the time base framesync picks (`av_gcd_q()`). The events must be the frames libvmaf passed on, and those with a reference frame as many as the frames of FFmpeg's libvmaf log; otherwise the pair fails ("FFmpeg's pairs could not be established from its log") and no scores are compared.
  - **Times** from each input's start: FFmpeg's as `ffmpeg` shifts each input, by the start its log gives (`Input #N ... start:`); `mirage_vmaf`'s PTS less the starts of its report's `alignment`, or, for a report without one (from before D-028), less FFmpeg's starts rescaled as `ffmpeg` rescales them. Both are compared exactly, as fractions of their time bases.
  - **The same pair**: a pair of each side with the same reference frame and the same distorted frame, by their positions in the inputs (`ref_index`, `dis_index`; for a report from before D-028, which paired in decode order, the pair's index for both), each matched once, in Mirage's order. Its two frames must have the same times on both sides.
  - **The verdict**: besides P-009's checks, a pair passes only if FFmpeg's pairs are established, both sides scored the same pairs (`PAIRING MISMATCH` otherwise, after `FRAME COUNT MISMATCH` where the numbers differ), at the same timestamps (`TIMESTAMP MISMATCH`), more than none, and no pair both scored has |delta| > the threshold (D-019). The scores are compared over the pairs both scored; the reports name each compared pair by Mirage's pair index and FFmpeg's frame number.
  - **What each side counted as failed, never hidden**: the report's `alignment`, and "Pairing by timestamp" in its `delta.md`, give both sides' pairing in counts: the pairs; per input, the frames in no pair and in several; the pairs whose two frames are further apart than the tolerance, half the reference's frame duration from the report's alignment, else from ffprobe's `r_frame_rate`; `mirage_vmaf`'s failures by reason, its failure rate and libvmaf contexts; FFmpeg's distorted frames passed on unscored, the pairs a reference frame made after the distorted input ended, and the timestamp discontinuities `ffmpeg` corrected. Then the pairs only one side scored, the pairs both scored at other timestamps, and every frame that the two sides pair with other frames, or that `mirage_vmaf` counts as failed, which FFmpeg never does, with what each side does with it: at most 1000 of each listed in `delta.json`, 20 in `delta.md`, all counted. Such frames, and corrected discontinuities, are warnings; they don't fail a pair whose pairs and scores agree.
  - P-009's warning about `mirage_vmaf` pairs whose two frames have different PTS is dropped: the comparison settles which frames each side paired, and the report keeps the count.
- **Alternatives**
  - `showinfo` after libvmaf only, as the plan has it: it gives each event's time, but not whether the event was scored (distorted frames before the reference's first are passed on unscored), nor its reference frame, nor the distorted frame of an event made after the distorted input ended.
  - Pairs matched by their timestamps only: a duplicated timestamp is ambiguous, and `ffmpeg`'s corrected timestamps (MPEG-TS, HLS) make pairs of the same frames look different. Or by positions only, without the timestamps.
  - Failing a pair on the differences in failure accounting as well; or only warning when the pairs differ.
  - FFmpeg's `metadata` filter printing to separate files, which would keep the verbose log as it was.
  - The comparison by position as a fallback when FFmpeg's pairs can't be established.
- **Rationale**
  - D-028 and the plan's "Where the code changes": the delta tool compares by timestamp, a pair passes only if both sides scored the same timestamps, and the differences in what each side counted as failed are reported.
  - Positions identify the frames exactly, so a pair both scored is scored on the same pictures; the timestamps check that both sides measure time alike.
  - Reconstructing framesync's events from both chains, checked against what libvmaf passed on and scored, is what FFmpeg's log can tell without changing FFmpeg's pairing; a reconstruction that doesn't match fails the pair rather than comparing pairs that aren't known.
  - `showinfo` is FFmpeg's own frame logger and leaves the evidence in the raw verbose log (D-032). The cost is a longer log: for each frame and each of the three filters, a line with its timestamp, one with its color tags, and one for each item of side data the frame carries.
- **Limits**: framesync's rule as FFmpeg 8.1.1 has it under its default options; a filter graph that `ffmpeg` configures more than once fails the reconstruction.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: confirmed by the owner (2026-10-26, D-041)

### P-048 The alignment cases, scored before and after D-028, and the §1.4 acceptance in them

- **Choice**
  - `eval/consistency/align_cases.py` makes the cases of the work order's §1.4 and of the plan's "Tests" from `color_cases.py`'s pattern, the first 10 pictures at 30000/1001 fps, references at 1920x1080 and distorted inputs at 640x360, 8-bit yuv420p, all tagged bt709/tv, with FFmpeg's `select`, `loop`, `setpts` and `fps` filters: a dropped distorted frame (§1.4's acceptance stream), a dropped reference frame, a duplicated timestamp, half the frame rate, a later start in a video-only container and with an audio track from 0, and MPEG-TS timestamps that restart midway; and two controls, frames that line up, and the same frames with millisecond (Matroska) reference timestamps against 90 kHz (MPEG-TS) distorted ones. Matroska with uncompressed frames; MPEG-TS with FFmpeg's MPEG-2 encoder, every frame a key frame at a fixed quantizer, the restart written as two files one after the other, the second's first packets marked as discontinuous. The script checks each frame's picture by its MD5 and its timestamp, and records the clips (`clips.txt`).
  - The runs: from a new clean worktree of `d9b1f51`, the clips made once there, `run_delta.sh` over every frame with bicubic, vmaf_v0.6.1, 4 threads and the threshold 0.01 named; with the `mirage_vmaf` of `e8b867f`, the last commit before D-028 that changed the code `mirage_vmaf` is built from, built in a new worktree (`eval/consistency/align-cases-before/`), and with that of `d9b1f51`, built in the run's worktree (`align-cases-d028/`). `checks.txt` of each afterwards from the clean tree at `d45372d`.
  - §1.4's acceptance as committed evidence: the run of `a01_dropped_distorted` in `align-cases-d028/`, `mirage_vmaf`'s report with the failed frame, its warning in the report and on stderr and its exit status, every pair's PTS and the scores against the control's, beside the same stream before D-028 (`align-cases-d028/checks.txt`, check 4; `align-cases-before/checks.txt`, checks 4 and 5).
- **Alternatives**
  - The six kinds of case only, one later start, and a forward jump in Matroska, which `ffmpeg` doesn't correct, as the discontinuity.
  - Clips made with libav* as the analyzer tests make theirs (P-045), or a bipbop rung with frames removed.
  - The program of `02ebed5` or `0496b21` for the run before D-028: the code is that of `e8b867f`, and their builds have its SHA-256 (`eval/consistency/defaults-300/build.txt`, `eval/run_all/2026-10-23-d027/`).
  - A run of `mirage_vmaf` alone on the dropped-frame stream for §1.4.
- **Rationale**
  - These are the plan's delta runs on synthetic clips, "made with FFmpeg's `select`, `setpts` and `fps` filters and scored by both sides, committed like `color-cases-before/`".
  - One pattern and one full clip per size and container make each frame's MD5 name its picture, so every pair of the reports can be read as two pictures.
  - The controls separate what the alignment changes from what the clips' coding does: `a08` came out of making `a07`, whose Matroska reference against MPEG-TS distorted frames showed FFmpeg pairing frames with the reference frame before; the two later starts are those of the analyzer test (P-045) and of `docs/PHASE1_DECISIONS_PENDING.md`, "D-028's timestamp origin and a clip cut with its timestamps kept".
  - In a delta run the acceptance stream shows both sides: `mirage_vmaf`'s report of the failure and FFmpeg's scores of the same frames.
- **Result** (`eval/consistency/README.md`, "`align-cases-before/`, `align-cases-d028/`"): the clips are as intended. Since D-028, `mirage_vmaf` reports §1.4's dropped frame instead of a wrong score: the reference's frame 5 as unmatched, a failure rate of 0.1 with the warning, exit status 0, every pair's two frames at the same PTS, and the scores of the control away from the gap; before D-028 it scored the 4 pairs after the gap one picture apart, 22.609365 to 24.440811, without a warning. Of the 9 cases, the control `a00` passes with both programs, and `a06` (a later start in a container from 0) passes since D-028 only, its 3 unmatched reference frames reported; every other case fails, and its reports show where FFmpeg's rule B and Mirage's rule A pair other frames, or where pairs both scored differ through libvmaf's motion features. Once the distorted input has ended, FFmpeg goes on scoring each later reference frame against the last distorted frame, which the plan's reading of the sources missed; FFmpeg's side is the same in both runs.
- **Limits**: synthetic clips of 10 frames, one container per case and one case of each kind; FFmpeg 8.1.1 only. The scores only compare the two tools.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: confirmed by the owner (2026-10-26, D-041)

### P-049 How eval/run_all.sh reruns the committed runs after the comparison by timestamp, and the rerun

- **Choice**
  - Every `run_delta.sh` step is rerun with HEAD's delta tool, which compares the two sides by timestamp since D-028 (P-047); the runs made before it, with the comparison by position, are judged by the new comparison too. No option keeps the old comparison: where both sides pair the same frames in order, the new comparison compares the same scores, and the rerun shows whether every committed verdict holds.
  - The programs stay as before (P-034, P-046), with one more row in `PROGRAMS` for the alignment cases before D-028: `before-d028`, the commit `e8b867f` that `align-cases-before/build.txt` names (`BEFORE_D028`, `--mirage-vmaf-before-d028`). New steps: `align-clips` (`align_cases.py`, compared with `align-cases-before/clips.txt`; a clips step now names its script), `align-cases-before` with that program and `align-cases-d028` with HEAD's, each with the options of its `run.cmd.txt`.
  - One run of `eval/run_all.sh --skip-full --jobs 4` with no program given, so that it built all four programs from their commits, from a new clean worktree of `4eeb539`, the commit after the alignment runs' checks, with its output directory and `TMPDIR` outside the repository. After `4eeb539` only documents and the record change. Committed in `eval/run_all/2026-10-23-align/` as P-046's record is.
- **Alternatives**
  - An option of `run_delta.sh` for the comparison by position, given to the reruns of the runs made before D-028.
  - Rerunning each run with the delta tool of the commit that made it.
  - Every step, the two over every frame included.
- **Rationale**
  - Rerunning the committed runs with the head's tool, the settings they recorded named, is what shows that the new comparison moves no verdict and the `showinfo` filters no score of FFmpeg's.
  - The steps over every frame are left to the full-length report with bicubic that D-024 asks for, as in P-042 and P-046.
- **Result** (`eval/run_all/2026-10-23-align/rerun_check.md`, `run_all.md`): all 20 steps ran, and everything compared is as committed: every verdict, Mirage's per-frame scores at full precision and every libvmaf metric of FFmpeg's logs, in the 300-frame runs, the synthetic color tag, reference format and alignment cases, the clips' records, the FFmpeg records and the pooling steps. The program built from `4eeb539` has the SHA-256 of `eval/consistency/align-cases-d028/build.txt`, `7fb455e9…`; the one built from `e8b867f` that of `align-cases-before/build.txt`, `85535c5c…`; the one built from `d993686` that of `format-cases-after/build.txt`; and the one built from `83c8ed6` that of the runs before D-016. 620.4 s of wall time, the four builds included.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: confirmed by the owner (2026-10-26, D-041)

### P-050 How D-029's ladder scoring is implemented in mirage_vmaf, and the values it leaves open

- **Choice**
  - **Where.** `src/VMAFLadder.h` (`vmafladder`), built into `mirage_vmaf` and `mirage_tests`, not the player. It reads master playlists itself and resolves their URIs with `hls::absolute_uri()` of `src/HlsMaster.h`, which needs only the standard library; the player's `ManifestParser` would bring pugixml and the player's network logger into `mirage_vmaf`. `mirage_vmaf --ladder <master> [--mezzanine <video>] --out <report>` runs it (`tools/mirage_vmaf.cpp`); the player's ladder analysis (`start_manifest()`) is left as it was.
  - **Reading the master.** With FFmpeg's avio and its default options, as the analyzer opens its inputs, from a file or any URL FFmpeg opens; stopped as soon as the text doesn't start with `#EXTM3U`. Its first line has to be `#EXTM3U`, as FFmpeg's hls demuxer requires (`libavformat/hls.c` of FFmpeg 8.1.1). Its entries are its `EXT-X-STREAM-INF` tags, each with the URI line after it, blank lines, comments and other tags between them allowed, as the hls demuxer pairs them; each line is taken without the white space around it and each tag's attributes are read as `eval/streams/fetch_streams.py` reads them. I-frame playlists and `EXT-X-MEDIA` renditions are no entries. A master with an entry without a `BANDWIDTH`, or with one that isn't digits only or is above 2^63 − 1 (RFC 8216's decimal-integer goes up to 2^64 − 1), an entry without a URI line, a URI line without an entry (a media playlist), or a URI the next point doesn't resolve is refused: `mirage_vmaf` writes no report and exits with status 1.
  - **URIs** are resolved against the URL the master was read from after HTTP redirects, which is what the hls demuxer resolves against: for http(s), by `hls::absolute_uri()`, which takes relative paths, absolute paths and http(s) URLs and refuses dot segments, `//host` references, query- or fragment-only references and other schemes; for a local master, by joining the URI to the master's directory, a URI with a scheme or an absolute path taken as it is.
  - **Video.** An entry has video when it has a `RESOLUTION`, or a `CODECS` entry whose sample entry name is a video codec's (`avc1`, `avc3`, `hvc1`, `hev1`, `dvh1`, `dvhe`, `av01`, `vp09`, `vp08`, `mp4v`): the rule and the list of `fetch_streams.py`, whose committed records say which entries of the repository's streams have video (`eval/streams/fetched-2026-10-23/`). Entries without video are skipped without being opened and listed in the report with the reason `no video`, also one that names a video playlist.
  - **Each video playlist once.** Entries whose resolved URLs are the same string name one playlist, scored once; each pair lists the video entries that name it, and the top rung's are the reference's.
  - **The top rung** is the playlist of the video entry with the highest `BANDWIDTH`; of two or more such entries that name different playlists, the first in the master's order, with a warning in the report and on stderr, where `mirage_vmaf` prints it when the run ends.
  - **The pairs** are in the master's order of their first entry. Top-rung mode: every video playlist but the top rung's, against it; an error when there is no other. Mezzanine mode, `--mezzanine`: every video playlist, the top rung's included, against the video `--mezzanine` names. One run has one reference mode and writes one report; both modes on one ladder are two runs.
  - **Scoring.** One pair after another with the run's settings, each with `VMAFAnalyzer::analyze()`, as a pair given with `--ref` and `--dis`: each pair opens and decodes its two inputs anew, the reference included, and `--frames N` limits each pair. The pairing warning of a pair (D-028) goes to stderr with the pair's number, and the run still exits with status 0. The first pair whose analysis is incomplete, by P-006's rules, ends the run: no report, exit status 1, and the error names the pair; so does a pair scored with other settings than the first, which only a model file changed during the run can cause, since the report gives the settings once.
  - **The report** (`src/VMAFReport.h`, `ladder_to_json()`): the settings and libraries once; the master as given, the URL it was read from and the SHA-256 of its bytes; its entries with their index, line, URI, resolved URL, `BANDWIDTH`, `AVERAGE-BANDWIDTH`, `RESOLUTION`, `CODECS` and `FRAME-RATE` as the master gives them (`null` when absent) and whether they have video; the reference with its entries; the skipped entries; the plan's warnings; and `pairs`, each with its index, its entries and the keys of a pair's report that are a pair's own, written by the same code. A pair's report keeps its layout.
  - **The command line.** The inputs are one of two forms: `--ref` and `--dis`, or `--ladder` with an optional `--mezzanine`. Options of both forms in one place are an error; an option of one form on the command line leaves out the config file's settings of the other (D-030); `--out` is needed by both; `--help` shows a synopsis per form and a "Ladder" paragraph.
- **Alternatives**
  - The player's `ManifestParser`; probing each entry with FFmpeg for a video stream instead of reading its attributes; skipping the entries whose inputs fail to open.
  - `EXT-X-MEDIA` video renditions scored as well, each as a video playlist of its own.
  - For a missing `BANDWIDTH`, taking 0, as FFmpeg's hls demuxer and the player's parser do; for a bad one, its leading digits, as the demuxer's `atoi()` does.
  - For equal highest bandwidths: refusing the master, or a tie-break by `AVERAGE-BANDWIDTH` or resolution.
  - The pairs in descending `BANDWIDTH`, as `start_manifest()` scores them.
  - Both modes in one run, writing two reports.
  - Scoring every pair and listing every failure before exiting; decoding the reference once for all pairs, which is §2's orchestration.
- **Rationale**
  - D-029 fixes the design; where it is silent the rules are FFmpeg's (the first line, how URIs pair with entries, the base after redirects) or the repository's (`fetch_streams.py`'s rule for video), so that the same master gives the same video entries in the committed stream records and in a ladder run.
  - D-029's "each video playlist" is read as the playlists of the `EXT-X-STREAM-INF` entries, the entries whose scoring the §1.2 package asked about (`docs/PHASE1_DECISIONS_PENDING.md`).
  - A master the tool can't read without guessing is refused: `BANDWIDTH` decides the top rung, and taking 0 for a missing one could make another rung the reference without a word.
  - The first among equals is the order the master gives them; the warning keeps that choice visible.
  - With P-006 and D-029 a failed pair leaves no report, so the pairs after it would only take time.
  - `mirage_vmaf` stays free of the player's parser, pugixml and network logger, which D-031's headless build leaves out.
- **Limits**: HLS master playlists only, no DASH; `EXT-X-MEDIA` video renditions and I-frame playlists aren't scored; one pair at a time, the reference decoded again for each.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: confirmed by the owner (2026-10-26, D-041)

### P-051 The ladder check: ladder mode on the bipbop master against the pairs scored one by one, first 300 frames

- **Choice**
  - **The tool.** `eval/consistency/ladder_check.py` runs `mirage_vmaf --ladder <master>` and, with the same settings, `mirage_vmaf --ref <reference> --dis <distorted>` on each pair of a pairs file. It passes when every pair of the ladder report has, value for value, the keys of its own report that are a pair's (`scored_as`, `dis_conversion`, `ref_conversion`, `warnings`, `frames_scored`, `pooled`, `alignment`, `ref`, `dis`, `frames`); both reports have the same settings and libraries; the ladder scored exactly the pairs of the pairs file, each once; and its report accounts for every entry of the master once. It records its runs as `run_delta.sh` does (P-011). Its tests use `mirage_vmaf`'s stand-in, which takes `--ladder` and can be made to score a ladder's pair otherwise than on its own (`eval/consistency/tests/test_ladder_check.py`).
  - **The run.** Build the release `mirage_vmaf` in a new worktree of `7fbaa8c`, the commit after this round's changes to the code and the tool, release preset, the `mirage_vmaf` target only (`eval/consistency/ladder-300/build.txt`). From that worktree, run `ladder_check.py` on bipbop 16x9's master and the pairs of D-018 (`pairs/bipbop16x9.txt`), with `--frames 300`, bicubic and vmaf_v0.6.1 named, one process at a time (`ladder-300/top-rung/`). `checks.txt` afterwards from the clean tree at `f983bac`.
  - **`eval/run_all.sh`**: a new kind of step, `ladder`, reruns `ladder_check.py` with the options of its `run.cmd.txt` and HEAD's program. It compares the check's verdict, the ladder report's master and plan, and every per-frame score of the ladder's pairs and of the pairs scored one by one with the committed run's. The step is `ladder-300`.
  - An earlier run of the same kind, from a clean worktree of `382a85d`, isn't cited: the code changed after it (`7fbaa8c`: the last line of a ladder run, and the error when a pair's settings differ from the first's).
- **Alternatives**
  - Comparing the ladder's scores with the committed reports of `defaults-300/` only, without scoring the pairs again; that is check 7 of `checks.txt`, and it rests on another program.
  - A delta run in ladder mode, with FFmpeg's side.
  - Every frame; a mezzanine run, for which there is no ladder with a mezzanine yet (D-011).
- **Rationale**
  - The same program in the same run, on the same inputs, is what shows that ladder mode scores a pair as a pair given on its own. `defaults-300/` adds that those scores are FFmpeg's (its §5 run passes), so ladder mode needs no §5 run of its own.
  - 300 frames are the range of `defaults-300/`, which the run is compared with frame by frame.
  - bipbop's master has the entries D-029 is about: five video playlists and an audio-only entry.
- **Result** (`eval/consistency/ladder-300/checks.txt`):
  - The master `mirage_vmaf` read has the SHA-256 of the copy in `eval/streams/fetched-2026-10-23/`, and each of its six entries the URL, `BANDWIDTH` and video flag of that record (check 3).
  - The reference is gear5, entry 4. gear1 to gear4, entries 0 to 3, are scored against it, one pair each. gear0, entry 5, is skipped as `no video`. No warning (check 4).
  - The check passes: every pair of the ladder has every compared value of its own report, all 300 frames' scores, PTS and positions the same at full precision (checks 5 and 6).
  - Every per-frame score of the four ladder pairs is that of `defaults-300/` at full precision and, written with 6 decimals, FFmpeg's there (check 7).
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: confirmed by the owner (2026-10-26, D-041)

### P-052 The rerun of the committed measurements after D-029's ladder scoring, and what of it is committed

- **Choice**
  - D-029 adds ladder scoring to `mirage_vmaf`'s command line and a report for ladder runs; a pair given with `--ref` and `--dis` is scored and reported as before. So `eval/run_all.sh` reruns the committed runs with the programs and the options it gave them before (P-034, P-046, P-049): the runs made after D-016 with HEAD's program and the options their `run.cmd.txt` records, `format-cases-after` with the program of `d993686`, `align-cases-before` with that of `e8b867f`, and the runs before D-016 with that of `83c8ed6`. It adds the step `ladder-300` (P-051), with HEAD's program.
  - One run of `eval/run_all.sh --skip-full --jobs 4` with no program given, so that it built all four programs from their commits, from a new clean worktree of `d340b32`, the commit after the ladder check's run and its checks, with its output directory and `TMPDIR` outside the repository. After `d340b32` the code changes in comments only (`e73272c` in a test, `93deef6` in two headers) and in the text `mirage_vmaf --help` prints (`db140be`, with a check of it in the usage test), which no scoring reads; the other commits are documents and the record. Committed in `eval/run_all/2026-10-23-ladder/` as P-049's record is.
- **Alternatives**
  - Every step, the two over every frame included.
  - `--only ladder-300`.
- **Rationale**
  - HEAD's program reproducing every committed run is what shows that the new command line and report moved no score of a pair given on its own, and the rerun of `ladder-300` shows that the ladder check reproduces. The steps over every frame are left to the full-length report with bicubic that D-024 asks for, as in P-042, P-046 and P-049.
- **Result** (`eval/run_all/2026-10-23-ladder/rerun_check.md`, `run_all.md`): all 21 steps ran, and everything compared is as committed, `ladder-300` included: its verdict, the ladder report's master and plan, and every per-frame score of the ladder's pairs and of the pairs scored one by one. The program built from `d340b32` has the SHA-256 of `eval/consistency/ladder-300/build.txt`, `eab1651cc09237fce5451fd1de0c236c2d589b2d3b2d2848065b675e8e35a4c7`; it reproduces the scores of the other runs it reran that were made after D-016, made by the builds `420b8776…`, `2220ce3a…`, `3633c43e…`, `85535c5c…` and `7fb455e9…`. The one built from `e8b867f` has the SHA-256 of `align-cases-before/build.txt`, `85535c5c…`; the one built from `d993686` that of `format-cases-after/build.txt`; and the one built from `83c8ed6` that of the runs before D-016. 670.0 s of wall time, the four builds included.
- **Who**: agent (Claude)
- **Date**: 2026-10-23
- **Status**: confirmed by the owner (2026-10-26, D-041)

### P-053 How D-031's #11a is implemented: glad's loader committed, its regeneration, its license terms

- **Choice**
  - **The files.** `third_party/glad/` holds `include/KHR/khrplatform.h`, `include/glad/gl.h` and `src/gl.c` as `third_party/glad/regenerate.sh` generated them, with the sizes and SHA-256s that the build check at `6a6661f` records for the files its release and debug builds generated (`eval/build_check/macos-2026-10-23-6a6661f.txt`, "Generated glad files, release vs debug"; `third_party/glad/README.md`).
  - **The library.** `add_library(glad_gl_core_41 STATIC third_party/glad/src/gl.c)` with the include directory and `${CMAKE_DL_LIBS}` public, which is what glad 2.0.8's `glad_add_library()` made of the same files for a static library (its `cmake/GladConfig.cmake`: no compile definition but for a shared library). The target keeps its name, so nothing that links it changes. glad is no longer fetched.
  - **The script.** `regenerate.sh <python3.13> <scratch>` creates a venv from the given interpreter with `jinja2==3.1.6` and `markupsafe==3.0.4`, the venv of `docs/BUILD.md` before D-031 (P-003, P-021); clones glad, checks that its tag `v2.0.8` still names `73db193f853e2ee079bf3ca8a64aa2eaf6459043`, the commit the build fetched (`macos-2026-10-23-6a6661f.txt`, "Dependencies fetched at configure time"), and checks that commit out; runs `python -m glad --out-path <dir> --api gl:core=4.1 --reproducible c` in glad's source directory, the command `glad_add_library()` ran (its `args.txt`); and compares each file's bytes with the committed one: exit status 0 when all three are identical, 1 when one differs or is missing, 2 when a step fails. The build check runs it (P-058).
  - **The license terms**, in `third_party/glad/README.md`: the SPDX line of `gl.h` and `gl.c`, `(WTFPL OR CC0-1.0) AND Apache-2.0`; glad's README on the generated code, quoted; that `khrplatform.h` carries the Khronos Group's notice at its top; and glad's `LICENSE` at the commit above for the generator and the Khronos files. No license text is copied. How the terms sit with GPL-3.0-or-later, and whether the texts belong next to the files, are left with D-008's open question.
  - **No Python.** With glad gone from FetchContent nothing calls `find_package(Python)`: Catch2 looks for Python only when it is the top-level project (its `CMakeLists.txt`), and the other dependencies' CMake files don't. The build check shows every line naming Python in each configuration's configure log, CMake cache and Ninja file (P-058).
- **Alternatives**
  - Another directory, such as `src/glad/`; another target name.
  - glad from PyPI (`glad2==2.0.8`) for the regeneration, rather than the git tag's commit the build fetched; the PyPI packages checked by hash (`pip --require-hashes`).
  - glad's `LICENSE` copied next to the files, or the texts of WTFPL, CC0-1.0 and Apache-2.0.
  - Regeneration as a CMake option that fetches glad again.
- **Rationale**
  - The committed files are the bytes every recorded build generated, and the script shows that the pinned generator still makes them; a moved tag would fail its commit check.
  - Keeping the target as glad made it keeps the rest of the build as it was.
  - The terms are stated as the files and glad state them; what they require next to a GPL-3.0-or-later program is the legal question D-008 leaves open.
- **Who**: agent (Claude)
- **Date**: 2026-10-24
- **Status**: confirmed by the owner (2026-10-26, D-041)

### P-054 How D-031's #11b is implemented: MIRAGE_HEADLESS, the presets, the Linux instructions

- **Choice**
  - **The option** `MIRAGE_HEADLESS`, off by default. On, CMake configures neither the player nor what only the player uses: OpenGL, GLFW, glad, stb, Dear ImGui, nativefiledialog-extended, and the snapshot check's `snapshot_diff` and `pathb_regression`; FetchContent fetches miniaudio, pugixml and Catch2. It builds `mirage_vmaf` and all of `mirage_tests`: that target links Catch2, FFmpeg, libvmaf and pugixml only, and none of its sources includes a header of OpenGL, GLFW, glad, Dear ImGui, nativefiledialog-extended or stb, directly or through another header. Off, the build is as before, with the player's dependencies grouped in one block of `CMakeLists.txt`.
  - **The presets.** `macos-headless`: `macos-release` (Ninja, Release, ffmpeg-full's pkg-config directory) with the option on, in `build/macos-headless/`, the configuration the build check verifies on macOS. `linux-release`: Ninja, Release, the option on, in `build/linux-release/`, no `PKG_CONFIG_PATH`, and a condition that offers it only on a Linux host, so on macOS it can't pick up Homebrew's plain `ffmpeg` through pkg-config's default path. No Linux debug preset.
  - **The Linux instructions** (`docs/BUILD.md`, step 6): what the headless build needs, read from `CMakeLists.txt` and the sources; the commands; `eval/run_all.sh` given the Linux build's program, FFmpeg and the model directory; marked as not built on Linux (MEASUREMENT PENDING).
  - **`eval/run_all.sh` is unchanged.** On Linux it builds the programs of the older commits as on macOS, with their `macos-release` preset, whose Homebrew `PKG_CONFIG_PATH` entry doesn't exist there. Those commits' `CMakeLists.txt` configure the player, so they need OpenGL, GLFW and GTK 3 (nativefiledialog-extended's Linux backend) development files and a Python 3 interpreter (glad's configure step) to configure; the Docker image has them (P-055).
- **Alternatives**
  - The option as `MIRAGE_BUILD_PLAYER`, on by default; the player left out automatically on hosts other than macOS.
  - stb and `snapshot_diff` kept in the headless configuration: they don't need the player's dependencies, but only the player's snapshot check uses them.
  - Only the VMAF tests in the headless configuration.
  - A Linux debug preset; a Linux preset with a `PKG_CONFIG_PATH`; the macOS presets limited to macOS hosts.
  - The macOS check through `-DMIRAGE_HEADLESS=ON` given to another preset, without a preset of its own.
  - A rule in `run_all.py` for building the older commits on Linux; they have no headless option, so it would still need their configure-time dependencies.
- **Rationale**
  - D-031's #11b: `mirage_vmaf`, `mirage_tests` and the eval tooling on Linux, the player macOS-only, and a CMake option without the player's dependencies.
  - A preset makes the macOS check of the headless configuration a step anyone can repeat with the FFmpeg of the other macOS presets, and its `mirage_vmaf` is byte for byte the release preset's (P-058).
- **Limits**: nothing has been built on Linux.
- **Who**: agent (Claude)
- **Date**: 2026-10-24
- **Status**: confirmed by the owner (2026-10-26, D-041)

### P-055 How D-031's #11c is implemented: the Dockerfile, not built

- **Choice**
  - **The base image**: Debian 13, `debian:trixie-20261020-slim@sha256:62bd423aeee912639ca8ee681c2b762cb3e25137e0a92b20be455898a9f034ad`, the image index Docker Hub's registry API named for the tags `trixie-20261020-slim`, `trixie-slim` and `13-slim` on 2026-10-24 (`docker/base-image.txt`, made by `docker/query_base_image.sh`). The index holds an image per platform, and Docker picks the host's.
  - **The sources**: FFmpeg 8.1.1 from ffmpeg.org and libvmaf 3.1.0 from GitHub's archive of its tag, each checked with `sha256sum` against the SHA-256 of the formula installed on the machine of the measurements; x264 at the git commit of the installed x264 r3222 formula, checked after the checkout (`eval/build_check/macos-2026-10-24-5ae44a4.txt`, "The sources of the installed ffmpeg-full, libvmaf and x264"). Each is configured as its formula configures it: x264 with the formula's options; libvmaf with `meson setup build libvmaf` and the options of Homebrew's `std_meson_args`, its model directory installed under `share/libvmaf/model`, and the build stopped unless libvmaf's configuration has its built-in models, for which libvmaf's build needs `xxd`; FFmpeg with the formula's options for what the evaluation uses, `--enable-shared --enable-pthreads --enable-version3 --enable-gpl --enable-gnutls --enable-libvmaf --enable-libx264`, and `--disable-doc`, without the formula's other external libraries, its macOS options and `ffplay`.
  - **The repository**: the build context is the `.git` directory alone (`.dockerignore`); a first stage clones it with `git clone --no-local --single-branch`, and the image gets that clone: the commit checked out in the context, its branch's history, a clean working tree, and none of the context's other branches or untracked files.
  - **The programs**: `mirage_vmaf` and `mirage_tests` with the `linux-release` preset.
  - **Debian's packages** come from Debian's archive as it is when the image is built, not pinned; the image lists their versions in `/usr/local/share/mirage-image/packages.txt`. Besides the build tools: git, Python 3, `xxd`, the development files of GnuTLS, zlib and bzip2, and those of OpenGL, GLFW and GTK 3, which the older commits' `CMakeLists.txt` need to configure (P-054).
  - **The command**: `ENTRYPOINT` `eval/run_all.sh --mirage-vmaf build/linux-release/mirage_vmaf --ffmpeg /usr/local/bin/ffmpeg --model-dir /usr/local/share/libvmaf/model`, `CMD` `/out`, an empty directory mounted from the host; `run_all.sh` builds the older programs itself. It runs as root.
  - **Not built**: the machine has no container runtime (D-031's known limit). The Dockerfile's header, `docker/README.md`, `docs/BUILD.md` and `eval/README.md` say so.
- **Alternatives**
  - Ubuntu 24.04, or another date of Debian 13.
  - Debian's packages from `snapshot.debian.org` at the base image's date, or each pinned by version.
  - Debian's own libx264.
  - FFmpeg with all the formula's external libraries.
  - A copy of the context's working tree instead of a clone of its `.git`; all its branches.
  - The older programs built into the image, or the image's command leaving out the steps that need them.
  - A user other than root.
- **Rationale**
  - D-031's #11c: a base image pinned by digest, FFmpeg 8.1.1 and libvmaf built from release sources checked by SHA-256 at the versions of the measurements, `mirage_vmaf` built from the repository, `eval/run_all.sh` as the command.
  - The installed formulae are the sources the measured libraries were built from; Homebrew's current formulae name newer versions ("installed vs current stable" in the same record).
  - The formula's options for the parts the evaluation uses keep the image's FFmpeg like the measured one where the scores and the clips depend on it, without the libraries nothing of the evaluation calls.
  - A clone of a commit gives `run_all.sh`'s records a clean tree and a commit to cite.
  - Without the older commits' configure-time dependencies `run_all.sh` couldn't build their programs.
- **Limits**: not built or run, so whether it builds, and whether its scores equal those measured on Apple Silicon, are MEASUREMENT PENDING; Debian's packages are not pinned; in the image the libvmaf package version of D-006 is `unknown` (`docker/README.md`).
- **Who**: agent (Claude)
- **Date**: 2026-10-24
- **Status**: confirmed by the owner (2026-10-26, D-041)

### P-056 The README after D-031, and vcpkg.json removed

- **Choice**
  - `README.md`: the quick start, the release build and the test section give way to a pointer to `docs/BUILD.md`, with a note on the headless, Linux and Docker builds and that the last two aren't built; the run commands stay, with `mirage_vmaf --help` added.
  - Corrected from the repository's files: the tests (`mirage_tests`, `docs/BUILD.md` step 4, instead of 34 `ctest` tests of `NetworkLogger` and `ManifestParser`); the project tree (`tools/`, `third_party/glad/`, `docs/`, `eval/`, `docker/`, the VMAF sources, `Clock.h` instead of `Clock.cpp/h`, the four presets, `mirage_tests` instead of two test files); the common issues (no jinja2 issue since the build runs no Python; ffmpeg-full's pkg-config directory, which Homebrew doesn't link into its prefix; built-in models loaded by version and model files by `mirage_vmaf --model-file`, instead of a model path in `VMAFAnalyzer.cpp` and `LIBVMAF_MODEL_PATH`); the development workflow's test command. The FetchContent list, the first configure's time and the linker warning went with the build part.
  - Left as they were: the controls, the VMAF menu, the thread model and the license section, which need the player running to check, or which no file contradicts. The thread model lists fewer threads than the code starts (subtitle, playlist and network report threads among them); it doesn't say it lists all.
  - `vcpkg.json` removed: no reference to vcpkg or to a toolchain file in `CMakeLists.txt`, `CMakePresets.json`, the scripts, `src/`, `tests/` or `tools/` (`git grep`). It is still named in the original plan (`plan/`), in the gap report and in two committed `build.txt` files, which stay as they are.
- **Alternatives**: a three-line quick start kept in the README; the thread model completed from the code.
- **Rationale**: D-031: one place for the build steps, which the build checks verify, and a README whose facts match the repository.
- **Who**: agent (Claude)
- **Date**: 2026-10-24
- **Status**: confirmed by the owner (2026-10-26, D-041)

### P-057 A test fixed: a stop token that outlives the context it guards

- **Choice**: `tests/test_hls_variants.cpp`, test case `HLS master → variant playlist: FFmpeg structure, open_variant checks, same timeline`, passed `ss.get_token()`, a temporary, to `ffopen::open_variant` and then read packets from the context it opened. The context's interrupt callback keeps a pointer to the token, which `src/FfOpen.h` requires to outlive the context, so after the call the callback read a token that no longer existed. The token is now a named variable (`a0d69fc`). The test only; `src/FfOpen.*` and the player's helper decoders, which pass tokens that outlive their contexts, are unchanged. It came up while the headless configuration, a release build, was tried in scratch, whose runs aren't committed; no committed record ran a release build's tests before the build check at `5ae44a4`, which runs the fixed test in the debug and the headless build (P-058).
- **Alternatives**: leaving the test as it was, as a failure of release builds only; running the headless build's tests from a debug build; `open_input` keeping its own copy of the token, a change to code the player's helper decoders use (D-007).
- **Rationale**: the test broke the contract of the function it tests, and the tests of the headless configuration are part of D-031's Linux build; the code under test does what its header says.
- **Who**: agent (Claude)
- **Date**: 2026-10-24
- **Status**: confirmed by the owner (2026-10-26, D-041)

### P-058 The build check at 5ae44a4, after D-031's build changes

- **Choice**
  - `eval/build_check/run_build_check.sh` (P-007, P-022) changed with D-031: the release, debug and headless presets configured and built with nothing more than `docs/BUILD.md` gives, no glad venv; every line naming Python in each configuration's configure log, CMake cache and Ninja file; the unit tests of the debug and the headless build; the SHA-256 of the three builds' `mirage_vmaf` and whether the release and headless ones are identical; the committed glad files and a run of `third_party/glad/regenerate.sh` with the given Python 3.13; and the `url` and `sha256`, or git `revision`, of the installed ffmpeg-full, libvmaf and x264 formulae, with the x264 and libvmaf that ffmpeg-full's libraries link, which `docker/Dockerfile` pins. Its listing of the FetchContent dependencies reads declarations inside an `if()` block too.
  - One run, from a clean worktree of `5ae44a4`, the commit after the script's change and after every change to the build of this round; its output committed verbatim as `eval/build_check/macos-2026-10-24-5ae44a4.txt`. After `5ae44a4` the build changed in no file: the commits after it are documents, the record and a docstring of `eval/run_all.py`.
- **Alternatives**: the venv steps kept as well; the Python suites of `eval/` in the check too; a build of the commit before this round for a comparison of `mirage_vmaf`.
- **Rationale**: the record backs every statement of `docs/BUILD.md` about the build after D-031, as P-007 has it.
- **Result** (`eval/build_check/macos-2026-10-24-5ae44a4.txt`):
  - The release, debug and headless presets configured and built with exit status 0, and no line of the nine files names Python ("Python in the configure and the build").
  - The headless configuration fetched miniaudio, pugixml and Catch2 only, built `mirage_vmaf` and `mirage_tests` and neither `mirage` nor `snapshot_diff`, and its `mirage_vmaf` is identical to the release preset's, SHA-256 `9f04085f73440a9c9969f9bf6a86767eaf9ade89ae8ca393f8b6c99bb78b9b06`, which no committed measurement's `build.txt` names.
  - Every unit test passed in the debug build and in the headless build: 311 test cases each, with 111765 and 111752 assertions; the subtitle test of D-032 passed in a clean checkout.
  - `regenerate.sh` made glad's three files again with the bytes of the committed ones; the six FetchContent dependencies are at the commits the check at `6a6661f` fetched; CMake offers `macos-debug`, `macos-release` and `macos-headless` on the machine, not `linux-release`.
  - The installed formulae name the sources the Dockerfile pins: `ffmpeg-8.1.1.tar.xz` with SHA-256 `b6863adde98898f42602017462871b5f6333e65aec803fdd7a6308639c52edf3`, libvmaf's `v3.1.0.tar.gz` with `80090e29d7fd0db472ddc663513f5be89bc936815e62b767e630c1d627279fe2`, and x264's commit `b35605ace3ddf7c1a5d67a2eb553f034aef41d55` (r3222).
- **Limits**: one machine with its Homebrew packages, as P-022's; the player's window wasn't opened; neither the Linux preset nor the Docker image was built.
- **Who**: agent (Claude)
- **Date**: 2026-10-24
- **Status**: confirmed by the owner (2026-10-26, D-041)

### P-059 The full-length §5 report with the defaults of D-022, D-024, D-025, D-027 and D-028

- **Choice**
  - Build the release `mirage_vmaf` in a new worktree of `a5b3cc5`, the branch's head when the run started, release preset, the `mirage_vmaf` target only (`eval/consistency/defaults-full/build.txt`). After `5ae44a4` only documents, records and a docstring of `eval/run_all.py` had changed (P-058), and the program has the SHA-256 of the release and headless builds of the build check at `5ae44a4`, `9f04085f…`.
  - From that worktree, run `eval/consistency/run_delta.sh` without `--frames`, `--scaler` or `--model` on the four pairs of D-018, `--jobs 4`: so with the delta tool's defaults since D-024 and D-025 on both sides, bicubic and vmaf_v0.6.1, 4 libvmaf threads and the threshold 0.01 (`eval/consistency/defaults-full/plain/`). Its `run.cmd.txt` has `--scaler bicubic --model vmaf_v0.6.1`, the settings the run had, since `run_delta.py` writes every option, the defaults included; that the command gave neither `--scaler` nor `--model` is the agent's account, which no committed output records (P-064). The inputs are read from Apple's CDN, as in every committed run.
  - After committing the run: `pooling.py --delta-run eval/consistency/defaults-full/plain --check-pooled` from the clean tree at `8e61d95` (`eval/pooling/defaults-full/`); the steps `defaults-full`, with the head's program and bicubic named, and `pooling-defaults-full` in `eval/run_all.sh`, which `--skip-full` leaves out with the two earlier steps over every frame (`54fa9a2`); and `checks.txt` from the clean tree at `54fa9a2`, each command with its output.
  - The full-length report with bilinear, `eval/consistency/acceptance-full/`, stays committed as the record it is (D-024).
- **Alternatives**
  - `--scaler bicubic` and `--model vmaf_v0.6.1` named in the run.
  - `--jobs 1`, one scoring process at a time.
  - A build in the main working tree, as P-026 did.
- **Rationale**
  - D-024 asks for the full-length report of D-018 made again with bicubic. A run that names no scaler or model has the delta tool's defaults, as `defaults-300` had (P-041); `mirage_vmaf`'s own default is shown by the direct runs of `defaults-300/checks.txt`, check 4.
  - `--jobs` doesn't enter the scores (P-026); with 4 the run takes about as long as `acceptance-full`, and check 8 compares its first 300 frames with `defaults-300`, made one process at a time.
  - A new worktree ties the program to its commit (P-034, P-041).
- **Result** (`eval/consistency/defaults-full/checks.txt`): every pair passes (check 13). In each pair both sides scored the same 53946 pairs at the same timestamps and every score agrees at the 6 decimals of FFmpeg's log, a largest |delta| of 0.000000 (checks 4 and 13). Both sides used bicubic and vmaf_v0.6.1, the delta tool's defaults: `run.cmd.txt` has them, and each pair's commands give them to both sides (check 3); that the run named neither, which check 3's heading states, no output of the check shows (P-064). `mirage_vmaf` paired every frame, with no failure and one libvmaf context per pair (checks 5 and 6), and its conversions are FFmpeg's, gear2 and gear1 from smpte170m to bt709 (check 7). Frames 0 to 299 have the scores of `defaults-300` on both sides (check 8). The nine pooled values of each report are those `pooling.py` computes at full precision, and its mean, minimum and harmonic mean FFmpeg's at 6 decimals (checks 9 and 10; `eval/pooling/defaults-full/pooling.md`). Against `acceptance-full` no frame scores lower, and the scores rise by 5.863072 to 10.983899 on average per pair (check 11). With the three `showinfo` filters of P-047, FFmpeg's verbose log of a pair holds 91659880 to 92092360 bytes, 1877848 to 1903556 bytes compressed (check 12). Each side of each pair took 1250.509 to 1282.152 s (check 13).
- **Who**: agent (Claude)
- **Date**: 2026-10-24
- **Status**: confirmed by the owner (2026-10-26, D-041)

### P-060 The records at the end of the second round of Phase 1: the unit tests, and a rerun of every step

- **Choice**
  - The unit test record of P-028 and P-032 again, from the clean main working tree at `12715a6`, whose code and tests are those of `54fa9a2`, the last commit of this round that changes code or tests (`eval/test_runs/macos-2026-10-24-12715a6.txt`). It shows the changes to the C++ sources, CMake files and scripts since the build check at `5ae44a4`; instead of whether the subtitle fixture is present, that no source or test names its directory any more (D-032); and it runs the Python suites with `TMPDIR` in a scratch directory outside the repository.
  - `eval/run_all.sh` with no program given and `--jobs 4`, every step, the four over every frame included, from a new clean worktree of `61241e2`, the commit after the test record, with its output directory and `TMPDIR` outside the repository; its record is committed as P-032's is (`eval/run_all/2026-10-24-full/`).
- **Alternatives**
  - `--skip-full`, as the reruns of this round before it (P-037, P-042, P-046, P-049, P-052).
  - The unit tests in a clean worktree, as the build check runs them (P-058).
- **Rationale**
  - The PR's test record has to be of the code it merges (P-032).
  - The steps over every frame were last rerun with the program and the delta tool of `9855319` (P-032). Since then D-022, D-024, D-027, D-028 and D-029 changed the program and the delta tool, and the reruns left those steps to the full-length report with bicubic; the rerun of `acceptance-full` with the head's program and delta tool shows whether any of it moved a score of the bilinear report, and the rerun of `defaults-full` that the new report reproduces.
- **Result** of the unit tests (`eval/test_runs/macos-2026-10-24-12715a6.txt`): `mirage_tests '~[audio]'` passed at the first run, 111752 assertions in 311 test cases, and the four Python suites passed, 27, 132, 11 and 16 tests.
- **Result** of the rerun (P-062; `eval/run_all/2026-10-24-full/`): every step but `defaults-full` and `pooling-defaults-full` found everything as committed, `acceptance-full` and `pooling-full` among them. The machine's sleep cut those two short; rerun on their own, they found everything as committed (`eval/run_all/2026-10-24-defaults-full/`).
- **Who**: agent (Claude)
- **Date**: 2026-10-24
- **Status**: confirmed by the owner (2026-10-26, D-041)

### P-061 Information for D-011: candidate source clips, examined without downloading them

- **Choice**
  - Eight public clips from four sources as candidates for D-011's mezzanines, each with a Creative Commons license on an official page and a stable download address: Netflix's El Fuente sequences "Boat" and "Food Market" (4096x2160, 10-bit 4:2:0, in Netflix's open content bucket and in Xiph.org's test media collection), the 10-bit SDR source D-026's measurement needs; three SDR sequences of SVT's Natural Complexity suite (3840x2160 at 50 fps, 12-bit 4:4:4 JPEG 2000, lossless); and Xiph.org's YUV4MPEG2 copies of three Blender Foundation films, the Sintel trailer, Big Buck Bunny and Elephants Dream (1920x1080 at 24 fps, 8-bit 4:2:0).
  - `eval/streams/d011-candidates-2026-10-24.txt`, made from the clean tree at `dd9a007`, each command with its output: the sentences of each official page that name a license, before any reader comments; the bucket's listing and its license file; the copyright files Xiph.org keeps next to its copies of El Fuente; SVT's FTP listing and SHA-256 list; ffprobe of three SVT files over FTP; each YUV4MPEG2 file's header line, and its frame count from its size, read from its first and last bytes; each file's HTTP headers. No clip was downloaded.
  - The summary goes into `docs/PHASE1_DECISIONS_PENDING.md`, "D-011", with no recommendation: the clips and the encoding parameters are the owner's (D-011).
- **Alternatives**
  - Other sources: Netflix's other titles, Xiph.org's other test sequences, EBU or ITU test material under other terms; the DASH-IF and Unified Streaming streams that the work order's §8 names, which are streams without a mezzanine.
  - Downloading the clips, to decode their pictures and checksum them.
  - The pages read through a summarizing fetch rather than as their own text.
- **Rationale**
  - The license terms have to come from the official pages, and the properties without downloading tens of gigabytes; a record of commands and their outputs is what the pending document's numbers rest on.
  - A YUV4MPEG2 header gives the size, frame rate, chroma format and bit depth exactly, and its frame count follows from the file's size; ffprobe can't read an xz-compressed file without decompressing it from the start.
- **Limits**: what the pages and files said on 2026-10-24. For El Fuente, Netflix's page and its bucket's license file name the Creative Commons Attribution 4.0 license, where the copyright files of 2015 at Xiph.org name Attribution-NonCommercial-NoDerivatives 4.0; which terms hold is not settled here. The El Fuente files of Netflix's bucket and of Xiph.org have the same sizes but weren't compared byte for byte. The SVT sequences' color range is not tagged and their transfer is unknown to ffprobe; the YUV4MPEG2 files carry no color tags at all.
- **Who**: agent (Claude)
- **Date**: 2026-10-24
- **Status**: confirmed by the owner (2026-10-26, D-041)

### P-062 The rerun of every step at 61241e2, which the machine's sleep cut short, and the rerun of its two unfinished steps

- **Choice**
  - Commit the record of P-060's rerun of every step as it ended, in `5908e3b` (`eval/run_all/2026-10-24-full/`): the files every record in `eval/run_all/` keeps, and besides them the files of the failed `defaults-full` run that show why it failed (the run's reports, environment and command, and of the two failed pairs their commands, reports and both programs' stderr logs), the machine's sleep and wake events during the run from macOS's power management log, `pmset -g log`, with their times in UTC (`power_log.txt`), and a README that says what happened.
  - The `mirage_vmaf` of `gear2_vs_gear5` was stopped with SIGTERM by the workflow running this stage, before the delta tool's own limit of 6 h per scoring process (`run_delta.py --timeout`) would have stopped it. What the committed files show of it: its stderr log has two lines about the playlists' MIME type and otherwise FFmpeg's "Invalid timestamps" warnings, as the log of the `mirage_vmaf` of `gear1_vs_gear5`, which ran to its end, has too, and no line with "error", "fail" or "timed out"; and it exited with status -15 after 5801.1 s of the time the machine was awake (`logs/defaults-full_plain.log`; `consistency/defaults-full/plain/gear2_vs_gear5/delta.md`). What the workflow saw that made it stop the process is in no committed file, so nothing here rests on it. The step it belonged to, `defaults-full`, then failed, and `pooling-defaults-full` found no report to read.
  - Then the two steps that didn't complete, on their own: `eval/run_all.sh <out_dir> --only defaults-full,pooling-defaults-full --jobs 4` with no program given, from a new clean worktree of `5908e3b`, the commit of the first record, with its output directory and `TMPDIR` outside the repository, under `caffeinate -i -m -s`; its record is committed in `eval/run_all/2026-10-24-defaults-full/` (`37fc66e`), as that of P-027's run of two steps is in `eval/run_all/2026-10-23-full/`. It is the second attempt. The first, from another new clean worktree of `5908e3b`, was stopped by the agent and left no record, since `run_all.py` writes its record when it ends; what the agent saw of it is in no committed file, so nothing here rests on it.
  - No code changed for it. Whether the analyzer should limit its network reads, which FFmpeg 8.1.1's defaults leave without a time limit (read from its sources), is an open item for the owner ("A network read without a time limit" in `docs/PHASE1_DECISIONS_PENDING.md`).
- **Alternatives**
  - Every step again, from a new worktree.
  - Only the rerun of the two steps committed, or the first record with only the usual files.
  - The process left to the 6 h limit.
- **Rationale**
  - Every other step of the first rerun found everything as committed, and the two that didn't complete need no output of the others (`pooling-defaults-full` needs only `defaults-full`), so `--only` reruns what is missing, as the record `2026-10-23-full/` reran the full-length steps alone (P-027).
  - The first rerun is a record as it ended, a failure included. Why it failed is in files the record holds: the sleep in the power log, the lost connections in the programs' logs, the stop in the delta report.
  - A scoring process stopped by its 6 h limit fails its pair as well; the record shows the stop as it was made, with its exit status.
- **Result** of the rerun at `61241e2` (`eval/run_all/2026-10-24-full/rerun_check.md`, `run_all.md`, `README.md`):
  - 23 of its 25 steps found everything as committed. Among them `acceptance-full` and `pooling-full`: the head's program and delta tool give every pair of the full-length report with bilinear the committed verdict, Mirage's per-frame scores at full precision and every libvmaf metric of FFmpeg's logs, though its program, `9f04085f…`, is not the one that made it, `3633c43e…`. The programs built from `83c8ed6`, `d993686` and `e8b867f` have the SHA-256s of the runs they reran.
  - In `defaults-full`, `gear4_vs_gear5` and `gear3_vs_gear5` have every verdict and per-frame score of both sides as committed. While `gear2_vs_gear5` and `gear1_vs_gear5` were scored, the machine went to sleep because its lid was closed, on battery, from 2026-10-24T20:16:34Z to 22:19:35Z, waking in between only for maintenance, 5 to 19 s at a time (`power_log.txt`). `caffeinate -i` keeps a Mac from idle sleep, not from sleep with its lid closed. The programs then logged TLS reads that timed out and a host name that didn't resolve, and FFmpeg's hls demuxer skipped each segment it couldn't open: in `gear1_vs_gear5`, `mirage_vmaf` scored 28525 frame pairs and FFmpeg 28704, and in `gear2_vs_gear5` FFmpeg 29597; `mirage_vmaf` of `gear1_vs_gear5` counted 395 of 28920 frames as failed, 348 of them after a discontinuity, and warned (D-028; its `mirage.stdout.txt`). The `mirage_vmaf` of `gear2_vs_gear5` logged no error and was stopped as described above (`delta.md`: exit status -15). `pooling-defaults-full` didn't run.
  - The times the tools record count only the time the machine was awake: they come from Python's `time.monotonic()`, `mach_absolute_time()` on macOS, which stops while the machine sleeps. `run_all.md` gives 10127.3 s for a run whose start and end are 4 h 50 min apart.
- **Result** of the rerun of the two steps (`eval/run_all/2026-10-24-defaults-full/rerun_check.md`, `run_all.md`): both found everything as committed: every pair's verdict, all 53946 per-frame scores of every pair on Mirage's side at full precision, every libvmaf metric of FFmpeg's logs and every pooling series. The program it built from `5908e3b` has the SHA-256 of `eval/consistency/defaults-full/build.txt`, `9f04085f73440a9c9969f9bf6a86767eaf9ade89ae8ca393f8b6c99bb78b9b06`. 3146.5 s of wall time, the build included, of which 3055.2 s for `defaults-full`; each side of each pair took 1453.8 to 1535.4 s (`logs/defaults-full_plain.log`). The run's start and end, 2026-10-25T00:08:21Z and 01:00:48Z, are as far apart as its wall time, so the machine was awake throughout.
- **Who**: agent (Claude)
- **Date**: 2026-10-24
- **Status**: confirmed by the owner (2026-10-26, D-041)

### P-063 A reference whose frames change size is not scored

- **Choice**
  - The analyzer that the player and `mirage_vmaf` share ends the analysis with an error when the reference frame of a pair has another size than the one the reference's decoder had when it was opened, the size every pair is scored at (`score_pair()` in `src/VMAFAnalyzer.cpp`). The error names both sizes and the reference frame's position in its input. `mirage_vmaf` then writes no report and exits with status 1 (P-006), and the player's VMAF panels get the error as they get the analyzer's other errors (not seen on screen). `mirage_vmaf --help` says so.
  - Before, read from the code: a yuv420p reference's frames of another size went through `sws_scale()` with the context made for the first size, the decoder's current height as the slice height; a reference in another pixel format was converted back to the first size by D-022's conversion; either way without an error or a warning.
  - A distorted input whose frames change size is scaled frame by frame to the reference's size, as before (`vmafcolor::convert()`).
  - `tests/test_vmaf_analyzer.cpp` writes MPEG-TS clips whose MPEG-2 stream changes size midway, in yuv420p and in yuv422p (D-022's conversion), with MPEG-2's low-delay flag so that FFmpeg's decoder keeps the frame before the change. It checks the error, that the frames before the change are scored with a frame limit that stops there, and that a distorted input whose size changes is scored without a failure.
- **Alternatives**
  - Converting each reference frame of another size to the first size, as D-022's conversion did for references in other pixel formats.
  - Scoring each run of frames of one size in a libvmaf context of its own, at that size, with the distorted frames scaled to it.
  - A warning instead of an error; the size of the reference's first decoded frame instead of the decoder's when it was opened.
- **Rationale**
  - The decisions don't cover a reference whose size changes. FFmpeg's libvmaf filter takes inputs of one size only (`input width must match`, `eval/consistency/ffmpeg-libvmaf-filter/checks.txt`, check 4), so FFmpeg has no score for such pairs to match, and the copy of a yuv420p reference read frames of another size with the first size's geometry.
  - An error leaves no report that could pass for a complete one (P-006), and changes nothing for a reference whose size stays the same.
- **Limits**: no committed run has an input whose size changes; what FFmpeg's §5 graph does with such a reference, or with such a distorted input, is not measured in a committed run (MEASUREMENT PENDING).
- **Who**: agent (Claude)
- **Date**: 2026-10-26
- **Status**: confirmed by the owner (2026-10-26, D-041)

### P-064 run.cmd.txt names the options its command line gave

- **Choice**: `eval/consistency/run_delta.py` and `ladder_check.py` write, before a run's command in `run.cmd.txt`, a comment line with the options the run's command line gave, by their long names, in the order given (`given_options()`: the arguments parsed again with every default suppressed, so that an abbreviation or `--name=value` counts as the option it names). The command keeps every option the run had, the defaults included, which `eval/run_all.sh` reads. Runs made before this change, `defaults-300/` and `defaults-full/` among them, have no such line; their command blocks in `eval/consistency/README.md` are the commands as the agent gave them.
- **Alternatives**
  - The arguments as given, with their values, each local path replaced by its placeholder (P-011) again.
  - The command with every option only, as before.
  - The same line in the run's `delta.json` as well.
- **Rationale**
  - A run made to show the tool's defaults, such as `defaults-300/` (P-041) and `defaults-full/` (P-059), has `--scaler bicubic --model vmaf_v0.6.1` in its `run.cmd.txt` whether or not they were typed, so no committed output says that the defaults were used without naming them. The names of the options given say it without repeating the values the command has, and without paths.
- **Who**: agent (Claude)
- **Date**: 2026-10-26
- **Status**: confirmed by the owner (2026-10-26, D-041)

### P-065 The records after the review fixes: the unit tests at a4692a2, and a rerun of every step, which the machine's sleep cut short

- **Choice**
  - The unit test record of P-028, P-032 and P-060 again, from the clean main working tree at `a4692a2`, the last commit of the review fixes that changes code, tests or scripts (`eval/test_runs/macos-2026-10-26-a4692a2.txt`).
  - `eval/run_all.sh` with no program given and `--jobs 4`, every step, the four over every frame included, from a new clean worktree of `a4692a2`, with its output directory and `TMPDIR` outside the repository. The machine's sleep cut it short, and its record is committed as it ended, as P-062's is, with the failed run's reports and logs and the machine's sleep and wake events (`eval/run_all/2026-10-26-fix/`).
  - After the machine woke, the four scoring processes still running were sampled twice, 120 s apart, before they were stopped with SIGTERM, and the samples are committed with the record (`stall_check.txt`), so that what is said about them rests on a committed file.
  - The two steps the sleep cut short rerun on their own, `eval/run_all.sh <out_dir> --only defaults-full,pooling-defaults-full --jobs 4`, from a new clean worktree of `1f7d42d`, the commit of that record (`eval/run_all/2026-10-26-defaults-full/`).
- **Alternatives**
  - Waiting for the stalled processes: they read without a time limit (P-062), so only the delta tool's 6 h limit would have ended them.
  - Rerunning every step again, not only the two.
  - Not committing the record that was cut short: its other 23 steps are the only rerun of them with the program built after P-063 and P-064.
- **Rationale**
  - P-063 changed the analyzer, and P-064 the delta tool and the ladder check, after the last rerun of every step (P-060, P-062). The rerun shows whether either changed a committed verdict or score.
  - A record that is cut short is kept as it ended, with what shows why, as P-062's is: committed outputs aren't edited afterwards, and the two steps' rerun stands on its own record.
- **Result** of the unit tests (`eval/test_runs/macos-2026-10-26-a4692a2.txt`): `mirage_tests '~[audio]'` passed at the first run, 112039 assertions in 312 test cases, one more case than at `12715a6`, P-063's; and the four Python suites passed, 27, 133, 11 and 16 tests.
- **Result** of the rerun at `a4692a2` (`eval/run_all/2026-10-26-fix/`): 23 of its 25 steps found everything as committed, `acceptance-full` and `pooling-full` among them. The program it built from `a4692a2`, SHA-256 `217cc2948bf361aa11a37f4aea467ffe1caa30ab251943ba18b72c55005c6f10`, which no earlier run's `build.txt` names, gives every run it reran that was made after D-016 by another build the committed verdicts and scores; the programs built from `83c8ed6`, `d993686` and `e8b867f` have the SHA-256s of the runs they reran (`rerun_check.md`). In `defaults-full`, `gear4_vs_gear5` and `gear3_vs_gear5` were as committed. The machine went to sleep with its lid closed at 2026-10-26T19:51:20Z, while the other two pairs were being scored, and woke at 21:12:21Z (`power_log.txt`). Their four scoring processes, `mirage_vmaf` and `ffmpeg` alike, had last written their stderr logs at 19:51:16Z and 19:51:17Z, logged no error, and after the wake used 0.05 s of CPU time each in 120 s (`stall_check.txt`); they were stopped with SIGTERM. `pooling-defaults-full` had no reports to read (`README.md`).
- **Result** of the rerun of the two steps (`eval/run_all/2026-10-26-defaults-full/rerun_check.md`, `run_all.md`): both found everything as committed: every pair's verdict, all 53946 per-frame scores of each of the four pairs on Mirage's side at full precision, every libvmaf metric of FFmpeg's logs and every pooling series. The program it built from `1f7d42d` has the SHA-256 of the one built from `a4692a2`, `217cc294…`. It took 2719.8 s of wall time, the build included, of which 2666.9 s for `defaults-full`.
- **Who**: agent (Claude)
- **Date**: 2026-10-26
- **Status**: confirmed by the owner (2026-10-26, D-041)

### P-066 How D-035 to D-038 are implemented, the negotiation cases' run, and the records after them

- **Choice**
  - **D-036.** `src/VMAFNet.h`, header-only since `mirage_vmaf` doesn't link the player's `src/FfOpen.cpp`, holds the limits: `timeout` and `rw_timeout` of 5 s (`5000000` µs, FfOpen's values) for an http(s), rtmp or rtsp URL, FfOpen's rule, and an interrupt callback whose opaque is a `std::stop_token`. The analyzer's `DecodeCtx::open()` allocates the format context, sets the callback on the analysis's stop token and passes the limits to `avformat_open_input()`; `vmafladder::read_master()` passes the limits to `avio_open2()`, without a callback, since `mirage_vmaf` runs it synchronously. None of FfOpen's other options (segment extensions, `http_multiple`) is added. `mirage_vmaf --help` states the limits. Tests (`tests/SilentServer.h`, a local server that accepts connections and sends nothing): the analyzer's open and the ladder's master read fail within 4 to 15 s, where the server would hold the connection for 30 s, and `cancel()` returns within 2 s of a read that waits; with the limits and the callback taken out, the three fail after the server's 30 s (a scratch run, not committed).
  - **D-037.** `vmafpool::pool()` takes the libvmaf contexts' first pairs (`context_starts`; pair 0 always starts one) and reports `min_without_context_first_frames`, the smallest score of the pairs that start no context, `null` when every pair starts one; the report's `pooled` object and `--help` have it after `min_without_first_frame`, which stays as D-027 has it. `eval/pooling/pooling.py` doesn't compute the new value, so its `--check-pooled` checks D-027's nine values as before, and the committed pooling records reproduce.
  - **D-038.** CMake's cache variable `MIRAGE_LIBVMAF_PACKAGE` gives the package version where the library's real path names none; a version the path names wins, with a warning when the two differ. `run_delta.py --ffmpeg-libvmaf-package` does the same for FFmpeg's side and is recorded in `run.cmd.txt`; `run_all.py` passes it to every delta run. The Dockerfile configures with `-DMIRAGE_LIBVMAF_PACKAGE="${LIBVMAF_VERSION}"`, and its entry point gives `--ffmpeg-libvmaf-package 3.1.0`, the `LIBVMAF_VERSION` it builds, written out since an exec-form entry point doesn't expand build arguments. Not built (no container runtime; MEASUREMENT PENDING).
  - **D-035's run.** `color_cases.py --set negotiation`: (a) the full range H.264 reference of the format cases, decoded as yuvj420p, against a distorted input without a range tag, with the limited range 10-bit FFV1 reference against the same input as its control, and (b) the 10-bit reference against a gray and an RGB FFV1 distorted input; FFV1 gives the planar RGB clip back as bgr0, which the clip states. `run_delta.sh` with its defaults, bicubic among them, the first 10 frames, with a release `mirage_vmaf` built in a clean worktree of `da0464c`; committed as `eval/consistency/negotiation-cases/` with `build.txt`, the clips' record and `checks.txt`, and the `run_all.sh` steps `negotiation-clips` and `negotiation-cases`.
  - **The records after them.** The unit test record of P-065 again, from the clean main working tree at `bbc669d`, the last commit that changes code, tests or scripts (`eval/test_runs/macos-2026-10-26-bbc669d.txt`), and `eval/run_all.sh` with `--jobs 4`, every step, from a new clean worktree of `bbc669d`. A process that had stopped writing its log and the machine's sleep cut that run short, and its record is committed as it ended, as P-062's and P-065's are, with the failed run's reports and logs, the machine's sleep and wake events and the stopped process's CPU times (`eval/run_all/2026-10-26-d035/`). The two steps it didn't finish were rerun on their own, `eval/run_all.sh <out_dir> --only defaults-full,pooling-defaults-full --jobs 4`, from a new clean worktree of `953d42e`, the commit of that record (`eval/run_all/2026-10-27-defaults-full/`).
- **Alternatives**
  - D-036: `ffopen::open_input()` itself, with its other options and the network logger, in the player's analysis only; an option of `mirage_vmaf` for the limit (option 4, not chosen).
  - D-037: other names, such as `min_excluding_context_starts`; computing the value in `pooling.py` too, which would change the committed pooling records' series.
  - D-038: a variable that overrides the path's version; an environment variable for the delta tool.
  - D-035: other clips for (a), such as a 10-bit full range reference, and another frame count; the delta tool's old bilinear default, as the format cases.
- **Rationale**: each follows the decision's wording with the least change to what exists: the player's own limits and rules, D-027's value untouched, the version from the path where Homebrew gives it. The negotiation cases use the clips and options of the earlier synthetic runs where they can, with today's defaults.
- **Result** of the negotiation cases (`eval/consistency/negotiation-cases/checks.txt`): all four pass, every frame within FFmpeg's 6 decimals (check 4). In n01 both tools make the distorted input without a range tag full range, `bt709/unknown -> bt709/pc`, against the full range reference (checks 3 and 5); in n02, the control, limited range. `mirage_vmaf` warns that bgr0's conversion isn't checked byte for byte against FFmpeg's, and the delta tool that FFmpeg's log names more conversions of the gray input than `mirage_vmaf`'s report (check 6).
- **Result** of the unit tests (`eval/test_runs/macos-2026-10-26-bbc669d.txt`): `mirage_tests '~[audio]'` passed at the first run, 112082 assertions in 317 test cases, and the four Python suites passed, 28, 136, 11 and 16 tests.
- **Result** of the rerun at `bbc669d` (`eval/run_all/2026-10-26-d035/`): 25 of its 27 steps found everything as committed, `acceptance-full`, `pooling-full`, `negotiation-clips` and `negotiation-cases` among them. The program it built from `bbc669d`, SHA-256 `aaac3630522dc4261f6fa27bb8e81dc66a64ba9860292ecdeae5e9164fb372d3`, has that of the negotiation cases' run, and gives every run it reran that was made after D-016 by another build the committed verdicts and scores; the programs built from `83c8ed6`, `d993686` and `e8b867f` have the SHA-256s of the runs they reran (`rerun_check.md`). In `defaults-full`, `gear4_vs_gear5` was as committed. The `ffmpeg` of `gear2_vs_gear5` last wrote its stderr log at 2026-10-26T23:59:41Z, the machine went to sleep with its lid closed at 2026-10-27T00:03:35Z and woke for good at 17:06:51Z (`power_log.txt`), and after the wake that process used 0.04 s of CPU time in 120 s; it was stopped with SIGTERM (`stall_check.txt`). The stderr logs of `mirage_vmaf` in the three other pairs hold network reads that timed out ("Operation timed out"), and its reports scored 52442, 4425 and 15639 of the 53946 frame pairs, each with D-028's warning that more frames than the threshold failed to pair, most of them after a discontinuity, and exit status 0, as D-028 has it (`README.md`). `pooling-defaults-full` read those reports and differs from its committed outputs.
- **Result** of the rerun of the two steps (`eval/run_all/2026-10-27-defaults-full/rerun_check.md`, `run_all.md`): both found everything as committed: every pair's verdict, all 53946 per-frame scores of each of the four pairs on Mirage's side at full precision, every libvmaf metric of FFmpeg's logs and every pooling series. The program it built from `953d42e` has the SHA-256 of the one the rerun above built from `bbc669d`, `aaac3630…`. It took 3172.9 s of wall time, the build included, of which 3089.7 s for `defaults-full`.
- **Who**: agent (Claude)
- **Date**: 2026-10-26
- **Status**: pending owner confirmation

### P-067 How D-043 to D-048 are implemented: the pool, the memory limit and the end of a run

- **Choice**
  - `vmafladder::score()` runs `min(jobs, pairs)` workers; each takes the next pair in the plan's order and scores it with `VMAFAnalyzer::analyze()` and the run's options, which now takes a stop token. The default size is `vmafproc::default_jobs()`: `hw.physicalcpu` minus one on macOS, the distinct (package, core) pairs of `/sys/devices/system/cpu/cpu*/topology` minus one on Linux, at least 1 (`src/VMAFProcess.h`). `--jobs` takes 1 to 256.
  - The memory limit: before a worker starts a pair while others run, it reads the process's resident memory (`task_info`'s resident size on macOS, `/proc/self/statm` on Linux) and starts it only if that plus the memory gained since the run began, divided by the pairs running, stays under the limit; otherwise it waits, reading again every 20 ms. With no pair running a pair always starts. A pair running alone over the limit gets one warning, checked every 50 ms and when it ends. The report's peak resident memory is `getrusage`'s `ru_maxrss` at the end. `--memory-limit` takes a whole number of bytes, or of KiB, MiB, GiB or TiB with K, M, G or T.
  - The end: a stop requested on the run's token, which SIGINT and SIGTERM request in `mirage_vmaf`, or the first pair whose analysis is incomplete or whose settings aren't those of the first pair to complete, stops the pairs running and starts no other. A pair cut short by the stop is "stopped", one never started "not started"; the run is "failed" when a pair failed, else "cancelled" when a pair isn't complete, else "complete". The report lists the complete pairs, each with its start and end in seconds from the run's start, then the others with their state, and the pool's fields; `mirage_vmaf` writes it in all three cases and exits with 1 unless the run is complete.
  - The player's analysis is unchanged (D-043).
- **Alternatives**: a fixed queue of pairs per worker; a memory estimate per pair from the inputs' frame sizes (D-045's alternative); a sampled peak instead of `ru_maxrss`; no upper bound on `--jobs`.
- **Rationale**: each follows its decision with the least machinery: the plan's order keeps the report the same at every size; the admission rule measures the memory the limit is about, and needs no model of a pair's memory; `ru_maxrss` is the system's own peak, which `/usr/bin/time -l` reports too.
- **Who**: agent (Claude)
- **Date**: 2026-10-26
- **Status**: pending owner confirmation

### P-068 How D-046 is implemented: the discontinuity sequences come from the timestamps

- **Choice**: a discontinuity, as D-028's rule finds it in an input (a timestamp that goes back, or forward by more than 10 s), starts the input's next discontinuity sequence, whose times are measured from that frame; the k-th sequences of the two inputs are paired by D-028's rules, a sequence only one input has pairs with nothing, and each sequence after the first starts a new libvmaf context (`src/VMAFAlign.h`). The playlists' `EXT-X-DISCONTINUITY` tags aren't read: FFmpeg's hls demuxer doesn't say which segment a frame came from, so a tag can't place a sequence's start in the frames the analyzer gets, and M5 shows that where the timestamps go on across a tag, the frames pair without one (`eval/phase2-survey/m5/record.txt`).
- **This departs from D-046's wording**, which takes the sequences "from the playlists' `EXT-X-DISCONTINUITY` tags where there are any, else from the timestamp jumps". In a ladder whose rungs all jump at the same tags, as M5's streams do, the two agree; they would differ where a tag's timestamps jump in one rung and go on in another, or where timestamps jump without a tag. Provisional until the owner decides.
- **Alternatives**: reading each media playlist's tags and placing each sequence by the playlist's durations before it; counting the tags and warning when the timestamps' jumps don't match them.
- **Result**: with the release `mirage_vmaf` of `f988091`, M5's streams pair every frame where both rungs' timestamps start again or jump 60 s ahead, 60 pairs and no failure (`eval/phase2-survey/m5-d046/record.txt`). The committed alignment case `a07_restart`, whose distorted input alone restarts, keeps its pairs and scores; its 10 failures are now 5 unmatched reference and 5 extra distorted frames, not 10 after a discontinuity (from the code; its rerun is the next `eval/run_all.sh` run's).
- **Who**: agent (Claude)
- **Date**: 2026-10-26
- **Status**: pending owner confirmation

### P-069 Phase 2's measurements so far: the machine, M5, and the local copy of the acceptance ladder

- **Choice**
  - M1, the machine's cores and memory, as `sysctl` gives them (`eval/phase2-survey/machine.txt`).
  - M5, `eval/phase2-survey/m5_discontinuities.py`: two rungs, 1920x1080 and 640x360, two periods of 30 frames from FFmpeg's `testsrc2` and `testsrc`, MPEG-2 in MPEG-TS with every frame a key frame; an `EXT-X-DISCONTINUITY` whose timestamps start again, start 60 s later, or go on, and rungs whose segments differ; each frame's timestamp through FFmpeg's hls demuxer (ffprobe), and `mirage_vmaf`'s alignment of each pair, before D-046 (`m5/record.txt`, the release program of `e5a34ea`) and after it (`m5-d046/record.txt`, of `f988091`).
  - D-049's local copy: `eval/streams/local_copy.py` keeps the master of Apple's advanced sample and its 8 video playlists cut after their first 2 segments, with an `EXT-X-ENDLIST`, enough for the first 300 frame pairs of each pair at 30 or 60 fps, the media outside the repository and their SHA-256s in `eval/streams/local-advanced-2026-10-26/`.
  - The acceptance's check, `eval/ladder/pool_check.py`: the ladder at several pool sizes, each run's report, output and `/usr/bin/time -l`, and every pair compared at full precision.
- **Alternatives**: M5's streams from a real packager; a copy of every segment; comparing only the pooled values.
- **Rationale**: the smallest inputs that show the behavior each measurement is about, from FFmpeg alone, as the earlier synthetic cases are made.
- **Who**: agent (Claude)
- **Date**: 2026-10-26
- **Status**: pending owner confirmation

### P-070 M5's gaps: discontinuity sequences that don't line up, and the report's start after a discontinuity

- **Choice**
  - `eval/phase2-survey/m5_gaps.py`, after M5, for gaps: a synthetic ladder of two rungs, 36 s of FFmpeg's `testsrc2` at 640x360 and 320x180, MPEG-2 in MPEG-TS with every frame a key frame, in segments of about 2 s, and media playlists without six segments in a row, about 12 s, and without a tag, as when FFmpeg's hls demuxer skips segments it can't read: the reference alone, both inputs at different places, both at the same place. Each case scored by the release `mirage_vmaf` of `e5d6e9d`, D-046's pairing, and by the one `eval/run_all.sh` built from `bbc669d` on `vmaf/phase-1`, D-028's rule (`eval/phase2-survey/m5-gaps/record.txt`).
  - Made after Phase 1's rerun at `bbc669d`, in which `mirage_vmaf`'s network reads timed out and its reports of two pairs counted 2 and 7 timestamp discontinuities (`eval/run_all/2026-10-26-d035/consistency/defaults-full/plain/gear3_vs_gear5/delta.md` and `gear1_vs_gear5/delta.md` on `vmaf/phase-1`).
  - The fix it found: since `f988091` a discontinuity replaced the input's start with its sequence's, so the report's `alignment.reference.start` and `alignment.distorted.start` gave the start of the input's last sequence. `7ad0929` keeps each sequence's start apart for the pairing, and the report gives the input's start again (`tests/test_vmaf_align.cpp`); pairs and scores don't change. No committed record has these fields for an input with a discontinuity.
- **Alternatives**: segments of Apple's sample left out of its playlists; a unit test on timestamps alone, which doesn't go through FFmpeg's hls demuxer.
- **Result** (`eval/phase2-survey/m5-gaps/record.txt`):
  - Both inputs lack different segments, the reference its segments 4 to 9, the distorted input its segments 10 to 15: with D-046's pairing, 359 pairs, of which the 119 of the second libvmaf context pair a reference frame with the distorted frame 12.012 s later, from reference 21.453367 s with distorted 33.465367 s to reference 25.390633 s with distorted 37.402633 s. It counted 360 unmatched reference and 360 extra distorted frames and warned of the failure rate, 66.73%, as for any failures; nothing in the report sets the 119 pairs apart, and their mean score, 11.564222, is above that of the 240 others, 9.873372. With D-028's rule, 240 pairs and 958 frames after a discontinuity.
  - Both lack the same segments: with D-046's pairing, 719 pairs and no failure; with D-028's rule, 240 pairs and 958 frames after a discontinuity.
  - The reference alone lacks them: 240 pairs with either; with D-046's pairing 479 unmatched reference and 839 extra distorted frames, with D-028's rule 1318 frames after a discontinuity.
  - Each input's start is 1.433367 s in all six reports, of both programs.
- **Who**: agent (Claude)
- **Date**: 2026-10-27
- **Status**: pending owner confirmation; the question the result raises is the owner's ("Discontinuity sequences that don't line up" in `docs/PHASE2_DECISIONS_PENDING.md`)

### P-071 Phase 2's measurements: M2, M3, the acceptance across pool sizes at 300 frames, and the memory limit

- **Choice**
  - M2 and M3 by `eval/phase2-survey/m2_m3_baseline.sh`, with the `mirage_vmaf` that `eval/run_all.sh` built from `953d42e` on `vmaf/phase-1`, from before the pool (SHA-256 `aaac3630…`, `eval/run_all/2026-10-27-defaults-full/builds/head/build.txt` on that branch), on the local copy of Apple's advanced sample (`eval/streams/local-advanced-2026-10-26/`), the first 300 frame pairs (`eval/phase2-survey/m2-m3/record.txt`).
  - D-049's runs across pool sizes: `eval/ladder/pool_check.py` at the sizes 1, 2, 4 and the default, the first 300 frame pairs of each pair, with a `mirage_vmaf` built in a new clean worktree of `1cc1ab0` (`eval/ladder/advanced-local-300/`).
  - The memory limit at 300 frames: `pool_check.py --jobs 1,0 --memory-limit 1G`, 1 GiB being about three pairs' worth by M2 and under the default size's peak, so that the limit holds pairs back. Since `048a9ac` the check fails a run whose peak, by its report or by `/usr/bin/time -l`, is over the limit, with tests (`eval/ladder/tests/test_pool_check.py`). Run with the programs of `1cc1ab0`, `a3d011b` and `dfed5a3`, each built in a new clean worktree (`eval/ladder/advanced-local-300-mem1g-1cc1ab0/`, `-a3d011b/`, `-dfed5a3/`), the last two with one pair's peak memory after 1 to 300 frame pairs (`warm_up.txt`).
  - Every run one at a time with nothing else running, on battery power, with no sleep (the records' READMEs).
- **Alternatives**: the CDN, as D-049's one check run does; another limit; the wall times on AC power.
- **Result**
  - M2: the top rung, 1080p60, against `v8`, 1080p60, peaked at 292995072 bytes of resident memory in 4.20 s, against `v2`, 480x270 at 30 fps, at 272957440 bytes in 4.19 s. M3: the ladder's 7 pairs one after another, 304955392 bytes in 28.91 s.
  - Across pool sizes: PASS, every pair the same at every size at full precision. The peaks were 304529408, 581894144, 1111425024 and 2009759744 bytes at the sizes 1, 2, 4 and the default, 11, at which 7 pairs ran at once; the wall times 33.146, 20.173, 13.558 and 14.553 s, one run each on battery power.
  - The memory limit: FAIL with each program. The peak was 2011676672 bytes with 7 pairs at once (`1cc1ab0`), 1132167168 with 4 (`a3d011b`) and 1131544576 with 4 (`dfed5a3`), against the limit of 1073741824 (P-072).
  - One pair's peak alone (`warm_up.txt` of `dfed5a3`): 128729088 bytes after 1 frame pair, then 271597568 after 5, 284639232 after 30 and 293208064 after 300; and in another run each, 293306368 after 300, 294453248 after 450, 295059456 after 600 and 301056000 over all 720 frame pairs of the copy (`eval/ladder/advanced-local-pair-memory/record.txt`).
- **Who**: agent (Claude)
- **Date**: 2026-10-27
- **Status**: pending owner confirmation

### P-072 The memory limit's rule: a pair's memory measured once it has warmed up

- **Choice**
  - `60c1f7b`: P-067's rule took a pair to use the memory gained since the run began shared out among the pairs running. At a run's start every worker asked at once, before any pair had allocated what it uses, so with 1 GiB all 7 pairs started within 0.34 ms (`eval/ladder/advanced-local-300-mem1g-1cc1ab0/`). A pair now warms up until it has scored `vmafladder::kWarmUp`, 30, frame pairs, or all of them if it has fewer, which `VMAFOptions::scored` counts for the pool; the player's analysis sets no counter. What a pair uses is measured while no pair is warming up, and kept at its largest; until it has been measured no other pair starts.
  - `dfed5a3`: `60c1f7b` counted the pairs still warming up at that measure, and three started together once the first had warmed up (`-a3d011b/`). Now no pair starts while a running pair is warming up, so that each admission sees what every running pair uses: a pair starts while the resident memory, plus what a pair uses as measured then, stays within the limit, D-045's rule. `vmafladder::admits()` holds the rule, and `tests/test_vmaf_ladder.cpp` tests it on its own.
- **Alternatives**: a fixed time between starts; a margin on what a pair uses; D-045's alternative, an estimate from the inputs' frame sizes.
- **Rationale**: D-045 lets a pair in on what a running pair uses, which the resident memory shows only once the pair has allocated it. By `warm_up.txt` a pair alone is at 97% of its peak after 30 frame pairs, 284639232 bytes of 293208064 after 300.
- **Result** (`eval/ladder/advanced-local-300-mem1g-dfed5a3/`): the pairs started one after another, each once the one before had warmed up, and 4 ran at once; the peak, 1131544576 bytes, is 5.4% over the limit of 1073741824. After the first four ended the others ran one at a time, and the report warns that pair 4 took the process over the limit on its own, at 1095270400 bytes, more than three times one pair's peak. Whether the limit should hold the peak, and how, is the owner's ("The memory limit's peak" in `docs/PHASE2_DECISIONS_PENDING.md`).
- **Who**: agent (Claude)
- **Date**: 2026-10-27
- **Status**: pending owner confirmation

### P-073 How D-050 to D-052 are implemented, and their runs

- **Choice**
  - **D-050** (`23d733f`): each input keeps where each of its discontinuity sequences ended. Once an input has jumped to its next sequence, a frame of the other input's current sequence later than the jumped input's last by more than the grid's frame duration, the coarser input's, puts the two jumps at different places; pairing then stops: the other input's frames dropped since the grid input's last decided frame are counted as before, and every frame not paired or counted yet, and every frame to come, fails as after a discontinuity, as under D-028's option (a) (`src/VMAFAlign.h`). Jumps at the same place, or a frame apart, pair as under D-046.
  - **D-051** (`af17121`): `vmafladder::admits()` lets a pair start while the memory before the run, plus what a pair uses as measured and `kMarginPercent`, 10, more for each running pair and the new one, stays within the limit; P-072's warm-up stays.
  - **D-052**: no change; there was no default limit.
  - `pool_check.py --frames 0` scores every frame (`cee3540`), for D-049's run over every frame.
  - The runs, with a `mirage_vmaf` built in a new clean worktree of `af17121`: M5 (`eval/phase2-survey/m5-d050/`), M5's gaps with it and with Phase 1's program (`m5-gaps-d050/`), the memory limit at 1 GiB on the local copy (`eval/ladder/advanced-local-300-mem1g-af17121/`), and D-049's run over every frame at the default size with a limit of 1 GiB, from Apple's CDN.
- **Alternatives**: for D-050, comparing the sequences' ends only when both inputs have jumped, which would leave the frames of one input's sequence past the other's jump paired or counted as before; for D-051, the margin on the new pair only.
- **Rationale**: D-050's "within a frame of the coarser input" is the grid's frame duration, the pairing's own unit for the coarser input; checking each frame as it comes stops the pairing at the first frame past the other input's jump. D-051 counts "each pair", running or new.
- **Result**
  - M5 (`m5-d050/record.txt`): `restart` and `jump` 60 pairs each and no failure, in two libvmaf contexts of 30, as with D-046; `continuous` 60 and `segments` 30, no failure.
  - M5's gaps (`m5-gaps-d050/record.txt`): where the inputs lack different segments, 240 pairs and no pair of frames at different times, 1 extra distorted frame and 957 frames after a discontinuity; where both lack the same segments, 719 pairs and no failure; where the reference alone lacks them, 240 pairs, 1 extra distorted frame and 1317 after a discontinuity.
  - The memory limit at 1 GiB (`advanced-local-300-mem1g-af17121/`): PASS; at the default size 3 pairs at once and a peak of 825720832 bytes.
  - D-049's run over every frame (`eval/ladder/advanced-cdn-full-af17121/`): FAIL. All 7 pairs complete, every frame, 36000 or 18000 frame pairs each and none failing to pair, in 1907.7 s; 3 pairs ran at once, and the process peaked at 1096171520 bytes, 2.1% over the limit of 1073741824. The three pairs of the first wave, scored together for about 695 s, used more over their full length than the rule counted from what a pair used after 30 frame pairs, plus 10%. One pair alone over every frame from Apple's CDN, its resident memory sampled every 15 s, was at 322256 KiB 45 s into its run and 336096 KiB 405 s in, and peaked at 345407488 bytes (`eval/ladder/advanced-cdn-pair-full/record.txt`).
- **Who**: agent (Claude)
- **Date**: 2026-10-27
- **Status**: pending owner confirmation
