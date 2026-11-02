# Revision 1 of the merged Phase 3 design (2026-10-31)

What changed in `design.md` against its adversarial review (`review-adversarial/review.json`: the verdict and issues 0 to 17) and against the lead's items L1 to L8, issue by issue, with the runs made for it. Nothing decided changed: D-002, D-007, D-013, D-046, D-050, D-060 and D-066 to D-070 bind the design as before, every recommendation stays labelled as the agent's, and no choice is presented as made. Nothing of Phase 3 is implemented, and nothing in `<repo>` was written (it stays at `75cf6e0`, status clean).

**What was edited.** The sources only: `design/src/part1.md`, `design/src/part2.md`, `design/owner_questions.json`, `design/work_packages.json`, `design/p_entries.json`, `design/risks.json`, and `design/tools/render.py`; `design.md` is their render (`design/runs/render-check.txt`). New: `design/tools/axis_check.py`, this file, and the records in `design/runs/` below.

**Counts after the revision.** 44 owner questions (Q01 to Q42 kept with their numbers, Q43 and Q44 new) in three rounds of 27, 9 and 8; 22 work packages (WP00, WP09 and WP11 split); 28 P-entries (9 moved into questions, 2 new); 22 risks (2 new); 14 differences from the work order (items 5 and 6 new, items 3 and 14 extended, every item in this design's words).

## New runs

Each was made with `design/tools/run.sh` (environment `OUT`, `REPO` and `SCRATCH` set as the task gives them); each record holds its working directory, exact command, start and end (UTC), full output and exit status.

| Record | Working directory and command | What it shows |
|---|---|---|
| `design/runs/axis-check.txt` | `<out>/testgen`: `python3 -I -B <out>/design/tools/axis_check.py` with the reports and layouts of `ad-x264`, `ad-mpeg2`, `ad-x264-origin1` and `degraded-x264`, and `--drop 40` | Round 1's check of Q02 (d) flags 75 of 200 frames (125 to 199) of `ad-x264` and of `ad-mpeg2`, none of the other two; the revised check, with the layouts as the segment map, flags none of the four; without a map it lists one step on each ad ladder, frame 125, +3.040 s; (d)'s seconds equal (c)'s on all four (largest difference 0.000 s); with reference frame 40 removed (simulated), the revised check flags that step (+0.080 s), round 1's check 109 frames, and (d)'s seconds are 0.040 s early from frame 40. Exit status 0. |
| `design/runs/log-callback-grep.txt` | `<repo>`: `grep -rn -e av_log_set_callback -e av_log_set_level -e avformat_network_init src tools tests` | The only log-callback installs are `src/NetworkLogger.cpp:175` and `:182`; `tools/mirage_vmaf.cpp:155-156` set the level and initialize the network; `src/Demuxer.cpp:831` and `tests/test_vmaf_color.cpp:130-131` are the other hits. Exit status 0. |
| `design/runs/render-check.txt` | `<out>`: `render.py` writing into a pipe that `cmp` compares with `design.md`, so that no second file is written | `design.md` is byte-identical to the render of the sources; the renderer's consistency checks passed. |
| `design/runs/quote-check.txt` | `<out>`: the quote checker on each file of the design, against the work order's translation kept outside the repository | Each file's runs of six or more words shared with the translation, by lines and kind, without the words: 0 prose runs in each file (L1). |

`design/tools/axis_check.py` (new) reads testgen's reports and layouts and writes nothing; the layouts stand in for the demux pass's segment map, which design-evaluation run 04 found equal to them on these ladders. Its missing-frame case is a simulation on a copy in memory, labelled so in its output.

## The review's verdict

The verdict's five major problems are issues 0 to 4 below, and its thirteen minor ones issues 5 to 17; each is resolved there. What it found to hold still holds and was rechecked where this revision touched it: the code claims at their file:lines (the new ones are listed at the end), `design.md` as the exact render of its sources (`design/runs/render-check.txt`), no local paths, names or emails in the design's files, and `<repo>` clean at `75cf6e0`. Its statement of where Phase 3 stands is superseded by the design's own: the owner's answers are now asked in three rounds, 44 questions in all.

## The review's issues

### Issue 0 (major): Q02 (d)'s safeguard

**Fixed.** The review is right: round 1's check, from the report alone, flags 75 of 200 frames of D-069's ad ladder (`review-adversarial/runs/axis-d-check.txt`, reproduced in `design/runs/axis-check.txt`), because a forward jump under 10 s stays inside one report sequence (`src/VMAFAlign.h:114`). The check is respecified in §4.2 as the review suggests: each step between consecutive reference frames against one frame duration, a step at the first frame of a segment after an `EXT-X-DISCONTINUITY` expected, any other a warning; it needs the reference's segment map, the input (c) needs. Without a map the document only lists the jumps within report sequences, claiming nothing about the seconds (the review's second suggestion, kept for that case). The new run shows the revised check flags nothing on the four ladders and flags a simulated missing frame, after which (d)'s seconds are 0.040 s early. Changed: §4.2 (the table's last column, the check paragraph, the recommendation), §0 item 3, §1.2 K2, §2 (a new row), Q02 (option (d), its question and why), R11, WP01's description and tests (the D-069 ad shape with and without a map, a missing frame, a change of rate), §8 (`axis.checked`), Appendix B (`Axis::checked`). **The recommendation stays (d)**, as the agent's, with changed reasons: (d) is defined from the report for every reference, a mezzanine included, with no fallback; but since its check needs the same map as (c), (c) is now called a close second and the better choice if the owner wants the playlists' clock or expects references with missing frames.

### Issue 1 (major): the critical path against the dependency table

**Fixed**, by the review's first suggestion. WP09 is split into WP09a (the builder and the CLI for a ladder: depends on WP01, WP02, WP03, WP04, WP06, WP08) and WP09b (sessions and decoded messages: depends on WP09a, WP05, WP07); WP11 into WP11a (the two acceptance tests, `tests/test_attribution_acceptance.cpp`, after WP09a and WP10) and WP11b (the composite example, now in `tests/test_attribution_session.cpp`, after WP09b and WP10). The acceptance path is WP00a, WP01, WP02, WP03, WP04, WP06, WP08, WP09a, WP10 and WP11a; it no longer waits for WP05 or WP07, nor for Q10 to Q12, Q14 to Q16, Q21 or Q41. Q03, Q07 and Q18 still block it, through WP00a's declarations, WP08's document and WP04's tag parsing, which the ladder-only path needs; they are round 1. `design/tools/render.py` now checks that every dependency of a package on the acceptance path is on it and that every question blocking it is in round 1, so the text can't contradict the table again. Changed: §14 (the table, WP09a, WP09b, WP11a, WP11b, "Order and the critical path"), §3.2, §9.2, §9.3, the blocks of every question naming WP09 or WP11.

### Issue 2 (major): ctest's process per test case

**Fixed**, by the review's second suggestion rather than its first. The premise is right: `tests/test_vmaf_ladder.cpp:192-216` is a function-local static, the tests' temporary directory is per process and removed at exit (`tests/TestTemp.h:3-7`), and ctest runs each Catch2 test case in its own process (`review-adversarial/runs/ctest-registration.txt`). The acceptance cases are now sections of one test case, which makes each of its ladders once per ctest run (P-Q); the composite example and WP10's determinism check are test cases of their own. A CTest fixture writing the ladders once into the build tree is kept as P-Q's alternative, not chosen: it would make one directory that two ctest runs of a build tree share, against the per-process rule of D-032 and P-036. Generations are now counted per ctest run: seven under Q34 (d) (§9.5, R9); their time in a Debug build stays MEASUREMENT PENDING. Under `MIRAGE_KEEP_MEDIA` each test case writes only under its own subdirectory (P-U). Changed: P-Q, P-U, §9.2, §9.5, R9, WP10, WP11a, WP11b, Q34 (d), Q35 (a) (which no longer states the old premise).

### Issue 3 (major): P-entries that change default outputs

**Fixed**, following L5. The P-entries the review names, and three more that fail the same test (P-E, P-X, P-AC), moved into questions: Q43 (new, round 1) has one item per former P-entry of the ladder timeline: (a) live playlists (P-E), (b) which reports are read (P-X), (c) a segment map with a PTS mismatch (the end of P-G), (d) where an unpaired frame sits (part of P-H, with K15), (e) the BANDWIDTH that ranks a rung (P-AA), (f) a tag in several rungs' playlists (P-AB), (g) a drop's end and intervals in seconds (P-AE), and (h) the corrupt packet's place (issue 15). Q44 (new, round 2) has (a) plans with gaps or overlaps (P-P), (b) where requests sit (part of P-H, with K17), (c) the fetch's retries (part of P-N, issue 12), (d) a redirected fetch (issue 12), and (e) a message in a CUE-OUT's segment (P-K). P-AC went into Q04 (a), being part of §8's layout. P-K's first half, a tag belonging to the segment whose URI follows it, is RFC 8216's rule and stays in §5.1 as such. The kept P-entries are process and measurement only; the moved letters are listed in §13 and not reused. Changed: the Conventions, §1.2 (K3, K15, K17), §5.1, §5.2, §5.5, §5.6, §5.7, §6.2, §6.3, §13, the questions and packages that cited the moved letters.

### Issue 4 (major): D-070's interfaces

**Fixed**, by the review's suggestion. §5.5 defines the extension point, `CueInput {carrier, anchor, form, bytes}`, with carriers playlist tag, DASH `EventStream`, MPEG-TS section and ID3 PRIV, and anchors of three kinds: playlist position, media PTS on the carrying variant, and period time; WP00a declares it; `Scte35Events` places every kind by §5.1's rules, a period-time anchor going to the warnings since no DASH timeline is scored; WP05's tests feed a section anchored by media PTS; Phase 3 builds no reader for either carriage. Changed: §0 item 4, §5.4, §5.5, §5.1 (the media-PTS row), Appendix B (`CueCarrier`, the anchors, `CueInput`, `scte35_events()`), WP00a, WP04, WP05, Q20 (a), R19.

### Issue 5 (minor): session drops' open cases, Q23's blind start

**Fixed.** §6.3 rule 4 now gives Q22 (c)'s three rules with their alternatives: (i) a clipped piece is a drop only if it keeps more than N points (the prototype keeps every piece, `design/tools/detection_semantics.py:138-157`); (ii) pieces of two pairs that meet at a switch stay two drops; (iii) a piece reports its pair's baseline at its own first point. On (iii): under Q23 (a) a low point never enters the baseline, so the baseline is the same at every point of a drop (`design/tools/detection_semantics.py:40-50`) and the two readings the review names give the same value; they differ only under another statistic, and the rule says which one then applies. Q23 (a) and (b) now state the blind start (the first W/2 points of every curve are never low; at W = 50 a D-069 ladder's whole first segment, 25 frames, testgen.md §3.1), (d) that it has none; R21 is new. Q22's claim about T now cites the review's run `switch-t-sweep` (a drop at T = 3, 4 and 5, none at 5.5, 6, 6.5, 7 and 8). Tests: WP02 a drop inside the blind start and a baseline unchanged through a drop; WP07, which now owns `session_drops()`, one test per rule. Changed: §1.2 K7, §2 (a new row), §6.3, Q22, Q23, Q27's why, WP02, WP07, R21.

### Issue 6 (minor): the API can't express a session's attribution

**Fixed**, by the review's second suggestion: a `CurveRef` (a pair's index or a session's) names a curve, `attribute(const Timeline&, CurveRef, Interval, const QueryOptions&)` takes it, and `curve_points()` derives a session's curve with its spans; `session_drops()` moved to `SessionPlan` and takes the session by index. The statements were corrected: Q28 (a) now says run 10 used its scope for pairs only, and §6.4 says that under Q28 (a) the reference `v360`'s copies are listed too, naming them from run 10's attribution of the `v180` pair (the cue_out, cue_out_cont and ad_break, two segments, the discontinuity and two keyframes); the prototype was not rerun. Changed: §4.4, §6.1, §6.4, Q28, Appendix B, WP01's tests (scope for a CurveRef of each kind).

### Issue 7 (minor): claims beyond their sources, wrong citations

**Fixed**, each by limiting the claim to what was run; nothing new was run for (b) or (c).
- (a) The grid is stated as measured: T in {3, 4, 5, 6, 7, 8, 10, 12, 15, 20, 25, 30, 40}, N in {1, 2, 3, 4, 5, 8, 12, 16, 20, 24}, W = 10, 25, 50 and 100 (§2, §6.3, Q23 (a), §0 item 5); the 21-curve result is "every grid value of N from 3 to 24".
- (b) The encoder claim names what ran with MPEG-2: the degraded ladder and the ad from the main origin; the ad from 1 s and the continuous ad with MPEG-2 are MEASUREMENT PENDING (§1.2 K20, Q33 (c), R17, §16).
- (c) The seek claim is limited to the probe's ad, whose timestamps restart at 0.040 s; an ad restarting at the main content's origin, as D-069's does, was not probed (MEASUREMENT PENDING) (§2, §0 item 8, §10, §11 item 14, §1.2 K21, Q34 (c), Q37, R12, §16).
- (d) §2's ad row now says frames 75 to 124 and frame 0, the first of the pair's libvmaf context (testgen.md §3.2).
- (e) §5.5's table list is corrected to scte35.md §3's: SCTE 35 2023r1 Tables 5 to 17, 20 to 25 and 29.
- (f) The duplicate-key citation is `tests/test_vmaf_report.cpp:104` (P-Y).
- (g) The log-callback claim now rests on the grep record: only `NetworkLogger` installs a callback (`src/NetworkLogger.cpp:175, 182`), and `tools/mirage_vmaf.cpp:155-156` set the level and the network (§2, P-N).

### Issue 8 (minor): differences missing from §11

**Fixed.** §11 now has 14 items: (i) item 3 says that unpaired frames are filed under `vmaf` where §3.1 groups dropped frames with the decoder, and Q04 (a)'s option text names it; (ii) item 5, only observed events have a wall clock, and Q03 (b)'s text says so; (iii) item 6, a ladder without a session has no network events, both acceptance tests included, said in Q10's question, and Q10 gains option (e), the sources survey's scoring-read recorder (sources.md §11, choice 5 (a)), labelled scoring-paced, with its D-007 and D-068 caveat; (iv) item 3 and §5.6 now say that under Q13 (a) Phase 3 has none of the decoder events D-068's limits expect for frames dropped at a switch, and Q13 (a)'s text says so. Changed: §4.6, §5.3, §5.6, §5.7, §11, Q03, Q04, Q10, Q13, §3.6 (Q10 (e)'s files).

### Issue 9 (minor): a splice time against the wrong clock

**Fixed**, by the review's suggestion. A media-PTS anchor, a decoded splice time included, is placed on the carrying variant's own frames (from the demux pass), within the carrying playlist's discontinuity sequence of the anchor's segment, compared in 90 kHz ticks modulo 2^33, and then mapped to the reference through the pair (or the next paired frame); §5.1 says why the playlist's sequence and the carrying variant's PTS are used. WP05 gains a test in which a distorted variant's PTS origin differs from the reference's. Changed: §5.1, §5.5, §1.2 K11, Q14 (a), WP05.

### Issue 10 (minor): gaps between WP04 and WP05

**Fixed**, with a different owner than the review suggests. (i) Every embedded message, the `SCTE35=` of an `EXT-X-CUE-OUT-CONT` included, becomes a `CueInput` (§5.5, WP04, its test), and WP05 tests the decoding of a CUE-OUT-CONT's message. (ii) The pairing into `ad_break` and Q17's end rule have one owner, WP04 (`pair_breaks()`), and WP05 now depends on WP04 and returns its decoded cues to that pairing. The review proposed WP05 as the owner; that would make acceptance test 2, whose assertions include the `ad_break`, wait for the round-2 decoder although D-069's ladders carry plain tags only (testgen.md §2.3). Changed: §3.2, §5.5, Appendix B (`pair_breaks()`, `MessageEvents`, `scte35_events()`), WP04, WP05, Q17's blocks (WP04 only).

### Issue 11 (minor): file ownership and blocking lists

**Fixed.**
- (i) `src/SessionLog.{h,cpp}` is in §3.2's table. Instead of an empty stub in the round-1 wiring, which would present Q13 (b) as answered, the lead wires a conditional package's files in one commit when it starts (P-C); the same holds for WP16's `src/TimelineDecode.{h,cpp}`.
- (ii) Copying is chosen (P-AJ, new): WP12 copies `nice_step` and `axis_label`, and `src/NetInspectorAlign.cpp` is no longer among WP13's files.
- (iii) Q09 (b)'s decode pass moved out of WP06 into WP16's own adapter, so Q09 rightly blocks WP16 only and WP06 stays in round 1.
- (iv) WP09b is blocked by Q15 and Q16 (`--cue-types`, `--bad-crc`), WP09a by Q29 (margins).
- (v) WP08 is blocked by Q02, Q22 and Q25; Q12 (`spans[].scored`) blocks WP09b, which now writes the document's session part.
- `design/tools/render.py` now checks each question's blocks against the packages' lists, and the rounds.

### Issue 12 (minor): the session fetch's edge cases

**Fixed.** (i) A redirected fetch: NetworkLogger starts a row at each request line, which ends the previous row on that handle (`src/NetworkLogger.cpp:595-688`, the comment at `:601`; by reading, not run, R22); one fetched file is one event, its times from the first row's start to the last row's end, its status the last row's, the chain in its payload (Q44 (d)); `tests/HttpFileServer.h` gains a redirect rule owned by WP07 (P-O), and WP07's tests a redirect case. (ii) Each file's rows are copied and the logger cleared after its close, so the 4000-row limit (`src/NetworkLogger.h:194`) evicts nothing (P-N, §5.7). (iii) The retry rule is now Q44 (c), with its consequence in the option text; the agent recommends no retries. (iv) `run_timeline()` and `run_attribute()` set the log level and initialize the network themselves, since they are dispatched before `tools/mirage_vmaf.cpp:155-156` (P-N, §3.4 step 1, §7, WP09a). Changed: §2 (two new rows), §5.7, P-N, P-O, Q44, WP07, WP09a, R22, Appendix B (`RequestRow`, `Request`).

### Issue 13 (minor): what stays unchanged, and two comments

**Fixed.** §3.6 now has a table of every answer that would change a file otherwise unchanged: Q01 (A) or (C), Q08 (b), Q09 (a) and Q10 (e) for the analyzer and report; Q12 (d) for `VMAFLadder`; Q37 (b) for the render loop; Q37 (c) and Q13 (b) for `Demuxer`; Q13 (b) for `MediaSession`; Q40 (b) for `ManifestParser`; Q40 (c) for `DebugHUD`; Q41 (b) for `NetworkLogger`. Both comments go to WP00b, the wiring commit that adds `src/NetworkLogger.cpp` to `mirage_vmaf` under Q10 (a) and Q41 (a). One nuance from reading them: `src/VMAFNet.h:8-9` would become false, while `CMakeLists.txt:229-232` would stay literally true (the ladder scoring still reads playlists itself) but mislead; both are reworded. Changed: §3.6, §3.3, WP00b, Q41 (a).

### Issue 14 (minor): defaults in no question

**Fixed.** Q26 is now a question of two items: (a) the defaults of T, W and N, as before; (b) when `timeline` detects, with the agent recommending detection only when W and N are given (`detection: null` and a line on stderr otherwise). `attribute`'s default `--format` is text, stated in Q31 (a) and §7 (P-AD keeps only the text layout). Q39 is now a question of two items: (a) the panel's settings, as before; (b) the interval a click outside a drop queries, the agent recommending the clicked frame's segment. Changed: §3.4, §7, §8 (`detection: null`), §10, Q26, Q31, Q39, P-AD, WP02, WP09a, WP12, WP13.

### Issue 15 (minor): the panel's loader; corrupt packets at the splice

**Fixed.** (i) The loader with its hash checks and its snapshot handoff is `TimelineLoad`, an ImGui-free unit of WP12 tested in `mirage_tests` (`tests/test_timeline_load.cpp`); the panel only starts it and draws. (ii) Verified in `testgen/logs/61_ad_x264_nodisc_probe_v180.txt`: the flagged packets 73 and 123 carry the PTS of frames 74 and 122 (lines 149, 156, 249, 252), one and three frames before the boundaries at 75 and 125. Where the event sits is Q43 (h): the agent recommends the flagged frame, with the next segment boundary (its first frame, and whether a discontinuity precedes it) in the payload, and says that the ad's drop then lists the second flag only unless a margin is given; WP06 tests it on a splice with unmarked counters. Changed: §2 (a new row), §3.1, §3.2, §5.2, §5.8, §10, Appendix B (`Loader`), WP06, WP12, WP13, Q29 (c).

### Issue 16 (minor): quotations of the work order

**Fixed**, by the review's first suggestion. Every passage is now in this design's words, in `design.md`, its sources and its data (L1); WP15's documents are required to do the same, and its check before each commit includes the quote check (P-AK, new). The owner wasn't asked: D-060 already accepted Phase 2's rendering with no passage quoted as within D-013. Changed: §0, §4.2, §6.4, §8, §9, §10, §11, the Conventions, Q02, Q03, Q22, Q29, Q30, Q39, Q41, WP01, WP15, P-AK.

### Issue 17 (minor): WP00 said to need no answer

**Fixed**, by the review's first suggestion, extended. WP00 became WP00a, blocked by the questions its declarations encode (Q02, Q03, Q04, Q07) and by those that decide which files and targets exist (Q01, Q08, Q31, Q32), which the review didn't list; the session declarations (Q11, Q12) moved to WP00b, round 2, and the UI's wiring (Q36) to WP00c, round 3. "Where Phase 3 stands" no longer says that a package can start before an answer, except WP15's pending-decisions document. Changed: "Where Phase 3 stands", §3.3, §14, P-C, P-AG, R15.

## The lead's items

### L1: quotations

**Fixed.** `design.md`, `design/src/*.md`, `design/*.json`, this file, `design/tools/*` and `design/runs/*` were checked with the quote checker against the work order's translation; each has 0 prose runs and 0 runs of names (`design/runs/quote-check.txt`). Before the revision the checker found prose runs in `design.md`, `part1.md`, `owner_questions.json` and `work_packages.json`, at the passages issue 16 names among others; that check was not kept as a record and those files have since been rewritten, so its counts are not given here. The translation was not copied anywhere, and no record names a shared word.

### L2: rounds and the D-007 mark

**Fixed.** Every question has `round` and `d007` fields; §12 groups its tables and its questions by round, with the counts: 27 in round 1, 9 in round 2, 8 in round 3, also in "Where Phase 3 stands", the Conventions and §0. A question's round is the earliest round of the packages it blocks, packages having rounds of their own as L2 lists them; `render.py` checks both. Round 1's packages are WP00a, WP01, WP02, WP03, WP04, WP06, WP08, WP09a, WP10 and WP11a; round 2's WP00b, WP05, WP07, WP09b and WP11b; round 3's the rest. One placement differs from L2's wording: Q41, a difference to report, is in round 2, because it also decides whether NetworkLogger is compiled into `mirage_vmaf` for the session fetch, which WP00b and WP07 need before they start. `d007` is true for Q01, Q08, Q09, Q10, Q13, Q36 and Q37, and each of their texts says which answer is the approval or the ruling. Q01 to Q42 keep their numbers; Q43 and Q44 are new.

### L3: summaries

**Fixed.** Every question has a `summary`, one sentence saying what the question decides and what the agent recommends; for the questions of items it names each item's recommended default. `render.py` prints it under each question's heading.

### L4: questions short enough to read alone

**Fixed.** Every question is two to four sentences and every option one to three, as counted with a sentence splitter over the data while revising. Detail moved into the design's sections, which each question names: Q02's check to §4.2, Q10's fetch to §5.7, Q22's session rules to §6.3, Q26's and Q39's second items to §7 and §10, Q43's and Q44's context to §5.1, §5.2, §5.5 to §5.7 and §6.3. No option was dropped: Q10 gained option (e); the items of Q43 and Q44 carry every alternative their P-entries named (Q43 (b) 2 to 4 are P-X's three, Q04 (a) names P-AC's), and a few more where the review asked for them (Q43 (h), Q44 (c) and (d)).

### L5: moved P-entries grouped

**Fixed.** The moved P-entries form two questions of items, Q43 (eight items, round 1) and Q44 (five items, round 2), one per round because their packages are; each item has its options and the agent's recommendation and can be accepted or changed on its own. The 28 P-entries kept are choices of process and measurement only (P-A to P-AK, with P-E, P-H, P-K, P-P, P-X, P-AA, P-AB, P-AC and P-AE moved).

### L6: citations to what won't be committed

**Fixed.** The design no longer cites `scte35_vectors.json` (the surveys' list in the introduction, P-M) or any copy of FFmpeg's source, RFC download or third-party extract: FFmpeg is cited by its URL at n8.1.1 (`hls.c#L1916-L1945` in Q08 (c) and `mpegts.c#L463` in P-L, both checked against the copies the scte35 survey fetched; `http.c#L592-L608` in Q10 (b) as sources.md §3.3 cites it, its copy having been removed after that survey's check), RFC 8216 by its URL, Catch2 by its URL at v3.7.1, and the collected SCTE-35 facts by scte35.md's sections. The corrupt-packet facts cite `testgen/logs/61_ad_x264_nodisc_probe_v180.txt`, a record that is committed; reports and layouts in `testgen/reports/` are records too. A Conventions bullet states the rule.

### L7: counts and dates

**Fixed.** "Where Phase 3 stands", §0 item 9, the Conventions and §12 to §15 agree: 44 questions (27, 9, 8), 14 differences, 28 P-entries, 22 work packages, 22 risks; `render.py` now refuses to render if `part1.md` states other counts. Dates are 2026-10-31.

### L8: conventions

**Kept.** Every new number cites its record or source (`design/runs/axis-check.txt`, `design/runs/log-callback-grep.txt`, the review's runs, testgen's log 61, the repository's file:lines, each opened); what wasn't measured says MEASUREMENT PENDING (the ported generator's time, the MPEG-2 cases not run, the seek into an ad restarting at the main origin, the redirect rows); nothing is presented as decided, and every recommendation is labelled as the agent's.

## Code claims added, each checked in `<repo>` at `75cf6e0`

`src/VMAFAlign.h:23-25` (the tolerance) and `:114` (`kForwardJump`); `tests/test_vmaf_ladder.cpp:192-216` (a function-local static ladder); `tests/TestTemp.h:3-7`; `CMakeLists.txt:86` (Catch2 v3.7.1), `:227`, `:229-232`, `:371`; `src/VMAFNet.h:8-9`; `tools/mirage_vmaf.cpp:143`, `:155-156`; `src/NetworkLogger.h:157-160` (`copy_changed_since`, `clear`), `:194` (`kMaxRequests = 4000`); `src/NetworkLogger.cpp:175`, `:182`, `:389`, `:595-688` (`:601`, `:687`); `src/VMAFSegments.h:28`, `:37`; `tests/test_vmaf_report.cpp:104`; `src/NetInspectorAlign.cpp:69-107`, `:109`; `docs/DECISIONS.md` D-032 and P-036 (one temporary directory per process), line 7 (the agent's choices are measurement and process only).
