# Revision 2 of the merged Phase 3 design (2026-10-31)

What changed in `design.md` against the second review (`review-2/review.json`: its verdict, the items it marks partly, which are the first review's issues 0 and 6 and the lead's item L8, and its new issues N1 to N15) and against the lead's items L1 to L8, item by item, with the runs made for it. Nothing decided changed: D-002, D-007, D-013, D-046, D-050, D-060 and D-066 to D-070 bind the design as before, every recommendation stays labelled as the agent's, and no choice is presented as made. One recommendation changed because its evidence did (Q02, under N1). Nothing of Phase 3 is implemented, and nothing in `<repo>` was written: it is at `75cf6e0` with a clean status (`design/runs/repo-state.txt`).

**What was edited.** The sources only: `design/src/part1.md`, `design/src/part2.md`, `design/owner_questions.json`, `design/work_packages.json`, `design/p_entries.json`, `design/risks.json`, `design/tools/render.py`, and one comment of `design/tools/run.sh`; `design.md` is their render (`design/runs/render-check.txt`). New: `design/tools/seconds_check.py`, this file, and the records below. `design/tools/axis_check.py` and its record `design/runs/axis-check.txt` are kept as revision 1 left them, since the second review's `review-2/runs/rate-change-check.txt` loads that tool and would no longer reproduce if it changed. `design/revision-1.md` is kept as the record of revision 1; where it says "round 1's check" it means the first version of this design.

**Counts after the revision.** 44 owner questions, Q01 to Q44 with no number changed or added, in three rounds of 27, 9 and 8 (unchanged); Q41 now carries the D-007 mark, so eight questions do (Q01, Q08, Q09, Q10, Q13, Q36, Q37, Q41); Q44 has seven items. 24 work packages (WP09b split into WP09b and WP09c, WP15 into WP15a and WP15b). 28 P-entries (P-N narrowed; P-C, P-AG, P-AH and P-AK extended; none moved or added). 22 risks (R6 and R11 rewritten). 14 differences from the work order.

## New runs

Each was made with `design/tools/run.sh` (`OUT`, `REPO` and `SCRATCH` set as the task gives them); each record holds its working directory, exact command, start and end (UTC; the records' times fall on 2026-11-01 in UTC), full output and exit status, with local paths written `<out>`, `<repo>` and `<scratch>`.

| Record | Working directory and command | What it shows |
|---|---|---|
| `design/runs/seconds-check.txt` | `<out>/testgen`: `python3 -I -B <out>/design/tools/seconds_check.py` on the reports and layouts of `ad-x264`, `ad-mpeg2`, `ad-x264-origin1` and `degraded-x264`, on `degraded-x264-mezzanine` and `ad-x264-mezzanine` with no map, `--drop 40 180 --retime 75 124 25 24 30 50 60 --extinf 1 1` | Revision 2's check (§4.2) beside revision 1's and beside the second review's comparison at segment starts. On the four ladders: (1) and (2) 0 runs, (d) against (c) 0.000 s; without the map, the two ladders whose ad restarts at the main content's origin (`ad-x264`, `ad-mpeg2`) list frames 125 to 199 (+3.000 s against their report sequence's grid) and the other two nothing; the two mezzanine reports 0 runs. Simulated: frame 40 removed, (1) frames 40 to 198 (−0.040 s), (2) frames 40 to 73; frame 180 removed, (1) and (2) frames 180 to 198, which the segment-start comparison misses (0 flagged); the ad at 25 fps nothing; at 24, 30, 50 and 60 fps, (1) from frame 88, 79, 77 and 76 to 199, largest 0.083, 0.333, 1.000 and 1.167 s, (2) within the ad, revision 1's check 0, 0, 0 and 49 steps; segment 1's EXTINF a frame longer, (1) frames 50 to 199 (−0.040 s), (2) nothing. Exit status 0. |
| `design/runs/networklogger-users.txt` | `<repo>`: `grep -rl -e NetworkLogger src tools tests CMakeLists.txt`, sorted, and its count | 21 files name `NetworkLogger`: `CMakeLists.txt`, 17 under `src/` (`src/Demuxer.cpp`, `src/main.cpp`, `src/ManifestParser.cpp` and `src/NetInspectorAlign.cpp` among them) and 3 tests. Exit status 0. |
| `design/runs/question-lengths.txt` | `<out>`: `python3 -I -B review-2/tools/question_lengths.py design/owner_questions.json` (the second review's tool) | 44 questions, 185 options, 0 counts outside L4's ranges. Exit status 0. |
| `design/runs/package-check.txt` | `<out>`: `python3 -I -B review-2/tools/package_check.py design/work_packages.json` (the second review's tool) | 24 packages, 110 files named, 0 pairs of owners not ordered by the dependencies. Exit status 0. |
| `design/runs/repo-state.txt` | `<repo>`: `git --no-optional-locks log -1 --format=%H%n%s` and `git --no-optional-locks status --short --branch` | `<repo>` at `75cf6e0`, branch `vmaf/phase-3`, nothing changed. Exit status 0. |
| `design/runs/render-check.txt` | `<out>`: `render.py` writing into a pipe that `cmp` compares with `design.md` | `design.md` is byte-identical to the render of its sources, and the renderer's checks passed. Exit status 0. |
| `design/runs/privacy-check.txt` | `<out>`: `python3 -I -B review-2/tools/privacy_check.py` (the second review's tool) on `design.md` and every file under `design/` but itself and `quote-check.txt`, which are made after it | 0 local paths, 0 e-mail addresses and 0 identity words in each file. Exit status 0. |
| `design/runs/quote-check.txt` | `<out>`: the quote checker on `design.md` and every file under `design/` but itself, against the work order's translation kept outside the repository | 0 prose runs and 0 runs of names in each file, K = 6. Exit status 0. |

`design/tools/seconds_check.py` (new) reads testgen's reports and layouts and writes nothing; the layouts stand in for the demux pass's segment map, as in revision 1. Its simulations work on copies in memory and are labelled so in its output.

## The second review's items marked partly

### Issue 0 (partly): Q02 (d)'s safeguard

**Fixed** with N1 below. The check no longer compares steps; it compares (d)'s seconds with (c)'s at every scored reference frame and each frame's PTS with the frame grid of its playlist discontinuity sequence, and the new run shows it warns in every simulated failure, missing frames and changes of rate included, and on none of the four ladders.

### Issue 6 (partly): the session half of the API without a feasible owner

**Fixed** with N2 below: WP01 implements and tests `curve_points()` and `attribute()` for a pair's curve; WP07 adds a session's to the same files after WP01 has merged and WP00b has declared the session types.

### L8 (partly): two claims without a record

**Fixed.** The claim that the axis check warns at a change of rate now rests on `design/runs/seconds-check.txt`, made with the revised check (N1); the citation of `src/HlsAlign.h:12` now says what the line says (N14).

## The second review's new issues

### N1 (major): the revised axis check doesn't catch a change of frame rate

**Fixed, with a different check from the one suggested, and with the recommendation changed.** The review is right: `design/runs/seconds-check.txt` reproduces its finding with revision 1's check (0 steps flagged at 24, 30 and 50 fps). The review suggested comparing (d) with (c) at each segment's first frame, or each PTS with its playlist discontinuity sequence's grid. The revised check (§4.2) does both, at every scored reference frame: (1) (d)'s seconds against (c)'s, which bounds the accumulated error wherever it comes from; (2) each PTS against the grid of its playlist discontinuity sequence, which says whether the frames themselves leave the grid, so that a warning of (1) without one points at the playlist's EXTINF. Why not segment starts alone: the new run shows that a comparison at segment starts misses a frame missing from the last segment (frame 180: 0 flagged, where (1) and (2) warn at frames 180 to 198), and warns within a segment only at the next segment's start (frame 100 rather than 79 for the ad at 30 fps). Without a map, (2) runs on the report's sequences: warnings for a reference that isn't a playlist (two mezzanine-mode reports give no run), facts for a playlist whose map was refused. A simulated EXTINF a frame too long shows the other side: (c) is then 0.040 s late and (1) warns without (2).

**The recommendation for Q02 moved from (d) to (c)**, falling back to (d) where the reference has no segment map, with (d) alone the close second. The evidence that changed: the check that catches a change of rate has to compute (c)'s seconds at every frame, so (d) no longer saves that work where the map exists, and the choice is which seconds to print where the two part; (c) is right through a missing frame or a change of rate (0.040 s; 0.083 to 1.167 s off under (d)), and RFC 8216 puts changes of encoding parameters at discontinuities ([§4.3.2.3](https://www.rfc-editor.org/rfc/rfc8216#section-4.3.2.3), the review's citation), as at an ad; (c) is off only where an EXTINF departs from its frames, which RFC 8216 §4.3.2.1 advises against, and (d) stays the better choice if the owner expects whole-second or rounded EXTINF. The recommendation is the agent's, and Q02 asks the owner to choose.

Changed: §4.2 (the table's last column, the paragraph on (c) and (d), the check, what it did, the recommendation), §0 item 3, §1.1 (new in revision 2), §1.2 K2, §2 (a row rewritten, two new: the simulations and RFC 8216), §8 (`axis.seconds`), §9.1 (`test_timeline_axis.cpp`), §11 item 4, Q02 (summary, question, options (c) and (d), recommendation, why), R11, WP01 (description and the check's test), Appendix A (the new record; the old one relabelled), Appendix B (`Axis::clock`, `build_axis()` taking the reference's playlist, `frame_at_or_after()` on an axis that can step back).

### N2 (major): session attribution assigned to WP01 before its types exist

**Fixed**, by the review's suggestion with one difference. WP01 implements `curve_points()` and `attribute()` for a pair's curve, and its scope test is for a pair only; WP07 adds, after WP01 has merged, a session's curve to `curve_points()` in `src/TimelineModel.cpp` and a session's scope to `attribute()` in `src/TimelineQuery.cpp`, both among WP07's changed files, WP07 now depending on WP01 directly and blocked by Q28. The difference: WP07's tests of the session's curve and scope, and the work order's example on a hand-built timeline (whose switch is a session's event, which a pair's scope doesn't list), go into WP07's own `tests/test_session_plan.cpp` rather than into WP01's `tests/test_timeline_query.cpp`, so that WP01's test files keep one owner. `curve_points()` is declared in `src/TimelineModel.h` by WP00a and defined in `src/TimelineModel.cpp` (Appendix B says so); WP00b adds `Span`, `Session`, `Timeline::sessions` and `CurveRef`'s session kind; §3.2 no longer gives the session curve to `SessionPlan`. Changed: §3.2 (the rows of `TimelineModel`, `TimelineQuery` and `SessionPlan`, the shared files), §4.4, §6.1, §6.2, §9.1 (`test_timeline_query.cpp`, `test_session_plan.cpp`), WP00b, WP01, WP07, Appendix B.

### N3 (minor): the composite example waits for the SCTE-35 decoder

**Fixed**, by the review's suggestion. WP09b keeps the session part (after WP09a and WP07; blocked by Q03, Q04, Q07, Q10, Q11, Q12, Q22, Q44); the decoded messages are WP09c (new; after WP09b, since both change the builder and the CLI, and after WP05; blocked by Q15 and Q16, which no longer block WP09b). WP11b depends on WP09b only. `render.py` computes the composite path again: it adds WP00b, WP07, WP09b and WP11b, all of round 2, and now asserts that WP05 and WP09c are on neither path. Changed: §3.2, §8 (who writes what), §9.1, §14 (WP09b, WP09c, the order, the critical path), Q15 and Q16 (Blocks).

### N4 (minor): two orderings the files need

**Fixed.** WP00c depends on WP00b, so the three wiring packages edit `CMakeLists.txt` in round order, and P-C says that the lead's wiring commit for a conditional package is made while no wiring package is open. WP12 depends on WP07 (the session's curve) and WP09b (the document's session part), and WP13 follows WP12. `render.py` now refuses to render when a file that two packages name isn't owned in turn, one depending on the other, and the second review's `package_check.py` finds 0 unordered pairs (`design/runs/package-check.txt`). Changed: P-C, §1.2 K19, §3.2, §3.3, WP00c, WP12, the order in §14.

### N5 (minor): conditional options with no complete package

**Fixed, and extended to every option of §3.6.** WP16 now changes `src/TimelineBuild.cpp` and its test after WP09c has merged (the last owner before it, so WP09a and WP09b come before it too), turning what the scorer records into events, and its conditions are every option that changes the scorer or adds a decode pass: Q01 (C), Q08 (b), Q09 (a) or (b), Q10 (e) (with `src/VMAFSegments.h`) and Q12 (d) (with `src/VMAFLadder.*`), each question blocking WP16. Checking the rest of §3.6 the same way found four more options without a package, now given one: Q01 (A) (WP08 owns `src/VMAFReport.*` for it), Q37 (c) (WP13 owns `src/Demuxer.*`), Q40 (b) (WP13 owns `src/ManifestParser.*` and its test) and Q41 (b) (WP00b makes the rename, N9); Q13 (c), a reader of the player's switch trace, is WP17's, and Q37 (b), the render loop's play-to-a-frame, is now named in WP13's description. §3.6's table gains a column naming the package of each change. Changed: §3.2, §3.6, WP04 (Q20 (b)), WP08, WP13, WP16, WP17, and the options that now name their package: Q01 (C), Q05 (b), Q09 (a), Q10 (e), Q12 (d), Q13 (c), Q20 (b), Q40 (b), Q42 (b).

### N6 (minor): the critical path holds only under Q08 (a)

**Fixed**, by the review's suggestion. §14 says that the path assumes the agent's recommendations for Q01 and Q08, under which the scorer is unchanged, and what moves under the others: under Q08 (b) WP16's recording of each frame's segment and key flag moves into round 1 ahead of WP06, which then reads the map from the report, and the acceptance tests wait for the owner's ruling or approved plan under D-007; Q01 (C) the same for the per-variant events; Q01 (A) WP08 writing into the report. Q08 (b) and Q01 (C) say so in their options, and WP06 and WP16 in their descriptions.

### N7 (minor): Blocked-by lists miss answers the packages encode

**Fixed, by both of the review's suggestions.** WP00a now writes only the declarations that packages running in parallel share (it lists them), not every module's header, so `QueryOptions` and `DetectOptions` are written by WP01 and WP02, which the questions they encode block (Q28 and Q29; Q23, Q24 and Q26). Every package's Blocked-by list was then checked against what its code, tests or output encode, and completed: WP00a gains Q05 and Q20 (their options (b) change its CMake lines); WP08 Q23, Q24, Q26, Q28, Q29 and Q30 (the detection and overlap records); WP09a Q02, Q04, Q08, Q19, Q22, Q27 and Q30; WP09b Q03, Q04 and Q07; WP11a Q01 and Q04; WP11b Q01, Q04 and Q07; WP12 Q06; WP14 Q02, Q07 and Q22; WP17 Q11. No question's round changed. §14 defines both lists: a package starts only once every question of its round is answered, and Blocked-by lists the questions whose answers change what it builds, its code, its tests or the form of its output. `render.py` now also refuses to render when a package's text names a question that doesn't block it.

### N8 (minor): P-N keeps a choice that changes the network events

**Fixed**, by the review's suggestion, extended to the other part of P-N's pattern. Q44 gains (f), the fetch's pacing (one file at a time unpaced, by the agent's recommendation; paced to the plan's media time, which makes a build with a session last at least its media's length, 8 s over a whole D-069 ladder, testgen.md §2.3; a buffer like a player's), and (g), whether a variant's playlist is fetched once or at each span that plays it, which changes the playlist requests. P-N keeps only how the fetch is implemented, and names the two items. Changed: Q44 (title, summary, question, items (f) and (g), recommendation, why), P-N, §3.2, §3.4, §5.1 (the playlist request's row), §5.7, §9.1, R6, WP07, Appendix B (`fetch_session()`), §16 (a paced fetch is not run).

### N9 (minor): Q10 and Q41 decide the same thing

**Fixed**, by the review's suggestion. Linking `NetworkLogger` into `mirage_vmaf` is Q10 (a)'s alone (its option says so, with §3.3), and Q41 is the name only: keep it and report the difference, rename it, or wrap it; where Q10's answer isn't (a), only the report applies. Q41 stays in round 2, since a rename changes WP00b's wiring and WP07's code. Reading the rename's extent for this, `design/runs/networklogger-users.txt` finds 21 files that name `NetworkLogger`, `src/Demuxer.cpp` and `src/main.cpp` among them, which are the player's decode and render path: Q41 (b) now says so, its answer would include the owner's ruling under D-007 or the approval of a plan, and Q41 carries the D-007 mark (L2). WP00b makes the rename as a commit of its own, only under Q41 (b). Changed: Q41 (title, summary, question, options, d007), Q10 (a), §1.2 K25, §3.2, §3.3, §3.6, §11 item 1, WP00b.

### N10 (minor): the PR text is outside the quote check

**Fixed**, by the review's suggestion. P-AK covers Phase 3's PR description and any other text about Phase 3 before it is posted, the checker's output kept as the record of the posting; §11 and WP15a's and WP15b's tests say so.

### N11 (minor): Q21 leaves out one licence class

**Fixed**, by the review's suggestion. Q21 names both threefive repositories with their licences as GitHub detects them (superkabuki/threefive_is_scte35's documentation GPL-2.0; futzu/threefive's example playlists and smoke test MIT, scte35.md §5), and option (b) adds futzu/threefive's 6 messages beside scte35-go's cases, with their trade-off: their source decodes none of them, so a test could check only that they decode with a valid CRC (scte35.md §5's table). The recommendation is unchanged.

### N12 (minor): "round 1" names two things

**Fixed.** "Round 1" now names only the first round of owner questions. Q43's and Q44's texts say "the first version of this design" and "earlier versions of this design" (and that (h) of Q43 and (d) of Q44 answer the first review's cases rather than former P-entries, which their old wording implied); §4.2, K2, §2 and Appendix A say "the first version of this design" or "revision 1"; Appendix A notes that `design/runs/axis-check.txt` labels the first version's check "round-1 wording", the record's own label being kept.

### N13 (minor): WP15 starts now but is a blocked round-3 package

**Fixed**, by the review's first suggestion. WP15a (round 0, blocked by nothing, the one package that can start now) writes the pending-decisions document, `docs/plans/timeline-ui.md` (design §10 as the written D-007 plan for the owner to approve, which must exist before the answer to Q36) and §13's P-entries as pending entries of `docs/DECISIONS.md`. WP15b (round 3, after WP15a, blocked by Q41 and Q42) writes the PR description's differences, under Q42 (b) the dated correction of `docs/VMAF_GAP_REPORT.md`, which it now owns, and the documents that report results, the status document last. `render.py` gives a package of round 0 no round of answers to wait for. P-AH says that the lead records each round's answers in `docs/DECISIONS.md` and the pending-decisions document before that round's packages start. Changed: "Where Phase 3 stands", §3.2, §3.6, §10, §11, §14, Q42 (b), P-AH, WP15a, WP15b.

### N14 (minor): a wrong citation for HlsAlign

**Fixed**, by the review's suggestion: §3.3 says that `src/HlsAlign.cpp` needs nothing beyond the C++ standard library (`src/HlsAlign.h:12`); it includes only its own header and standard headers (`src/HlsAlign.cpp:1-9`).

### N15 (minor): an absolute path in a comment of run.sh

**Fixed.** The comment of `design/tools/run.sh` now has the wording the public copy gave `design-library/tools/run.sh:24` and `probe/sanitize.sh:7`, rather than the review's copy's, so that the three files say it alike; the privacy check finds no local path in it (`design/runs/privacy-check.txt`).

## The lead's items

### L1: quotations

**Kept.** `design.md`, `design/src/*.md`, `design/*.json`, this file, `design/tools/*` and `design/runs/*` have 0 prose runs and 0 runs of names (`design/runs/quote-check.txt`, made through `design/tools/run.sh`). The translation was not copied anywhere, and no record names a shared word.

### L2: rounds and the D-007 mark

**Kept, with one mark added.** Every question keeps `round` and `d007`; the rounds are unchanged (27, 9 and 8), since no added blocker lowered a question's round, and §12, "Where Phase 3 stands", the Conventions and §0 give them. `d007` is now true for Q41 too (N9), whose text says which answer includes the ruling. Q01 to Q42 keep their numbers; no question was added.

### L3: summaries

**Kept.** Each changed question has its summary rewritten to state what it decides and the agent's recommendation exactly: Q02 (the recommendation now (c)), Q41 (the name only) and Q44 (seven items, each named in order); the others' recommendations are unchanged.

### L4: questions short enough to read alone

**Kept.** 44 questions, 185 options, 0 counts outside the ranges (`design/runs/question-lengths.txt`, with the second review's tool). Nothing was dropped: what Q41 (a) said about linking `NetworkLogger` moved to Q10 (a) and §3.3, and what Q02 (d) said about the check without a map is in §4.2, which the question names.

### L5: moved P-entries grouped

**Kept.** P-N's pacing and playlist fetches went into Q44 as items (f) and (g), the question that already holds P-N's retries, with their options and the agent's recommendation; the 28 P-entries kept were read again for choices that change an output by default, and none other was found.

### L6: citations to what won't be committed

**Kept.** The new citations are to records in `design/runs/` and `review-2/runs/`, to testgen's reports and layouts in `testgen/reports/` (kept by the public copy), to survey sections (testgen.md §2.3, scte35.md §5) and to RFC 8216 by its URL (§4.3.2.1 and §4.3.2.3, read in the RFC's text).

### L7: counts and dates

**Kept.** "Where Phase 3 stands", §0 item 9, the Conventions and §12 to §15 agree: 44 questions (27, 9, 8), 14 differences, 28 P-entries, 24 work packages, 22 risks; `render.py` refuses to render if `part1.md` states other counts, and it rendered. The design's date stays 2026-10-31, as this log's; the new records' UTC times fall on 2026-11-01.

### L8: conventions

**Fixed** (see above). Every new number cites its record or source: `design/runs/seconds-check.txt` (the check's runs and the simulations' seconds), `design/runs/networklogger-users.txt` (21 files), testgen.md §2.3 (eight 1 s segments), scte35.md §5 (6 MIT messages); what isn't run says so (a paced fetch, in §16); nothing is presented as decided, and every recommendation, Q02's included, is labelled as the agent's.

## Code claims added, each checked in `<repo>` at `75cf6e0`

`src/HlsAlign.h:12` (pure functions, no FFmpeg or ImGui) and `src/HlsAlign.cpp:1-9` (its own header and standard headers only); `src/NetworkLogger.cpp:15-20` and `src/NetworkLogger.h:3-11` (FFmpeg's and the standard headers only); `src/Demuxer.cpp:5`, `:876` and `src/main.cpp:17` (they name `NetworkLogger`; the full list is `design/runs/networklogger-users.txt`); `tests/test_vmaf_report.cpp:909` (the exact key count under `pairs[]`); and that `src/Demuxer.h`, `src/ManifestParser.h`, `src/VMAFLadder.{h,cpp}`, `src/VMAFSegments.h`, `src/MediaSession.cpp`, `tests/test_vmaf_analyzer.cpp`, `tests/test_vmaf_segments.cpp`, `tests/test_vmaf_ladder.cpp`, `tests/test_manifest_parser.cpp` and `tests/test_network_logger.cpp` exist, which §3.6 and §14 now name for conditional changes.
