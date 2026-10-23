# AI Contributions

Purpose: material for the AI-disclosure paragraph in the paper's methods section (see hard rule 6 of the VMAF work order). ACM's policy, as revised in 2026, requires disclosing AI involvement in the research process, including coding. AI-assisted writing no longer needs to be disclosed.

Only verifiable facts are recorded here. The extent of AI involvement and the owner's design and review work are added or confirmed by the owner. AI-related notes aren't scattered through code comments; they are all in this file.

---

## AI attribution in the repository history (from git, collected 2026-10-21)

**SHA mapping**: this section and the next cite SHAs from before the 2026-10-23 history rewrite (see `docs/DECISIONS.md` D-012). The rewrite only changed commit emails. Each pair below has the same tree, commit message and author time (each pair was checked).

| Before rewrite | After rewrite |
|---|---|
| `4bc5ffc` | `c6b4cfd` |
| `521465d` | `6bc12fe` |
| `bded21a` | `3dfba86` |
| `92e0c4c` | `3c06a8a` |
| `62f3210` | `a5e2c03` |
| `79407e4` | `5593f54` |
| `ce4157b` | `9c62928` |
| `9b67621` | `e739c80` |
| `eea030f` | `05ac796` |
| `85e59a3` | `d44f56f` |
| `cb29189` | `95eaf86` |
| `a4e0289` | `1bab7a3` |

How this was collected: ran `git log` on branch `fix/high-speed-playback` (HEAD `a4e0289`) and read the `Co-Authored-By` trailers.

- 25 commits are reachable from HEAD, and 18 of them carry a `Co-Authored-By: Claude …` trailer. The model attributions that appear are:
  - Claude Sonnet 4.6
  - Claude Opus 4.7
  - Claude Opus 4.7 (1M context)
  - Claude Opus 5.5 (1M context)
- The 7 commits without a trailer: `4bc5ffc`, `521465d`, `bded21a`, `92e0c4c`, `62f3210`, `79407e4`, `ce4157b`.

**Note:** a missing trailer doesn't prove there was no AI involvement; the trailer is only a convention. The owner confirms the actual involvement in each commit.

## VMAF code

- **`src/VMAFAnalyzer.cpp` / `src/VMAFAnalyzer.h`** (the scoring core)
  - Only one commit: `62f3210` (2026-04-22, "Add VMAF implementation"), with no AI trailer.
  - **Pending owner confirmation**: whether AI was involved in this commit, and to what extent.
  - Phase 1 changes them in `e3a445f`, `0444859`, `6a9aca2` and `44cd4b5`, each with a Claude trailer (the Phase 1 rows below).
- **Later VMAF wiring and UI changes**
  - Commits found with `git log -G vmaf`:
    - without a trailer: `79407e4`
    - with a Claude trailer: `9b67621`, `eea030f`, `85e59a3`, `cb29189`, `a4e0289`
  - These commits change the UI, panels and session structure, not the scoring logic. The commit-by-commit review is still to be done.

## VMAF work order (this round)

| Date | Phase | What the AI did | What the owner did |
|---|---|---|---|
| 2026-10-21 | Phase 0 | Claude Opus 5.5 (via Claude Code):<br>• surveyed the existing VMAF code<br>• collected environment and dependency information<br>• built HEAD in a clean worktree and ran the baseline<br>• ran a diagnostic comparison against FFmpeg libvmaf on the first 300 frames of two rungs, and checked the pairing with ffprobe and extracted frames<br>• wrote the scripts under `eval/baseline/`<br>• drafted `docs/VMAF_GAP_REPORT.md`, this file and the process entries in `docs/DECISIONS.md` | Provided the work order. On 2026-10-22–23, reviewed the gap report, confirmed P-001–P-005 and signed off Phase 0 (D-015) |
| 2026-10-22–23 | Phase 0 wrap-up | Claude Opus 5.5 (via Claude Code):<br>• turned the open items in the gap report into options for the owner to decide one by one<br>• drafted entries D-001–D-015 from the owner's decisions<br>• rewrote the repository history and redacted the Phase 0 raw outputs per D-012–D-014, and checked the trees and checksums<br>• changed one comment line in `src/Demuxer.cpp` on `main` to remove a service name (`b878ee1`; not on the Phase 0 branch; no behavior change)<br>• corrected two of its own mistakes: the phase numbering in the first draft of D-001 (the wording of its question), and the hosting location in item 9 of the gap report<br>• translated the Phase 0 documents and the PR description from Chinese into English at the owner's request | Made all of the decisions D-001–D-014 (D-003 and D-004 are deferrals), decided the scope of the history rewrite and the redaction, signed off Phase 0, and asked for all repository text to be in English |
| 2026-10-23 | Phase 1, before the workflow below | Claude Opus 5.5 (via Claude Code):<br>• recorded the owner's decisions D-016–D-019 in `docs/DECISIONS.md` (`37472fd`) | Decided D-016–D-019: convert the distorted frames' color matrix and range as FFmpeg does, a separate `mirage_vmaf` tool, the inputs of the committed §5 report, and its threshold |
| 2026-10-23 | Phase 1, workflow stage 1 (`e3a445f`–`83c8ed6`) | Claude Opus 5.5 (via Claude Code), in an unattended workflow of six stages:<br>• extended `VMAFAnalyzer` (frame limit, each pair's PTS, the inputs' stream parameters, how each input ended, a synchronous `analyze()`)<br>• wrote `mirage_vmaf` (D-017): `tools/mirage_vmaf.cpp`, `src/VMAFCli`, `src/VMAFReport`, `src/JsonText`, with Catch2 tests<br>• wrote the build check (`eval/build_check/`) and `docs/BUILD.md`<br>• recorded P-006 and P-007 | Asked for the workflow; made no decision during it |
| 2026-10-23 | Phase 1, stage 2 (`b330f20`–`e6a2aee`) | Claude Opus 5.5 (via Claude Code), unattended:<br>• wrote the §5 delta tool (`eval/consistency/run_delta.sh`, `run_delta.py`, `vmafdelta.py`) with Python tests<br>• ran and committed the diagnosis before D-016 (`eval/consistency/diag-before-300/`)<br>• recorded P-008 to P-012 | — |
| 2026-10-23 | Phase 1, stage 3 (`5e412a6`–`e015638`) | Claude Opus 5.5 (via Claude Code), unattended:<br>• wrote `eval/consistency/color_cases.py`, ran the color tag cases through both tools, and read the FFmpeg 8.1.1 sources to explain FFmpeg's logs<br>• implemented D-016: `src/VMAFColor`, the analyzer's conversion of the distorted frames and the report's `dis_conversion`, with tests that compare its frames with FFmpeg's byte for byte<br>• ran and committed the checks after D-016 (`diag-after-300/`, `color-cases-before/`, `color-cases-after/`)<br>• recorded P-013 to P-015 | — |
| 2026-10-23 | Phase 1, stage 4 (`6a9aca2`–`9062d75`) | Claude Opus 5.5 (via Claude Code), unattended:<br>• added `--scaler`, `--model`, `--model-file` (`src/FileHash`) and `reference_mode` to `mirage_vmaf`, with tests, keeping every default<br>• extended the delta tool (`--model-file`, both libvmaf versions of both sides) and wrote `compare_runs.py`<br>• ran and committed the scaler and model runs (`scalers-300/`, `models-300/`) and two records of FFmpeg's defaults<br>• wrote `eval/pooling/` and committed its measurement<br>• wrote the frame-alignment plan (`docs/plans/frame-alignment.md`; nothing of it implemented, D-007) and the decision packages of `docs/PHASE1_DECISIONS_PENDING.md`<br>• recorded P-016 to P-019 | — |
| 2026-10-23 | Phase 1, stage 5 (`1d4a261`–`3650f83`) | Claude Opus 5.5 (via Claude Code), unattended:<br>• added `LICENSE` (D-008) and the ffmpeg-full install line of `README.md` and `CLAUDE.md` (D-009)<br>• pinned miniaudio and stb, extended the build check and committed its run at `6a6661f`<br>• wrote `eval/streams/fetch_streams.py` and `eval/streams.md`, and committed a fetch<br>• wrote `eval/run_all.sh` and committed its verification run<br>• wrote the #11 packages and the §8 differences of `docs/PHASE1_DECISIONS_PENDING.md`<br>• recorded P-020 to P-025 | — |
| 2026-10-23 | Phase 1, stage 6 (`17a2a28` and later) | Claude Opus 5.5 (via Claude Code), unattended:<br>• ran the full-length §5 run of D-018 and committed it with its checks (`eval/consistency/acceptance-full/`), and the pooling measurement over every frame (`eval/pooling/acceptance-full/`)<br>• added the full-length steps to `eval/run_all.sh`, with tests<br>• committed a record of the unit test runs (`eval/test_runs/`)<br>• wrote `docs/PHASE1_STATUS.md`, added the open items of every stage to `docs/PHASE1_DECISIONS_PENDING.md`, updated the READMEs and wrote these rows<br>• recorded P-026 to P-028<br>• drafted the Phase 1 PR description, without opening the PR | — |

Phase 0 doesn't change any existing code, so in this phase the AI only produced documents, scripts and measurements.

Phase 1, from `git log main..vmaf/phase-1` and the workflow's stage reports (2026-10-23):

- Every Phase 1 commit carries the trailer `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`, and all of them are dated 2026-10-23. Every commit after `37472fd` was made in one unattended workflow of six stages, which the owner asked for; the owner made no decision during it.
- Phase 1 changes code. The AI wrote the new files `src/VMAFColor.{h,cpp}`, `src/VMAFCli.{h,cpp}`, `src/VMAFReport.{h,cpp}`, `src/JsonText.{h,cpp}`, `src/FileHash.{h,cpp}` and `tools/mirage_vmaf.cpp`; the tests `tests/test_vmaf_analyzer.cpp`, `test_vmaf_cli.cpp`, `test_vmaf_color.cpp`, `test_vmaf_report.cpp`, `test_json_text.cpp` and `test_file_hash.cpp`; and the scripts and tests under `eval/` outside `eval/baseline/`. It changed `src/VMAFAnalyzer.{h,cpp}` and `CMakeLists.txt`; no other file under `src/` changed.
- Design decisions: D-016 to D-019 are the owner's. Every default stayed as it was (scaler, model, bit depth, pairing, pooling). Where a choice was the owner's, the AI implemented an option only where the work order requires one, measured, and wrote a decision package (`docs/PHASE1_DECISIONS_PENDING.md`). Its own measurement and process choices are P-006 to P-028 in `docs/DECISIONS.md`.
- One statement the AI made without checking it: the commit message of `1d4a261` says that the `LICENSE` text is the revision "the FSF publishes today", which was not checked against gnu.org; P-020 doesn't rely on it.
- **Pending owner confirmation**: the owner's review of the Phase 1 commits, P-006 to P-028, and these rows.
