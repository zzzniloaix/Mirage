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
  - **Where FFmpeg's negotiation and D-022's wording part.** D-022 leaves the distorted frames' color as it is. FFmpeg 8.1.1's graph does so whenever the distorted input's buffer source offers one matrix and one range, as in every committed case. For a distorted input without a range tag against a reference that has one, or is yuvj, both inputs get the reference's range, so a full range reference makes the distorted frames full range; for a distorted input in a format that isn't plain YUV, such as gray or RGB, both get the reference's matrix and range. The analyzer follows FFmpeg there too, as D-022's rationale, parity with plain FFmpeg, has it (the negotiation test in `tests/test_vmaf_color.cpp`). Listed for the owner's confirmation in `docs/PHASE1_DECISIONS_PENDING.md`.
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
- **Status**: pending owner confirmation

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
- **Status**: pending owner confirmation

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
- **Status**: pending owner confirmation

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
- **Status**: pending owner confirmation

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
- **Status**: pending owner confirmation
