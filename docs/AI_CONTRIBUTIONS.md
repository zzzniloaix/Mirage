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

Phase 0 doesn't change any existing code, so in this phase the AI only produced documents, scripts and measurements.
