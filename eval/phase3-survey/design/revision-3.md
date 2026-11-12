# Revision 3 of the merged Phase 3 design (2026-11-02)

What changed in `design.md` against the third review (`review-3/review.json`, `review-3/review.md`), whose verdict was that the design is ready for round 1's questions once seven minor issues are corrected, with no blocker and no major issue; the two items it marked partly (the second review's N7 and the lead's L8) are among those seven. This revision was made by the lead agent, not by a reviser of the team, and it was not reviewed again: every edit is in `design/tools/revise3.py`, which applies them to revision 2's sources and stops if a text it replaces isn't there exactly once, and `design/runs/revise3-reproduce.txt` shows that revision 2 as committed in `7c0e8d2` with that script applied renders byte-identical to this revision. Nothing decided changed, no recommendation changed, and no question was added, removed or renumbered; every recommendation stays labelled as the agent's. Nothing of Phase 3 is implemented: the code is as at `75cf6e0`, the one commit since, `7c0e8d2`, adding the survey's records under `eval/phase3-survey/` only (`design/runs/repo-state.txt`).

**What was edited.** The sources only: `design/src/part1.md`, `design/src/part2.md`, `design/owner_questions.json`, `design/work_packages.json`, `design/p_entries.json`, `design/risks.json` and three lines of `design/tools/render.py`; `design.md` is their render (`design/runs/render-check.txt`). New: `design/tools/revise3.py`, this file, and the records below.

**Counts after the revision.** 44 owner questions in rounds of 27, 9 and 8, unchanged, eight of them with the D-007 mark, unchanged. 25 work packages: WP15b split into WP15b and WP15c (issue 7). 28 P-entries, P-C's text changed (issue 6). 22 risks, R11's text extended (issue 1). 14 differences from the work order; item 14 now names its record.

## New runs

Made with `design/tools/run.sh` as before (`OUT`, `REPO` and `SCRATCH` set; local paths written `<out>`, `<repo>` and `<scratch>`).

| Record | Working directory and command | What it shows |
|---|---|---|
| `design/runs/revise3-reproduce.txt` | `<scratch>/phase3/repro3b`, an empty directory: revision 2's `design.md` and `design/` taken from `7c0e8d2` with `git archive`, `revise3.py` applied to them, `render.py` run, the result compared with `design.md` by `cmp` and with `design/` by `diff -r` (the records, the new tool and this log aside) | Revision 2 with the script applied renders byte-identical to this revision, and its sources, data and tools are this revision's. Exit status 0. |
| `design/runs/repo-state.txt` | `<repo>`: the last two commits, `git status --short --branch`, and the count of files that differ from `75cf6e0` to `HEAD` outside `eval/phase3-survey/` | `vmaf/phase-3` at `7c0e8d2`, whose parent is `75cf6e0`; nothing changed in the worktree; 0 files outside the records. Exit status 0. |
| `design/runs/render-check.txt` | `<out>`: `render.py` writing into a pipe that `cmp` compares with `design.md` | `design.md` is byte-identical to the render of its sources, and the renderer's checks passed: 44 questions, 25 packages, 28 P-entries (9 moved), 22 risks. Exit status 0. |
| `design/runs/question-lengths.txt` | `<out>`: the second review's `review-2/tools/question_lengths.py` on `design/owner_questions.json` | 44 questions, 185 options, 0 counts outside L4's ranges. Exit status 0. |
| `design/runs/package-check.txt` | `<out>`: the second review's `review-2/tools/package_check.py` on `design/work_packages.json` | 25 packages, 110 files named, 0 pairs of owners not ordered by the dependencies; every pair the third review's issues 2, 6 and 7 concern (`src/SessionPlan.h`, `src/Scte35*`, the round-2 test files, `docs/DECISIONS.md`, `docs/PHASE3_DECISIONS_PENDING.md`) listed in turn. Exit status 0. |
| `design/runs/privacy-check.txt` | `<out>`: the second review's `review-2/tools/privacy_check.py` on `design.md` and every file under `design/` but itself and `quote-check.txt` | 0 local paths, 0 e-mail addresses, 0 identity words. Exit status 0. |
| `design/runs/quote-check.txt` | `<out>`: the quote checker on `design.md` and every file under `design/` but itself, against the work order's translation kept outside the repository | 0 prose runs and 0 runs of names in every file, K = 6. Exit status 0. |

The other records of `design/runs/` are as revision 2 left them.

## The third review's issues

### Issue 1: a frame missing at the start of a playlist discontinuity sequence

**Fixed as suggested.** The review removed the first frame of each of the ad ladder's three playlist discontinuity sequences in turn (`review-3/runs/seconds-check-sequence-start.txt`): the PTS test (2) flagged nothing, since it starts each sequence's grid at the sequence's first scored frame, and (1), (d) against (c), flagged only from the next segment on, at −0.040 s; within the segment that lost the frame neither warned, (c) too starting that segment at its first remaining frame. §4.2 now says so where it explains what a run of (1) without (2) means, and where it lists what the check did; §0 item 3, the recommendation's paragraph, Q02 (c) and its Why, and R11 state the limit; WP01's test of the check and §9.1's row for `test_timeline_axis.cpp` add the case in the shapes of that record. The recommendation stands, as the review found: (c) follows the frames again from the next segment, while (d) stays a frame early.

### Issue 2: WP00b's session types in headers it didn't list

**Fixed by the first of the review's two suggestions.** `RequestRow` and `Request` move from `src/TimelineBuild.h`, a file of WP09a, to `src/SessionPlan.h`, next to `PlanSpan` and `SessionPlan` (Appendix B). WP00b's description now names the header of each declaration (`src/TimelineModel.h` for `Span`, `Session`, `Timeline::sessions` and `CurveRef`'s session kind; `src/SessionPlan.h` for the plan's and the fetch's types), and `src/SessionPlan.h` is among its changed files, so `render.py` checks its owners: WP00a creates it, WP00b adds the declarations, WP07 owns its contents, each after the one before (`design/runs/package-check.txt`). `BuildInputs` keeps its round-2 members, marked as WP09b's. The other suggestion, WP00b depending on WP09a, would have held round 2's wiring until round 1's builder had merged.

### Issue 3: answers that change packages whose Blocked-by lists didn't name them (N7)

**Fixed as suggested.** Added: Q43 to WP00a, Q44 to WP00b, Q20 to WP09a, Q01 and Q05 to WP09b, Q23 to WP11a, Q23 and Q25 to WP11b and to WP14, and Q05 to WP12. `render.py` then derives each question's Blocks from the lists, so Q01, Q05, Q20, Q23, Q25, Q43 and Q44 show the packages the review named. No question changes round: each is of the package's round or earlier, and every question that blocks a package on the acceptance path is still of round 1 (`render.py` asserts both).

### Issue 4: Q16's count

**Fixed.** Q16 now says that 1 of the 9 messages with a bad CRC_32 is a library's intended negative test, citing `scte35_evidence/runs/check_vectors.txt` and `review-3/runs/vector-status.txt` (which also has 3 among the 9 that don't decode), and that the scte35 survey's summary gives 3 in error. The committed copy of the survey marks that error where it stands, as a dated erratum (the records' `README.md`, item 5 of its changes). The recommendation didn't rest on the number.

### Issue 5: RFC 8216's rule on whole-second EXTINF

**Fixed as suggested.** §4.2's third bullet now says that RFC 8216 §4.3.2.1 requires whole seconds below compatibility version 3 and allows them at every version, and its conclusion drops "of an older version".

### Issue 6: WP00a's list of new files and P-C

**Fixed as suggested.** WP00a's New files now say what its description says: the empty files whose existence the owner's decisions or round 1's answers settle, round 1's and the round-2 files that D-068 and D-070 settle (`src/SessionPlan.{h,cpp}`, `src/Scte35.{h,cpp}`, `src/Scte35Events.{h,cpp}`, `tests/test_session_plan.cpp`, `tests/test_scte35.cpp`, `tests/test_scte35_events.cpp`, `tests/test_attribution_session.cpp`), named, so that `render.py` checks their owners in turn, contents then owned by WP01 to WP11b. P-C now says that each wiring package adds the files whose existence that round's answers or the owner's decisions settle.

### Issue 7: WP15b's documents last

**Fixed by the review's first suggestion.** WP15b keeps what waits only for Q41 and Q42: §11's differences in the wording those answers settle, in the pending document's list, and under Q42 (b) the dated correction of `docs/VMAF_GAP_REPORT.md`. The new WP15c holds what comes last: the Phase 3 rows of `docs/AI_CONTRIBUTIONS.md`, `docs/PAPER_METHOD_NOTES.md`, dated lines in `docs/DECISIONS.md`'s pending entries, `docs/PHASE3_STATUS.md` completed item by item, and Phase 3's PR description with its acceptance outputs. It depends on WP09c, WP13, WP14 and WP15b, and through them on every other package but WP16 and WP17, which the lead adds to its dependencies when the owner's answers start them; no question blocks it. §3.2's table and shared files, §11's closing paragraph and the order of §14 say so, and `render.py`'s list of packages that can start now is now the packages of round 0, since WP15c, which no question blocks, waits for others.

## Other edits of this revision

- **P-entry numbers.** The Conventions, §13 and WP15a now number the design's P-entries from P-082 when recorded, since P-081 records how this survey and design were made and what of their records is committed.
- **§11 item 14** names the record of its measurement: ui.md §2.4, from `experiments/hls-seek-probe/run-output.txt`, which the check of the documents' drafts confirmed (`review-docs/verify.md`).
- **Appendix A** lists the third review's runs that the design now cites, `seconds-check-sequence-start` and `vector-status`.
- **Q02's Why** says "otherwise" where it says where (c) is off, since issue 1 adds a case.
- **Where Phase 3 stands** is dated 2026-11-02 and lists the third review and this revision, and its "Not done" now says how the repository differs from `75cf6e0`; the opening paragraph and §1.1 name the three revisions.
