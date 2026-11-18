# Second adversarial review of the revised Phase 3 design (2026-11-01)

A review by an agent that wrote neither the merged design nor its revision. It read `<out>/design.md`, its revision log `<out>/design/revision-1.md`, the first review `<out>/review-adversarial/review.json` (its verdict and issues 0 to 17), the lead's items L1 to L8, the design's sources and data under `<out>/design/`, and what they cite: `<repo>` (`vmaf/phase-3` at `75cf6e0`), the five surveys, the two designs' runs and the first review's runs. Nothing was written outside `<out>/review-2/`; `design.md`, `design/` and `<repo>` were not changed. Nothing of Phase 3 is implemented. Line numbers below are `design.md`'s unless a file is named. Every recommendation here is the reviewer's; the choices stay the owner's.

## Verdict

**Revise once more before round 1's questions go out; no blocker.** The revision answers the first review well: of its 18 issues, 16 are resolved and 2 partly (issues 0 and 6), and of the lead's items, L1 to L7 are resolved and L8 partly. What was checked and holds: `design.md` is byte-identical to the render of a copy of `design/`, render.py's consistency checks pass and the copy's JSON comes out unchanged; the quote checker finds 0 prose runs and 0 runs of names in `design.md` and in each of the 16 files under `design/`; none of them has a user or host name or an e-mail address, and the only absolute path is a generic one in a comment of `design/tools/run.sh` (N15); every question has its round, its D-007 mark and a one-sentence summary, two to four sentences of question and one to three per option; each question's Blocks equals the packages whose Blocked-by names it; the acceptance path is WP00a, WP01, WP02, WP03, WP04, WP06, WP08, WP09a, WP10 and WP11a, with round-1 questions only; the code claims match their `file:line`s, except one citation; the numbers trace to their records, except the claim about rate changes below; D-002, D-007, D-046, D-050, D-068, D-069 and D-070 are respected; `<repo>` is at `75cf6e0` and clean.

Two major problems remain. (1) The revised check of Q02 (d)'s seconds compares each frame step with one frame duration within half a frame, so it does not catch a change of frame rate, although §4.2, Q02 (d), R11 and WP01's test say it does: with testgen's ad re-timed at 24, 30 and 50 fps it flags nothing while (d)'s seconds end 0.083, 0.333 and 1.000 s away from (c)'s (`runs/rate-change-check.txt`), so the comparison of (c) and (d) that Q02 asks of the owner rests on a safeguard that covers missing frames only. (2) WP01, in round 1, is to implement and test attribution on a session's curve, whose `Span` and `Session` types WP00b declares in round 2 after WP01 has merged, and no later package owns that code. Thirteen minor issues follow (§2, N3 to N15). The records of this review are in `<out>/review-2/runs/`, made with `<out>/review-2/tools/run.sh`, a copy of `design/tools/run.sh` that differs from it in one comment, reworded for N15.

## Records of this review

Each record holds its working directory, exact command, start and end (UTC), full output and exit status, with local paths written `<out>`, `<repo>` and `<scratch>`.

| Record (`runs/`) | What it shows |
|---|---|
| `render.txt`, `render-cmp.txt`, `render-json-unchanged.txt` | `design/tools/render.py` on a copy of `design/`: its checks pass (44 questions in rounds of 27, 9 and 8, 22 packages, 28 P-entries with 9 moved, 22 risks); `cmp` of its output with `design.md`: exit 0, byte-identical; `diff -r` of the copy with `design/` after the render: no difference, so the JSON files are a fixed point of the rewrite. The copy and the render were deleted after the comparison; the recorded commands make them again. |
| `quote-check.txt` | the quote checker on `design.md` and the 16 files under `design/`, against the work order's translation kept outside the repository: 0 prose runs, 0 runs of names, K = 6 |
| `question-lengths.txt` | `tools/question_lengths.py` on `design/owner_questions.json`: 44 questions, 180 options, 0 counts outside L4's ranges (question 2 to 4 sentences, option 1 to 3, summary 1) |
| `package-check.txt` | `tools/package_check.py` on `design/work_packages.json`: every file two packages name is owned in turn, except `CMakeLists.txt` between WP00b and WP00c |
| `rate-change-check.txt` | `tools/rate_change_check.py`: the design's own check (`design/tools/axis_check.py`) on testgen's ad report with the ad's reference frames 75 to 124 re-timed at 25 (unchanged), 24, 30, 50 and 60 fps, a simulation on copies in memory; and an alternative check |
| `code-lines.txt` | `tools/show_lines.py`: the `<repo>` lines this review relies on, with their numbers |
| `player-keys.txt` | the letter keys `src/main.cpp` binds (`GLFW_KEY_A` to `GLFW_KEY_Z`): A, C, D, F, G, H, I, K, M, N, O, Q, T, V, W, which leaves Q38's eleven |
| `ffmpeg-citations.txt` | `tools/line_has.py`: the lines of FFmpeg n8.1.1's `hls.c` (989, 990, 1916, 1944) and `mpegts.c` (463) that the design cites by URL hold what it says, in the scte35 survey's copies, whose SHA-256s are those of its fetch records; the lines themselves are not printed |
| `catch2-citation.txt` | Catch2's checkout in `<scratch>` is `v3.7.1`, and its `extras/Catch.cmake` lines 28 to 34 list `TEST_SPEC` and `PROPERTIES` (P-Q's URL) |
| `privacy-check.txt` | `tools/privacy_check.py` on `design.md`, the files under `design/` and this review's files: counts only, no values printed |
| `quote-check-review.txt` | the quote checker on this review's own files: 0 prose runs |
| `repo-state.txt`, `repo-status.txt` | `<repo>` at `75cf6e0`, status clean (read-only git, without optional locks) |

## 1. The first review's issues and the lead's items

| Id | Status | Evidence |
|---|---|---|
| issue 0 | partly | The false alarm is gone: with the segment map the revised check flags none of the four ladders, and without one it lists only the +3.040 s jump at frame 125 (`design/runs/axis-check.txt`, reproduced at 25 fps in `runs/rate-change-check.txt`); Q02 (d) says the check needs the segment map (line 745); WP01 tests the D-069 shape with and without a map (line 1696). Not resolved: the issue asked for a safeguard that covers (d)'s failures, and the revised check misses one of the two it names. §4.2 (lines 244, 246), Q02 (d), R11 (line 1972) and WP01's test say it flags a change of rate; at 24, 30 and 50 fps it flags 0 steps while (d)'s seconds end 0.083, 0.333 and 1.000 s off (c)'s (`runs/rate-change-check.txt`). N1. |
| issue 1 | resolved | WP09 and WP11 are split (lines 1657, 1659, 1663, 1664); WP11a depends on WP09a and WP10 only; the acceptance path (line 1954) waits for neither WP05 nor WP07, nor for a question outside round 1. `design/tools/render.py` now asserts both, and the render passed (`runs/render.txt`). |
| issue 2 | resolved | The acceptance cases are sections of one Catch2 test case (lines 593, 1788), each ladder made once per process (P-Q, line 1613), the generations counted per ctest run, seven under Q34 (d) (line 608; R9, line 1970), each test case under its own `MIRAGE_KEEP_MEDIA` subdirectory (P-U, line 1617). The premise checks against `tests/test_vmaf_ladder.cpp:192-196`, `tests/TestTemp.h:3-7` and `CMakeLists.txt:371` (`runs/code-lines.txt`); the CTest-fixture alternative is kept with a reason P-036 supports (one temporary directory per process). |
| issue 3 | resolved | The seven P-entries named, and P-E, P-X and P-AC, moved: Q43 (eight items, round 1, lines 1199-1248), Q44 (five items, round 2, lines 1403-1437), Q04 (a) for P-AC (line 779); §13 lists 28 kept and 9 moved with their letters retired (lines 1597-1640). Of the kept ones, only P-N's fetch pattern still decides what an output says (N8). |
| issue 4 | resolved | `CueInput {carrier, anchor, form, bytes}` with four carriers and three anchor kinds (line 347; Appendix B, lines 2114-2125), declared by WP00a (line 1675), placed by `Scte35Events` for every kind, period-time anchors to the warnings (lines 312, 347); WP05 tests a PTS-anchored section (line 1822); no reader is built (R19, line 1980). |
| issue 5 | resolved | §6.3 rule 4 gives Q22 (c)'s three rules with their alternatives (line 457); Q23 (a) and (b) state the blind start (lines 962-963) and R21 records it; Q22 (b) cites the T sweep (line 944), matching `review-adversarial/runs/switch-t-sweep.txt`; WP02 and WP07 test the cases (lines 1709, 1835). The baseline is constant through a drop by the prototype's code (`design/tools/detection_semantics.py:40-50`: a low point never joins the pool). |
| issue 6 | partly | The API takes a `CurveRef` for a pair or a session (lines 413-441; Appendix B, lines 2061, 2076, 2193), and Q28 (a) and §6.4 are corrected: run 10's attribution of the v180 pair lists v360's cue_out, cue_out_cont, ad_break, two segments, discontinuity and two keyframes (line 484; `design-evaluation/runs/10_attribute_final.txt`). But the session half has no feasible owner: WP01, in round 1, implements and tests it (lines 1687, 1697, 578) while `Span` and `Session` come from WP00b in round 2, after WP01 (lines 2062-2064, 1800, 1805). N2. |
| issue 7 | resolved | (a) the grid is stated (lines 101, 461) and matches design-evaluation run 05; (b) and (c) are limited to what ran, the rest MEASUREMENT PENDING (lines 68, 106, 1154, 1502, 1978); (d) frame 0 added (line 111; testgen.md §3.2); (e) Tables 5 to 17, 20 to 25 and 29 (line 348), the tables scte35.md §3 cites; (f) `tests/test_vmaf_report.cpp:104` (P-Y) and (g) the grep record (line 92; `design/runs/log-callback-grep.txt`), both checked (`runs/code-lines.txt`). |
| issue 8 | resolved | §11 items 3, 5 and 6 (lines 633, 635, 636); Q04 (a), Q03 (b), Q10 and Q13 (a) name them (lines 779, 762, 1258, 1467); Q10 (e) offers the scoring-read recorder with its D-007 and D-068 caveat (line 1264). No package would implement (e) if chosen (N5). |
| issue 9 | resolved | A media-PTS anchor is placed on the carrying variant's own frames, within the carrying playlist's discontinuity sequence, then mapped through the pair (lines 306, 312); WP05 tests a distorted variant whose PTS origin differs (line 1822). |
| issue 10 | resolved | CUE-OUT-CONT's `SCTE35=` becomes a `CueInput` (line 346; WP04's test, line 1735); `pair_breaks()` has one owner, WP04, and WP05 depends on WP04 and returns its cues to it (lines 349, 1727, 1812, 1816); Q17 blocks WP04 only (line 879). The reason for not giving the pairing to WP05 holds: acceptance test 2 asserts an ad_break (line 596). |
| issue 11 | resolved | (i) `SessionLog` in §3.2 (line 162), wired by the lead when WP17 starts (P-C, line 1603); (ii) copying chosen as P-AJ, pending the owner (line 1627); (iii) Q09 (b)'s pass moved to WP16 (line 1922); (iv) WP09b blocked by Q15 and Q16, WP09a by Q29 (lines 1845, 1770); (v) WP08 blocked by Q02, Q22 and Q25, and Q12 blocking WP09b (lines 1758, 1308); render.py keeps Blocks equal to Blocked-by. Gaps that remain are N4 and N7. |
| issue 12 | resolved | (i) a redirected fetch is one event with its chain (Q44 (d), lines 1424-1427; R22), as `src/NetworkLogger.cpp:595-612` and `:687` row it; (ii) rows copied and the logger cleared after each file (P-N, line 1611; `src/NetworkLogger.h:155-160`, `:194`); (iii) retries are Q44 (c) (lines 1419-1423); (iv) the subcommands set the log level and the network themselves (lines 180, 507, 1764), as `tools/mirage_vmaf.cpp:142-156` shows they must (`runs/code-lines.txt`). |
| issue 13 | resolved | §3.6 tabulates every answer that would change an otherwise unchanged file (lines 197-209); WP00b rewords `CMakeLists.txt:229-232` and `src/VMAFNet.h:8-9` (line 1800), both read in `runs/code-lines.txt`. |
| issue 14 | resolved | Q26 (b) asks when `timeline` detects, the agent recommending only with W and N (lines 1022-1026; §7, line 510); `attribute`'s default `--format` is text (lines 509, 1115); Q39 (b) asks the click's interval (lines 1545-1549). |
| issue 15 | resolved | (i) `TimelineLoad`, ImGui-free, holds the loader, its hash checks and the handoff, tested in `mirage_tests` (lines 616, 589, 1874, 1881); (ii) Q43 (h) asks where a corrupt packet sits (lines 1238-1242), on facts from `testgen/logs/61_ad_x264_nodisc_probe_v180.txt:149, 156, 249, 252` (packets 73 and 123 carry the PTS of frames 74 and 122, checked), and WP06 tests it (line 1748). |
| issue 16 | resolved | Every file of the design has 0 prose runs (`runs/quote-check.txt`); WP15's documents must be in their own words and pass the check before each commit (lines 1910, 1918; P-AK, line 1628). The PR description is not under that check (N10). |
| issue 17 | resolved | WP00a is blocked by Q01 to Q04, Q07, Q08, Q31 and Q32 and runs once all of round 1's answers are in (lines 1650, 1675, 1946); the session declarations moved to WP00b and the UI's wiring to WP00c; no package starts before an answer, WP15's pending-decisions document aside (line 20). WP00a's headers still encode some answers whose questions don't block it (N7). |
| L1 | resolved | `runs/quote-check.txt`: 0 prose runs and 0 runs of names, K = 6, in `design.md` and the 16 files under `design/` (sources, data, revision log, tools, records); `design/runs/quote-check.txt` was made through `design/tools/run.sh`. |
| L2 | resolved | Every question has `round` and `d007` (render.py asserts both); §12 groups its tables and its full texts by round with 27, 9 and 8 (lines 652-711), as do "Where Phase 3 stands", the Conventions and §0 (lines 20, 10, 32); Q01 to Q42 keep their numbers and Q43, Q44 are new; `d007` is true for Q01, Q08, Q09, Q10, Q13, Q36 and Q37, each text naming the ruling or the approval. Q41 is in round 2, not 3, because WP00b and WP07 need its answer; the revision log says so (`design/revision-1.md:121`), and it follows the rule that a question's round is the earliest round of the packages it blocks. |
| L3 | resolved | All 44 questions have a one-sentence summary (`runs/question-lengths.txt`), rendered under each heading; compared with each recommendation and each item's, they state them exactly. |
| L4 | resolved | Every question has 2 to 4 sentences and every option 1 to 3 (`runs/question-lengths.txt`: 0 outside the ranges); the detail moved to the sections each question names (§4.2, §5.1 to §5.7, §6.3, §7, §10). Whether anything was lost by shortening could not be checked against the earlier text, which `<out>` no longer holds; it rests on the revision log (`design/revision-1.md:129`), and every option the first review discussed is present. |
| L5 | resolved | The moved P-entries form two questions of items, one per round (Q43, Q44), each item with its options and the agent's recommendation (lines 1199-1248, 1403-1437); the 28 kept are process and measurement, except P-N's fetch pattern (N8). |
| L6 | resolved | No citation of generated media, binaries, FFmpeg copies, downloads, extracts, `rfc/`, `testgen/refs/`, `scte35_vectors.json` or `vectors_src.json` remains in `design.md` or `design/`; FFmpeg is cited by URL at n8.1.1 (`hls.c#L989-L991`, `#L1916-L1945` and `mpegts.c#L463` match the scte35 survey's copies, `runs/ffmpeg-citations.txt`; `http.c`'s lines are as sources.md §3.3 gives them, its copy having been removed), Catch2 by URL at v3.7.1 (`extras/Catch.cmake`, lines 29 to 34, lists `TEST_SPEC` and `PROPERTIES`, `runs/catch2-citation.txt`), and the records cited (`testgen/logs/`, `testgen/reports/`, `experiments/hls-seek-probe/`) are among those the public copy keeps (`<scratch>/phase3/records/phase3-survey/README.md`, its layout table). |
| L7 | resolved | 44 questions (27, 9, 8), 22 packages, 28 P-entries (9 moved), 22 risks: render.py refuses to render unless `part1.md` states these counts, and it rendered (`runs/render.txt`); §11 has the 14 items §0 counts (lines 32, 627-644); dates 2026-10-31 (line 16; `design/revision-1.md:1`, `:141`). |
| L8 | partly | The new numbers carry their records (checked: `design/runs/axis-check.txt`, `log-callback-grep.txt`, `switch-t-sweep.txt`, testgen's log 61, design-evaluation runs 05, 06, 09, 10 and 11), MEASUREMENT PENDING marks what wasn't run, and nothing is presented as decided. Two claims have no record or contradict theirs: that the axis check warns at a change of rate (lines 246, 577, 1696, 1972), which no run tried and `runs/rate-change-check.txt` contradicts at 24, 30 and 50 fps; and `src/HlsAlign.h:12`, cited for `HlsAlign.cpp` needing only FFmpeg, where the line says it uses none (line 174; `runs/code-lines.txt`). |

## 2. New issues (N1 to N15)

### N1 (major): the revised axis check doesn't catch a change of frame rate

**Where.** §4.2 (lines 244, 246, 248), §1.2 K2 (line 50), §2 (line 99), Q02 option (d) and its "Why" (lines 745, 749), R11 (line 1972), §9.1's `test_timeline_axis.cpp` (line 577), WP01's description and tests (lines 1687, 1696).

**Issue.** The check compares each step between consecutive reference frames with one frame duration, within half a frame. By its own rule, a reference whose frames come at any rate from two thirds to twice its declared rate passes every step, while (d)'s seconds, the frame index times the declared duration, drift. With testgen's ad report re-timed so that the ad's frames 75 to 124 come at 24, 30 or 50 fps (simulated, the playlist's EXTINF following the media), the design's own `axis_check.py` flags 0 steps with the segment map, and (d)'s seconds end 0.083, 0.333 and 1.000 s away from (c)'s after the ad; only 60 fps is flagged, at 49 steps (`runs/rate-change-check.txt`). RFC 8216 expects an `EXT-X-DISCONTINUITY` where encoding parameters change ([§4.3.2.3](https://www.rfc-editor.org/rfc/rfc8216#section-4.3.2.3)), so an ad at another rate after one is a stream the check must expect. §4.2, Q02 (d), R11 and WP01's test say the check warns at a change of rate; the test as specified passes only for changes outside those bounds. Q02 asks the owner to weigh (c) against a checked (d), and the agent's recommendation rests on the check covering both failures it names.

**Suggestion.** Bound the accumulated error, not the step. The check needs the segment map anyway, so compare (d)'s seconds with the playlist time at each segment's first frame, or each frame's PTS with the PTS expected from the first frame of its playlist discontinuity sequence, flagged beyond half a frame. In the same simulation the first form flags 4 segment starts at 24, 30, 50 and 60 fps and none on the unchanged ladder (`runs/rate-change-check.txt`). Then correct §4.2, Q02 (d) and its "Why", K2, R11 and WP01's test, and let the owner compare (c) and (d) on that basis.

### N2 (major): session attribution assigned to WP01, before its types exist

**Where.** WP01 (lines 1687, 1697), §9.1's `test_timeline_query.cpp` (line 578), §3.2's rows for `TimelineModel`, `TimelineQuery` and `SessionPlan` (lines 144, 146, 154), §6.1 (lines 434-441), Appendix B (lines 2062-2071, 2076), WP00b (lines 1800-1805), the files of WP07 and WP09b (lines 1829, 1842).

**Issue.** WP01, in round 1, implements `attribute()` for a `CurveRef` of either kind, with Q28 (a)'s session scope (the played variant's events in each span), and tests that scope; `TimelineModel.h`, a WP01 file, declares `curve_points()`, which derives a session's curve with its spans. But `Span` and `Session` are declared by WP00b in round 2, once Q11 and Q12 are answered, and WP00b depends on WP01, so WP01 has no session type to build or test against. No later package owns that code: WP07 changes only `tests/HttpFileServer.h`, WP09b's files include neither `TimelineQuery.*` nor `TimelineModel.cpp`, and §3.2 gives the session curve to `SessionPlan`. Building it in WP01 would put `Span`'s `scored` flag (Q12, round 2) on the acceptance path, against what render.py checks.

**Suggestion.** Give the session part of `attribute()` and of `curve_points()`, with their tests, to WP07 (or WP09b), listing `src/TimelineQuery.cpp`, `src/TimelineModel.cpp` and `tests/test_timeline_query.cpp` among its changed files after WP01 has merged; keep WP01's scope tests to pairs; say which file defines `curve_points()`.

### N3 (minor): the composite example waits for the SCTE-35 decoder

**Where.** WP09b (lines 1838-1847), WP11b (lines 1849-1858), the critical-path paragraph (line 1954), WP05's blockers (line 1817).

**Issue.** WP11b depends on WP09b, and WP09b on WP05 as well as WP07, so the paper's E2 waits for the decoder and for Q14, Q15, Q16, Q18 and Q21, the licence question, although D-069's ad ladder carries plain cue tags only (testgen.md §2.3). It is the coupling the first review's issue 1 removed from the acceptance path.

**Suggestion.** Split WP09b into a session part (after WP09a and WP07) and a decoded-message part (after WP09a and WP05), let WP11b depend on the first only, as WP09 was split, and recompute the composite path.

### N4 (minor): two orderings the files need are missing

**Where.** WP00b and WP00c (lines 1660, 1665, 1798-1870), WP12's and WP13's dependencies (lines 1878, 1892), the order's item 7 (line 1952), §3.2 (line 170), P-C.

**Issue.** WP00b (round 2) and WP00c (round 3) both change `CMakeLists.txt` and neither depends on the other (`runs/package-check.txt`, the only unordered pair of owners), so if round 3's answers come while WP00b is open, two wiring commits edit the file at once, against P-C and §3.2's one owner at a time. And the panel draws the session's curve (line 617) from documents whose session part WP09b writes, through a derivation that belongs with the session code (N2), yet WP12 and WP13 depend on neither WP09b nor WP07.

**Suggestion.** Add WP00b to WP00c's dependencies, or state that the wiring packages run in round order, and add WP09b and the session curve's owner (N2) to WP12's.

### N5 (minor): conditional options with no complete package

**Where.** WP16 (lines 1920-1930), §3.6 (lines 199-200), Q09 (a) (line 1449), Q10 (e) (line 1264).

**Issue.** Under Q09 (a), WP16 records decode errors in the report and reads them in `LadderReportRead`, but no package turns them into `decode_error` events: `TimelineBuild.cpp` and `TimelineDoc.cpp`, owned by WP09a, WP08 and WP09b, are not among WP16's files, and WP16 depends on neither WP09a nor WP09b. Q10 (e), which §3.6 says changes `VMAFAnalyzer`, `VMAFReport` and `VMAFSegments.h`, has no package at all; WP16's conditions are Q09 (a) and Q08 (b) only.

**Suggestion.** Add `src/TimelineBuild.cpp` (after WP09b) to WP16's files and WP09a and WP09b to its dependencies; name Q10 (e) among WP16's conditions, with Q10 blocking WP16, or say that (e) would need a package of its own.

### N6 (minor): the critical path holds only under Q08 (a)

**Where.** Q08 (b) (line 854), WP06 (line 1740), WP16 (line 1922), "Order and the critical path" (line 1954).

**Issue.** Q08 is a round-1 question. Under (b) each frame's segment comes from the analyzer, which is WP16's work (round 3, conditional, after a D-007 ruling), so the acceptance tests would wait for WP16 and that ruling. §14 states the path without this condition.

**Suggestion.** Say that the path assumes Q08 (a), and what moves into round 1 under (b): WP16's Q08 (b) part, and WP06 reading the map from the report.

### N7 (minor): Blocked-by lists still miss answers the packages encode

**Where.** WP00a (lines 1650, 1675), WP09a (line 1770), WP12 (line 1879), WP09b (line 1845), Appendix B's `QueryOptions` and `DetectOptions` (lines 2185-2189), §12's Blocks columns.

**Issue.** WP00a writes each round-1 module's header as Appendix B sketches it, `QueryOptions` (margins and scope: Q28, Q29) and `DetectOptions` (Q23, Q24, Q26) among them, and none of those questions blocks it; WP09a writes the detection on pair curves that Q22 (d) would remove, without Q22; WP12's loader checks the two hashes of Q06, without Q06; WP09b writes the session's wall-clock fields of Q03, without Q03. Nothing starts early because of it (round 1's packages wait for all of round 1's answers), but the Blocks columns, which tell the owner what an answer affects, are incomplete.

**Suggestion.** Add these questions to the packages' Blocked-by lists (render.py then keeps the Blocks columns equal to them), or say in §14 that a package waits for all of its round's answers and that Blocked-by lists the answers that change its content.

### N8 (minor): P-N keeps a choice that changes the network events

**Where.** §13's P-N (line 1611), Q10 (a) (line 1260), Q44.

**Issue.** P-N fixes the fetch's pattern: sequential and unpaced, each variant's playlist fetched the first time it plays. Its alternative, pacing like a player's buffer, would change every `segment_request`'s measured times. Q10 (a) describes the sequential fetch but neither Q10 nor Q44 offers pacing, so by L5's test this part is the owner's, not a process choice.

**Suggestion.** Make the fetch's pacing an item of Q44 (unpaced, as now; paced to the plan's media time; a buffer like a player's), with the agent's recommendation, and keep in P-N only how the fetch is implemented.

### N9 (minor): Q10 and Q41 decide the same thing

**Where.** Q10's summary and option (a) (lines 1256, 1260), Q41 (a) (line 1393), WP00b (line 1800), K25 (line 73).

**Issue.** Q10 (a) times the fetch with `NetworkLogger` compiled into `mirage_vmaf`, as its summary says, and Q41 (a) again compiles `NetworkLogger` into `mirage_vmaf` for the session fetch. Answers can then conflict (Q10 (b), (c) or (d) with Q41 (a)), against K25's set of questions without duplicates.

**Suggestion.** Leave linking `NetworkLogger` into `mirage_vmaf` to Q10 (a) and make Q41 about the name only (keep it, rename it, or wrap it), asked when Q10 (a) is the answer; it stays in round 2 if a rename would touch WP07's code.

### N10 (minor): the PR text is outside the quote check

**Where.** §11 (line 629), P-AK (line 1628), WP15's tests (line 1918).

**Issue.** The differences go into `docs/PHASE3_DECISIONS_PENDING.md` and Phase 3's PR in the design's own words, but P-AK runs the quote check before each commit only, so the PR description, which is public and which the first review named, is not checked.

**Suggestion.** Extend P-AK to the PR description and any other posted text, keeping the checker's output as the record.

### N11 (minor): Q21 leaves out one licence class

**Where.** Q21 (lines 1372-1377).

**Issue.** scte35.md §5 lists 6 messages from the example playlists and smoke test of futzu/threefive, under MIT by GitHub's detection, besides the GPL-2.0 documentation of another threefive repository. Q21's text says only that threefive is GPL-2.0, and no option names the MIT-licensed messages, as option (b) names scte35-go's Apache-2.0 ones.

**Suggestion.** Name both threefive sources with their licences in Q21, and add the MIT-licensed examples to option (b) or as an option of their own.

### N12 (minor): "round 1" names two things

**Where.** The question texts of Q43 and Q44 (lines 1205, 1409), §4.2 (line 246), K2 (line 50), §2 (line 99), Appendix A (lines 2005, 2012).

**Issue.** Since revision 1, "round 1" names the first round of owner questions. Q43 and Q44, read alone under a round mark, say that round 1 of this design made their choices as process choices, and §4.2, K2, §2 and Appendix A call the pre-revision check round 1's check. A reader of Q43, or of its translation, can take the first round of questions for the first version of the design.

**Suggestion.** Write "the first version of this design" or "before revision 1" in those places; the records' own labels stay as recorded.

### N13 (minor): WP15 starts now but is a blocked round-3 package

**Where.** WP15 (lines 1908-1918), "Where Phase 3 stands" (line 20), the order's item 1 (line 1946), §14's definitions (lines 1646, 1956), Q42 (b) (line 1584).

**Issue.** WP15 is in round 3 and blocked by Q41 and Q42, yet the design has it start now. §14 defines Blocked-by as the answers a package needs before it starts, and a package's round as the round of answers it waits for; the rendered paragraph then says both that every package waits for at least one answer and that WP15 can start before any. Separately, the dated correction of `docs/VMAF_GAP_REPORT.md` under Q42 (b) is in no package's files.

**Suggestion.** Split WP15 into the pending-decisions document (blocked by nothing, starting now) and the documents that wait for Q41 and Q42, or define its blockers as those of §11's wording only; give `docs/VMAF_GAP_REPORT.md` to WP15 under Q42 (b).

### N14 (minor): a wrong citation for HlsAlign

**Where.** §3.3 (line 174).

**Issue.** `src/HlsAlign.h:12` is cited for `src/HlsAlign.cpp` needing only FFmpeg. The line says its functions are pure, with no FFmpeg or ImGui, and `HlsAlign.cpp` includes only standard headers (`runs/code-lines.txt`). The conclusion, that it can be compiled into `mirage_vmaf`, holds all the more.

**Suggestion.** Say that it needs no library beyond the C++ standard library (`src/HlsAlign.h:12`).

### N15 (minor): an absolute path in a comment of the design's run.sh

**Where.** `design/tools/run.sh`, line 24.

**Issue.** The comment that explains the sanitizer names macOS's temporary directory with its private prefix as an absolute path. It is the only absolute path in `design.md` and under `design/` (`runs/privacy-check.txt`), and a generic one, but the public copy's privacy check matched the same comment in `design-library/tools/run.sh:24` and in `probe/sanitize.sh:7`, and both were reworded there (`<scratch>/phase3/records/phase3-survey/README.md`, the first of its changes to the copies). `design/` is to be committed the same way.

**Suggestion.** Reword the comment as those two were before `design/` is committed; this review's copy of `run.sh` shows one wording.

## 3. What was checked and holds

- **The render.** `design.md` is byte-identical to `render.py`'s output from a copy of `design/`, and the copy's JSON is unchanged by the render (`runs/render.txt`, `render-cmp.txt`, `render-json-unchanged.txt`). Note: the revision's own `design/runs/render-check.txt` ran `render.py` on `design/` itself, which rewrites its four JSON files in place; since they are a fixed point of that rewrite, nothing changed.
- **Quotations.** 0 prose runs and 0 runs of names in `design.md` and each file under `design/` (`runs/quote-check.txt`); this review's own files likewise (`runs/quote-check-review.txt`).
- **Privacy.** No user or host name and no e-mail address in `design.md`, under `design/` or in this review's files, and no absolute path but the generic one of N15 (`runs/privacy-check.txt`, which checks the local account's login, git name and e-mail and the real paths of `<out>`, `<repo>` and `<scratch>` without printing them); records write `<out>`, `<repo>` and `<scratch>`.
- **Code claims.** Each `file:line` the design rests on was opened in `<repo>`: `CMakeLists.txt:2`, `:150-194`, `:227`, `:229-232`, `:371`; `src/VMAFAlign.h:9-11`, `:23-25`, `:114`; `src/VMAFReport.cpp:193-201`; `src/VMAFReport.h:149`; `src/HlsAlign.h:33-75`, `:46`, `:69`; `src/JsonText.h:1-21`; `src/NetworkLogger.h:118-125`, `:157-160`, `:189-194`; `src/NetworkLogger.cpp:15-20`, `:175`, `:179-185`, `:389`, `:595-688`; `src/VMAFNet.h:8-9`; `src/VMAFSegments.h:28`, `:37`; `src/VMAFAnalyzer.cpp:166-194`, `:197-241`; `src/VMAFLadder.h:33-38`, `:88`, `:128-131`; `src/VMAFLadder.cpp:293-334`; `src/VMAFCli.cpp:26-35`, `:92-124`, `:195-257`, `:224`, `:273-344`, `:288`; `tools/mirage_vmaf.cpp:7-17`, `:143`, `:155-156`; `src/main.cpp:553-556`, `:1751`, `:3333-3339`; `src/NetInspector.cpp:387-394`; `src/NetInspectorAlign.cpp:69-109`, `:637-662`; `src/ManifestParser.cpp:121-157`; `src/NetProbe.cpp:120-141`; `tests/HttpFileServer.h:31-36`; `tests/TestTemp.h:3-7`; `tests/test_vmaf_ladder.cpp:192-216`, `:286-310`; `tests/test_vmaf_report.cpp:104`, `:909`; `tests/test_vmaf_cli.cpp:161` (it pins the synopsis with `starts_with`, so a paragraph after it leaves the test passing); `tests/test_manifest_parser.cpp:95`; `tests/test_vmaf_segments.cpp:46-117`; `docs/VMAF_GAP_REPORT.md:230-231`; `docs/DECISIONS.md:783-808`, D-032 and P-036; `eval/ladder/pool_check.py:117-118`; `eval/consistency/ladder_check.py:25-27`. All match, except `src/HlsAlign.h:12` (N14). Q38's free letters are those `src/main.cpp` leaves unbound (`runs/player-keys.txt`).
- **Numbers.** Checked against their records: design-evaluation runs 03 and 04 (15 playlists, 200 of 200 frames each, 100 frames of each ad playlist with a PTS that two segments share), 05 (the grids and the per-curve table at T = 6, W = 50), 06 (2313133 µs, 12032 bytes, the other segments 578 to 3569 µs), 09 (50 shared PTS, the −2.960 and +3.040 s steps), 10 and 11 (the attributions, 8577 to 14470 bytes, 32 to 44 events); design-library runs `axis-ad`, `axis-ad-origin1`, `axis-*-float`, `map-by-pos-*` (145 and 193 of 200); `design/runs/axis-check.txt` and `detection-semantics-ad.txt`; `review-adversarial/runs/switch-t-sweep.txt`, `ctest-registration.txt`; testgen.md §3.1 to §3.6 and log 61; schema.md §1.7, §1.8, §3.1, §3.3; sources.md §2 and §7; scte35.md §3 to §5. All agree, except the rate-change claim (N1).
- **Questions.** Each has `round`, `d007` and a summary; rounds hold 27, 9 and 8; each round is the earliest round of the packages it blocks; Blocks equals the packages whose Blocked-by names it (render.py asserts these, and the render passed); every design choice found is a question, and the P-entries kept are process and measurement, but for P-N's pacing (N8).
- **Packages.** File ownership is in turn for every shared file but `CMakeLists.txt` between WP00b and WP00c (`runs/package-check.txt`); the acceptance path is closed under its dependencies and waits for no optional input and no question outside round 1, given Q08 (a) (N6); the composite path adds round-2 packages only.
- **Decisions.** D-068 (one scoring, sessions as spans with their HTTP timing, render and decode paths unchanged), D-069 (synthetic ladders made in the tests through FFmpeg's libraries, nothing downloaded), D-070 (the listed tags read and their messages decoded; DASH `EventStream` and in-segment SCTE-35 as a declared interface), D-002 (top-rung mode by default, mezzanine references handled), D-007 (every render or decode change marked and gated, Q36 as the UI's plan), D-046 and D-050 (pairing untouched; the playlists' tags used for placement only) are respected. No `ffmpeg` or `ffprobe` program is part of the design or the tests.

## Where Phase 3 stands

Done: the five surveys, the two designs, the merged design, its first adversarial review, revision 1, and this second review. Nothing of Phase 3 is implemented; `<repo>` is at `75cf6e0` with a clean status (`runs/repo-state.txt`, `runs/repo-status.txt`). Next: fix the two major issues (Q02 (d), §4.2, K2, R11 and WP01's test; WP01, WP07 or WP09b and Appendix B) and the minor ones, render and check again, then send round 1's 27 questions to the owner.
