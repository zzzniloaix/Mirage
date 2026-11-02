# Phase 3 design: the unified event timeline (§3)

The design of the work order's §3 (the unified event timeline, §3.1; attribution queries, §3.2; the UI, §3.3) under the owner's decisions D-068, D-069 and D-070, with D-002, D-007, D-046, D-050 and D-066. It merges two designs written from different angles, `design-library.md` (the timeline as a C++ library with a small API) and `design-evaluation.md` (worked back from what the paper's evaluation must show), both built on the five surveys (`sources.md`, `scte35.md`, `testgen.md`, `schema.md`, `ui.md`), and is revised twice: against its adversarial review (`review-adversarial/review.json`; `design/revision-1.md`) and against a second review (`review-2/review.json`; `design/revision-2.md`), each log saying what changed, issue by issue. **Nothing of Phase 3 is implemented.** This document says how to build it and what the owner has to decide first.

**Conventions**

- `<repo>` is the worktree of `vmaf/phase-3` at `75cf6e0`; a `file:line` without a prefix is in it. `<out>` is the survey directory, `<scratch>` the session's scratch directory. FFmpeg is cited at the tag `n8.1.1` by its public URL, RFCs by theirs.
- **Numbers.** Every number about this code, these streams or a run comes from a cited `file:line` or URL; from a survey file in `<out>`, named next to it; or from a run: of one of the two designs (`design-library/runs/<name>.txt`, `design-evaluation/runs/NN_<name>.txt`), of the review (`review-adversarial/runs/<name>.txt`) or of this design (`design/runs/<name>.txt`, made by `design/tools/run.sh`), each record holding its working directory, exact command, start and end (UTC), full output and exit status (Appendix A). Where there is no source the text says MEASUREMENT PENDING. Parameters a prototype ran with are named as the prototype's; none is a default.
- **Citations.** Only what is committed with the survey is cited for a fact: not its generated media, binaries, copies of FFmpeg's source, third-party downloads or the SCTE-35 messages it collected (Q21). For those, the record or survey section that states the fact is cited, or the public URL.
- **Decisions are the owner's.** Every design choice is an owner question, Q01 to Q44 (§12), asked in three rounds: round 1 holds the answers the packages on the path to §3.2's two acceptance tests need before they start (27 questions); round 2 those that sessions, the session fetch, the decoded SCTE-35 messages and the composite example need (9); round 3 those of the UI, the conditional and later packages and the differences to report (8). Each question has 2 to 5 options with their trade-offs, or items that each have them, and a recommendation labelled as the agent's; a D-007 mark shows where the answer is an approval or a ruling under D-007. P-entries (§13), lettered from P-A to P-AK with nine letters moved into questions by revision 1, are the agent's choices of process and measurement only: how something is built, tested or measured, with no effect on what an output says by default. They await the owner's confirmation and are numbered from P-081 when recorded in `docs/DECISIONS.md`. Nothing here is decided. The same questions, packages, P-entries and risks are in `design/owner_questions.json`, `design/work_packages.json`, `design/p_entries.json` and `design/risks.json`, from which §12 to §15 are rendered (`design/tools/render.py`).
- **No quotation of the work order** (D-013; D-060 accepted Phase 2's rendering of it in its own words). Its requirements are given here in this design's words; field, tag, option and component names stay as names. `design/runs/quote-check.txt` records the check of every file of the design against the translation, which names no shared words.
- "The work order" is the owner's document as translated in the task. D-0xx and P-0xx are entries of `docs/DECISIONS.md`. Names of files, types and options are proposals; the owner may rename them.

---

## Where Phase 3 stands (2026-10-31)

- **Done.** The five surveys of §3's sources, SCTE-35, the synthetic test content, the report against §7, and the player's UI. Two designs, each with prototypes and saved runs (`design-library/`: 14 records; `design-evaluation/`: 12 runs). This merged design, which resolves their conflicts; an adversarial review of it and revision 1 against that review (`design/revision-1.md`); a second review (`review-2/`) and revision 2 against it (`design/revision-2.md`). The design's own records are in `design/runs/` (Appendix A).
- **Not done.** Any Phase 3 code. Nothing in `<repo>` was changed: `vmaf/phase-3` is at `75cf6e0` and its status is clean (read-only git commands at the start and at the end of this work).
- **Next.** The owner's answers to round 1's 27 questions (§12), which every package on the path to the two acceptance tests waits for, the wiring package WP00a included; then round 2's 9 and round 3's 8, 44 questions in all. WP15a can start now: it records the questions, the differences and the P-entries as pending (`docs/PHASE3_DECISIONS_PENDING.md`, `docs/DECISIONS.md`) and commits §10 as the UI's written D-007 plan (`docs/plans/timeline-ui.md`). Every other package names the questions that block it (§14).

## 0. Summary

1. **What Phase 3 must make possible** (from design-evaluation §1): attribution that is right on known causes, checked against ground truth the test content's generator writes (E1, D-069's two ladders); the work order's example answer, a drop that overlapped an SCTE-35 cue-out, a switch between two numbered rungs and a segment request of 2.3 s, from a scripted session whose HTTP timing is measured, not declared (E2); later, D-011's real ladders (E3); reproduction from one command (E4); costs on real ladders (E5, MEASUREMENT PENDING).
2. **Architecture** (from design-library §2): a core of plain C++20 modules (namespace `timeline`, no libavformat, libavcodec, libvmaf, GL or ImGui) for the model, the merge, the overlap query and detection; FFmpeg adapters at its edge for the demux pass and the session fetch; two thin `mirage_vmaf` subcommands and the player's panel on top. The ladder report is read as §2 writes it (D-068: scored once); nothing in the scorer changes unless the owner picks an option that changes it (§3.6).
3. **One time base** (§4). Every event is placed at a reference frame, and intervals are compared on reference frames, exact integers. Every event also keeps its media key, the reference's discontinuity sequence and PTS, which raw PTS alone can't be: 50 PTS values each name two frames in testgen's ad ladder (design-evaluation run 09). The seconds in which offsets are printed are Q02; the agent recommends the reference's playlist time, with the frame index times the frame duration where the reference has no segment map, the two compared at every frame wherever both exist. They agree on the four ladders checked; after a simulated missing frame or change of rate the frame index's seconds were off and the playlist time's weren't, and after an EXTINF simulated a frame too long the reverse (`design/runs/seconds-check.txt`). Which events an interval lists doesn't depend on that choice.
4. **Sources** (§5). Per-pair VMAF curves; each scored media playlist read and parsed for its segments and tags; a demux pass that places every scored frame in its segment by byte position, checked against the report's PTS (200 of 200 frames on each of 15 playlists, design-evaluation runs 03 and 04); D-070's SCTE-35 tags with a splice_info_section decoder, and one cue interface for every carrier, D-070's DASH and in-segment interfaces included; scripted sessions as segment ranges (D-068); their HTTP timing from a fetch timed by the player's own `NetworkLogger` (a prototype measured 2.313 s for a segment served 2.3 s late, design-evaluation run 06); keyframes and corrupt packets from the demux pass; decode errors only if the owner allows the analyzer to record them (Q09).
5. **Queries and detection** (§6). `timeline::attribute()` returns the events meeting a half-open interval of a pair's curve or a session's, by §3.1's six sources, with offsets in seconds and in frames; `timeline::detect()` finds drops below a trailing baseline. A session's drops are its pairs' drops clipped to the spans that played them (Q22). With the prototype's parameters the two acceptance drops were found exactly, with no drop in their controls, on a grid of T from 3 to 40 and N from 1 to 24 (design-evaluation run 05), so the tests pin their own parameters and don't wait for the defaults (Q26).
6. **Output** (§8). A separate, versioned timeline document per build, naming its ladder report by two hashes; scores are not copied (Q01, Q04 to Q06).
7. **Tests** (§9). A unit-test file per module; the two acceptance tests of §3.2 in one test case, on ladders made once per process from testgen's prototype (P-Q); the composite example on a scripted session served with a delayed segment; an eval harness that reproduces it all from one command.
8. **UI** (§10). ui.md's panel, reading the document through an ImGui-free loader tested in ctest and calling the same query API; by the agent's reading nothing recommended touches the render or decode path; the measured limit (no seek reached an ad whose timestamps restart at 0.040 s) is Q37.
9. **For the owner.** 44 questions in three rounds of 27, 9 and 8 (§12), 14 differences from the work order (§11), 28 P-entries (§13), 24 work packages (§14), 22 risks (§15).

---

## 1. How the two designs were merged

### 1.1 What each contributes

- **From design-library**: the layered structure (a core that takes and returns plain structs, adapters that are the only code opening inputs, consumers with no logic of their own), which is how `HlsAlign` and `VMAFAlign` are already built and unit-tested; the placement rules (every event at a reference frame); the per-module files and unit tests; the API sketch; a document that writes each event once and refers to it by id; the scores digest that survives redaction.
- **From design-evaluation**: the evaluation map (E1 to E5) as the measure of what Phase 3 must deliver; the critical path to the acceptance tests; tests that don't depend on open choices, with detection parameters pinned inside a measured region; the measured session fetch; a wiring package that owns `CMakeLists.txt`; segment-range session plans; strict overlap with optional margins; both BANDWIDTH ranks; the eval harness; the conditional and later packages.
- **New in this merge**: placement and comparison on reference frames in every option but raw PTS, which separates which events an interval lists from the seconds its offsets are printed in, and narrows the axis conflict to the seconds; a session's drops as its pairs' drops clipped to the spans that played them; this design's runs (`design/runs/`), which settle conflicts with numbers.
- **New in revision 1**: the questions in rounds, with the package split that makes the acceptance path wait for round 1 only (WP00a, WP09a, WP11a); the P-entries that changed default outputs moved into two questions of items (Q43, Q44); the axis check made sound (`design/runs/axis-check.txt`); D-070's interfaces declared (§5.5); one test process for the acceptance cases (P-Q).
- **New in revision 2**: the axis check compares (c)'s and (d)'s seconds at every frame, since revision 1's check of steps missed a change of rate (`review-2/runs/rate-change-check.txt`; `design/runs/seconds-check.txt`), and the agent's recommendation for Q02 moved from (d) to (c) with (d) as its fallback; a session's curve and scope given to WP07, after its types exist; WP09b split so that the composite example doesn't wait for the SCTE-35 decoder (WP09c); WP15 split so that its first part starts now (WP15a); the fetch's pacing and playlist fetches moved from P-N into Q44; Q41 narrowed to the component's name; every conditional option given a package (§3.6), and the Blocked-by lists completed and checked by `design/tools/render.py`.

### 1.2 Conflicts, and how each is resolved

| # | Topic | design-library | design-evaluation | This design, and why |
|---|---|---|---|---|
| K1 | Structure | a library first, consumers thin | the smallest system for the evaluation | library's layers, evaluation's experiments and critical path: the layers are the codebase's style, and the experiments say what each layer must deliver |
| K2 | Axis | (d) frame position | (c) playlist time, falling back to continuous PTS | both already keep `ref_index` on every event; this design places and compares on it in every option but raw PTS, so the conflict is only about the seconds of offsets (Q02). Recommended since revision 2: (c), with (d) where the reference has no segment map, the two compared at every frame (§4.2): checking (d) needs (c)'s seconds anyway, and (c) stays right through a missing frame or a change of rate, after which (d) was 0.040 s and 0.083 to 1.167 s off (simulated, `design/runs/seconds-check.txt`); (d) alone is a close second. The first version's check flagged 75 of 200 frames of D-069's ad ladder where both are right (`design/runs/axis-check.txt`; review-adversarial run `axis-d-check`), and revision 1's missed an ad re-timed at 24, 30 and 50 fps (`review-2/runs/rate-change-check.txt`); revision 2's warns on none of the four ladders and in every simulated case |
| K3 | Frames to segments | byte position, 200 of 200 on 4 rungs | byte position with a PTS check, 200 of 200 on 15 playlists | agreement; both runs cited, the PTS check kept (P-G); what a mismatch costs is Q43 (c) |
| K4 | Decode errors | recorded by the analyzer, if D-007 allows | not in Phase 3 | recorded by the analyzer if the owner rules it outside D-007 or approves its plan, in a conditional package off the critical path (Q09, WP16): only the scoring decode's errors explain the scores, at no decode cost, and neither acceptance test waits for it |
| K5 | Session plan | JSON, spans in seconds | JSON, spans as segment ranges | segment ranges (Q11): exact, no snapping rule, the fetch list follows directly, HLS switches at segment boundaries; library's idea of one format for plans and the later player log kept |
| K6 | Rung names | entry and URI, ascending rank | entry and URI, both ranks | both ranks (Q07): the work order's example numbers rungs without saying from which end, and printing both imposes no reading |
| K7 | Detection on sessions | the session's curve with its own baseline | each point against the played variant's own baseline | the pairs' drops clipped to the played spans (Q22 (c)), which is evaluation's semantics made exact. Both find the composite drop (frames 75 to 124, `design/runs/detection-semantics-ad.txt`); under library's semantics a switch from v270 to v180 alone is a drop at T = 3, 4 and 5 and none at T = 5.5, 6, 6.5, 7 and 8 (`review-adversarial/runs/switch-t-sweep.txt`), the two rungs' medians, 97.934 and 91.140 (testgen.md §3.2), being little more than T apart |
| K8 | First pairs of libvmaf contexts | left out of detection | no special case | no special case (Q25). This design's run: left out, the ad's drop starts at frame 76 and the `EXT-X-CUE-OUT`'s frame 75 falls before it, so the half-open query no longer lists the cue; with no special case, the drop starts at 75 with the cue inside (`design/runs/detection-semantics-ad.txt`) |
| K9 | Defaults | T = 6; W and N unmeasured | T = 6; W and N required until measured | evaluation's form (Q26 (a)): an option with no default is required, and detection runs only when it is given (Q26 (b)) |
| K10 | Overlap at a drop's end | points exactly at the end listed, flagged | strict half-open, optional margins | strict half-open with optional margins, default none (Q29): the cue-in at the end is still shown by the `ad_break` span's end offset (design-evaluation run 10), and listing end points also lists the next segment and its keyframe at every boundary |
| K11 | SCTE-35 anchor | every anchor recorded, a configurable precedence, position first | position; a separate `splice_point` event | position, every other anchor in the payload, a `splice_point` event placed on the carrying variant's frames where a splice time exists, a warning when an anchor falls outside the tag's segment (Q14): nothing is lost, and no precedence option needs defending |
| K12 | SCTE-35 vectors to commit | Apache-2.0 (scte35-go, its copies of SCTE's samples included) and self-built | self-made only | self-made only until the owner settles the licences (Q21): scte35-go's copies of SCTE's samples are SCTE's content whatever scte35-go's own licence; a local cross-check stays possible (P-M) |
| K13 | CRC | a constexpr table in a core free of FFmpeg | `av_crc` | `av_crc` and `av_base64_decode` (P-L): libavutil is linked by all three targets, and `VMAFAlign` already sets the precedent of a pure module on libavutil (`src/VMAFAlign.h:9-11`) |
| K14 | Exact time | a `Rational` type of its own with 128-bit intermediates | ticks in `int64` | frames and ticks in `int64`, seconds through libavutil's rationals (P-A): less code, the aligner's precedent |
| K15 | Unpaired frames | `decoder/pairing_failure`, at the next paired frame | `vmaf/unpaired_frame`, at the nearest reference frame | source `vmaf` (they are the aligner's facts, whatever caused them; Q04 (a)), placed by index at the next paired frame (Q43 (d)): nearest by PTS is ambiguous where PTS repeat. §3.1 groups dropped frames with the decoder: a difference (§11, item 3) |
| K16 | Drops | attributions only | also `vmaf/drop` events | attributions only: as events they would list themselves in their own and other curves' attributions |
| K17 | Playlist requests | a point at the first span that plays the variant | no media span | a point at the first span that plays it (Q44 (b)): a request off the axis can never be listed |
| K18 | CLI parser | `vmafcli`'s parser made reusable | an option table of its own | a table of its own (P-V): `vmafcli`'s `parse()` and `read_settings()` are written against its own `kOptions` and `Args` (`src/VMAFCli.cpp:195-257, 273-344`), and making them generic changes tested code |
| K19 | Build | an OBJECT library; each package adds its own CMake block | a skeleton owns every CMake line | wiring packages own every CMake line, one per round (WP00a, WP00b, WP00c), each after the one before, with per-target source lists as the repository has them (P-C) |
| K20 | Encoder | libx264, the tests skipped without it | libx264, else MPEG-2 | the fallback (Q33): an acceptance test that skips shows nothing. With MPEG-2 the pinned parameters passed on the degraded ladder and on the ad from the main content's origin (design-evaluation run 05); the ad from 1 s ran with libx264 only and the continuous ad not at all, so those two with MPEG-2 are MEASUREMENT PENDING |
| K21 | The ad's timestamps | two cases (main origin, 1 s) | three (plus continuous) | three (Q34): the continuous case is the only one in which ui.md's probe reached the ad by a seek (ui.md §2.4) |
| K22 | Document layout | segments as events; attributions grouped by source in an object | segment tables in the variants; `groups` | segments as events only, each written once; variants carry their segment map's status; `overlapping_events` an array of groups in §3.1's order, which keeps §7's name and type (Q04) |
| K23 | Schema check | key tests | a question about a validator | a P-entry (P-AI): a subset validator in the tests adds no dependency |
| K24 | Media playlists in the report | read at build time | a question whether to record their SHA-256s in the report | read and hashed at build time, the report unchanged (P-D); recording them in the report stays the owner's alternative |
| K25 | Question set | 33 questions | 40 questions | 44 in three rounds, without duplicates (since revision 2 the linking of `NetworkLogger` into `mirage_vmaf` is Q10 (a)'s alone and Q41 the name only): each standalone, a question wherever the work order or the owner's rules reserve the choice or an output changes by default, P-entries for process and measurement only |
| K26 | Names | `TimelineDoc`, `PlaylistEvents`, `SessionPlan`, `TimelineFetch`, `SyntheticLadder` | `TimelineReport`, `HlsEvents`, `Session`, `SessionFetch`, `TestLadders` | `TimelineDoc` (no confusion with `VMAFReport`), `PlaylistEvents`, `SessionPlan`, `SessionFetch`, `SyntheticLadder`; proposals only |

---

## 2. What the design rests on

| Fact | Where |
|---|---|
| A ladder report's frame carries `index`, `ref_index`, `dis_index`, `ref_pts`, `dis_pts` (raw best-effort timestamps in seconds) and `vmaf`; each input's discontinuity sequences are in `alignment.*.sequences` (D-057), its libvmaf contexts in `alignment.vmaf_contexts` | `src/VMAFReport.cpp:193-201`; sources.md §2; `testgen/reports/ad-x264.json` |
| Each reference frame has one `ref_pts` in every pair that scored it (36000 frames of the advanced sample); a 30 fps variant against a 60 fps reference is scored at every other reference frame | schema.md §3.1 (schema-survey run 11) |
| A report sequence starts only at a backward step or a forward jump of more than 10 s, so a smaller jump stays inside one sequence; on D-069's ad ladder the report has two sequences where the playlist has three | `src/VMAFAlign.h:114`; testgen.md §3.5 |
| `mirage_vmaf` reads only the master; FFmpeg's hls demuxer skips every timeline tag with a VERBOSE `Skip` line; nothing headless parses media playlists | sources.md §1, §4.1 ([hls.c#L989-L991](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/hls.c#L989-L991)) |
| `hls::parse_media_timeline` is pure and gives each segment's `seq`, `start`, `duration`, `uri`, `range`, `disc_seq`, `pdt` and the line of its URI; it ignores cue tags | `src/HlsAlign.h:33-75`; sources.md §4.3 |
| `ManifestParser` takes CUE-OUT-CONT for a cue-out, drops OATCLS, leaves DATERANGE unparsed and doesn't count discontinuity sequences | sources.md §4.2; scte35.md §6 |
| No code decodes a splice_info_section; of 79 published messages, 61 decode with a valid CRC and no disagreement with their sources, 9 have a bad CRC_32 and 9 don't decode | scte35.md §5 |
| No AVOption exports the HTTP status in 8.1.1; only `av_log` lines or a failed open's `AVERROR_HTTP_*` give it; the analyzer's reads are paced by scoring | sources.md §3.3 ([http.c#L180-L206](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/http.c#L180-L206)) |
| `NetworkLogger` records status, request sent, first byte, end and bytes, and has a manual tracking API for plain `avio` opens, which `ManifestParser` and `NetProbe` use; its `uninstall()` restores FFmpeg's default callback | `src/NetworkLogger.h:118-125`; `src/ManifestParser.cpp:121-157`; `src/NetProbe.cpp:120-141`; `src/NetworkLogger.cpp:179-185` |
| A request line on a handle ends that handle's previous row and starts a new one, a redirect's included (by reading, not run); the logger keeps at most 4000 rows, the oldest evicted | `src/NetworkLogger.cpp:595-688` (the comment at `:601`, the new row at `:687`); `src/NetworkLogger.h:194`; `src/NetworkLogger.cpp:389` |
| Only `NetworkLogger` installs an FFmpeg log callback in `src/`, `tools/` and `tests/`; `mirage_vmaf` sets the log level and initializes the network after parsing its options | `src/NetworkLogger.cpp:175, 182`; `tools/mirage_vmaf.cpp:155-156` (`design/runs/log-callback-grep.txt`) |
| The scorer's hls demuxer tries a segment up to 10 more times before going on without it | `src/VMAFSegments.h:28, 37` |
| The player has no ABR; manual switches are logged as free text without the rung | sources.md §6 |
| A damaged segment scored to a complete report with no trace of its decode errors: 6 frames with `decode_error_flags` 4, 17 decoder errors, scores of 0 to 34.6 on those frames | sources.md §7 |
| FFmpeg's log callback is process-global: one thread's replacement redirected another's lines | sources.md §9 (run `logprobe-concurrent`) |
| Frames to segments by byte position: 40 of 40 (the sources survey's fixture), 200 of 200 packets on each of 4 rungs, 200 of 200 frames on each of 15 playlists with every frame's PTS agreeing; by PTS against EXTINF: 37 or 39 of 40, and 145 or 193 of 200 | sources.md §2; design-library runs `map-by-pos-*`; design-evaluation runs 03, 04 |
| On testgen's ad ladder (ad from the main content's origin), 50 PTS values each name two reference frames; on continuous PTS the 2 s break spans 5.000 s; on playlist time and on frame position it spans 2.000 s | design-evaluation run 09; design-library run `axis-ad` |
| On the four ladders checked, frame position and playlist time give the same seconds for every scored reference frame; the first version's check of frame position, from the report alone, flagged 75 of 200 frames of each ad ladder whose ad restarts at the main content's origin (x264 and MPEG-2), none of the other two | `design/runs/axis-check.txt`; `review-adversarial/runs/axis-d-check.txt` |
| Simulated: after one missing reference frame, frame position is 0.040 s early; after the ad's frames re-timed at 24, 30, 50 or 60 fps with their EXTINF following, it ends 0.083, 0.333, 1.000 or 1.167 s off the playlist time; after one EXTINF made a frame too long, the playlist time is 0.040 s late. Revision 2's check (§4.2) warns in each case and on none of the four ladders; revision 1's check of steps missed the ads at 24, 30 and 50 fps and the long EXTINF, and a comparison at segment starts only missed a frame missing from the last segment | `design/runs/seconds-check.txt`; `review-2/runs/rate-change-check.txt` |
| RFC 8216 asks for EXTINF durations accurate enough that their sums don't drift, whole seconds below compatibility version 3, and an `EXT-X-DISCONTINUITY` where encoding parameters change | [RFC 8216 §4.3.2.1](https://www.rfc-editor.org/rfc/rfc8216#section-4.3.2.1), [§4.3.2.3](https://www.rfc-editor.org/rfc/rfc8216#section-4.3.2.3) |
| Seconds compared as doubles listed boundary events inconsistently (an interval end of 5.000000000000001); ticks did not | design-evaluation runs 07, 08; design-library runs `axis-ad-float`, `axis-degraded-float` |
| On the four acceptance curves, a trailing median of the last W non-low scores found both drops exactly, with no drop in either control, at every point of a grid of T in {3, 4, 5, 6, 7, 8, 10, 12, 15, 20, 25, 30, 40} and N in {1, 2, 3, 4, 5, 8, 12, 16, 20, 24}, for W = 10, 25, 50 and 100 | design-evaluation run 05 |
| On the switch-down plan, detection on the session's own curve finds a drop at T = 3, 4 and 5 and none at T = 5.5, 6, 6.5, 7 and 8 (W = 50, N = 12) | `review-adversarial/runs/switch-t-sweep.txt` |
| A sequential fetch measured 2313133 µs, 12032 bytes, `HTTP/1.0 200 OK`, for a segment a local server answered 2.3 s late; the other segments took 578 to 3569 µs | design-evaluation run 06 |
| End to end, the prototype returned segment 4 of `v270` for the degraded drop, the `EXT-X-CUE-OUT` for the ad drop, and for a scripted session the cue-out, the switch from entry 1 to entry 2 and the 2.313 s request, each at +0.000 s; its documents were 8577 to 14470 bytes with 32 to 44 events | design-evaluation runs 10, 11 |
| Excluding the first pair of each libvmaf context moves the ad's drop to frame 76, before which the cue-out falls; the session semantics of Q22 (b) and (c) both find the composite drop, with baselines 97.681 and 91.692 | `design/runs/detection-semantics-ad.txt` |
| No seek reached the frames of an ad whose timestamps restart at 0.040 s, with FFmpeg 8.1.1's hls demuxer and the player's clamp; an ad that restarts at the main content's origin, as D-069's does, was not probed | ui.md §2.4 (`experiments/hls-seek-probe/run-output.txt`) |
| With continuity counters left unmarked at the splice, FFmpeg flagged the packets of frames 74 and 122 corrupt, one and three frames before the boundaries at 75 and 125 | testgen.md §3.6, item 2; `testgen/logs/61_ad_x264_nodisc_probe_v180.txt:149, 156, 249, 252` |
| ctest runs each Catch2 test case in its own process: all 359 tests of the debug build run `mirage_tests` with one test case's name; the tests' temporary directory is per process and removed at exit (D-032, P-036) | `review-adversarial/runs/ctest-registration.txt`; `CMakeLists.txt:371`; `tests/TestTemp.h:3-7` |
| Per-frame lines are 99.91% of a full ladder report (27969051 bytes of text); rescoring the full advanced ladder took 1882.86 s | schema.md §3.3 |
| `src/` has no JSON reader; the report has no `schema_version`, `tool_version` or `git_commit`; the build has no version | schema.md §1.1, §1.7; `CMakeLists.txt:2` |
| testgen's ladders: degraded `v270` segment 4 mean 41.239 against 97.134 to 98.231 elsewhere, its frames more than 6 below the pair's median exactly 100 to 124; the ad's segments in `v180` 27.409 and 27.380 against 90.966 to 91.978, its frames more than 6 below the median 75 to 124 and frame 0, the first of the pair's libvmaf context (D-037); controls without a drop; 29 of 29 files identical in two generations; generation 2.022 to 2.313 s, 16.941 s unoptimized; scoring 0.457 to 0.515 s | testgen.md §3 |

---

## 3. Architecture

### 3.1 Layers

```
  consumers (thin: parse, call, write or draw)
    mirage_vmaf timeline / attribute  (src/TimelineCli.*)       player: TimelinePanel (src/TimelinePanel.*)
              │                                                          │
  adapters (libavformat; mirage_vmaf, mirage_tests)          UI math (ImGui-free; mirage, mirage_tests)
    TimelineMedia: playlists, demux pass, frame→segment         TimelineLoad:  document and report loaded, checked
    SessionFetch:  the session's fetch through NetworkLogger    TimelineView:  view, envelopes, hit test
                                                                TimelineMatch: matching, playhead, seek decision
              │                                                          │
  core (C++20 and libavutil only; mirage_vmaf, mirage_tests, mirage) ◄───┘
    TimelineModel · TimelineAxis · TimelineQuery · TimelineDetect · TimelineBuild · TimelineDoc
    JsonRead · LadderReportRead · PlaylistEvents · Scte35 · Scte35Events · SessionPlan
    (it calls hls::parse_media_timeline and json::quote/number, both already in the tree)
```

- **Core.** Plain structs and text in, plain structs and text out; no libavformat, libavcodec, libvmaf, GL or ImGui header; libavutil allowed for rationals, CRC and base64, as `VMAFAlign` uses it (P-B). Everything that changes an attribution lives here, so the CLI and the panel can't disagree (ui.md §2.2 asks for the same). Nothing in it reads a clock, the environment or a file.
- **Adapters.** The only code that opens inputs. Each returns a core struct (`VariantMedia`, `Request` rows), so a test can build those by hand and test the core without media.
- **Consumers.** Option parsing, calls, output. No logic of their own beyond formatting; the panel's loading, checks and handoff are in `TimelineLoad`, so ctest covers them.

### 3.2 Files

New files (names are proposals):

| File | Layer | Contents | Package |
|---|---|---|---|
| `src/TimelineModel.{h,cpp}` | core | `Source`, `Event`, `MediaKey`, `Variant`, `Curve`, `CurveRef`, `Span`, `Session`, `Timeline`; canonical order; the overlap index on reference frames; `curve_points()`, a curve's points, a session's derived from the pairs' | WP01, a session's curve by WP07 (declarations by WP00a and WP00b) |
| `src/TimelineAxis.{h,cpp}` | core | media keys per reference frame; the seconds of Q02 and their check (§4.2); seconds to frames | WP01 |
| `src/TimelineQuery.{h,cpp}` | core | `attribute()`: overlap, margins, scope, groups, offsets | WP01 for a pair's curve, WP07 for a session's |
| `src/TimelineDetect.{h,cpp}` | core | `detect()` on one curve | WP02 |
| `src/JsonRead.{h,cpp}` | core | the JSON reader (Q32) | WP03 |
| `src/LadderReportRead.{h,cpp}` | core | a ladder report into `LadderScores`; the scores digest's canonical text (Q06) | WP03 |
| `src/PlaylistEvents.{h,cpp}` | core | a media playlist's segments and tags; the plain cue tags into cue events; every embedded message as a `CueInput`; the pairing of cues into `ad_break` (Q17) | WP04 |
| `src/Scte35.{h,cpp}` | core | the splice_info_section decoder | WP05 |
| `src/Scte35Events.{h,cpp}` | core | `CueInput`s decoded and placed, by every anchor kind (Q14 to Q16, Q44 (e)) | WP05 |
| `src/TimelineMedia.{h,cpp}` | adapter | playlists read and hashed; the demux pass; frame→segment maps; keyframes; corrupt packets | WP06 |
| `src/SessionPlan.{h,cpp}` | core | the plan (Q11); spans; switch events; the session's drops (Q22) | WP07 |
| `src/SessionFetch.{h,cpp}` | adapter | the session fetch through `NetworkLogger` (Q10, Q44 (c), (d), (f), (g)) | WP07 |
| `src/TimelineDoc.{h,cpp}` | core | the document's writer and reader | WP08, then WP09b for the session part |
| `src/TimelineBuild.{h,cpp}` | core | the merge into a `Timeline` | WP09a, then WP09b (sessions), then WP09c (decoded messages); WP16 under its options |
| `src/TimelineCli.{h,cpp}` | consumer | the subcommands, their option tables and config files | WP09a, then WP09b, then WP09c |
| `src/TimelineLoad.{h,cpp}`, `src/TimelineView.{h,cpp}`, `src/TimelineMatch.{h,cpp}` | UI math | the panel's loader with its hash checks and snapshot handoff; ui.md §2.5's view math, envelopes, hit test; matching, playhead, seek decision, the click's interval (Q39) | WP12 |
| `src/TimelinePanel.{h,cpp}` | UI | the ImGui panel (player only) | WP13 |
| `src/TimelineDecode.{h,cpp}` | adapter | only under Q09 (b): the decode pass | WP16 (conditional) |
| `src/SessionLog.{h,cpp}` | player | only under Q13 (b): the player's session log | WP17 (conditional) |
| `tests/SyntheticLadder.{h,cpp}` | test | D-069's generator, ported from `testgen/src/testgen.cpp` | WP10 |
| `tests/test_*.cpp` (§9.1) | test | a file per module | WP01 to WP12 |
| `tests/test_attribution_acceptance.cpp`, `tests/test_attribution_session.cpp` | test | §3.2's two acceptance tests; the composite example | WP11a; WP11b |
| `docs/schema/mirage-timeline-1.schema.json`, `docs/TIMELINE_FORMAT.md` | docs | the schema file and the format's description | WP08, then WP09b |
| `docs/PHASE3_DECISIONS_PENDING.md`, `docs/plans/timeline-ui.md` | docs | the pending questions, differences and P-entries; the UI's D-007 plan | WP15a |
| `docs/PHASE3_STATUS.md` | docs | the status, item by item | WP15b |
| `eval/timeline/*` | eval | the one-command reproduction | WP14 |

A file has one owner at a time; where two packages own it in turn, the second depends on the first and starts after it has merged (`design/tools/render.py` checks every such pair). Shared files and their owners: `CMakeLists.txt` (the wiring packages WP00a, WP00b and WP00c in turn, each depending on the one before, and the lead's wiring commit when a conditional package starts, made while no wiring package is open; P-C); `src/TimelineModel.cpp` and `src/TimelineQuery.cpp` (WP01, then WP07 for a session's curve and scope); `src/TimelineBuild.{h,cpp}`, `src/TimelineCli.{h,cpp}` and their tests (WP09a, WP09b, WP09c in turn, then WP16 for `src/TimelineBuild.cpp`); `src/TimelineDoc.{h,cpp}`, its test and the two format documents (WP08, then WP09b); `tools/mirage_vmaf.cpp` and `src/VMAFCli.cpp` (WP09a); `tests/HttpFileServer.h` (WP07); `src/VMAFNet.h`, a comment only (WP00b, under Q10 (a)); `src/NetworkLogger.{h,cpp}` and the files that name it (WP00b, only under Q41 (b), before any later owner of those files); `src/main.cpp`, `src/PlayerUI.{h,cpp}`, and under the answers §3.6 names `src/Demuxer.{h,cpp}`, `src/ManifestParser.{h,cpp}` and `src/DebugHUD.{h,cpp}` (WP13, then WP17 for `src/main.cpp` and `src/Demuxer.cpp`); `src/VMAFAnalyzer.{h,cpp}`, `src/VMAFReport.{h,cpp}`, `src/VMAFSegments.h`, `src/VMAFLadder.{h,cpp}` and their tests (WP16, conditional; `src/VMAFReport.*` WP08's first under Q01 (A)); `src/LadderReportRead.cpp` (WP03, then WP16); `docs/DECISIONS.md` (WP15a, then WP15b, with the lead's records of each round's answers between); `docs/AI_CONTRIBUTIONS.md`, `docs/PAPER_METHOD_NOTES.md` and, under Q42 (b), `docs/VMAF_GAP_REPORT.md` (WP15b). `src/NetInspectorAlign.cpp` changes only under Q41 (b): WP12 copies its two axis helpers (P-AJ). What stays unchanged unless an answer says otherwise is §3.6.

### 3.3 Build

- Per-target source lists, as the repository has them (P-C), added in one wiring commit per round. Round 1 (WP00a): `mirage_vmaf` gains the core, `TimelineMedia`, `TimelineCli` and `src/HlsAlign.cpp`, which needs nothing beyond the C++ standard library (`src/HlsAlign.h:12`); `mirage_tests` gains the same and their tests. Round 2 (WP00b, after WP00a): `SessionFetch` under Q10 (a) or (b) and, under Q10 (a), `src/NetworkLogger.cpp` in `mirage_vmaf` (it needs only FFmpeg, `src/NetworkLogger.cpp:15-20`), with the two comments that say `mirage_vmaf` has no network logger reworded (`CMakeLists.txt:229-232`; `src/VMAFNet.h:8-9`). Round 3 (WP00c, under Q36, after WP00b): the player gains the core, the UI math, the panel and `src/JsonText.cpp`, which it doesn't compile today (`CMakeLists.txt:150-194`); `mirage_tests` gains the UI math, not the panel, since it links no ImGui (D-031).
- `tests/SyntheticLadder.cpp` is compiled with optimization in every build type, as `src/VMAFPool.cpp` gets its own compile options (`CMakeLists.txt:227`; P-Q).
- The headless presets (`MIRAGE_HEADLESS`) build everything but the panel.

### 3.4 What `mirage_vmaf timeline` does

1. **Sets up FFmpeg** as `mirage_vmaf` does for scoring, the log level and the network (P-N): the subcommands are dispatched before `tools/mirage_vmaf.cpp:155-156`.
2. **Reads the ladder report** (`LadderReportRead`): settings, master entries, the reference, each complete pair's frames, failures, sequences and libvmaf contexts. Computes the report's SHA-256 and its scores digest (Q06). Which layouts and reports it accepts is Q43 (b).
3. **Reads each scored media playlist** (`TimelineMedia`): the reference's and each pair's distorted playlist, through `vmafladder::read_master` with the analyzer's network limits, recording its URL, location and SHA-256 (P-D); a live playlist as Q43 (a) says. Parses it (`PlaylistEvents`): segments, tags, cues and their messages.
4. **Runs the demux pass** (`TimelineMedia`, Q08 (a)): each playlist opened as the analyzer opens it, its packets read without decoding, mapped to segments by byte position and to the report's frames, checked against the report's PTS (P-F, P-G; a mismatch as Q43 (c) says).
5. **Builds the timeline** (`TimelineBuild`, `TimelineAxis`): reference frames with their media keys and seconds (Q02), the pairs' curves, every source's events placed by §5.1's rules, the cues' messages decoded (`Scte35`, `Scte35Events`, from round 2).
6. **With a session plan** (`SessionPlan`, Q11; from round 2): spans, switch events and the session's curve; then **fetches the session's files** (`SessionFetch`, Q10, P-N, Q44 (f) and (g)) and adds the requests as network events.
7. **Detects drops and attributes them** (`TimelineDetect`, `TimelineQuery`; Q22 to Q30), when W and N are given (Q26 (b)).
8. **Writes the document** (`TimelineDoc`; Q01, Q04 to Q07) through a temporary file renamed over `--out` (P-W).

### 3.5 What `mirage_vmaf attribute` does

It reads a document and the ladder report it names, checking the report's SHA-256, or for a committed, redacted copy its scores digest (Q06), rebuilds the `Timeline` from the document's events and the report's curves, and answers an interval for a pair's curve or a session's: one given in seconds or reference frames, a detected drop by its number, or detection again with other parameters (`--auto`), without reading any media.

### 3.6 What doesn't change

`mirage_vmaf --ref/--dis` and `--ladder` keep their meaning and their reports' layout: today an argument that isn't an option is an error (`src/VMAFCli.cpp:288`), so a first-word subcommand changes no existing command line. The other files stay as they are unless an answer changes them; every answer that would, with the package that would make the change (each listed among that package's files in §14, and each such question among its blockers):

| Files | Changed only under | By |
|---|---|---|
| `src/VMAFAnalyzer.*`, `tests/test_vmaf_analyzer.cpp` | Q01 (C), Q08 (b), Q09 (a), Q10 (e): what the analyzer records | WP16 |
| `src/VMAFReport.*`, `tests/test_vmaf_report.cpp` | Q01 (A), the timeline in the report; Q01 (C), Q08 (b), Q09 (a), Q10 (e), the analyzer's records in it | WP08 under Q01 (A), WP16 under the others |
| `src/VMAFSegments.h`, `tests/test_vmaf_segments.cpp` | Q10 (e), the scoring reads' recorder | WP16 |
| `src/VMAFLadder.*`, `tests/test_vmaf_ladder.cpp` | Q12 (d), the top rung scored against itself | WP16 |
| `src/NetworkLogger.*` and the 19 other files that name it, `src/Demuxer.cpp`, `src/main.cpp`, `src/ManifestParser.cpp` and `src/NetInspectorAlign.cpp` among them | Q41 (b), the rename (`design/runs/networklogger-users.txt`); under Q10 (a) `NetworkLogger` is compiled into `mirage_vmaf` unchanged | WP00b |
| `src/ManifestParser.*`, `tests/test_manifest_parser.cpp` | Q40 (b) | WP13 |
| `src/DebugHUD.*` | Q40 (c), the GL ruler retired | WP13 |
| `src/main.cpp`'s render loop and seek handling | Q37 (b); WP13's and WP17's other changes to `src/main.cpp` are the panel's wiring and the logging hooks | WP13 |
| `src/Demuxer.*` | Q37 (c); Q13 (b) | WP13; WP17 |
| `src/MediaSession.*` | Q13 (b) | WP17 |
| `CMakeLists.txt:229-232` and `src/VMAFNet.h:8-9` (comments) | Q10 (a): reworded, since `mirage_vmaf` then links the network logger | WP00b |
| `docs/VMAF_GAP_REPORT.md` | Q42 (b), a dated correction line | WP15b |
| `src/HlsAlign.*`, `src/Decoder.*`, `src/VideoRenderer.*` | no answer | |

---

## 4. The data model and the single time base

### 4.1 Identities

- **Reference frame**: its position in the reference input, `ref_index`, the join key the ladder already has (sources.md §2). Every scored frame of every pair reaches it through the pair's frames.
- **Distorted frame**: `(pair, dis_index)`.
- **Variant**: a scored playlist, the reference's or a pair's distorted one, with the master entries that name it (`src/VMAFLadder.h:128-131`).
- **Segment**: `(variant, media sequence number)`, with its URI, byte range, EXTINF, discontinuity sequence and PDT (`src/HlsAlign.h:33-47`), and from the demux pass its first frame and its number of frames.
- **Event**: a deterministic string id within its document (P-I).

### 4.2 Frames, media keys and seconds (Q02)

§3.1 asks for one time base keyed by media PTS, a wall clock recorded beside it, with variants compared through the reference's timeline. Three things are kept apart:

1. **Where an event sits**: always a reference frame, `ref_index` for a point, `[ref_index, ref_end)` for a span (§5.1's rules). Integers.
2. **How intervals are compared**: on those integers, half-open (Q29). No seconds are compared, so the boundary errors of doubles can't occur (design-evaluation runs 07 and 08; design-library runs `axis-*-float`).
3. **Which seconds offsets are printed in**: the axis, Q02. The axis also turns durations given in seconds into frames: a declared ad duration without a cue-in (Q17), margins (Q29), and intervals a user gives in seconds (Q43 (g)).

Every event also carries its **media key**: the reference's discontinuity sequence of its frame (D-057's sequences) and that frame's PTS in ticks of the reference's time base. (sequence, PTS) was unique for all 200 frames of the ad ladder, where PTS alone had 150 values (design-library run `axis-ad`): the work order's PTS key made unique. Where its segment is known, the event also carries its playlist time.

The axes, as the two designs measured them on testgen's ladders:

| Option | Ad ladder, ad from the main origin | Ad from 1 s | Degraded ladder | Needs |
|---|---|---|---|---|
| (a) raw PTS for everything | 150 distinct of 200 frames; overlap on PTS lists `main_000.ts` and `main_001.ts` for the ad's drop; −2.96 s and +3.04 s steps | −11.96 s and +12.04 s steps | segment 4 only | the report |
| (b) frames; seconds of PTS, the report's sequences end to end | one 3.04 s step inside a sequence; the 2 s break spans 5.000 s | 0.04 s steps; the break 2.000 s | segment 4 only | the report |
| (c) frames; seconds of the reference's playlist time | 0.04 s steps; the break 2.000 s; the drop 3.000 to 5.000 s | the same | segment 4 only | the frame→segment map and EXTINF that matches its frames; (d) where there is no map |
| (d) frames; seconds = frame index × frame duration | the same as (c) | the same as (c) | segment 4 only | the report for the seconds; the frame→segment map to check them |

(design-library runs `axis-ad`, `axis-ad-origin1`, `axis-degraded`; design-evaluation run 09; `design/runs/seconds-check.txt` for (c) against (d) and for the check.)

Under (b) to (d) the events an interval lists are the same, because they are compared on frames; only the printed seconds differ, apart from the durations in seconds named above. (c) and (d) gave the same seconds at every scored reference frame of the four ladders checked, and each is off where the other is right (`design/runs/seconds-check.txt`, on simulated copies of the ad ladder's report and layout):

- (d) is right while the reference has one frame per frame duration. After one missing reference frame it was 0.040 s early from that frame on; with the ad's frames re-timed at 24, 30, 50 or 60 fps, their EXTINF following them, it ended 0.083, 0.333, 1.000 or 1.167 s off (c).
- (c) follows the frames through both, as long as each EXTINF matches its segment's frames; after one EXTINF made a frame too long, it was 0.040 s late from the next segment on, where (d) was right. FFmpeg's hls muxer wrote a last EXTINF of 0.36 s for 10 frames at 25 fps (sources.md §2), and an EXTINF shorter than its frames makes (c)'s seconds step back at the next segment, so seconds are turned into frames as the first frame, in index order, at or after them.
- RFC 8216 asks for EXTINF accurate enough that their sums don't drift, allowing whole seconds only below compatibility version 3 ([§4.3.2.1](https://www.rfc-editor.org/rfc/rfc8216#section-4.3.2.1)), and for an `EXT-X-DISCONTINUITY` where encoding parameters change ([§4.3.2.3](https://www.rfc-editor.org/rfc/rfc8216#section-4.3.2.3)): an ad at another frame rate after its discontinuity is a stream to expect, and so is a playlist of an older version with whole-second EXTINF.
- (c) needs the reference's segment map; (d) is defined for every reference from the report alone, a mezzanine (D-002's alternative) or a variant whose map was refused (Q43 (c)) included.

**The check of the seconds** (under (c) and under (d)). Wherever the reference's segment map exists, both clocks are computed at every scored reference frame and two things are checked, each with half a frame's tolerance, the aligner's (`src/VMAFAlign.h:23-25`): (1) (d)'s seconds against (c)'s; (2) each frame's PTS against the PTS of the first frame of its playlist discontinuity sequence plus its distance in frames times one frame duration. Each run of consecutive frames beyond the tolerance is written to `axis.warnings` with its first and last frame and its largest difference. (1) bounds the difference between the two clocks wherever it comes from, whichever is printed; (2) says whether the frames themselves leave the frame grid (a missing frame, a change of rate), so that a run of (1) without one comes from the playlist's EXTINF disagreeing with the frames' count. Without a segment map, (2) runs on the report's sequences instead: for a reference that isn't a playlist, which signals no discontinuity, its runs are warnings; for a playlist whose map was refused they are listed as facts, claiming nothing about the seconds, since a jump under 10 s stays inside one report sequence (`src/VMAFAlign.h:114`) and may be one the playlist signals.

What the check did (`design/runs/seconds-check.txt`): on the four ladders, (1) and (2) warn of nothing; without the map, each ad ladder whose ad restarts at the main content's origin (x264 and MPEG-2) lists one run, frames 125 to 199, whose PTS lie 3.000 s past their report sequence's frame grid, and the other two none; two mezzanine-mode reports, whose reference is one file, give no run. In the simulations, (1) warns from the frame where the difference first passes half a frame to the last frame: from frame 40 or 180 after a missing frame; from 88, 79, 77 and 76 for the ad at 24, 30, 50 and 60 fps; from 50 after the long EXTINF; (2) warns within the missing frames' sequences and within the re-timed ads, and not for the EXTINF. Comparing (d) with (c) at segment starts only, as the second review suggested, flags the same cases from the next segment start on, but misses the frame missing from the last segment, which no segment start follows; comparing at every frame catches it and warns within a segment, from frame 79 rather than 100 for the ad at 30 fps. The first version of this design checked each PTS against its report sequence from the report alone, which flagged 75 of 200 frames of each of those two ad ladders where both clocks are right (`design/runs/axis-check.txt`; `review-adversarial/runs/axis-d-check.txt`); revision 1 compared each step between frames with one frame duration, which flagged the missing frame and the ad at 60 fps but not the ads at 24, 30 and 50 fps nor the EXTINF, every step there staying within half a frame (same record; `review-2/runs/rate-change-check.txt`).

**Agent's recommendation: (c)**, with (d) where the reference has no segment map and this check wherever both exist, every event also carrying its media key and, where known, its playlist time. Since the check computes both anyway, the choice between them is which seconds are printed where they part: (c) is right where the frames leave the grid, as at an ad at another frame rate, and is off only where an EXTINF departs from its frames, which RFC 8216 advises against. **(d) alone is a close second**: one definition for every reference and both reference modes, and the better choice if the owner expects playlists with rounded or whole-second EXTINF. Offsets are seconds of the axis, not PTS differences: a difference from §3.1 to report (§11, item 4).

### 4.3 Events

```cpp
// One thing that happened, or holds, on the reference's timeline (src/TimelineModel.h).
struct Event {
    std::string id;                          // deterministic (P-I)
    Source      source = Source::Vmaf;       // §3.1's six sources
    std::string type;                        // §5.9's vocabulary
    int         variant = -1;                // index into Timeline::variants; -1: none (a session's own)
    int         session = -1;                // index into Timeline::sessions; -1: none
    std::optional<std::int64_t> segment;     // media sequence number, for segment-bound events
    std::int64_t ref_index = 0;              // reference frame it sits at
    std::optional<std::int64_t> ref_end;     // spans: exclusive end frame
    MediaKey    key;                         // sequence and PTS ticks of the frame at ref_index
    std::optional<std::int64_t> wallclock_us;   // observed events only: µs from the session's anchor (Q03)
    Payload     payload;                     // ordered (name, value): what the source says
};
```

- Points and spans are one type: segments, ad breaks, a session's variant spans and segment requests are spans; tags, switches, keyframes and playlist requests are points.
- A point at frame `f` meets `[a, b)` when `a ≤ f < b`; a span `[s, e)` when `s < b` and `e > a` (Q29).
- The payload is an ordered list of names and values (null, bool, integer, number, string, list, object), written as a JSON object in that order.

### 4.4 Curves and sessions

- **A pair's curve**: its scored frames as (`ref_index`, `vmaf`), ascending, with the positions where a libvmaf context starts (`alignment.vmaf_contexts`). A 30 fps variant against a 60 fps reference has a point at every other reference frame (schema.md §3.1), and the curve keeps those points only.
- **A session** (D-068): spans of reference frames, each with the variant played, from a plan of segment ranges (Q11). Its curve is, at each reference frame of a span, the score of that span's variant there; it is derived from the pairs' curves by `curve_points()`, not stored, and that derivation is WP07's, written once WP00b has declared the session types. Spans that play the top rung in top-rung mode have no score (Q12).
- **Naming a curve**: a `CurveRef`, a pair's index or a session's, which the query and detection take for either kind (§6.1).

### 4.5 Variants and rungs (Q07)

Each scored playlist is listed once with its master entries, URL, BANDWIDTH (Q43 (e): the highest among its entries, by the agent's recommendation), RESOLUTION, CODECS, role (reference or distorted), pair, playlist hash, and the status of its segment map. Its identity is its entry index and URI; its ranks from the top and from the bottom by BANDWIDTH are labels (Q07 (d)).

### 4.6 Wall clock (Q03)

Only observed events have one: the session fetch's requests now, the player's log later. It is microseconds of one steady clock from the session's start, with one UTC time sampled at the start as the session's anchor; offline events (scores, playlist tags, decoder facts) have none, and a segment's `EXT-X-PROGRAM-DATE-TIME` stays in its payload. A ladder built without a session therefore has no event with a wall clock (§11, item 5).

### 4.7 Order and determinism

Events are kept in the canonical order of P-I; the overlap index is built once (P-J). Identical inputs give byte-identical documents, apart from the fetch's measured times and wall clock.

---

## 5. From each §3.1 source to events

### 5.1 Placement rules (Q43, Q44)

| Anchor | Placed at |
|---|---|
| a reference frame | itself |
| a distorted frame of a pair | the reference frame it is paired with (`frames[]`) |
| a distorted frame that failed to pair | the next paired frame of its input, by index (Q43 (d)) |
| a reference frame that failed to pair | itself |
| a segment of the reference | its first frame (the demux pass); a span to the next segment's first frame |
| a segment of a distorted variant | the reference frame its first paired frame is paired with; a span to the next segment's |
| a playlist tag | the segment whose URI follows it, by line, a tag between `#EXTINF` and its URI included: RFC 8216's rule for media segment tags ([§4.3.2](https://www.rfc-editor.org/rfc/rfc8216#section-4.3.2)), which the cue tags' vendors follow (scte35.md §2, §6) |
| a media PTS on the carrying variant: a decoded splice time, `(pts_time + pts_adjustment) mod 2^33` (scte35.md §3.5), or a section read from the media (§5.5) | the first frame of the carrying variant, within the carrying playlist's discontinuity sequence of the anchor's segment, whose PTS is at or after it, compared in 90 kHz ticks modulo 2^33, the standard's splice point (scte35.md §3.2); then that frame's reference frame through the pair, or the next paired frame's if it failed to pair. A splice time is its own `splice_point` event (Q14) |
| a session's span | the reference frames of its segments, through the pair (Q44 (a)) |
| a segment request | its segment's media span (Q44 (b)) |
| a playlist request | the first frame of the first span that plays its variant, or of each span that plays it under Q44 (g) 2; the master's at the session's first frame (Q44 (b)) |
| a corrupt packet | its frame, the next segment boundary in its payload (Q43 (h)) |

The sequence searched for a splice time is the playlist's discontinuity sequence, not the report's: the report's sequences break only at jumps the aligner takes for discontinuities (testgen.md §3.6, item 5). The carrying variant's own PTS are compared, not the reference's: they differ wherever the variant's timestamps differ from the reference's, as they do in mezzanine mode against testgen's one-file mezzanine (testgen.md §3.5). An anchor that can't be placed (a segment none of whose frames paired, a splice time outside its sequence, a variant whose segment map was refused, a period-time anchor, which no scored DASH timeline exists to place) is listed in the document's `warnings` with its reason, never dropped silently.

### 5.2 Media facts: the demux pass (Q08)

- **What it reads.** Each scored playlist, opened as `DecodeCtx::open()` opens an input: the interrupt callback, `vmafnet::set_io_limits`, `vmafsegments::set_hls_options` (one connection per file), `avformat_find_stream_info`, the best video stream (`src/VMAFAnalyzer.cpp:166-194`). Its own `io_open`/`io_close2` recorder keeps each file in open order with its `bytes_read` at close; `av_read_frame` reads every packet; nothing is decoded (P-F).
- **Packets to segments.** By byte position: segment k holds the positions from the sum of the sizes of the segments opened before it, the playlist's own open left out, to that plus its own size (sources.md §11, choice 2 (a)).
- **Frames to packets.** An input's frames are each segment's packets in PTS order, the segments in the order they were read; the PTS the report gives frame i must equal its packet's, to the tick, for every scored frame (P-G). What a mismatch costs is Q43 (c): by the agent's recommendation the variant gets no segment map and its segment-bound events are left out, with a warning.
- **Measured.** 200 of 200 frames on each of 15 playlists of five testgen ladders, every frame's PTS agreeing, including the ad ladders where 100 of 200 frames have a PTS two segments share (design-evaluation runs 03, 04); 200 of 200 packets on four rungs (design-library runs `map-by-pos-*`). PTS against EXTINF, FFmpeg's own rule, got 145 of 200 on the ad ladders and 193 of 200 on the degraded one (same runs) and is not used.
- **Also from the pass**: keyframes (key packets: one per segment, at its first packet, design-evaluation run 03 and design-library runs `map-by-pos-*`); corrupt packets (`AV_PKT_FLAG_CORRUPT`); and the reference's PTS for reference frames no pair scored. With continuity counters left unmarked at the splice, the flags fell on the packets of frames 74 and 122 (`testgen/logs/61_ad_x264_nodisc_probe_v180.txt:149, 156` and `:249, 252`), one and three frames before the boundaries at 75 and 125 whose counter restart caused them (testgen.md §3.6, item 2): where the event sits, and so whether the ad's drop `[75, 125)` lists the first flag, is Q43 (h).
- **A reference that isn't a playlist** (a mezzanine, D-002's alternative): the pass reads the file for its frames' PTS and keyframes; it has no segments and no playlist events.
- **Not measured**: fMP4 with EXT-X-MAP, ID3-timestamped audio, byte ranges on real streams, open GOPs, decoders that output fewer frames than packets, and the pass's time on real ladders, local or over HTTP (R1, R2).

### 5.3 VMAF

- **Curves** per pair from `pairs[].frames` (§4.4); the scores are not copied into the document (Q01).
- **Events**: `context_start` at the first pair of each libvmaf context but the first, since that pair scores low (D-037; testgen.md §3.6, item 7), and `unpaired_frame` for each of `alignment.failures` (input, index, PTS, reason), which is how a frame a decoder never output shows (sources.md §7). §3.1 lists dropped frames under the decoder; this design files the aligner's unpaired frames under `vmaf`, so they are listed in the `vmaf` group (Q04 (a); §11, item 3).
- **Drops** are attributions (§6), not events.

### 5.4 Playlist (HLS)

`PlaylistEvents` runs `hls::parse_media_timeline` for the segments, then scans the same text once more for tags, attaching each to the segment whose URI line follows it (`Segment::line`, `src/HlsAlign.h:46`). `ManifestParser` stays the player's, with its tested behaviour (`tests/test_manifest_parser.cpp:95`; Q40).

| Type | From | Payload |
|---|---|---|
| `segment` (span) | each segment | URI, media sequence, EXTINF, byte range, discontinuity sequence, PDT, playlist time, bytes from the demux pass, gap and map flags |
| `discontinuity` | `EXT-X-DISCONTINUITY` | the playlist's discontinuity sequence after it |
| `program_date_time` | `EXT-X-PROGRAM-DATE-TIME` | the date as written and as Unix seconds (`hls::parse_iso8601`) |
| `daterange` | `EXT-X-DATERANGE` without `SCTE35-*` | every attribute, as RFC 8216 §4.3.2.7 lists them |
| `tag` | any other `#EXT` tag not consumed above, vendor tags included | name, raw value |

A bare `EXT-X-DISCONTINUITY` stays a `discontinuity` (Q19). DASH: a `period` type is reserved, and an `EventStream` would reach the cue interface of §5.5 if a reader is written later (Q20).

### 5.5 SCTE-35 (D-070)

- **Tags read** (WP04): `EXT-X-CUE-OUT` (bare, with a duration, with `DURATION=`, with further attributes), `EXT-X-CUE-OUT-CONT` (`elapsed/duration`, or `ElapsedTime=…,Duration=…,SCTE35=…`), `EXT-X-CUE-IN` (bare or with an `ID`), as scte35.md §2 describes their forms; `#EXT-OATCLS-SCTE35` (base64); `EXT-X-DATERANGE`'s `SCTE35-OUT`, `SCTE35-IN` and `SCTE35-CMD` (hex). Signalings outside this list stay playlist `tag` events (Q18). Every message a tag embeds, the `SCTE35=` of an `EXT-X-CUE-OUT-CONT` included, becomes a `CueInput` for the decoder.
- **The cue interface** (D-070's interfaces, declared by WP00a): a `CueInput` is `{carrier, anchor, form, bytes}`. The carrier is a playlist tag, a DASH `EventStream` event, an MPEG-TS section, or an ID3 PRIV frame, which is how the one public VOD stream found carries its messages (scte35.md §8). The anchor is a playlist position (variant, segment, line); a media PTS on the carrying variant (variant, PTS, time base, and its segment where known); or a period time (period, `presentationTime`, timescale). `Scte35Events` places each kind by §5.1's rules; a period-time anchor goes to the warnings, since no DASH timeline is scored. Phase 3 builds no reader for DASH or in-segment carriage; WP05's tests feed a PTS-anchored section through the interface, so a later reader plugs in without changing the core's types.
- **Decoded** (WP05): the layout of scte35.md §3 (SCTE 35 2023r1 Tables 5 to 17, 20 to 25 and 29): splice_insert, time_signal, break_duration, splice_time with `pts_adjustment` added mod 2^33, the descriptor loop with unknown descriptors skipped by length, segmentation_descriptor with its UPID bytes kept; CRC-32/MPEG-2 through `av_crc` (P-L). An encrypted message is reported undecoded; encrypted streams are out of scope.
- **Who does what.** WP04 (round 1) parses the tags, makes the plain tags' cue events, hands every embedded message on as a `CueInput`, and pairs every cue, plain or decoded, into `ad_break` with Q17's end rule: one owner for the pairing. WP05 (round 2, after WP04) decodes the messages, classes them (Q15, Q16), merges a message into its `EXT-X-CUE-OUT`'s cue (Q44 (e)), adds the `splice_point` events (Q14) and the message-only cues, and returns its cues to WP04's pairing. D-069's ladders carry plain cue tags only (testgen.md §2.3), so the acceptance tests need no decoder.
- **Events** (under Q14 to Q17, Q44 (e)):

| Type | When | Payload |
|---|---|---|
| `cue_out` | a CUE-OUT, or an OATCLS or DATERANGE message that is an out (Q15); one event per cue when a message sits in a CUE-OUT's segment (Q44 (e)) | the form, the raw line, the declared duration, the decoded message, `crc_valid`, every anchor with its offset |
| `cue_out_cont` | a CUE-OUT-CONT | elapsed, duration, the decoded message if any |
| `cue_in` | a CUE-IN, or an in message | the same fields |
| `ad_break` (span) | a cue_out with the next cue_in in play order, or with its declared duration when there is none (Q17) | the durations each source gives, and how the end was found |
| `splice_point` | a decoded message with a splice time (Q14 (a)) | the splice time, `pts_adjustment`, the command |
| `message` | a decoded message that is neither out nor in (Q15) | its command and segmentation types |
| `cancel` | a message whose cancel indicator is set (Q15) | its event id |

- **Placement** by Q14. A warning is written when an anchor (the splice time, START-DATE through PDT) falls outside the tag's segment. The D-069 ad ladder's cue tags carry no message (testgen.md §2.3), so its events are placed by position under every option.
- **Not measured**: whether FFmpeg's demuxers deliver an SCTE-35 or timed-ID3 stream through `mirage_vmaf`'s input settings (scte35.md §10, item 6): MEASUREMENT PENDING.

### 5.6 Sessions and switches (D-068)

- **The plan** (Q11 (a)): a versioned JSON file of spans in play order, each `{entry, first_seq, last_seq}`, recorded by path and SHA-256. Each span covers the reference frames of its segments through its variant's pair; whether spans may leave gaps or overlap is Q44 (a).
- **Events** (`source: player`): `variant_span` (a span) with the variant played (entry, URI, BANDWIDTH, RESOLUTION, both ranks) and `scored: false` where it is the reference in top-rung mode (Q12); `variant_switch` at each span boundary after the first, with `from` and `to` as above and `origin: "plan"`.
- **The session's curve** (§4.4), and its drops (§6.3, Q22).
- **Frames a player drops at a switch** don't exist in a scripted session. D-068's limits expect them as the decoder's events; they would come from the player's log, which under Q13 (a) is not in Phase 3, so Phase 3 has none (§11, item 3).

### 5.7 Network: the session fetch (Q10)

- **The fetch** (`SessionFetch`, P-N). After the demux passes, the build fetches the session's files in play order: the master, then for each span its variant's playlist and the span's segments. How it is paced is Q44 (f), one file at a time and unpaced by the agent's recommendation; whether a variant's playlist is fetched again at each span that plays it is Q44 (g), once by the recommendation; each file has a connection of its own, with the analyzer's network limits (D-036); retries are Q44 (c), none by the agent's recommendation. Each fetch is a plain `avio_open2`, a read to the end and a close, tracked by `NetworkLogger::track_open_begin`, `track_open_end` and `track_close` (`src/NetworkLogger.h:118-125`), with the logger installed for the fetch only. Fetch URLs are the report's, or the same paths rebased onto `--fetch-base` (the tests score local files and fetch them from a local server).
- **The rows.** Status, status text, request sent, first byte, end, bytes, error. The rows of each file are copied and the logger cleared after its close (`copy_changed_since`, `clear`, `src/NetworkLogger.h:157-160`), so the logger's 4000-row limit (`src/NetworkLogger.h:194`) can't evict a long session's first requests. A redirect gives one row per request (`src/NetworkLogger.cpp:601`, by reading; WP07's test pins it), and one fetched file is one event with the chain in its payload (Q44 (d)). A prototype of this fetch, with its own parsing of the same `header='HTTP/…'` TRACE line that `NetworkLogger` parses, measured 2.313 s for a segment answered 2.3 s late, and the status line `HTTP/1.0 200 OK` (design-evaluation run 06). `NetworkLogger`'s own manual tracking over HTTP in a headless process is not yet run (R5).
- **Events** (`source: network`): `segment_request`, a span over the fetched segment's media span, and `playlist_request`, a point (§5.1), each with the URL, the times, the duration, bytes, status, error, `origin: "fetch"`, and the wall clock from the session's anchor (Q03).
- **What it is not**: a player's fetch pattern. Under Q44 (f) 1 it is sequential, with no buffer and no ABR, and its times are what one sequential client got at the time of the build (R6). The scoring reads are not used, since decoding and scoring pace them (sources.md §3.3); recording them, labelled scoring-paced, is Q10 (e). A ladder built without a session has no network events (§11, item 6).

### 5.8 Decoder

| Type | From | Placed at |
|---|---|---|
| `keyframe` | key packets of the demux pass | the frame |
| `corrupt_packet` | `AV_PKT_FLAG_CORRUPT` in the demux pass | Q43 (h): by the agent's recommendation the flagged packet's frame, with the next segment boundary (its first frame, and whether a discontinuity precedes it) in the payload |
| `decode_error` | only under Q09 (a): the scoring decode's `decode_error_flags`, `AV_FRAME_FLAG_CORRUPT` and `avcodec_send_packet` errors, from the ladder report; under Q09 (b), a decode pass | the frame, the flags in the payload |

The offline decode drops no frame of its own (sources.md §7): a frame a decoder never output is a `vmaf/unpaired_frame` (§5.3), and frames the player drops at a switch come only with the player's log (Q13).

### 5.9 The event vocabulary

| Source (§3.1's order) | Types |
|---|---|
| `vmaf` | `context_start`, `unpaired_frame` |
| `network` | `segment_request` (span), `playlist_request` |
| `playlist` | `segment` (span), `discontinuity`, `program_date_time`, `daterange`, `tag`; `period` reserved |
| `scte35` | `cue_out`, `cue_out_cont`, `cue_in`, `ad_break` (span), `splice_point`, `message`, `cancel` |
| `player` | `variant_span` (span), `variant_switch` |
| `decoder` | `keyframe`, `corrupt_packet`, `decode_error` (Q09) |

The vocabulary is part of the document's first schema version (Q04).

---

## 6. Queries and detection

### 6.1 The API

Appendix B has the headers. In short:

```cpp
namespace timeline {
struct Interval { std::int64_t ref_index = 0, ref_end = 0; };        // half-open, reference frames
struct CurveRef {                    // a pair's curve, stored; or a session's, derived (§4.4)
    enum class Kind : std::uint8_t { Pair, Session } kind = Kind::Pair;   // Session: WP00b's
    int index = 0;                   // into Timeline::curves or Timeline::sessions
};

struct Found {                       // one event an attribution lists
    const Event* event = nullptr;
    std::int64_t offset_frames = 0;  // event start − interval start
    std::optional<std::int64_t> offset_end_frames;   // spans
    double offset_s = 0;             // the same on Q02's axis, for output
    std::optional<double> offset_end_s;
};

struct Attribution {
    CurveRef    curve;
    Interval    interval;
    bool        detected = false;    // a detected drop, or a user's interval
    std::optional<double> baseline, min, mean, drop_magnitude, mean_drop;   // Q27
    std::array<std::vector<Found>, kSourceCount> by_source;              // §3.1's order
};

Curve curve_points(const Timeline&, CurveRef);                         // src/TimelineModel.h
Attribution attribute(const Timeline&, CurveRef, Interval, const QueryOptions&);   // src/TimelineQuery.h
std::vector<Drop> detect(const Curve&, const DetectOptions&);          // src/TimelineDetect.h
std::vector<Drop> session_drops(const Timeline&, int session, const DetectOptions&);   // src/SessionPlan.h, Q22
}
```

`attribute()` takes the curve by reference so that a session's scope (§6.2) has the spans it needs: the played variant per span comes from `Timeline::sessions`. WP01 writes `curve_points()` and `attribute()` for a pair's curve in round 1; WP07 adds a session's to the same files in round 2, after WP01 has merged and WP00b has declared `Span`, `Session`, `Timeline::sessions` and the session kind of `CurveRef`, so that no round-1 package needs a round-2 answer.

### 6.2 What an attribution lists (Q28 to Q30)

- **Overlap** (Q29 (c)): strict half-open overlap on reference frames, with optional margins before and after (none by default), given in seconds and turned into frames on the axis.
- **Scope** (Q28 (a)): for a pair's curve (WP01), its distorted variant's events and the reference's; for a session's (WP07), the played variant's events in each span, the reference's, and the session's own (spans, switches, requests). An event in several rungs' playlists is kept once per variant (Q43 (f)), so the reference's copy of a cue tag is listed with the distorted variant's.
- **Order** (Q30 (a)): groups in §3.1's order (vmaf, network, playlist, scte35, player, decoder), events by offset within a group, no ranking.
- **Offsets**: in reference frames (exact, independent of Q02) and in seconds on the axis.

### 6.3 Detection (Q22 to Q27)

`detect()` walks a curve's points once:

1. A point is low when its score is more than T below the baseline at that point (Q23: by the agent's recommendation the median of the last W points that weren't low, used once W/2 such points exist). Until then no point is low, so the first W/2 points of every curve are never examined: at W = 50, the whole first segment of a D-069 ladder (25 frames, testgen.md §3.1).
2. A drop is a run of more than N consecutive low points (Q24: points of the curve, gaps skipped; Q25: no special case for the first pair of a libvmaf context).
3. Each drop gets its interval (Q43 (g)), its baseline at its first low point, its minimum and mean, and its magnitudes (Q27). Under Q23 (a) a low point never enters the baseline, so the baseline is the same at every point of a drop (the prototype, `design/tools/detection_semantics.py:40-50`).
4. A session's drops (Q22 (c)) are its pairs' drops clipped to the spans that played each pair's variant, by three rules proposed with Q22 (c): (i) a clipped piece is a drop only if it keeps more than N points, as any drop must; the alternative keeps every piece, as the prototype does (`design/tools/detection_semantics.py:138-157`). (ii) Pieces of two pairs that meet at a switch stay two drops, each with its own pair's numbers; the alternative merges them into one drop across the switch, as a viewer sees it, at the cost of a drop with two baselines and of applying N to the merged run. (iii) A piece reports its pair's baseline at its own first point, which under Q23 (a) is the drop's (rule 3). Unscored spans (Q12) have none. `session_drops()` is in `SessionPlan` (WP07).

Every parameter goes into the document, with the config file it came from, since each changes the attributions as the scoring options change the scores (§7's rule).

**What the runs show, and don't.** On the four acceptance curves (the two targets and their controls) the prototype detector found both drops exactly, with no drop in either control, at every point of its grid of T (3, 4, 5, 6, 7, 8, 10, 12, 15, 20, 25, 30, 40) and N (1, 2, 3, 4, 5, 8, 12, 16, 20, 24) at W = 10, 25, 50 and 100; over all 21 pair curves of testgen's reports, at T = 6 and W = 50 every grid value of N from 3 to 24 passed, while N = 1 or 2 gave false drops on the busy ad's control (runs of 2 and 3 low frames, such as frames 88 to 90) and N ≥ 30 missed the degraded case's 25-frame drop (design-evaluation run 05). That shows the tests can pin parameters with a wide margin; it doesn't measure good defaults on real video. Three semantics questions were checked on the ad ladder with the prototype's W = 50, N = 12:

- At T = 6, with the first pair of every libvmaf context left out of the curve, `v180`'s drop starts at frame 76 and the cue-out's frame 75 falls before it; with no special case, or with those pairs left out of the baseline only, it starts at 75 with the cue-out inside (Q25; `design/runs/detection-semantics-ad.txt`).
- At T = 6, for the scripted session of design-evaluation run 10 (entry 1 for segments 0 to 2, entry 2 for 3 to 4, entry 1 for 5 to 7), the session's curve with its own baseline gave a drop at frames 75 to 124 with baseline 97.681 and magnitude 75.445; the pairs' drops clipped to the played spans gave the same frames with `v180`'s baseline 91.692 and magnitude 69.455. For a plan that switches from entry 1 to entry 2 at segment 5, neither gave a drop (Q22; same run).
- On that switch-down plan, the session's curve with its own baseline gave a drop at T = 3 (frames 125 to 199), T = 4 (126 to 199) and T = 5 (164 to 182), and none at T = 5.5, 6, 6.5, 7 and 8: whether a switch alone is a drop under Q22 (b) depends on T near the rungs' gap (`review-adversarial/runs/switch-t-sweep.txt`).

What would inform the defaults W and N that nothing measured (Q26): detection over D-011's ladders with degradations injected at several lengths and strengths, counting hits and false alarms for each (T, W, N), and the longest run of low points in unaltered rungs.

### 6.4 The work order's example, answered

The work order's example answer names three events: a drop that overlapped an SCTE-35 cue-out, a switch between two numbered rungs, and a segment request of 2.3 s. Each is an event of one group: the cue-out in `scte35` (type `cue_out`), the switch in `player` (`variant_switch`, with `from` and `to` and their ranks, Q07), the segment in `network` (`segment_request`, its media span meeting the drop, its measured duration in the payload, Q10).

The prototype's text output for the scripted session on the ad ladder, with a local server answering `v180/ad_000.ts` 2.3 s late (design-evaluation run 10, abridged there to the lines that answer the example):

```
Drop 1: reference frames 75-124 (50 frames); playlist time 3.000-5.000 s; ... baseline 97.681, min 22.237, ... magnitude 75.445
  scte35   cue_out          v180, segment 3 at +0.000 s: line 14 '2.000'
  scte35   ad_break         v180 at +0.000 s to +2.000 s: cue-out to cue-in, declared duration 2.0 s, placed by tag position
  player   variant_switch   v180 at +0.000 s: entry 1 (480x270, BANDWIDTH 565504; rank 2 from the top, 2 from the bottom) -> entry 2 (320x180, BANDWIDTH 345920; rank 3 from the top, 1 from the bottom)
  network  segment_request  v180, segment 3 at +0.000 s to +1.000 s: /v180/ad_000.ts: 2.313 s from request to last byte (2.312 s to the response head), 12032 bytes, HTTP/1.0 200 OK
  playlist discontinuity    v180, segment 3 at +0.000 s: line 13
```

Against the example: the drop overlapped a cue-out at +0.000 s, a switch at +0.000 s from the rung second from the top (second from the bottom) to the third from the top (first from the bottom), and a segment request of 2.313 s. That prototype detected on the session's curve with its own baseline and listed the played variant's events and the session's own. Under Q22 (c) the drop has `v180`'s baseline (91.692) and magnitude (69.455) (`design/runs/detection-semantics-ad.txt`), and under Q28 (a) the reference's copies are listed as well: in run 10's attribution of the `v180` pair they are `v360`'s `cue_out`, `cue_out_cont` and `ad_break`, its two segments, its discontinuity and its two keyframes. The implementation's text form (P-AD) adds the offsets in frames.

---

## 7. CLI (Q31)

Under Q31 (a), with the options a document records:

```
mirage_vmaf timeline --report <ladder report.json> --out <timeline.json>
                     [--session <plan.json>] [--fetch on|off] [--fetch-base <url>]
                     [--threshold T] [--window W] [--min-points N]
                     [--margin-before S] [--margin-after S]
                     [--cue-types <segmentation_type_id ranges>] [--bad-crc keep|drop]
                     [--config <file>]
mirage_vmaf attribute --timeline <timeline.json> [--report <ladder report.json>]
                      (--interval <t0>:<t1> | --frames <first>-<last> | --drop <k> | --auto)
                      [--curve pair:<k>|session:<id>]
                      [--threshold T] [--window W] [--min-points N]
                      [--margin-before S] [--margin-after S]
                      [--format text|json] [--out <file>] [--config <file>]
```

- **Dispatch**: `tools/mirage_vmaf.cpp` looks at the first argument before `vmafcli::parse`: `timeline` or `attribute` go to `timelinecli::run_timeline()` or `run_attribute()`; anything else goes to today's parser unchanged (§3.6). Since that is before `tools/mirage_vmaf.cpp:155-156`, each subcommand sets FFmpeg's log level and initializes its network itself (P-N).
- **Options and config files** (P-V): each subcommand has its own option table, in which one row gives the option, its config setting and its help, as `vmafcli`'s table does; every option is also a `name = value` setting of D-030's file, given once; the command line replaces the file; the file's path and SHA-256 go into the document. The plan is a list, so it is a file of its own (Q11), recorded by path and SHA-256.
- **Defaults**: none for W and N until the owner gives them (Q26 (a)); `--threshold` 6 under Q26 (a) 1; margins none (Q29); `--fetch on` when a session is given; `--cue-types` the set Q15 picks (by its recommendation 0x22-0x23, 0x30-0x37, 0x44-0x47) and `--bad-crc keep` (Q16); `attribute --format text` (Q31 (a)), JSON with `--format json`.
- **Detection**: `timeline` detects and writes the attributions with their parameters when `--window` and `--min-points` are given (Q26 (b) 2); without them it writes the events with `detection: null` and says so on stderr. `attribute --auto` detects again with the parameters given, without reading media or rewriting the document.
- **Exit status** (P-W): 0 written; 1 a failure (an unreadable input, a hash mismatch, a refused input under Q43 (a) or (b)), no document left at `--out`; 2 bad arguments or config file.
- **Help**: `mirage_vmaf --help` gains a paragraph naming the subcommands, after its existing synopsis lines (`tests/test_vmaf_cli.cpp:161` pins them); each subcommand has its own `--help`.

---

## 8. The timeline document

Where it goes is Q01 (agent: a separate document), its layout Q04, its versioning and provenance Q05, how it names its report Q06. The layout proposed, with §7's names where §7 has the field:

```
{
  "schema": "mirage-timeline", "schema_version": 1,
  "tool": "mirage_vmaf", "command": "timeline",
  "ffmpeg_libraries": { ... },                                   // as the ladder report's (P-035)
  "options": { ... every option of the build, with its value ... },
  "config_file": { "path", "sha256" } | null,
  "ladder_report": { "path", "sha256", "scores_sha256", "status", "reference_mode",
                     "model", "model_source", "model_sha256", "scaler",
                     "libvmaf_version", "libvmaf_package_version",
                     "master": { "url", "location", "sha256" } },   // provenance copied (Q04 (a))
  "session_plan": { "path", "sha256" } | null,
  "axis": { "kind": "reference-frames", "seconds": "playlist-time" | "frame-grid",
            "frame_duration": "1/25", "frames",
            "reference": { "url", "time_base", "frame_rate", "sequences": [ ... ] },
            "checked": "segment-map" | "report-only", "warnings": [ ... ] },
  "variants": [ { "id", "role": "reference" | "distorted", "pair", "entries", "url",
                  "bandwidth", "resolution", "codecs", "rank_from_top", "rank_from_bottom",
                  "playlist": { "url", "location", "sha256" },
                  "segment_map": "ok" | "refused: <why>" } ],
  "sessions": [ { "id", "name", "wallclock_anchor_utc",
                  "spans": [ { "variant", "entry", "first_seq", "last_seq",
                               "ref_index", "ref_end", "scored" } ] } ],
  "timeline": [ { "id", "source", "type", "variant", "session", "segment",
                  "ref_index", "ref_end", "t", "t_end", "seq", "pts", "pts_ticks",
                  "wallclock", "payload" } ],
  "detection": null | { "threshold", "window", "min_points", "baseline", "warm_up",
                        "context_first", "curves", "session_rules" },
  "overlap": { "rule": "half-open", "margin_before", "margin_after", "scope" },
  "attributions": [ { "curve", "detected",
                      "interval": { "ref_index", "ref_end", "t", "t_end", "points" },
                      "baseline", "min", "mean", "drop_magnitude", "mean_drop",
                      "overlapping_events": [ { "source": "vmaf",
                                                "events": [ { "event", "offset", "offset_end",
                                                              "offset_frames", "offset_end_frames" } ] },
                                              { "source": "network", "events": [ ... ] },
                                              ... ] } ],
  "warnings": [ ... ]
}
```

- `ref_index` and `ref_end` are reference frames, the positions compared; `t` and `t_end` their seconds on the axis (Q02), on the clock `axis.seconds` names (under Q02 (c) the playlist time, or the frame grid where the reference has no segment map), written with `json::number` (`src/JsonText.h:17-19`); `seq` and `pts` the media key (`pts` in seconds as §7 names it, `pts_ticks` exact over `axis.reference.time_base`); `wallclock` seconds from the session's anchor, null for offline events (Q03).
- `overlapping_events` keeps §7's name and type, an array, with one group per source in §3.1's order; each entry refers to a `timeline[]` event by id, so each event is written once.
- Per-frame scores are not copied: the document is read with the ladder report it names, by its SHA-256 or, for a committed redacted copy, its scores digest (Q06; P-Z). Per-frame lines are 99.91% of a full report (schema.md §3.3).
- `schema_version` and `docs/schema/mirage-timeline-1.schema.json` come with the document under Q05 (a); ctest checks the writer's output against the schema file with a subset validator in the tests (P-AI). The commit is recorded by the eval tooling (P-006, `docs/DECISIONS.md:783-808`) until Phase 5 decides §7's `git_commit`.
- **Who writes what**: WP08 writes the ladder part in round 1, with §5.9's whole vocabulary in the schema file; WP09b adds the session part (`session_plan`, `sessions`, the network and player events' fields) in round 2, after WP08 has merged; WP09c's decoded messages need no change of the format.
- **Against §7**: §7's timeline array keeps its five fields (`pts`, `wallclock`, `source`, `type` and `payload`), extended; its attributions keep `interval`, `drop_magnitude` and `overlapping_events`, with the interval an object, the events grouped, and offsets and parameters added. The differences are listed in §11.
- **Size**: the prototype's documents for 200-frame ladders were 8577 to 14470 bytes with 32 to 44 events (design-evaluation run 11); on real ladders MEASUREMENT PENDING. Their events grow with segments and tags, 100 segments per variant in Apple's advanced sample (schema.md §3.1), not with frames.

---

## 9. Tests

### 9.1 Unit tests (ctest, nothing downloaded)

| File | Package | What it checks |
|---|---|---|
| `test_timeline_model.cpp` | WP01 | canonical order; deterministic ids; the overlap index against brute force on random events; points, spans, empty intervals |
| `test_timeline_axis.cpp` | WP01 | media keys on hand-made reports with PTS that restart at the main content's origin, a forward jump under 10 s, a coarser-grid pair; the seconds of Q02's option and of its fallback; the check of §4.2 in the shapes of `design/runs/seconds-check.txt`: no warning on the D-069 ad shape with its segment map and its run of frames 125 to 199 listed as a fact without one; warnings at a missing frame, the last segment's included, at an ad at another frame rate after its discontinuity and at an EXTINF a frame too long, the PTS check (2) warning for the first two only; a warning for a reference that isn't a playlist where its PTS change rate; seconds to frames |
| `test_timeline_query.cpp` | WP01 | half-open overlap, margins, the scope of a pair's curve, group order, exact offsets |
| `test_timeline_detect.cpp` | WP02 | a step drop; single-frame dips at context starts; alternating dips; a drop inside the blind start (not found, as expected); gaps; the baseline unchanged through a drop; the same drops twice |
| `test_json_read.cpp` | WP03 | RFC 8259's grammar, escapes, UTF-8; numbers as `json::number` writes them; duplicate keys refused; the nesting limit; round trip |
| `test_ladder_report_read.cpp` | WP03 | a report from `vmafreport::ladder_to_json` on results built in the test, read back; Q43 (b)'s refusals; the digest unchanged by redaction |
| `test_playlist_events.cpp` | WP04 | tags attached by line; every cue form in its shape; DATERANGE attributes; OATCLS and CUE-OUT-CONT's `SCTE35=` handed on as `CueInput`s; discontinuity numbering; vendor tags; break pairing with cues from a stub decoder; Q43 (a)'s live playlist |
| `test_scte35.cpp`, `test_scte35_events.cpp` | WP05 | sections built by a writer in the test (P-M) through every field the decoder reads, CRC good and bad, truncation, the wrap mod 2^33; event classes, CRC policy, anchors, warnings, cancellations; a section anchored by media PTS; a splice time placed on a distorted variant whose PTS origin differs from the reference's; a CUE-OUT-CONT's message decoded |
| `test_timeline_media.cpp` | WP06 | the demux pass on HLS the test writes: byte map, B-frames, restarted timestamps, BYTERANGE, a refused map, corrupt packets at an unmarked splice with their boundary (Q43 (h)), over HTTP |
| `test_session_plan.cpp`, `test_session_fetch.cpp` | WP07 | plan parsing and errors, spans, switch events; the session's curve (`curve_points()`) and the scope of a session's curve (`attribute()`); the work order's example on a hand-built timeline (a cue-out, a switch between two numbered rungs of a five-rung ladder, a request of 2.3 s, all the test's inputs) returned in their groups; session drops with §6.3's three rules (a short piece, pieces meeting at a switch, the baseline reported); the fetch over `tests/HttpFileServer.h` with a delayed, a missing, a cut and a redirected file, in Q44 (f) and (g)'s order and pacing |
| `test_timeline_doc.cpp` | WP08, WP09b | the writer's keys and types against the schema file; round trip; byte-identical output twice; an unknown version refused |
| `test_timeline_build.cpp`, `test_timeline_cli.cpp` | WP09a, WP09b, WP09c | the merge on hand-built inputs, with a session (WP09b) and with decoded messages (WP09c); option tables, config files, errors, `--help`; detection only with W and N; one end-to-end run of each subcommand |
| `test_synthetic_ladder.cpp` | WP10 | one ladder made twice in one process, byte-identical; its segments, frames and tags against its layout; BANDWIDTH in rung order |
| `test_timeline_load.cpp`, `test_timeline_view.cpp`, `test_timeline_match.cpp`, `test_hls_seek_behaviour.cpp` | WP12 | the loader: hashes checked, a changed report refused, a redacted one accepted by its digest, the snapshot handed over once, cancelling; ui.md §5.1's UI math; the click's interval (Q39 (b)); §5.2's seek behaviour on playlists the test writes |

### 9.2 The two acceptance tests (§3.2; D-069)

In `tests/test_attribution_acceptance.cpp` (WP11a), one Catch2 test case tagged `[attribution][acceptance]` (P-T) with a section per case, so that ctest runs them in one process and `tests/SyntheticLadder` makes each ladder once (P-Q). Each case runs `mirage_vmaf --ladder` (top-rung mode, D-002), then `mirage_vmaf timeline` with detection parameters pinned inside design-evaluation run 05's region (T = 6, W = 50, N = 12, P-R), then `mirage_vmaf attribute --auto --format json`, and reads the document back. Assertions are on reference frame ranges, URIs, media sequence numbers, event types and offsets in frames, all of which hold under options (b) to (d) of Q02 (P-R); ground truth is the generator's layout.

1. **A segment encoded at a deliberately low quality.** The degraded ladder (three rungs, eight 1 s segments, `v270`'s segment 4 at the low quantizer). Passes when `v270`'s curve has exactly one drop, over the reference frames of segment 4 (100 to 124 in testgen's layout); its `playlist` group holds `v270`'s segment with media sequence 4 (`seg_004.ts`), whose span holds the drop; and the control rung `v180` has no drop. On testgen's content the low frames were exactly that segment's 25 (testgen.md §3.1).
2. **A score drop at an ad boundary.** The ad ladder (`EXT-X-DISCONTINUITY`, `EXT-X-CUE-OUT:2.000`, `EXT-X-CUE-OUT-CONT`, `EXT-X-CUE-IN`; `v180`'s ad at the low quantizer). Passes when `v180`'s curve has exactly one drop, over the ad's reference frames (75 to 124); its `scte35` group holds the `cue_out` of the `EXT-X-CUE-OUT` line at offset 0 frames and an `ad_break` that spans the drop; its `playlist` group holds the `discontinuity` at offset 0; and the control `v270` has no drop. Run in Q34's timestamp variants: the ad from the main content's origin (the acceptance test proper), from 1 s, and continuous.

### 9.3 The work order's example, end to end

In `tests/test_attribution_session.cpp` (WP11b), tagged `[attribution][session]`: the ad ladder scored from local files; a plan playing entry 1 for segments 0 to 2, entry 2 for segments 3 and 4, entry 1 for 5 to 7; the timeline built with `--fetch-base` pointing at `tests/HttpFileServer.h` serving the same ladder, with a delay rule on `v180/ad_000.ts` (P-O). Passes when the session's drop lists the `cue_out` (scte35), a `variant_switch` from entry 1 to entry 2 with their ranks (player), and a `segment_request` for `ad_000.ts` with a duration at least the delay and status 200 (network). The delay is the test's own, under the 5 s network limit (D-036), and longer than the undelayed requests in the same run (P-S). It is the work order's example with this ladder's events; the eval harness runs it with the example's 2.3 s (§9.4).

### 9.4 Reproduction (E4)

`eval/timeline/run_all.sh <build dir> <out>` (WP14) runs the acceptance cases with `MIRAGE_KEEP_MEDIA` (P-U), builds and queries their documents, runs the composite example against a local delaying server with 2.3 s, and checks every answer with `check.py` against the generator's layout. The check compares everything but wall-clock fields and measured durations; a duration must be at least the injected delay. Each record keeps `cmd.txt` and `environment.txt` with the commit (P-AF), as P-006 has it.

### 9.5 Time

ctest runs each Catch2 test case in its own process (`review-adversarial/runs/ctest-registration.txt`), and a ladder made once per process (as `tests/test_vmaf_ladder.cpp:192-216` makes its fixtures) is made again in each test case that uses it; the test temporary directory is per process too (`tests/TestTemp.h:3-7`). With P-Q's one test case for the acceptance cases, a ctest run makes seven ladders under Q34 (d): four in the acceptance test case (the degraded ladder and the three ads), one in the composite's (the ad from the main origin), and two in WP10's determinism case. testgen measured 2.022 to 2.313 s per generation with the prototype optimized and 16.941 s unoptimized, and 0.457 to 0.515 s to score a ladder, 0.458 and 0.462 s with the debug `mirage_vmaf` (testgen.md §3.4). The ported generator's time, and the tests' time in a Debug build, are MEASUREMENT PENDING (R9).

---

## 10. §3.3: the UI plan, for the owner (D-007)

ui.md §2 to §5 is the plan; this design changes where the panel gets its data and adds nothing that touches the render or decode path.

- **Input** (`TimelineLoad`, WP12, ImGui-free and tested in `mirage_tests`). The loader reads a timeline document and the ladder report it names off the main thread, checks both by hash (§8), builds the core's `Timeline`, and hands an immutable snapshot to the main thread once, as `ThumbnailStrip` hands over its thumbnails (ui.md §2.2); a new load cancels the old one through its stop token. The panel draws the snapshot: curves from the report, events and attributions from the document. It matches the document to the open master by SHA-256, else by URL, with the entry checks that the two parsers' different enumeration makes necessary (ui.md §2.3).
- **The panel** (ui.md §2.1; placement Q36): one curve per variant and the session's curve (`curve_points()`, WP07's for a session) as per-column min/max envelopes, so a one-frame dip stays visible (ui.md §1.8, F4); the detected drops shaded; one event lane per source; the playhead. Wheel zoom, drag pan, double-click fit, as the Alignment tab does (`src/NetInspectorAlign.cpp:637-662`); a click (a release that moved less than 3 px) picks the lowest frame near the cursor.
- **A click** inside a detected drop shows its attribution from the document; elsewhere the panel calls `timeline::attribute()` for an interval around the frame, by the agent's recommendation the clicked frame's segment of the clicked curve's variant (Q39 (b)), so the panel and the CLI agree. Its only setting is the document (Q39 (a)).
- **Seeking** (ui.md §2.4, measured): a frame whose timestamps continue from the stream's start, and content after an ad whose timestamps restarted, are reached through the existing `s_seek_to`, once per click release and never while scrubbing (the "buzz on one note" guard, `src/main.cpp:3333-3339`); the frames of an ad whose timestamps restart at 0.040 s were reached by no seek with FFmpeg 8.1.1, and an ad restarting at the main content's origin, as D-069's does, was not probed (MEASUREMENT PENDING). What the panel does there is Q37. The seek decision needs each variant's segments and discontinuities, which the document's `segment` events carry, so the panel fetches nothing.
- **Its math** (`TimelineView`, `TimelineMatch`; WP12), ImGui-free and tested in `mirage_tests`; `nice_step` and `axis_label` are copied from `src/NetInspectorAlign.cpp:69-107`, which stays unchanged (P-AJ).
- **Input capture**: the window uses `NoNav` and drops focus as `NetInspector` does (`src/NetInspector.cpp:387-394`; ui.md §4.1).
- **QA** (ui.md §5.3): `--timeline <document>`, `--ui-timeline <spec>`, `--timeline-report <txt> <secs>`, run as the repository's scripted runs are, outside ctest; the loader's checks and handoff are in ctest through `TimelineLoad`.
- **D-007.** By the agent's reading (ui.md §3) none of the recommended changes touches the render or decode path: the panel is an ImGui window in the overlay frame; its seeks go through the entry point the seek bar uses; its height joins the overlay's layout, not the video rectangle. The changes that would touch it, each needing a written plan approved first: a seek with its own landing point, or play to the frame then pause (Q37 (b)); another way to position the hls demuxer (Q37 (c)); scrubbing by dragging on the panel (not proposed); the player's own session log (Q13 (b)). `docs/plans/timeline-ui.md` (WP15a, committed before any answer) is this section as the written plan Q36 asks the owner to approve.

---

## 11. Differences from the work order, to report

None of these is fixed by editing a document, as the work order's constraints require; each goes into `docs/PHASE3_DECISIONS_PENDING.md` (WP15a) and the description of Phase 3's PR (WP15b), in this design's words, and both pass the quote check before they are committed or posted (P-AK). The question that settles each is named; "Q42" marks those no option of Phase 3 removes.

1. **The network component's name.** The work order calls it NetworkDebugger, an `av_log` interceptor; the code's `NetworkLogger` also hooks `io_open`/`io_close2` and is built into the player and `mirage_tests` only (sources.md §10, item 1). Under Q10 (a) it is compiled into `mirage_vmaf` and times a fetch, not a playback (Q10); how the two names are handled is Q41.
2. **ABR and the player's switches.** Mirage has no ABR (D-068); switches come from scripted plans, and the player's log is Q13 (Q42).
3. **The decoder's facts.** The offline decode drops no frame of its own; frames a decoder never output are the report's unpaired frames, filed under `vmaf`, where §3.1 groups dropped frames with the decoder (Q04 (a), Q43 (d)); frames the player drops at a switch are not in Phase 3's scores (D-068's limits), and under Q13 (a) Phase 3 has no decoder events for them either; decode errors only under Q09 (a) or (b) (Q09, Q13, Q42).
4. **The key.** PTS alone repeat (design-evaluation run 09); events sit on reference frames with (sequence, PTS) as their key, and offsets are seconds of the axis, the reference's playlist time under Q02 (c) or frames times the frame duration under (d), not PTS differences (Q02).
5. **The wall clock.** Only observed events have one (Q03 (b)), so offline events, all of an acceptance test's among them, have none (Q03).
6. **Network events need a session.** A ladder built without a session has no network events, both acceptance tests included (Q10; Q10 (e) would add scoring-paced ones).
7. **DASH.** `mirage_vmaf` scores HLS ladders only; DASH's period boundaries are a reserved type and its `EventStream` an interface, as D-070 has it (Q20).
8. **Splice points.** Several published messages carry no splice time, and the HLS-only tags none, so a cue's place is often only its tag's position (scte35.md §9, item 3); splice points are their own events where a time exists (Q14).
9. **D-070's list** omits `#EXT-X-SCTE35`, `#EXT-X-SPLICEPOINT-SCTE35` and Adobe's `#EXT-X-CUE` (Q18); the one public VOD stream found carries SCTE-35 in its segments as ID3, not as stream type 0x86 (scte35.md §9, item 2).
10. **Rung numbers.** The example's switch between numbered rungs assumes a numbering the report doesn't have (Q07).
11. **The top rung in top-rung mode** has no score, so a session's spans on it have none, and an ad low in every rung can't show as a drop in that mode (Q12; D-002's limit; Q42).
12. **The HTTP status** isn't exported by FFmpeg 8.1.1's API ([http.c#L180-L206](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/http.c#L180-L206)); it comes from `av_log` text through `NetworkLogger`, or as a class from a failed open (Q10, Q42).
13. **§7's single output, and §7's attribution shape.** Under Q01 (B) the timeline and attributions are a separate document; attributions group events by source, give offsets and carry the detection parameters, beyond §7's flat list; `tool_version` and `git_commit` wait for Phase 5 under Q05 (a) (Q01, Q04, Q05).
14. **§3.3's seek from a clicked low point** doesn't reach the frames of an ad whose timestamps restart at 0.040 s with FFmpeg 8.1.1 (measured); an ad that restarts at the main content's origin, as D-069's does, was not probed (MEASUREMENT PENDING) (Q37).

A repository document found wrong, reported and not edited: `docs/VMAF_GAP_REPORT.md:230-231` says the Decoder has a dropped-frame counter; it is the render loop's `total_dropped` (`src/main.cpp:1751`; sources.md §12) (Q42; under Q42 (b) WP15b adds a dated correction line to it).
