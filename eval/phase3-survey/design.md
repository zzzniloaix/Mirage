# Phase 3 design: the unified event timeline (§3)

The design of the work order's §3 (the unified event timeline, §3.1; attribution queries, §3.2; the UI, §3.3) under the owner's decisions D-068, D-069 and D-070, with D-002, D-007, D-046, D-050 and D-066. It merges two designs written from different angles, `design-library.md` (the timeline as a C++ library with a small API) and `design-evaluation.md` (worked back from what the paper's evaluation must show), both built on the five surveys (`sources.md`, `scte35.md`, `testgen.md`, `schema.md`, `ui.md`), and is revised three times: against its adversarial review (`review-adversarial/review.json`; `design/revision-1.md`), against a second review (`review-2/review.json`; `design/revision-2.md`) and, by the lead, against the seven minor issues of a third review (`review-3/review.json`; `design/revision-3.md`), each log saying what changed, issue by issue. **Nothing of Phase 3 is implemented.** This document says how to build it and what the owner has to decide first.

**Conventions**

- `<repo>` is the worktree of `vmaf/phase-3` at `75cf6e0`; a `file:line` without a prefix is in it. `<out>` is the survey directory, `<scratch>` the session's scratch directory. FFmpeg is cited at the tag `n8.1.1` by its public URL, RFCs by theirs.
- **Numbers.** Every number about this code, these streams or a run comes from a cited `file:line` or URL; from a survey file in `<out>`, named next to it; or from a run: of one of the two designs (`design-library/runs/<name>.txt`, `design-evaluation/runs/NN_<name>.txt`), of the review (`review-adversarial/runs/<name>.txt`) or of this design (`design/runs/<name>.txt`, made by `design/tools/run.sh`), each record holding its working directory, exact command, start and end (UTC), full output and exit status (Appendix A). Where there is no source the text says MEASUREMENT PENDING. Parameters a prototype ran with are named as the prototype's; none is a default.
- **Citations.** Only what is committed with the survey is cited for a fact: not its generated media, binaries, copies of FFmpeg's source, third-party downloads or the SCTE-35 messages it collected (Q21). For those, the record or survey section that states the fact is cited, or the public URL.
- **Decisions are the owner's.** Every design choice is an owner question, Q01 to Q44 (§12), asked in three rounds: round 1 holds the answers the packages on the path to §3.2's two acceptance tests need before they start (27 questions); round 2 those that sessions, the session fetch, the decoded SCTE-35 messages and the composite example need (9); round 3 those of the UI, the conditional and later packages and the differences to report (8). Each question has 2 to 5 options with their trade-offs, or items that each have them, and a recommendation labelled as the agent's; a D-007 mark shows where the answer is an approval or a ruling under D-007. P-entries (§13), lettered from P-A to P-AK with nine letters moved into questions by revision 1, are the agent's choices of process and measurement only: how something is built, tested or measured, with no effect on what an output says by default. They await the owner's confirmation and are numbered from P-082 when recorded in `docs/DECISIONS.md`, P-081 being the entry that records how this survey and design were made and what of their records is committed. Nothing here is decided. The same questions, packages, P-entries and risks are in `design/owner_questions.json`, `design/work_packages.json`, `design/p_entries.json` and `design/risks.json`, from which §12 to §15 are rendered (`design/tools/render.py`).
- **No quotation of the work order** (D-013; D-060 accepted Phase 2's rendering of it in its own words). Its requirements are given here in this design's words; field, tag, option and component names stay as names. `design/runs/quote-check.txt` records the check of every file of the design against the translation, which names no shared words.
- "The work order" is the owner's document as translated in the task. D-0xx and P-0xx are entries of `docs/DECISIONS.md`. Names of files, types and options are proposals; the owner may rename them.

---

## Where Phase 3 stands (2026-11-02)

- **Done.** The five surveys of §3's sources, SCTE-35, the synthetic test content, the report against §7, and the player's UI. Two designs, each with prototypes and saved runs (`design-library/`: 14 records; `design-evaluation/`: 12 runs). This merged design, which resolves their conflicts; an adversarial review of it and revision 1 against that review (`design/revision-1.md`); a second review (`review-2/`) and revision 2 against it (`design/revision-2.md`); a third review (`review-3/`), which found no blocker and no major issue, and revision 3, the lead's, against its seven minor issues (`design/revision-3.md`). The design's own records are in `design/runs/` (Appendix A).
- **Not done.** Any Phase 3 code. The code is as at `75cf6e0`: the only commit since on `vmaf/phase-3`, `7c0e8d2`, adds this survey's records under `eval/phase3-survey/` and changes no other file (`design/runs/repo-state.txt`).
- **Next.** The owner's answers to round 1's 27 questions (§12), which every package on the path to the two acceptance tests waits for, the wiring package WP00a included; then round 2's 9 and round 3's 8, 44 questions in all. WP15a can start now: it records the questions, the differences and the P-entries as pending (`docs/PHASE3_DECISIONS_PENDING.md`, `docs/DECISIONS.md`) and commits §10 as the UI's written D-007 plan (`docs/plans/timeline-ui.md`). Every other package names the questions that block it (§14).

## 0. Summary

1. **What Phase 3 must make possible** (from design-evaluation §1): attribution that is right on known causes, checked against ground truth the test content's generator writes (E1, D-069's two ladders); the work order's example answer, a drop that overlapped an SCTE-35 cue-out, a switch between two numbered rungs and a segment request of 2.3 s, from a scripted session whose HTTP timing is measured, not declared (E2); later, D-011's real ladders (E3); reproduction from one command (E4); costs on real ladders (E5, MEASUREMENT PENDING).
2. **Architecture** (from design-library §2): a core of plain C++20 modules (namespace `timeline`, no libavformat, libavcodec, libvmaf, GL or ImGui) for the model, the merge, the overlap query and detection; FFmpeg adapters at its edge for the demux pass and the session fetch; two thin `mirage_vmaf` subcommands and the player's panel on top. The ladder report is read as §2 writes it (D-068: scored once); nothing in the scorer changes unless the owner picks an option that changes it (§3.6).
3. **One time base** (§4). Every event is placed at a reference frame, and intervals are compared on reference frames, exact integers. Every event also keeps its media key, the reference's discontinuity sequence and PTS, which raw PTS alone can't be: 50 PTS values each name two frames in testgen's ad ladder (design-evaluation run 09). The seconds in which offsets are printed are Q02; the agent recommends the reference's playlist time, with the frame index times the frame duration where the reference has no segment map, the two compared at every frame wherever both exist. They agree on the four ladders checked; after a simulated missing frame or change of rate the frame index's seconds were off and the playlist time's weren't, and after an EXTINF simulated a frame too long the reverse (`design/runs/seconds-check.txt`); a frame missing at the start of a playlist discontinuity sequence put both a frame early within its segment, where neither part of §4.2's check sees it, and only the frame index's from the next segment on (`review-3/runs/seconds-check-sequence-start.txt`). Which events an interval lists doesn't depend on that choice.
4. **Sources** (§5). Per-pair VMAF curves; each scored media playlist read and parsed for its segments and tags; a demux pass that places every scored frame in its segment by byte position, checked against the report's PTS (200 of 200 frames on each of 15 playlists, design-evaluation runs 03 and 04); D-070's SCTE-35 tags with a splice_info_section decoder, and one cue interface for every carrier, D-070's DASH and in-segment interfaces included; scripted sessions as segment ranges (D-068); their HTTP timing from a fetch timed by the player's own `NetworkLogger` (a prototype measured 2.313 s for a segment served 2.3 s late, design-evaluation run 06); keyframes and corrupt packets from the demux pass; decode errors only if the owner allows the analyzer to record them (Q09).
5. **Queries and detection** (§6). `timeline::attribute()` returns the events meeting a half-open interval of a pair's curve or a session's, by §3.1's six sources, with offsets in seconds and in frames; `timeline::detect()` finds drops below a trailing baseline. A session's drops are its pairs' drops clipped to the spans that played them (Q22). With the prototype's parameters the two acceptance drops were found exactly, with no drop in their controls, on a grid of T from 3 to 40 and N from 1 to 24 (design-evaluation run 05), so the tests pin their own parameters and don't wait for the defaults (Q26).
6. **Output** (§8). A separate, versioned timeline document per build, naming its ladder report by two hashes; scores are not copied (Q01, Q04 to Q06).
7. **Tests** (§9). A unit-test file per module; the two acceptance tests of §3.2 in one test case, on ladders made once per process from testgen's prototype (P-Q); the composite example on a scripted session served with a delayed segment; an eval harness that reproduces it all from one command.
8. **UI** (§10). ui.md's panel, reading the document through an ImGui-free loader tested in ctest and calling the same query API; by the agent's reading nothing recommended touches the render or decode path; the measured limit (no seek reached an ad whose timestamps restart at 0.040 s) is Q37.
9. **For the owner.** 44 questions in three rounds of 27, 9 and 8 (§12), 14 differences from the work order (§11), 28 P-entries (§13), 25 work packages (§14), 22 risks (§15).

---

## 1. How the two designs were merged

### 1.1 What each contributes

- **From design-library**: the layered structure (a core that takes and returns plain structs, adapters that are the only code opening inputs, consumers with no logic of their own), which is how `HlsAlign` and `VMAFAlign` are already built and unit-tested; the placement rules (every event at a reference frame); the per-module files and unit tests; the API sketch; a document that writes each event once and refers to it by id; the scores digest that survives redaction.
- **From design-evaluation**: the evaluation map (E1 to E5) as the measure of what Phase 3 must deliver; the critical path to the acceptance tests; tests that don't depend on open choices, with detection parameters pinned inside a measured region; the measured session fetch; a wiring package that owns `CMakeLists.txt`; segment-range session plans; strict overlap with optional margins; both BANDWIDTH ranks; the eval harness; the conditional and later packages.
- **New in this merge**: placement and comparison on reference frames in every option but raw PTS, which separates which events an interval lists from the seconds its offsets are printed in, and narrows the axis conflict to the seconds; a session's drops as its pairs' drops clipped to the spans that played them; this design's runs (`design/runs/`), which settle conflicts with numbers.
- **New in revision 1**: the questions in rounds, with the package split that makes the acceptance path wait for round 1 only (WP00a, WP09a, WP11a); the P-entries that changed default outputs moved into two questions of items (Q43, Q44); the axis check made sound (`design/runs/axis-check.txt`); D-070's interfaces declared (§5.5); one test process for the acceptance cases (P-Q).
- **New in revision 2**: the axis check compares (c)'s and (d)'s seconds at every frame, since revision 1's check of steps missed a change of rate (`review-2/runs/rate-change-check.txt`; `design/runs/seconds-check.txt`), and the agent's recommendation for Q02 moved from (d) to (c) with (d) as its fallback; a session's curve and scope given to WP07, after its types exist; WP09b split so that the composite example doesn't wait for the SCTE-35 decoder (WP09c); WP15 split so that its first part starts now (WP15a); the fetch's pacing and playlist fetches moved from P-N into Q44; Q41 narrowed to the component's name; every conditional option given a package (§3.6), and the Blocked-by lists completed and checked by `design/tools/render.py`.
- **New in revision 3**, by the lead, against the third review's seven minor issues: a limit of §4.2's check stated and tested (a reference frame missing at the start of a playlist discontinuity sequence); RFC 8216's rule on whole-second EXTINF stated as it is; Q16's count corrected; the Blocked-by lists completed with the eight answers the review named; WP00b's session types put in headers it owns in turn; WP00a's list of new files and P-C made to say what WP00a's description says; WP15b split so that the closing documents come last (WP15c).

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
| `docs/PHASE3_STATUS.md` | docs | the status, item by item: committed with the survey, updated by the lead as each round is answered, completed last | WP15c |
| `eval/timeline/*` | eval | the one-command reproduction | WP14 |

A file has one owner at a time; where two packages own it in turn, the second depends on the first and starts after it has merged (`design/tools/render.py` checks every such pair). Shared files and their owners: `CMakeLists.txt` (the wiring packages WP00a, WP00b and WP00c in turn, each depending on the one before, and the lead's wiring commit when a conditional package starts, made while no wiring package is open; P-C); `src/TimelineModel.cpp` and `src/TimelineQuery.cpp` (WP01, then WP07 for a session's curve and scope); `src/TimelineBuild.{h,cpp}`, `src/TimelineCli.{h,cpp}` and their tests (WP09a, WP09b, WP09c in turn, then WP16 for `src/TimelineBuild.cpp`); `src/TimelineDoc.{h,cpp}`, its test and the two format documents (WP08, then WP09b); `tools/mirage_vmaf.cpp` and `src/VMAFCli.cpp` (WP09a); `tests/HttpFileServer.h` (WP07); `src/VMAFNet.h`, a comment only (WP00b, under Q10 (a)); `src/NetworkLogger.{h,cpp}` and the files that name it (WP00b, only under Q41 (b), before any later owner of those files); `src/main.cpp`, `src/PlayerUI.{h,cpp}`, and under the answers §3.6 names `src/Demuxer.{h,cpp}`, `src/ManifestParser.{h,cpp}` and `src/DebugHUD.{h,cpp}` (WP13, then WP17 for `src/main.cpp` and `src/Demuxer.cpp`); `src/VMAFAnalyzer.{h,cpp}`, `src/VMAFReport.{h,cpp}`, `src/VMAFSegments.h`, `src/VMAFLadder.{h,cpp}` and their tests (WP16, conditional; `src/VMAFReport.*` WP08's first under Q01 (A)); `src/LadderReportRead.cpp` (WP03, then WP16); `docs/DECISIONS.md` (WP15a, then WP15c, with the lead's records of each round's answers between); `docs/PHASE3_DECISIONS_PENDING.md` (WP15a, then WP15b for the differences' wording, with the lead's marks of each round's answers between); under Q42 (b), `docs/VMAF_GAP_REPORT.md` (WP15b); `docs/AI_CONTRIBUTIONS.md`, `docs/PAPER_METHOD_NOTES.md` and `docs/PHASE3_STATUS.md` (WP15c). `src/NetInspectorAlign.cpp` changes only under Q41 (b): WP12 copies its two axis helpers (P-AJ). What stays unchanged unless an answer says otherwise is §3.6.

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
- RFC 8216 asks for EXTINF accurate enough that their sums don't drift, requiring whole seconds below compatibility version 3 and allowing them at every version ([§4.3.2.1](https://www.rfc-editor.org/rfc/rfc8216#section-4.3.2.1)), and for an `EXT-X-DISCONTINUITY` where encoding parameters change ([§4.3.2.3](https://www.rfc-editor.org/rfc/rfc8216#section-4.3.2.3)): an ad at another frame rate after its discontinuity is a stream to expect, and so is a playlist with whole-second EXTINF.
- (c) needs the reference's segment map; (d) is defined for every reference from the report alone, a mezzanine (D-002's alternative) or a variant whose map was refused (Q43 (c)) included.

**The check of the seconds** (under (c) and under (d)). Wherever the reference's segment map exists, both clocks are computed at every scored reference frame and two things are checked, each with half a frame's tolerance, the aligner's (`src/VMAFAlign.h:23-25`): (1) (d)'s seconds against (c)'s; (2) each frame's PTS against the PTS of the first frame of its playlist discontinuity sequence plus its distance in frames times one frame duration. Each run of consecutive frames beyond the tolerance is written to `axis.warnings` with its first and last frame and its largest difference. (1) bounds the difference between the two clocks wherever it comes from, whichever is printed; (2) says whether the frames themselves leave the frame grid (a missing frame, a change of rate), so that a run of (1) without one comes from the playlist's EXTINF disagreeing with the frames' count, or from a reference frame missing at the start of a playlist discontinuity sequence: (2) starts each sequence's grid at its first scored frame and can't see that frame missing, and within the segment that lost it (c) too starts at its first remaining frame, so neither test warns there and (1) warns from the next segment on, where the case can't be told from an EXTINF one frame too long. Without a segment map, (2) runs on the report's sequences instead: for a reference that isn't a playlist, which signals no discontinuity, its runs are warnings; for a playlist whose map was refused they are listed as facts, claiming nothing about the seconds, since a jump under 10 s stays inside one report sequence (`src/VMAFAlign.h:114`) and may be one the playlist signals.

What the check did (`design/runs/seconds-check.txt`): on the four ladders, (1) and (2) warn of nothing; without the map, each ad ladder whose ad restarts at the main content's origin (x264 and MPEG-2) lists one run, frames 125 to 199, whose PTS lie 3.000 s past their report sequence's frame grid, and the other two none; two mezzanine-mode reports, whose reference is one file, give no run. In the simulations, (1) warns from the frame where the difference first passes half a frame to the last frame: from frame 40 or 180 after a missing frame; from 88, 79, 77 and 76 for the ad at 24, 30, 50 and 60 fps; from 50 after the long EXTINF; (2) warns within the missing frames' sequences and within the re-timed ads, and not for the EXTINF. Comparing (d) with (c) at segment starts only, as the second review suggested, flags the same cases from the next segment start on, but misses the frame missing from the last segment, which no segment start follows; comparing at every frame catches it and warns within a segment, from frame 79 rather than 100 for the ad at 30 fps. The first version of this design checked each PTS against its report sequence from the report alone, which flagged 75 of 200 frames of each of those two ad ladders where both clocks are right (`design/runs/axis-check.txt`; `review-adversarial/runs/axis-d-check.txt`); revision 1 compared each step between frames with one frame duration, which flagged the missing frame and the ad at 60 fps but not the ads at 24, 30 and 50 fps nor the EXTINF, every step there staying within half a frame (same record; `review-2/runs/rate-change-check.txt`). The third review removed the first frame of each of the ad ladder's three playlist discontinuity sequences in turn (frames 0, 75 and 125, simulated): (2) warned of nothing, and (1) from frame 24, 99 or 149 to frame 198, at −0.040 s, from the next segment on (`review-3/runs/seconds-check-sequence-start.txt`).

**Agent's recommendation: (c)**, with (d) where the reference has no segment map and this check wherever both exist, every event also carrying its media key and, where known, its playlist time. Since the check computes both anyway, the choice between them is which seconds are printed where they part: (c) is right where the frames leave the grid, as at an ad at another frame rate, and is off only where an EXTINF departs from its frames, which RFC 8216 advises against, and within a segment that lost its playlist discontinuity sequence's first frame, where (d) is off as well. **(d) alone is a close second**: one definition for every reference and both reference modes, and the better choice if the owner expects playlists with rounded or whole-second EXTINF. Offsets are seconds of the axis, not PTS differences: a difference from §3.1 to report (§11, item 4).

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
| `test_timeline_axis.cpp` | WP01 | media keys on hand-made reports with PTS that restart at the main content's origin, a forward jump under 10 s, a coarser-grid pair; the seconds of Q02's option and of its fallback; the check of §4.2 in the shapes of `design/runs/seconds-check.txt`: no warning on the D-069 ad shape with its segment map and its run of frames 125 to 199 listed as a fact without one; warnings at a missing frame, the last segment's included, at an ad at another frame rate after its discontinuity and at an EXTINF a frame too long, the PTS check (2) warning for the first two only; a reference frame missing at the start of each playlist discontinuity sequence, in the shapes of `review-3/runs/seconds-check-sequence-start.txt`, warned by (1) alone, from the next segment on; a warning for a reference that isn't a playlist where its PTS change rate; seconds to frames |
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

None of these is fixed by editing a document, as the work order's constraints require; each goes into `docs/PHASE3_DECISIONS_PENDING.md` (WP15a) and the description of Phase 3's PR (WP15c, in the wording WP15b settles), in this design's words, and both pass the quote check before they are committed or posted (P-AK). The question that settles each is named; "Q42" marks those no option of Phase 3 removes.

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
14. **§3.3's seek from a clicked low point** doesn't reach the frames of an ad whose timestamps restart at 0.040 s with FFmpeg 8.1.1 (measured: ui.md §2.4, from `experiments/hls-seek-probe/run-output.txt`); an ad that restarts at the main content's origin, as D-069's does, was not probed (MEASUREMENT PENDING) (Q37).

A repository document found wrong, reported and not edited: `docs/VMAF_GAP_REPORT.md:230-231` says the Decoder has a dropped-frame counter; it is the render loop's `total_dropped` (`src/main.cpp:1751`; sources.md §12) (Q42; under Q42 (b) WP15b adds a dated correction line to it).

---

## 12. Owner questions

44 questions in three rounds: 27 in round 1, the answers the packages on the path to §3.2's two acceptance tests need before they start; 9 in round 2, those that sessions, the session fetch, the decoded SCTE-35 messages and the composite example need; 8 in round 3, those of the UI, the conditional and later packages, and the differences to report. A question's round is the earliest round of the packages it blocks. Each question stands alone: its options carry their trade-offs, the recommendation is the agent's, and nothing is decided. A question of items (Q26, Q39, Q43, Q44) is answered item by item, an item's options numbered: "Q43 (c) 2". "D-007" marks the questions whose answer is, or includes, an approval or a ruling under D-007 (Q01, Q08, Q09, Q10, Q13, Q36, Q37, Q41). "Blocks" names the work packages of §14 that wait for the answer. The same text, with each question's one-sentence summary, is in `design/owner_questions.json`.

### Round 1: the path to §3.2's two acceptance tests (27 questions)

| # | Question | Agent's recommendation | Blocks | D-007 |
|---|---|---|---|---|
| Q01 | Where the timeline and its attributions are written | (B) | WP00a, WP08, WP09a, WP11a, WP09b, WP11b, WP12, WP16 | yes |
| Q02 | The single time base: where events sit, how they are compared, and the seconds of offsets | (c), falling back to (d) where the reference has no segment map, with design §4.2's check; (d) alone a close second | WP00a, WP01, WP08, WP09a, WP11a, WP12, WP14 |  |
| Q03 | Which events have a wall clock, and on what clock | (b) | WP00a, WP08, WP07, WP09b |  |
| Q04 | The timeline document's layout and event vocabulary, as its first schema version | (a) | WP00a, WP04, WP08, WP09a, WP11a, WP05, WP07, WP09b, WP11b, WP14 |  |
| Q05 | Versioning and provenance of the timeline document | (a) | WP00a, WP08, WP09b, WP12 |  |
| Q06 | How a timeline document names its ladder report | (c) | WP03, WP08, WP09a, WP12 |  |
| Q07 | How rungs are named in outputs | (d) | WP00a, WP08, WP09a, WP07, WP09b, WP11b, WP14 |  |
| Q08 | Where each scored frame's segment, and the keyframes, come from | (a) | WP00a, WP06, WP09a, WP16 | yes |
| Q17 | An ad break's end when several sources give a duration | (a) | WP04 |  |
| Q18 | SCTE-35 signalling beyond D-070's list | (a) | WP04, WP05 |  |
| Q19 | A bare EXT-X-DISCONTINUITY as an ad boundary | (a) | WP04, WP09a |  |
| Q20 | DASH period boundaries | (a) | WP00a, WP04, WP09a |  |
| Q22 | Which curves automatic detection runs on | (c), with design §6.3's three rules | WP02, WP08, WP09a, WP11a, WP07, WP09b, WP11b, WP14 |  |
| Q23 | The trailing baseline's statistic and warm-up | (a) | WP02, WP08, WP11a, WP11b, WP14 |  |
| Q24 | Units of W and N, and points without a score | (a) | WP02, WP08, WP09a |  |
| Q25 | Frames that start a libvmaf context (D-037) | (a) | WP02, WP08, WP11a, WP11b, WP14 |  |
| Q26 | Detection's defaults, and when the timeline subcommand detects | (a) 1; (b) 2 | WP02, WP08, WP09a |  |
| Q27 | What drop_magnitude measures | (c) | WP02, WP08, WP09a |  |
| Q28 | Which variants' events an attribution lists | (a) | WP01, WP08, WP09a, WP07 |  |
| Q29 | Which events overlap an interval | (c) | WP01, WP08, WP09a, WP11a |  |
| Q30 | Ordering and ranking of the events an attribution lists | (a) | WP01, WP08, WP09a |  |
| Q31 | The CLI subcommands: shape, names and options | (a), with design §7's option set | WP00a, WP09a, WP11a, WP14 |  |
| Q32 | The JSON reader | (a), moving to (b) if a full report's load needs it | WP00a, WP03 |  |
| Q33 | The encoder of the acceptance tests | (c) | WP10, WP11a |  |
| Q34 | The ad's timestamps in acceptance test 2 | (d) | WP10, WP11a |  |
| Q35 | The rest of the synthetic content | (a) | WP10, WP11a |  |
| Q43 | The ladder timeline's defaults: eight choices that change its output | (a) 1; (b) 1; (c) 1; (d) 1; (e) 1; (f) 1; (g) 1; (h) 1 | WP00a, WP01, WP02, WP03, WP04, WP06, WP09a |  |

### Round 2: sessions, the session fetch, decoded SCTE-35 messages and the composite example (9 questions)

| # | Question | Agent's recommendation | Blocks | D-007 |
|---|---|---|---|---|
| Q10 | Where a scripted session's per-segment HTTP timing comes from | (a) | WP00b, WP07, WP09b, WP11b, WP14, WP16 | yes |
| Q11 | The session plan's format | (a) | WP00b, WP07, WP09b, WP11b, WP17 |  |
| Q12 | A session's spans that play the reference in top-rung mode | (a) | WP00b, WP07, WP09b, WP16 |  |
| Q14 | The anchor that places an SCTE-35 cue | (a) | WP05 |  |
| Q15 | Which decoded SCTE-35 messages count as cue-outs and cue-ins, and cancellations | (a) | WP05, WP09c |  |
| Q16 | An SCTE-35 message whose CRC_32 fails | (a) | WP05, WP09c |  |
| Q21 | Which SCTE-35 test vectors may be committed | (a), until the owner settles the licences, as with D-008 | WP05 |  |
| Q41 | The component's name: the work order's NetworkDebugger against the code's NetworkLogger | (a) | WP00b, WP07, WP15b | yes |
| Q44 | Sessions, the fetch and decoded messages: seven choices that change their output | (a) 1; (b) 1; (c) 1; (d) 1; (e) 1; (f) 1; (g) 1 | WP00b, WP05, WP07, WP09b, WP11b |  |

### Round 3: the UI, the conditional and later packages, and the differences to report (8 questions)

| # | Question | Agent's recommendation | Blocks | D-007 |
|---|---|---|---|---|
| Q09 | Decode errors in the timeline | (a) if the owner so rules or approves the plan; otherwise (c) | WP16 | yes |
| Q13 | The player's own session log in Phase 3 | (a) | WP17 | yes |
| Q36 | The §3.3 UI plan under D-007, and the panel's place | (a), built after the headless packages | WP00c, WP12, WP13 | yes |
| Q37 | Clicking a low point inside an ad whose timestamps restart | (a) for Phase 3 | WP12, WP13 | yes |
| Q38 | The timeline panel's toggle key | The owner's preference; L if a key is wanted | WP13 |  |
| Q39 | The panel's settings, and what a click outside a drop queries | (a) 1; (b) 2 | WP12, WP13 |  |
| Q40 | The player's tag ruler and ManifestParser's cue defects | (a) now, (c) with the UI package | WP13 |  |
| Q42 | Differences from the work order with no alternative in Phase 3 | (a) | WP15b |  |

### Round 1, the questions in full

#### Q01. Where the timeline and its attributions are written

*Round 1; **D-007**: the answer is, or includes, an approval or a ruling under D-007.*

**Summary.** Q01 decides whether the timeline's events and attributions go into the ladder report or into a document of their own; the agent recommends a separate timeline document that names its ladder report by hash and copies no scores.

§7 places `timeline` and `attributions` in mirage_vmaf's one JSON output, while D-068 scores a ladder once and attributes many sessions against it. Where are the timeline's events and the attributions written? Option (C) needs the analyzer to record events while scoring, which under D-007 is the owner's to rule on, or to approve as a written plan.

- **(A) In the ladder report.** Top-level `timeline` and `attributions` next to `pairs`, as §7 lists them, with no JSON reader needed. But each new session means scoring again (the full advanced ladder took 1882.86 s, schema.md §3.3) or rewriting the scored file, and per-frame lines are already 99.91% of a full report (same section).
- **(B) A separate timeline document, naming the ladder report by hash.** One ladder report and at most one session per document; scores are read from the report, not copied, so the scoring run's output and every committed comparison stay as they are. The prototype's documents for 200-frame ladders were 8577 to 14470 bytes (design-evaluation run 11); it needs a JSON reader, a hash rule that survives redaction (Q06) and a reported difference from §7.
- **(B') As (B), with the session's per-frame curve copied.** The document reads without its report, but it grows with the frames (36000 entries for one 60 fps pair of Apple's advanced sample, schema.md §3.3) and repeats scores the report holds.
- **(C) Per-variant events in the report, the session in a document.** The per-variant events come from the scoring run itself, but the report changes (a key under `pairs[]` breaks the exact key count at tests/test_vmaf_report.cpp:909) and the analyzer must record them, a D-007 matter. That recording would be WP16's and would move into round 1, ahead of the packages that read it (design §14).

**Agent's recommendation:** (B).

**Why:** It keeps D-068's one scoring for many sessions, leaves the committed records and their checks valid, and keeps per-frame data in the report; its costs (a JSON reader, a hash rule, a reported difference) are each one bounded package.

**Blocks:** WP00a, WP08, WP09a, WP11a, WP09b, WP11b, WP12, WP16.

#### Q02. The single time base: where events sit, how they are compared, and the seconds of offsets

*Round 1.*

**Summary.** Q02 decides on what clock events are placed and compared and in which seconds offsets are printed; the agent recommends placing and comparing on reference frames and printing the reference's playlist time, falling back to the frame index times the frame duration where the reference has no segment map, with the two compared at every frame wherever both exist and the frame-index seconds everywhere as a close second.

§3.1 asks for one time base keyed by media PTS, the reference's timeline, but where an ad's timestamps restart at the main content's origin raw PTS repeat: 50 PTS values each name two reference frames in testgen's ad ladder (design-evaluation run 09). Under (b) to (d) events are placed and compared on reference frames and keep their media key (the report's sequence and the PTS), so the events an interval lists are the same and only the printed seconds differ, apart from durations given in seconds (design §4.2). Wherever the reference has a segment map, design §4.2's check computes both (c)'s and (d)'s seconds at every frame and warns where they differ by more than half a frame, so the choice between them is which to print there, and what to print where there is no map. On what clock are events placed and compared, and in which seconds are offsets printed?

- **(a) Raw reference PTS for everything.** The work order's wording, nothing derived. On the ad ladder it lists main_000.ts and main_001.ts for the ad's drop (design-library run axis-ad), and offsets step back by 2.960 s at the ad's first frame (design-evaluation run 09).
- **(b) Reference frames; seconds of the PTS, the report's sequences laid end to end.** Needs only the ladder report. A report sequence breaks only at a backward step or a forward jump of more than 10 s (src/VMAFAlign.h:114), so smaller jumps stay in the seconds: the 2 s ad break spans 5.000 s (design-evaluation run 09).
- **(c) Reference frames; seconds of the reference's playlist time.** Each frame at its segment's EXTINF sum plus its PTS offset in the segment: equal to (d) on the four ladders checked, and right through a missing frame or a change of rate, after which (d) was 0.040 s and 0.083 to 1.167 s off (simulated, design/runs/seconds-check.txt), except within a segment that lost its playlist discontinuity sequence's first frame, where both were a frame early (review-3/runs/seconds-check-sequence-start.txt). It is off instead where an EXTINF doesn't match its segment's frames, which RFC 8216 §4.3.2.1 advises against (0.040 s after one EXTINF simulated a frame too long, same run). It needs the reference's segment map, so a reference without one (a mezzanine, a refused map) falls back to (d).
- **(d) Reference frames; seconds = frame index times the frame duration.** Defined from the report for every reference, a mezzanine included, and equal to (c) on the four ladders checked (design/runs/seconds-check.txt). It is off after a missing frame or a change of rate, by the amounts under (c), and right where an EXTINF is off; design §4.2's check, which needs the segment map, warns at the frames where it leaves (c) by more than half a frame.

**Agent's recommendation:** (c), falling back to (d) where the reference has no segment map, with design §4.2's check; (d) alone a close second.

**Why:** Frames make the events an interval lists exact and independent of the seconds (design-evaluation runs 07 and 08 showed doubles listing boundary events inconsistently). Since design §4.2's check computes both wherever the reference has a segment map, the choice is which seconds to print where they differ: (c) stays right after a missing frame or a change of rate (from the next segment on if the frame missing is a sequence's first), and RFC 8216 §4.3.2.3 puts changes of encoding parameters at discontinuities such as an ad's, while (c) is otherwise off only where an EXTINF departs from its frames; (d) gives every reference and both reference modes one definition, the better choice if the owner expects rounded or integer EXTINF.

**Blocks:** WP00a, WP01, WP08, WP09a, WP11a, WP12, WP14.

#### Q03. Which events have a wall clock, and on what clock

*Round 1.*

**Summary.** Q03 decides what an event's wall clock holds; the agent recommends a wall clock only for observed events (the session fetch now, the player's log later), as microseconds of one steady clock from a UTC anchor sampled at the session's start, and none for offline events.

§3.1 records a wall clock beside media time without saying which clock. Offline facts (scores, playlist tags, decoder facts) were observed at no particular time, while a session fetch's requests were. What does an event's wall clock hold?

- **(a) The observation time of observed events only.** When a fetch's request was made, later also the player's log; null for offline events; no common origin stated.
- **(b) (a), as microseconds from one UTC anchor per session.** One steady clock from the session's start plus a UTC time sampled then (sources.md §11, choice 7 (c)); a segment's EXT-X-PROGRAM-DATE-TIME stays in its payload. Monotonic and comparable within a session; offline events, and so every event of an acceptance test, have none (design §11, item 5).
- **(c) A date from EXT-X-PROGRAM-DATE-TIME for offline events too.** Every event under a PDT gets a date, but a PDT is a program date, not an observation, and many playlists have none.

**Agent's recommendation:** (b).

**Why:** It records only what was observed, on one monotonic clock; NetworkLogger already keeps steady microseconds and maps them to wall time (src/NetworkLogger.h:189-192).

**Blocks:** WP00a, WP08, WP07, WP09b.

#### Q04. The timeline document's layout and event vocabulary, as its first schema version

*Round 1.*

**Summary.** Q04 decides the layout and event vocabulary of the timeline document's first schema version; the agent recommends design §8 and §5.9's layout, which keeps §7's names where §7 has the field and adds spans, groups by source, offsets, parameters and the report's provenance.

Design §8 proposes the document's layout and §5.9 each source's event types, with §7's field names where §7 has them. Which becomes the document's schema version 1?

- **(a) As design §8 and §5.9 propose.** §7's `timeline[]` and `attributions[]` names, extended with reference-frame positions, spans, ids and seconds; `overlapping_events` an array of groups in §3.1's source order, each event with offsets in seconds and frames; the parameters and the ladder report's provenance copied (formerly P-AC, whose alternative was the report's hashes only); scores not copied. It files the aligner's unpaired frames under `vmaf`, not under the decoder as §3.1 groups dropped frames, a difference to report (design §11, item 3).
- **(b) §7's literal shapes only.** `pts` and `payload` only, `overlapping_events` a flat list, the rest in payloads: closest to §7's text, but spans, groups and offsets then rest on payload conventions the schema doesn't state.
- **(c) (a), declared provisional until Phase 5.** The same layout, its names to be revisited when §7's report schema is decided; readers must expect a version 2.

**Agent's recommendation:** (a).

**Why:** It keeps §7's names and adds what the example and the reproduction need; a prototype of this shape answered the example (design-evaluation run 10).

**Blocks:** WP00a, WP04, WP08, WP09a, WP11a, WP05, WP07, WP09b, WP11b, WP14.

#### Q05. Versioning and provenance of the timeline document

*Round 1.*

**Summary.** Q05 decides what versioning and provenance the new document carries; the agent recommends schema_version and a JSON Schema file now, leaving tool_version and git_commit to Phase 5, with the commit recorded by the eval tooling as P-006 has it.

§7 (Phase 5) wants `schema_version`, a JSON Schema file, `tool_version` and `git_commit`; the build has no version (CMakeLists.txt:2), and P-006 has the eval tooling record the commit when it runs. What does the new document carry?

- **(a) `schema_version` and a schema file now; the rest with Phase 5.** An integer version from 1, as six eval scripts version their outputs (schema.md §1.7); the schema under docs/schema/; the commit recorded by the eval tooling (P-006).
- **(b) (a), plus `tool_version` and `git_commit` now.** The commit from a configure-time `git rev-parse` (a CMake step of WP00a), which can go stale between configure and run, the reason P-006 records it at run time; changes P-006.
- **(c) `schema_version` only, the schema file with Phase 5.** Less work now; the paper can't cite a schema for the new document until then.
- **(d) Neither until Phase 5.** Readers tell layouts apart by their keys, as for the 12 committed mirage_vmaf report layouts (schema.md §1.8).

**Agent's recommendation:** (a).

**Why:** A new format costs nothing to version from its first day, its schema can be cited, and P-006's confirmed rule for the commit stays.

**Blocks:** WP00a, WP08, WP09b, WP12.

#### Q06. How a timeline document names its ladder report

*Round 1.*

**Summary.** Q06 decides how a document identifies the ladder report it was built from; the agent recommends both the SHA-256 of the report's bytes and the SHA-256 of a canonical digest of what attribution uses, which survives the redaction of committed copies.

Committed copies of ladder reports are redacted (local paths replaced), so the hash of the bytes mirage_vmaf wrote doesn't match a committed copy (schema.md §1.8). How does a document name its report?

- **(a) SHA-256 of the report's bytes.** Exact identity of the file; fails against a redacted, committed copy.
- **(b) SHA-256 of a canonical digest of what attribution uses.** Each pair's index and entries, each frame's indices, PTS and score, and the settings that hold no path. Redaction leaves it unchanged; a canonical form to define and test (P-Z).
- **(c) Both.** The file it was built from, and a redaction-proof check against the committed record.

**Agent's recommendation:** (c).

**Why:** Each covers what the other can't, and both are cheap to compute when the report is read.

**Blocks:** WP03, WP08, WP09a, WP12.

#### Q07. How rungs are named in outputs

*Round 1.*

**Summary.** Q07 decides how outputs name a rung; the agent recommends the master entry index and URI as its identity, with its BANDWIDTH ranks from the top and from the bottom printed as labels.

The work order's example names a switch between two numbered rungs, but the ladder report has no rung numbers: entries are numbered in the master's order (src/VMAFLadder.h:88). How does Phase 3 name a rung? Which BANDWIDTH ranks a playlist that several entries name is Q43 (e).

- **(a) The master entry index and URI only.** Exact as the master gives them; no number to read at a glance.
- **(b) Plus an ascending BANDWIDTH rank (1 = lowest).** A switch from 5 to 3 then reads as down; the ranks change when a rung is added.
- **(c) Plus a descending rank (1 = highest).** A switch from 5 to 3 then reads as up; the ranks change when a rung is added.
- **(d) Entry and URI as identity, both ranks as labels.** Unambiguous whatever convention a reader assumes; longer text (design-evaluation run 10 prints both).

**Agent's recommendation:** (d).

**Why:** The identity never changes, and printing both ranks imposes no reading of the example that the work order doesn't state.

**Blocks:** WP00a, WP08, WP09a, WP07, WP09b, WP11b, WP14.

#### Q08. Where each scored frame's segment, and the keyframes, come from

*Round 1; **D-007**: the answer is, or includes, an approval or a ruling under D-007.*

**Summary.** Q08 decides how each scored frame is mapped to its segment and where keyframes come from; the agent recommends a demux-only pass in the timeline build, which leaves the scorer and its reports unchanged.

Placing playlist, SCTE-35 and network events needs each scored frame's segment, and §3.1 asks for keyframes; today no frame is mapped to a segment (sources.md §2). Who records them? Option (b) changes the analyzer's reading of its inputs, which needs the owner's ruling under D-007, or a written plan the owner approves.

- **(a) A demux-only pass in the timeline build.** Each scored playlist read again without decoding, packets placed by byte position and checked against the report's PTS; keyframes and corrupt flags from packet flags. Exact on every playlist probed (200 of 200 frames on each of 15 playlists, design-evaluation runs 03 and 04); scorer and report unchanged; every segment read again (time on real ladders: MEASUREMENT PENDING).
- **(b) The analyzer records them while scoring.** No second read, exact by construction; but the analyzer's reading and the report change (a key under `pairs[]`, counted at tests/test_vmaf_report.cpp:909), older reports lack it, and it is a D-007 and D-068 matter. The acceptance tests would then wait for that change, WP16's, and its ruling or plan (design §14).
- **(c) PTS against EXTINF sums, no extra read.** FFmpeg's own rule ([hls.c#L1916-L1945](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/hls.c#L1916-L1945)); right for 145 of 200 packets on the ad ladders and 193 of 200 on the degraded one (design-library runs map-by-pos-*), and it can't place a PTS two segments share.

**Agent's recommendation:** (a).

**Why:** It was exact on every playlist probed, leaves §2's scorer and its committed comparisons untouched, and its PTS check makes a failure visible; what a failure costs is Q43 (c).

**Blocks:** WP00a, WP06, WP09a, WP16.

#### Q17. An ad break's end when several sources give a duration

*Round 1.*

**Summary.** Q17 decides where an ad break ends when its tags and messages give several durations; the agent recommends ending it at its cue-in when there is one, else at its start plus the first of a declared order of durations, keeping every value in the payload.

A break's duration can come from EXT-X-CUE-OUT's value, DATERANGE's DURATION, PLANNED-DURATION or END-DATE, break_duration, segmentation_duration, or the cue-in's position. Which places the break's end?

- **(a) The cue-in, else a declared order.** Every value kept; without a cue-in, the tag's own duration (CUE-OUT's value, DATERANGE's DURATION or END-DATE), then PLANNED-DURATION, break_duration and segmentation_duration. On testgen's ad ladder the cue-in gave 2.000 s, equal to EXT-X-CUE-OUT:2.000 (design-evaluation run 10).
- **(b) The tag's duration first.** Google Video Stitcher's rule (scte35.md §4.4).
- **(c) The message's break_duration first.** The standard's value; absent from tag-only cues.

**Agent's recommendation:** (a).

**Why:** The cue-in is where the media changes back, and keeping every value shows any disagreement.

**Blocks:** WP04.

#### Q18. SCTE-35 signalling beyond D-070's list

*Round 1.*

**Summary.** Q18 decides whether Phase 3 reads SCTE-35 tags that D-070 doesn't list; the agent recommends D-070's list only, the other tags kept as raw playlist tag events.

Sources also use #EXT-X-SCTE35 (the standard's own HLS tag), #EXT-X-SPLICEPOINT-SCTE35 and Adobe's #EXT-X-CUE (scte35.md §2.6 and §9). Does Phase 3 read them?

- **(a) D-070's list only.** The others kept as raw `tag` events and listed as such; a difference to report.
- **(b) Add EXT-X-SCTE35 and EXT-X-SPLICEPOINT-SCTE35.** Their base64 messages through the same decoder; a little more parsing and tests.
- **(c) Add all three.** EXT-X-CUE carries attributes, no message.

**Agent's recommendation:** (a).

**Why:** D-070 decided the list; the others are visible as tags and can be added later at little cost.

**Blocks:** WP04, WP05.

#### Q19. A bare EXT-X-DISCONTINUITY as an ad boundary

*Round 1.*

**Summary.** Q19 decides whether a discontinuity without cue tags is taken as an ad boundary; the agent recommends never inferring one, so that it stays a discontinuity event.

AWS MediaTailor's stitched playlists drop the cue tags and keep only EXT-X-DISCONTINUITY (testgen.md §5). Is a discontinuity without cue tags an ad boundary on the timeline?

- **(a) Never inferred.** A `discontinuity` event only.
- **(b) Inferred.** An `ad_break` from pairs of discontinuities; helps with stitched streams, but it is an inference the paper would have to defend.
- **(c) Inference as an option, off by default.** Available for stitched streams; one more option.

**Agent's recommendation:** (a).

**Why:** The evaluation mustn't claim SCTE-35 where none was signalled, and the attribution still lists the discontinuity at the drop (design-evaluation run 10).

**Blocks:** WP04, WP09a.

#### Q20. DASH period boundaries

*Round 1.*

**Summary.** Q20 decides what Phase 3 does with DASH; the agent recommends an interface only: a reserved period event type and design §5.5's cue interface, with no DASH reader.

§3.1 lists DASH period boundaries, but mirage_vmaf scores HLS ladders only (src/VMAFLadder.h:15-22), and D-070 leaves DASH's EventStream as an interface. What does Phase 3 do with DASH?

- **(a) An interface only.** A `period` type reserved and the cue interface of design §5.5 declared, with no reader; reported as a difference.
- **(b) Parse an MPD's periods and EventStreams.** Events for a DASH presentation of a scored HLS ladder's content, read by WP04 with an XML parser that mirage_vmaf would then link (WP00a); the join between the two presentations is unmeasured.
- **(c) DASH ladder scoring.** Outside §2's scope.

**Agent's recommendation:** (a).

**Why:** Without DASH scoring, DASH events would reach no curve.

**Blocks:** WP00a, WP04, WP09a.

#### Q22. Which curves automatic detection runs on

*Round 1.*

**Summary.** Q22 decides which curves automatic detection runs on and how a session's drops are found; the agent recommends each pair's curve, with a session's drops taken as its pairs' drops clipped to the spans that played them, under design §6.3's three rules.

§3.2 compares the score with the variant's own trailing baseline, and D-068 defines a session's curve as the score of the variant played at each frame. Which curves does detection run on, and how are a session's drops found? Design §6.3 gives (c)'s three rules, with their alternatives: for pieces of N points or fewer, for drops that meet at a switch, and for the baseline a piece reports.

- **(a) Each scored pair's curve only.** Both acceptance tests are of this kind; a session adds events but no drops.
- **(b) The pairs' curves, and the session's curve with its own trailing baseline.** A switch can become a drop depending on the rung gap against T: on the switch-down plan, a drop at T = 3, 4 and 5 and none at T = 5.5, 6, 6.5, 7 and 8 (review-adversarial/runs/switch-t-sweep.txt). The composite drop's baseline was 97.681, v270's level (design/runs/detection-semantics-ad.txt).
- **(c) The pairs' curves, and session drops as the pairs' drops clipped to the spans that played each variant.** Each session point is compared with its own variant's baseline, so a switch alone is never a drop, and there is no second detector. The composite drop: frames 75 to 124, v180's baseline 91.692, magnitude 69.455 (design/runs/detection-semantics-ad.txt).
- **(d) The session's curve only.** Misses drops in variants no session plays, the two acceptance tests' among them.

**Agent's recommendation:** (c), with design §6.3's three rules.

**Why:** It applies the work order's per-variant baseline to sessions too and still finds the composite drop, while under (b) whether a switch is a drop changes between T = 5 and T = 5.5 on one plan, a fragility the paper would have to explain.

**Blocks:** WP02, WP08, WP09a, WP11a, WP07, WP09b, WP11b, WP14.

#### Q23. The trailing baseline's statistic and warm-up

*Round 1.*

**Summary.** Q23 decides how the trailing baseline is computed and from when it is used; the agent recommends the median of the last W points that weren't low, used once W/2 such points exist, which leaves the first W/2 points of every curve unexamined.

The work order doesn't define the sliding baseline. Which statistic over the last W points, and from when is it used?

- **(a) Median of the last W points that weren't low, used once W/2 such points exist.** Exact on the four acceptance curves at every point of the measured grid, T from 3 to 40 and N from 1 to 24 (design §2; design-evaluation run 05), and a drop never lowers its own baseline. Its blind start: the first W/2 points of every curve can't be low, at W = 50 the whole first segment of a D-069 ladder (25 frames, testgen.md §3.1).
- **(b) Mean of the same points.** Also exact on the acceptance curves on the same grid (design-evaluation run 05); less robust to outliers; the same blind start.
- **(c) Median of the last W points, low or not.** Follows a long drop down: the drops found were shorter than the truth at W = 10 to 50 and exact only at W = 100 (design-evaluation run 05).
- **(d) One median per curve.** No window and no blind start; blind to slow changes of level.

**Agent's recommendation:** (a).

**Why:** It was exact across the measured grid, is robust to outliers, and a drop doesn't pull its baseline down; its blind start is written in the document's detection record and tested (WP02).

**Blocks:** WP02, WP08, WP11a, WP11b, WP14.

#### Q24. Units of W and N, and points without a score

*Round 1.*

**Summary.** Q24 decides whether W and N count curve points, reference frames or seconds, and what happens at gaps; the agent recommends counting points of the curve and skipping gaps.

A 30 fps variant's curve has a point at every other reference frame of a 60 fps reference (schema.md §3.1), and a session's curve has no points on unscored spans. In what are W and N counted, and what happens at gaps?

- **(a) Points of the curve, gaps skipped.** Nothing invented at gaps; the same N covers more time on a coarser curve.
- **(b) Reference frames, gaps holding the last score.** The same N is the same time on every curve; scores are invented at gaps.
- **(c) Seconds of the axis, gaps skipped.** Uniform in time; N is no longer a count of frames.

**Agent's recommendation:** (a).

**Why:** The work order counts frames, and nothing is invented at the gaps.

**Blocks:** WP02, WP08, WP09a.

#### Q25. Frames that start a libvmaf context (D-037)

*Round 1.*

**Summary.** Q25 decides whether detection treats the first pair of each libvmaf context, which scores low, specially; the agent recommends no special case, which keeps the ad's drop starting on its cue-out's frame.

The first pair of every libvmaf context scores low (D-037): frame 75, where testgen's ad starts a new context, is the control rung v270's only frame more than 6 points below its median (testgen.md §3.6, item 7). How does detection treat these frames?

- **(a) No special case.** N of 1 or more passes over their single-frame dips, and the ad's drop starts at frame 75 with the cue-out's frame inside (design/runs/detection-semantics-ad.txt).
- **(b) Left out of the curve.** As D-037's second minimum leaves them out; the ad's drop then starts at frame 76 and the cue-out's frame 75 falls before it, so the strict overlap no longer lists the cue (same run).
- **(c) Left out of the baseline only.** They can be low but never enter the baseline; the same drop as (a) on this ladder (same run).

**Agent's recommendation:** (a).

**Why:** (b) moves an ad's drop off its cue by one frame, N of 1 or more already ignores single-frame dips, and (c) adds a rule that changed nothing measured.

**Blocks:** WP02, WP08, WP11a, WP11b, WP14.

#### Q26. Detection's defaults, and when the timeline subcommand detects

*Round 1.*

**Summary.** Q26 decides the shipped defaults of T, W and N and when the timeline subcommand runs detection; the agent recommends T = 6 by default with W and N required, and detection only when W and N are given, so that a build without them writes events only and says so.

§3.2 suggests a threshold of 6 VMAF points; W and N have no suggested value, nothing measured on real video sets them, and the acceptance tests pin their own (P-R). Item (a) is the defaults; item (b) is when `timeline` detects, since with W and N required an events-only build would otherwise fail (design §7).

- **(a) The defaults of T, W and N.**
  - **1. T = 6 by default; W and N required until measured.** The work order's value for T; W and N must be given.
  - **2. All three set now from the synthetic sweep.** Synthetic content, one source picture, 200 frames per pair: design-evaluation run 05 shows a wide region where the acceptance cases pass, not good defaults for real video.
  - **3. All three required.** No default until a measurement.
  - *Agent's recommendation:* 1. *Why:* What would set W and N is detection over D-011's ladders with injected degradations (design §6.3); the longest run of low points in testgen's busy control was 3 (design-evaluation run 05).
- **(b) When `timeline` detects.**
  - **1. Always, W and N required.** `timeline` without `--window` and `--min-points` fails, even when only events are wanted.
  - **2. Only when W and N are given.** Otherwise the document has `detection: null` and no attributions, and a line on stderr says why.
  - **3. A `--detect on|off` option, default on.** Explicit; W and N required when on; one more option.
  - *Agent's recommendation:* 2. *Why:* Events-only builds work without a new option, and both the document and stderr say that no detection ran.

**Agent's recommendation:** (a) 1; (b) 2.

**Why:** T has the work order's value and W and N have none, while the tests don't depend on defaults; and events-only builds then need no new option.

**Blocks:** WP02, WP08, WP09a.

#### Q27. What drop_magnitude measures

*Round 1.*

**Summary.** Q27 decides what an attribution's drop_magnitude measures; the agent recommends both depths: the baseline minus the drop's minimum as drop_magnitude, and the baseline minus its mean as mean_drop.

§7 names an attribution's `drop_magnitude` without defining it. What does it measure?

- **(a) Baseline minus the drop's minimum.** The worst frame's depth.
- **(b) Baseline minus the drop's mean.** The drop's average depth.
- **(c) Both.** (a) as `drop_magnitude`, (b) as `mean_drop`.

**Agent's recommendation:** (c).

**Why:** Both are cheap and answer different questions; §7's name keeps the first. Under Q23 (a) the baseline is the same at every point of a drop (design §6.3).

**Blocks:** WP02, WP08, WP09a.

#### Q28. Which variants' events an attribution lists

*Round 1.*

**Summary.** Q28 decides which variants' events an attribution of a curve lists; the agent recommends the curve's own inputs: for a pair, its distorted variant's and the reference's events; for a session, the played variant's in each span, the reference's and the session's own.

Every rung's playlist can carry the same tags (all three rungs of testgen's ad ladder carry the cue tags), and a query without scope lists each copy (design-evaluation run 09). Which variants' events does an attribution of a curve list?

- **(a) The curve's own inputs.** For a pair, its distorted variant's and the reference's; for a session, the played variant's in each span, the reference's, and the session's own. Run 10's prototype did this for pairs; for its session it listed only the played variant's and the session's own, so (a) adds the reference's copies there (design §6.4).
- **(b) Every scored variant's events.** Shows that a cue is in every rung; three copies of each tag on a three-rung ladder.
- **(c) The distorted input's only.** Leaves out the reference's segments and tags.

**Agent's recommendation:** (a).

**Why:** It keeps another rung's events out of a curve's answer and keeps the reference, whose timeline every curve is on.

**Blocks:** WP01, WP08, WP09a, WP07.

#### Q29. Which events overlap an interval

*Round 1.*

**Summary.** Q29 decides what overlapping an interval means for points and spans; the agent recommends strict half-open overlap on reference frames, with optional margins before and after that are zero by default.

§3.2 returns the events that overlap a drop's interval in time. Events are points (a tag, a switch) or spans (a segment, a break, a request). Which events overlap?

- **(a) Strict half-open overlap on reference frames.** A point at frame f meets [a, b) when a ≤ f < b, a span [s, e) when s < b and e > a. On the ad ladder the cue-in at the drop's end isn't listed, but the ad_break span is, with its end offset of +2.000 s (design-evaluation run 10).
- **(b) (a) plus points exactly at the end, flagged at_end.** Lists the cue-in at the drop's end, and with it every boundary event there: the next segment, its keyframe, its discontinuity.
- **(c) (a) plus optional margins before and after, zero by default.** At its default it is (a); a margin finds causes that precede their effect, a slow fetch or the corrupt flag one frame before the ad (Q43 (h)); one more pair of options.
- **(d) Closed intervals.** Touching ends count, at both ends.

**Agent's recommendation:** (c).

**Why:** By default it is overlap made exact, and the margins meet the one need for more without changing default outputs.

**Blocks:** WP01, WP08, WP09a, WP11a.

#### Q30. Ordering and ranking of the events an attribution lists

*Round 1.*

**Summary.** Q30 decides whether an attribution ranks its events by likely relevance; the agent recommends no ranking: groups in §3.1's source order and events by offset within a group.

§3.2 asks for the events by source, with offsets, and says nothing of their order. Should an attribution rank its events by likely relevance?

- **(a) No ranking.** Groups in §3.1's source order (vmaf, network, playlist, scte35, player, decoder), events by offset within a group.
- **(b) Ranked by a heuristic.** Distance from the drop's start, or overlap fraction: a causal heuristic needing an evaluation of its own.
- **(c) A likely-cause field.** An inference the paper would have to defend.

**Agent's recommendation:** (a).

**Why:** The claim is overlap; a ranking is a causal model that would need its own evaluation.

**Blocks:** WP01, WP08, WP09a.

#### Q31. The CLI subcommands: shape, names and options

*Round 1.*

**Summary.** Q31 decides the shape, names and options of the CLI subcommands; the agent recommends two first-word subcommands of mirage_vmaf, timeline to build and attribute to query, with design §7's options and text output by default.

§3.2 asks for a CLI subcommand; mirage_vmaf has none, and today an argument that isn't an option is an error (src/VMAFCli.cpp:288). What are the subcommands, and their options (design §7)?

- **(a) Two first-word subcommands, timeline and attribute.** timeline builds, detects and writes a document, doing the I/O once; attribute queries a document without reading media; the bare options still score. With design §7's options, attribute printing text by default and JSON with `--format json`.
- **(b) One subcommand that builds and queries.** Every query reads every segment and fetches the session again.
- **(c) A separate program, mirage_timeline.** As D-017 separated mirage_vmaf from the player; one more binary to build, package and test.
- **(d) A third form of mirage_vmaf's options.** `--timeline` beside `--ref/--dis` and `--ladder`: the parser's forms grow, and a query's options mix with scoring's.

**Agent's recommendation:** (a), with design §7's option set.

**Why:** The build does the I/O once, queries are offline, cheap and deterministic, and existing command lines keep their meaning.

**Blocks:** WP00a, WP09a, WP11a, WP14.

#### Q32. The JSON reader

*Round 1.*

**Summary.** Q32 decides whether Phase 3's JSON reader is written in the repository or taken as a dependency; the agent recommends a document-tree reader written in the repository, moving to streaming only if a measurement of a full report needs it.

src/ writes JSON by hand and reads none (src/JsonText.h:1-21); Phase 3 reads ladder reports, plans and documents, and a full ladder report is 27969051 bytes of text (schema.md §3.3). Written in the repository, or a dependency?

- **(a) Written in the repository, building a document tree.** No new dependency, as D-030 chose for the config file; its memory on full reports: MEASUREMENT PENDING.
- **(b) Written in the repository, streaming large arrays.** Bounded memory; more code.
- **(c) A header-only library through FetchContent.** Less code; a new dependency in D-031's list.

**Agent's recommendation:** (a), moving to (b) if a full report's load needs it.

**Why:** It follows D-030's choice of no new dependency and the repository's own writers; streaming is added only if a measurement says it is needed.

**Blocks:** WP00a, WP03.

#### Q33. The encoder of the acceptance tests

*Round 1.*

**Summary.** Q33 decides which encoder makes the synthetic ladders of the acceptance tests; the agent recommends libx264 where it is present and FFmpeg's MPEG-2 encoder otherwise, the encoder recorded in the test's output.

D-069's ladders are encoded within the tests through FFmpeg's libraries, and an acceptance test that skips shows nothing. Which encoder?

- **(a) libx264, the tests skipped where it is absent.** Real HLS ladders' codec, a CODECS value, B-frames, and the same x264 commit in Homebrew and the Docker image (testgen.md §3.3); but a skipped acceptance test shows nothing.
- **(b) FFmpeg's MPEG-2 encoder.** In every FFmpeg build; no HLS CODECS value; a smaller drop (62.022 against 96.923 to 97.671, testgen.md §3.1).
- **(c) libx264 when present, else MPEG-2.** The same assertions, the encoder recorded. With MPEG-2 the pinned parameters passed on the degraded ladder and the ad from the main origin (design-evaluation run 05); the ad from 1 s ran with libx264 only and the continuous ad not at all, so those two with MPEG-2 are MEASUREMENT PENDING.

**Agent's recommendation:** (c).

**Why:** Both encoders gave clear drops confined to the intended frames, with the same attributions, where they were run (design-evaluation run 10), and the tests then run in every build.

**Blocks:** WP10, WP11a.

#### Q34. The ad's timestamps in acceptance test 2

*Round 1.*

**Summary.** Q34 decides which timestamp arrangement the ad of acceptance test 2 uses; the agent recommends all three (restarting at the main content's origin as the acceptance test proper, from 1 s, and continuous), each generated once in the acceptance test's process.

The ad's PTS can restart at the main content's origin (PTS repeat across the splice), start at another origin (a jump of more than 10 s, where the report's sequences match the playlist's), or continue the main content's. Which does acceptance test 2 use?

- **(a) Restart at the main content's origin.** The hard case for the time key; testgen's default (testgen.md §3.6, items 4 and 5).
- **(b) From 1 s.** The report's sequences match the playlist's (testgen.md §3.5).
- **(c) Continuous.** The only arrangement in which ui.md's probe reached the ad by a seek (ui.md §2.4); not yet generated or scored.
- **(d) All three, (a) as the acceptance test proper.** Three ad ladders made in the acceptance test's process; their time in a Debug build: MEASUREMENT PENDING (design §9.5).

**Agent's recommendation:** (d).

**Why:** (a) is the case the time key must handle, and (b) and (c) cover the aligner's other case and the UI's seekable one.

**Blocks:** WP10, WP11a.

#### Q35. The rest of the synthetic content

*Round 1.*

**Summary.** Q35 decides the remaining choices of D-069's synthetic content as one package; the agent recommends testgen's measured choices: the fractal-noise source, a calm ad, continuity counters marked at the splice, no binary SCTE-35 in the ladders, the generator compiled with optimization, and top-rung mode.

testgen.md §4 lists other choices for D-069's content: the source picture, the ad's content, the continuity counters at the splice, binary SCTE-35 forms in the ladders, the generator's speed in a Debug build, and the reference mode. How are they settled?

- **(a) testgen's measured recommendations as one package.** The fractal-noise source (the ramp's ladder fails D-002's BANDWIDTH order, testgen.md §2.2), the calm ad (the busy one nears the threshold at the normal quantizer, testgen.md §3.2), counters marked at the splice, no binary SCTE-35 in the ladders (the decoder's tests build sections instead), the generator optimized in every build type (P-Q), and top-rung mode.
- **(b) Item by item.** The owner answers each of testgen.md §4's items on its own.
- **(c) The existing tests' ramp and MPEG-2 helpers.** Reuses code, but the ramp's measured BANDWIDTH puts v270 above v360, so the ladder is refused (testgen.md §2.2).

**Agent's recommendation:** (a).

**Why:** Each item was measured to give a clear, deterministic drop confined to the intended frames (testgen.md §3).

**Blocks:** WP10, WP11a.

#### Q43. The ladder timeline's defaults: eight choices that change its output

*Round 1.*

**Summary.** Q43 decides eight defaults of the ladder timeline that change its output, and the agent recommends each as follows: live media playlists refused, only 75cf6e0's report layout read, a variant's segment map dropped on any PTS mismatch, an unpaired frame placed at the next paired frame, a rung ranked by the highest BANDWIDTH of its entries, each rung's copy of a tag kept, a drop ending at the next point of its curve, and a corrupt packet placed at its own frame with the next segment boundary named.

The first version of this design made seven of these choices as process choices (P-E, P-X, P-G, P-H, P-AA, P-AB, P-AE), and (h) answers the first review's case of a corrupt packet at a splice; each changes what the timeline outputs by default, so each is the owner's. Each item has its options and the agent's recommendation and can be accepted or changed on its own; design §5.1, §5.2 and §6.3 give the context.

- **(a) A live media playlist (formerly P-E).**
  - **1. Refused.** A media playlist without EXT-X-ENDLIST that isn't PLAYLIST-TYPE:VOD (hls::MediaTimeline::live(), src/HlsAlign.h:69) fails the build, naming it.
  - **2. A warning only.** The build goes on with what the playlist held when it was read.
  - *Agent's recommendation:* 1. *Why:* Live streams are outside the work order's scope, and a partial playlist would give a partial timeline without saying so in its events.
- **(b) Which ladder reports are read (formerly P-X).**
  - **1. The layout of 75cf6e0 on only.** Older layouts refused, naming the missing key; a pair report (--ref/--dis) refused; relative paths taken from the current directory, as mirage_vmaf takes its inputs.
  - **2. Readers for the other layouts too.** mirage_vmaf's committed reports fall into 12 layouts, one of them 75cf6e0's (schema.md §1.8); the records of Phases 1 and 2 then get timelines, at the cost of a reader per layout.
  - **3. 1, and a pair report read as a ladder of one pair.** A pair gets a timeline, without master or playlist events.
  - **4. 1, with relative paths taken from the report's directory.** A report moved together with its media still resolves, unlike mirage_vmaf, which takes its inputs from the current directory.
  - *Agent's recommendation:* 1. *Why:* D-057 added the discontinuity sequences the media key needs, 75cf6e0's layout has every key (8 committed files, schema.md §1.8), and the acceptance tests make new reports.
- **(c) A variant whose frames don't all match the demux pass (formerly the end of P-G).**
  - **1. No segment map for the variant.** Its segment-bound events (segments, tags, cues, requests) are left out, with a warning: one PTS mismatch costs the whole variant.
  - **2. The map kept for the frames that match.** More events survive, with a warning for each mismatch; a partly wrong map can place events off by the mismatch.
  - **3. The variant's segments placed by PTS against EXTINF, flagged.** Every event placed, some wrongly: 145 and 193 of 200 packets right on testgen's ladders (design-library runs map-by-pos-*).
  - *Agent's recommendation:* 1. *Why:* No mismatch occurred on any probed playlist (design-evaluation run 04), and an event placed on a wrong frame would be a silently wrong attribution.
- **(d) Where an unpaired frame sits (formerly part of P-H).**
  - **1. At the next paired frame of its input, by index.** A reference frame that failed to pair sits at itself.
  - **2. At the nearest reference frame by PTS.** Ambiguous where PTS repeat across a splice (design-evaluation run 09).
  - *Agent's recommendation:* 1. *Why:* Index order is unambiguous where PTS repeat.
- **(e) Which BANDWIDTH ranks a playlist that several master entries name (formerly P-AA).**
  - **1. The highest among its entries.** As the top rung is chosen (src/VMAFLadder.h:33-35), so the reference's rank from the top is 1.
  - **2. Its first entry's.** Simpler, but the reference may then not rank first.
  - *Agent's recommendation:* 1. *Why:* It ranks rungs by the rule that picks the reference.
- **(f) A tag that several rungs' playlists carry (formerly P-AB).**
  - **1. Kept once per variant.** An attribution lists the copies its scope admits (Q28), so a pair's answer shows the reference's and the distorted variant's copy.
  - **2. One event across variants.** Shorter answers, but it loses which playlists carried the tag, and a tag in one rung only can't be told from one in all.
  - *Agent's recommendation:* 1. *Why:* Which playlist carried a tag is a fact the evaluation may need, and Q28 already limits the copies listed.
- **(g) A drop's end, and intervals given in seconds (formerly P-AE).**
  - **1. To the next point's frame.** A drop runs from its first low point's reference frame to the frame of the curve's next point (the last point's frame plus one at the curve's end), half-open; an interval in seconds starts and ends at the first reference frames at or after its two ends on the axis.
  - **2. To the last point's frame plus one, always.** On a coarser-grid curve the last low point then covers half the reference frames it stands for.
  - *Agent's recommendation:* 1. *Why:* On a coarser grid a point stands for the reference frames up to the next point.
- **(h) A corrupt packet at a splice.**
  - **1. At the flagged packet's frame, the next segment boundary in its payload.** FFmpeg's fact unaltered. On testgen's ladder with unmarked counters the flags fell on frames 74 and 122 (design §5.2), so the ad's drop [75, 125) lists only the second, unless a margin (Q29) is given.
  - **2. At the first frame of the next segment, when a discontinuity precedes it.** The flag then sits at the boundary whose counter restart caused it; an inference from the stream's structure, wrong for a counter error inside a segment.
  - **3. Both, as two events.** The fact and the inferred boundary each listed; two events for one flag.
  - *Agent's recommendation:* 1. *Why:* It records what FFmpeg reported and names the boundary for the reader; D-069's acceptance ladders mark their counters (Q35) and have no such flags.

**Agent's recommendation:** (a) 1; (b) 1; (c) 1; (d) 1; (e) 1; (f) 1; (g) 1; (h) 1.

**Why:** Each recommended default keeps what the data shows, places nothing it can't place exactly, and says in the document what it left out.

**Blocks:** WP00a, WP01, WP02, WP03, WP04, WP06, WP09a.

### Round 2, the questions in full

#### Q10. Where a scripted session's per-segment HTTP timing comes from

*Round 2; **D-007**: the answer is, or includes, an approval or a ruling under D-007.*

**Summary.** Q10 decides where a scripted session's per-segment HTTP timing comes from; the agent recommends a sequential fetch of the session's files after scoring, timed by the player's NetworkLogger compiled into mirage_vmaf.

D-068 gives a session the HTTP timing of its segments but no source for a scripted one, and the example needs a segment request's duration; the scoring reads are paced by decoding and scoring (sources.md §3.3). Without a session a ladder has no network events, both acceptance tests included (design §11, item 6). Which source? Option (e) changes the analyzer's input reading, which needs the owner's ruling under D-007 or a written plan, and D-068 keeps the decode path unchanged.

- **(a) A fetch of the session's files, timed by NetworkLogger.** The master, each played playlist and segment in play order, as Q44 (f) and (g) say; NetworkLogger compiled into mirage_vmaf unchanged and installed for the fetch only (design §3.3, §5.7), giving status, request sent, first byte, end and bytes. A prototype measured 2.313 s for a segment served 2.3 s late (design-evaluation run 06); it is not a player's fetch, and it relies on FFmpeg 8.1.1's log wording.
- **(b) The same fetch, timed by avio alone.** No log parsing; the status only as the class of a failed open's AVERROR_HTTP_* ([http.c#L592-L608](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/http.c#L592-L608)), so 200 and 206 are not told apart.
- **(c) Declared in the plan.** Deterministic, but it measures nothing: the example's 2.3 s becomes an input, which makes the answer circular.
- **(d) None for scripted sessions.** Timing only from the player's log, later; Phase 3 can't answer the example's network part.
- **(e) (a), plus what the scoring reads saw.** A per-input recorder in SegmentWatch's hooks, labelled scoring-paced (sources.md §11, choice 5 (a)), so that every ladder gets network events. Its times are paced by scoring, the report changes and ladders must be scored again: a D-007 and D-068 matter, and WP16's work.

**Agent's recommendation:** (a).

**Why:** It uses the component §3.1 names, gives every field §3.1 lists, and a test can make one segment slow with a delayed local server. (e) gives ladders without sessions network events, but paced by scoring they don't show how long a player would wait.

**Blocks:** WP00b, WP07, WP09b, WP11b, WP14, WP16.

#### Q11. The session plan's format

*Round 2.*

**Summary.** Q11 decides the scripted session plan's file format; the agent recommends versioned JSON whose spans are master entries with their first and last media sequence numbers.

A scripted session (D-068) says which variant plays over each span of media. A plan is a list, which D-030's config file can't hold (each setting is given once, src/VMAFCli.cpp:224), so it is a file of its own, recorded by path and SHA-256. Which format, and in which unit?

- **(a) Versioned JSON, spans as segment ranges.** `{entry, first_seq, last_seq}` in play order: exact, no snapping rule, the fetched segments follow directly, and HLS switches at segment boundaries. The player's log can later add observed fields to the same format.
- **(b) Versioned JSON, spans in seconds on the axis.** Closer to D-068's span of media time, but it needs a snapping rule, and a rule for fetching around a switch inside a segment.
- **(c) Text lines `<entry> <first>-<last>`.** The simplest to write; a second format for the player's log later.
- **(d) The player's `--qa-switch` syntax.** Already known (src/main.cpp:553-556), but its times are wall seconds after the first frame, and `v<height>` is ambiguous when two playlists share a height.

**Agent's recommendation:** (a).

**Why:** Segment ranges are exact for HLS and give the fetch list directly; JSON reuses the reader Phase 3 needs anyway.

**Blocks:** WP00b, WP07, WP09b, WP11b, WP17.

#### Q12. A session's spans that play the reference in top-rung mode

*Round 2.*

**Summary.** Q12 decides what a session's curve holds where its plan plays the top rung, which top-rung mode doesn't score; the agent recommends leaving those spans unscored and marked, with no detection there.

In top-rung mode the top rung is the reference and has no score (src/VMAFLadder.h:36-38). What does a session's curve hold where its plan plays the top rung?

- **(a) Unscored.** No points and no detection there; the document marks the span `scored: false`.
- **(b) Taken as 100.** Assumed, not measured; a switch down from it looks like a drop of 100 minus the next score.
- **(c) Sessions require mezzanine mode.** Every variant is scored, but a stitched ladder can't be scored against a one-file mezzanine (76.92% of frames failed, testgen.md §3.5).
- **(d) The top rung scored against itself once.** Measured, at the cost of one more pair in §2's run (WP16), for a curve that says nothing about encoding.

**Agent's recommendation:** (a).

**Why:** It claims nothing that wasn't measured and needs no change to §2.

**Blocks:** WP00b, WP07, WP09b, WP16.

#### Q14. The anchor that places an SCTE-35 cue

*Round 2.*

**Summary.** Q14 decides which anchor places a cue that carries an SCTE-35 message; the agent recommends the tag's position (the segment it precedes), with a decoded splice time kept as its own splice_point event and every anchor recorded.

A cue can be placed by its tag's position, by a DATERANGE's START-DATE through PROGRAM-DATE-TIME, or by its message's splice time; vendors disagree, and several published messages carry no splice time (scte35.md §4). D-069's ladders carry plain tags, which every option places by position (testgen.md §2.3). Which anchor places cues that carry a message?

- **(a) Position, with the other anchors recorded.** Cue events at the segments their tags precede; a splice time becomes a `splice_point` event, placed on the carrying variant's own frames (design §5.1); a warning when an anchor falls outside the tag's segment. Segment-accurate; the rule MediaTailor, Video Stitcher and broadpeak.io state (scte35.md §4.1).
- **(b) Splice time when the message has one, else position.** The standard's splice point, frame-accurate where present; absent from many published messages, and an upstream restamp without a pts_adjustment update misplaces it.
- **(c) START-DATE through PDT for DATERANGE, else position.** The HLS rule for DATERANGE, but it needs a PDT; in Wowza's published example tag and date are 13.8 s apart (scte35.md §4.2).
- **(d) A configurable precedence.** Every anchor recorded and one declared order (scte35.md suggests splice time, START-DATE, position), with a warning when they disagree; one more option to defend and test.

**Agent's recommendation:** (a).

**Why:** Position is where stitched media changes segment and every form has it, while the frame-accurate time stays visible as its own event.

**Blocks:** WP05.

#### Q15. Which decoded SCTE-35 messages count as cue-outs and cue-ins, and cancellations

*Round 2.*

**Summary.** Q15 decides which decoded SCTE-35 messages count as break starts and ends, and whether cancellations are kept; the agent recommends splice_insert by its out-of-network flag and time_signal with the break, advertisement, placement-opportunity and ad-block segmentation types, every other message kept as a non-break event and cancellations as their own events.

splice_insert says out or in by its out_of_network_indicator; for time_signal the segmentation_type_ids that count are a choice, and sources' defaults differ (scte35.md §4.4). Which set, and are cancelled events kept?

- **(a) Breaks, advertisements, placement opportunities and ad blocks.** time_signal with types 0x22-0x23, 0x30-0x37 and 0x44-0x47 (SCTE 35 2023r1 Table 23); every other message a `message` event and a cancellation a `cancel` event; the set configurable (`--cue-types`).
- **(b) splice_insert only.** Every time_signal a non-break message; misses segmentation-only signalling.
- **(c) MediaPackage v1's default set.** splice_insert plus provider and distributor advertisements and placement opportunities (scte35.md §4.4); one vendor's choice.
- **(d) Every Start/End pair of Table 23.** Programs, chapters and the rest become breaks too.

**Agent's recommendation:** (a).

**Why:** It covers the break hierarchy the standard defines and keeps every other message on the timeline, so a program or chapter boundary can still be attributed.

**Blocks:** WP05, WP09c.

#### Q16. An SCTE-35 message whose CRC_32 fails

*Round 2.*

**Summary.** Q16 decides what happens to an SCTE-35 message with a bad CRC_32; the agent recommends keeping it, marked crc_valid: false with a warning, with an option to drop such messages.

Of 79 published messages collected, 9 have a bad CRC_32, 1 of them a library's intended negative test (scte35.md §5; scte35_evidence/runs/check_vectors.txt; review-3/runs/vector-status.txt; the survey's summary, scte35.md §0 item 5, gives 3 in error). What does Phase 3 do with such a message?

- **(a) Kept, marked.** `crc_valid: false` and a warning; still attributable; `--bad-crc drop` drops them.
- **(b) Dropped.** A corrupted message isn't trusted; its tag still shows by position.
- **(c) Kept silently.** No signal of the doubt.

**Agent's recommendation:** (a).

**Why:** Vendors and the RFCs publish such messages, and an event the attribution omits can't explain a drop.

**Blocks:** WP05, WP09c.

#### Q21. Which SCTE-35 test vectors may be committed

*Round 2.*

**Summary.** Q21 decides which published SCTE-35 messages Phase 3's tests may commit; the agent recommends committing only sections the tests build themselves, until the owner settles the licences.

The published messages collected carry different terms (scte35.md §5): scte35-go is Apache-2.0 but includes copies of SCTE's samples, SCTE's PDF and several vendors' pages are All Rights Reserved, some pages state no licence, and of threefive's two repositories the documentation of superkabuki/threefive_is_scte35 is GPL-2.0 and the example playlists and smoke test of futzu/threefive are MIT, each by GitHub's detection. Which may Phase 3's tests commit to GPL-3.0-or-later Mirage?

- **(a) Self-made only.** Sections the test builds from field values, with their CRC; the decoder checked against the published vectors during development, outside the repository.
- **(b) Plus scte35-go's own test cases and futzu/threefive's examples.** Apache-2.0 for scte35-go's cases, its copies of SCTE's samples left out, and MIT for futzu/threefive's 6 messages, for which their source gives no decode, so a test could check only that they decode with a valid CRC (scte35.md §5).
- **(c) Plus the standard's samples and the RFCs' examples.** SCTE's All Rights Reserved; the RFC examples under the IETF Trust's terms.
- **(d) Plus vendor documentation examples.** Pages without a licence or All Rights Reserved, and threefive's GPL-2.0 documentation.

**Agent's recommendation:** (a), until the owner settles the licences, as with D-008.

**Why:** It needs no licence judgement, and the layout is still checked during development against the 61 published messages that decode with a valid CRC and no disagreement with their sources (scte35.md §5).

**Blocks:** WP05.

#### Q41. The component's name: the work order's NetworkDebugger against the code's NetworkLogger

*Round 2; **D-007**: the answer is, or includes, an approval or a ruling under D-007.*

**Summary.** Q41 decides how the difference between the work order's name NetworkDebugger and the code's NetworkLogger is handled, linking NetworkLogger into mirage_vmaf being Q10 (a)'s; the agent recommends keeping the code's name and reporting the difference.

§3.1 names the network source NetworkDebugger, an av_log interceptor, while the code's component is NetworkLogger, which also hooks io_open and io_close2 and is built into the player and mirage_tests only (sources.md §10, item 1). Whether mirage_vmaf links it for the session fetch is Q10 (a); this question is the name only, and where Q10's answer isn't (a) only option (a)'s report applies. How is the difference handled? Option (b) edits src/Demuxer.cpp and src/main.cpp, the decode and render path, so choosing it includes the owner's ruling under D-007 that a rename is outside it, or the approval of a written plan.

- **(a) Keep the code's name and report the difference.** NetworkLogger keeps its name and its tests, and design §11, item 1 reports the difference in this design's words.
- **(b) Rename it throughout.** Matches the work order's word, but edits the 21 files that name it, CMakeLists.txt, src/Demuxer.cpp and src/main.cpp among them (design/runs/networklogger-users.txt), and the documents, for a name; WP00b would make it a commit of its own before WP07.
- **(c) A headless wrapper named after the work order.** The session fetch calls a NetworkDebugger wrapper around NetworkLogger: two names for one component.

**Agent's recommendation:** (a).

**Why:** The component exists and is tested under its name, and the work order's rule is to report a difference, not to hide it.

**Blocks:** WP00b, WP07, WP15b.

#### Q44. Sessions, the fetch and decoded messages: seven choices that change their output

*Round 2.*

**Summary.** Q44 decides seven defaults of sessions, the session fetch and decoded SCTE-35 messages that change their output, and the agent recommends each as follows: plans with gaps or overlaps refused, requests placed on their segment's media span or at the first span that plays their playlist, no retries in the fetch, one event per fetched file with its redirect chain in the payload, one cue for a message in an EXT-X-CUE-OUT's segment, a fetch one file at a time without pacing, and each played variant's playlist fetched once.

Earlier versions of this design made six of these choices as process choices (P-P, P-H, P-K, and three parts of P-N: its retries, its pacing and its playlist fetches), and (d) answers the first review's case of a redirected fetch; each changes what a session's timeline or a cue outputs, so each is the owner's. Each item has its options and the agent's recommendation and can be accepted or changed on its own; design §5.5, §5.6 and §5.7 give the context.

- **(a) A plan whose spans leave gaps or overlap (formerly P-P).**
  - **1. Refused, naming the frames.** Each span must start where the previous one ended on the reference's frames.
  - **2. Allowed and shown.** Gaps unscored and overlaps kept as given, which a player's log with seeks will need later.
  - *Agent's recommendation:* 1. *Why:* In a scripted plan a gap or an overlap is a mistake, and the player's log, which may have seeks, isn't in Phase 3 (Q13).
- **(b) Where requests sit (formerly part of P-H).**
  - **1. On the media axis.** A segment request on its segment's media span; a playlist request at the first frame of the first span that plays its variant; the master's at the session's first frame.
  - **2. Playlist and master requests with no media position.** Kept in the document, but never listed by an overlap.
  - *Agent's recommendation:* 1. *Why:* A request off the axis can never be listed.
- **(c) Retries in the session fetch (formerly part of P-N).**
  - **1. None.** One try per file; a transient failure is recorded as a failed request, even for a segment the scoring read without trouble.
  - **2. As the scorer's demuxer retries.** Up to kSegMaxRetry = 10 more tries (src/VMAFSegments.h:28), each try its own row in the event's payload.
  - **3. An option, default none.** One more option to record and test.
  - *Agent's recommendation:* 1. *Why:* The fetch records what one sequential client got at the time, and its events carry origin fetch, so a failure there says nothing about the scores.
- **(d) A fetch that is redirected.**
  - **1. One event per fetched file.** NetworkLogger starts a row at each request line, a redirect's included (src/NetworkLogger.cpp:601, by reading, not run); the event's times run from the first row's start to the last row's end, its status is the last row's, and the chain is in its payload.
  - **2. One event per row.** Each request of the chain listed on its own.
  - *Agent's recommendation:* 1. *Why:* A player waits for the whole chain, and the example's duration is a segment's, not a request's.
- **(e) A message in the same segment as an EXT-X-CUE-OUT (formerly P-K).**
  - **1. That cue's message.** The two make one cue_out event, with the decoded message in its payload.
  - **2. Each its own cue.** Two cue_out events for AWS-style pairs of OATCLS and CUE-OUT (scte35.md §2.4).
  - *Agent's recommendation:* 1. *Why:* They are one signal written twice; two cue-outs would claim two breaks.
- **(f) The fetch's pacing (formerly part of P-N).**
  - **1. One file at a time, unpaced.** Each request starts once the file before it has been read; the fetch is as quick as the server, and an event's wall clock says when the fetch, not a player, made the request.
  - **2. One file at a time, paced to the plan's media time.** Each segment requested when its span reaches it on a clock started with the session, as a player without a buffer would: wall clocks then follow playback, but a build with a session lasts at least the session's media length, 8 s for a session over a whole D-069 ladder (testgen.md §2.3).
  - **3. A buffer like a player's.** Requests run ahead of playback up to a buffer length: a model of a player, whose length and rules would be one more choice for the paper to defend.
  - *Agent's recommendation:* 1. *Why:* A request's duration runs from its request to its last byte under every option, and the events carry origin fetch; pacing would only add the session's media length to every build, and a buffer is a player model.
- **(g) A variant's media playlist in the fetch (formerly part of P-N).**
  - **1. Once, the first time a span plays it.** One playlist_request per played variant, at that span's first frame (item (b)).
  - **2. At the start of every span that plays it.** A switch back to a variant fetches its playlist again: one playlist_request per span.
  - *Agent's recommendation:* 1. *Why:* A VOD playlist (Q43 (a)) doesn't change during a session, so one fetch per variant shows its request without repeating it.

**Agent's recommendation:** (a) 1; (b) 1; (c) 1; (d) 1; (e) 1; (f) 1; (g) 1.

**Why:** Each recommended default records what was planned or observed without inferring more, one signal stays one event, and the fetch is as quick as its server allows.

**Blocks:** WP00b, WP05, WP07, WP09b, WP11b.

### Round 3, the questions in full

#### Q09. Decode errors in the timeline

*Round 3; **D-007**: the answer is, or includes, an approval or a ruling under D-007.*

**Summary.** Q09 decides whether and how Phase 3 records decode errors; the agent recommends recording the scoring decode's errors in the analyzer if the owner rules that recording alone is outside D-007 or approves its short plan, and otherwise leaving decode errors out of Phase 3.

§3.1 lists decode errors, but today a damaged segment scores to a complete report with no trace of them: 6 frames carried decode_error_flags 4 and scored 0 to 34.6 (sources.md §7). Where do decode errors come from? Option (a) touches the analyzer's decode loop: answering it is a ruling under D-007 that recording alone is outside it, or the approval of a written plan.

- **(a) Recorded by the analyzer while scoring.** The flags and send errors read in DecodeCtx (src/VMAFAnalyzer.cpp:197-241), as a sparse per-pair list in the report; no frame, pairing or score changes, and no extra decode. A ladder must be scored again to carry them; WP16 records them and makes them decode_error events.
- **(b) A decode pass in the timeline build.** The scorer untouched; every variant decoded again (time: MEASUREMENT PENDING), in an adapter of its own (WP16), recording a second decode's errors, not the scored one's.
- **(c) Not in Phase 3.** Keyframes and corrupt packets from the demux pass and unpaired frames from the report only; decode errors a difference to report.

**Agent's recommendation:** (a) if the owner so rules or approves the plan; otherwise (c).

**Why:** Only the scoring decode's errors explain the scores being attributed, at no decode cost. Neither acceptance test needs them, so the package is conditional and off the acceptance path.

**Blocks:** WP16.

#### Q13. The player's own session log in Phase 3

*Round 3; **D-007**: the answer is, or includes, an approval or a ruling under D-007.*

**Summary.** Q13 decides whether the player's own session log is part of Phase 3; the agent recommends leaving it out, so that sessions come only from scripted plans, and taking it later with its own D-007 plan.

D-068 has sessions from a scripted plan now and later from a log the player records. Recording needs hooks in switch_video, read_loop, video_decode_loop and on_present (sources.md §6), which sit in the render and decode path. Is the log part of Phase 3? Answering (b) asks the agent for a written plan under D-007, for the owner to approve before any code.

- **(a) Not in Phase 3.** Scripted plans only, the plan's format leaving room for the log. The decoder events D-068's limits expect for frames dropped at a switch then don't exist in Phase 3, a difference to report (design §11, item 3).
- **(b) In Phase 3, under a D-007 plan.** Logging-only hooks with the rung, the take-over point and the frames dropped; its package shares src/main.cpp with the panel's, so the two run one after the other.
- **(c) From the existing switch-trace text.** No change to the player, a reader of it in WP17; but free text without the rung, on logger::uptime()'s clock, which isn't NetworkLogger's (sources.md §6 and §9).

**Agent's recommendation:** (a).

**Why:** Neither acceptance test nor the composite example needs it, and (b) is a render and decode path change the owner should take with its own plan.

**Blocks:** WP17.

#### Q36. The §3.3 UI plan under D-007, and the panel's place

*Round 3; **D-007**: the answer is, or includes, an approval or a ruling under D-007.*

**Summary.** Q36 asks the owner to approve design §10 as the written D-007 plan for §3.3's panel and to choose where the panel goes; the agent recommends approving it, docked above the control stack, and building it after the headless packages.

Design §10 (from ui.md) is a written plan for §3.3: an ImGui panel that reads a timeline document and its ladder report, with its loading and math in ImGui-free files tested in ctest. By the agent's reading none of it touches the render or decode path (ui.md §3), and approving it is the approval D-007 asks for. Does the owner approve it, and where does the panel go?

- **(a) Approve, docked above the control stack.** Full width beside the seek bar, lowest in the bottom stack so that it doesn't move when the filmstrip shows; it covers the lower video.
- **(b) Approve, as a tab of the network sidebar.** Docking, resizing and focus handling for free; narrow; tied to N.
- **(c) Approve, as a floating window.** The simplest; it can cover the seek bar, and its position isn't kept.
- **(d) Not in Phase 3.** The headless parts only; §3.3 reported as deferred.

**Agent's recommendation:** (a), built after the headless packages.

**Why:** A timeline belongs beside the seek bar it shares a clock with (ui.md §2.6), and the headless packages carry the paper's claim first.

**Blocks:** WP00c, WP12, WP13.

#### Q37. Clicking a low point inside an ad whose timestamps restart

*Round 3; **D-007**: the answer is, or includes, an approval or a ruling under D-007.*

**Summary.** Q37 decides what the panel does when the user clicks inside an ad that no seek can reach; the agent recommends saying the frame isn't reachable and offering a seek to the segment before the discontinuity, which changes no render or decode code.

With FFmpeg 8.1.1's hls demuxer, no seek reached the frames of an ad whose timestamps restart at 0.040 s: by PTS the seek fails and the player's clamp sends it to the stream's start, and by playlist position the ad is skipped (ui.md §2.4, measured). An ad that restarts at the main content's origin, as D-069's does, was not probed (MEASUREMENT PENDING). What does the panel do on such a click? Options (b) and (c) change the render loop or Demuxer, each needing a written plan under D-007.

- **(a) It says the frame isn't reachable by a seek.** And offers a seek to the start of the segment before the discontinuity, from which playing reaches the frame; no render or decode change; a difference from §3.3, reported.
- **(b) Play to the frame, then pause.** A new kind of request in the render loop (src/main.cpp).
- **(c) Another way to position the hls demuxer.** A change of src/Demuxer.cpp.

**Agent's recommendation:** (a) for Phase 3.

**Why:** It touches no render or decode path; (b) or (c) can follow with their own plan.

**Blocks:** WP12, WP13.

#### Q38. The timeline panel's toggle key

*Round 3.*

**Summary.** Q38 decides which key, if any, toggles the timeline panel; the agent leaves it to the owner's preference and suggests L if a key is wanted.

The letters bound to nothing are B, E, J, L, P, R, S, U, X, Y and Z (ui.md §1.5). Which, if any, toggles the timeline panel?

- **(a) L.** Reads as line, the timeline.
- **(b) E.** Reads as events.
- **(c) No key.** The View menu, the overlays popover and --ui-overlays only.

**Agent's recommendation:** The owner's preference; L if a key is wanted.

**Why:** A key binding is a matter of taste; L is the mnemonic one.

**Blocks:** WP13.

#### Q39. The panel's settings, and what a click outside a drop queries

*Round 3.*

**Summary.** Q39 decides what the panel lets the user configure and which interval it queries for a click outside a detected drop; the agent recommends only the document to load, with prefs.txt as the player's config file, and the clicked frame's segment as the interval.

The work order wants each setting on the command line and in a config file; the player's settings file is prefs.txt, key=value, written by the app and ignored in snapshot runs (ui.md §2.6, D9). Item (a) is what the panel lets the user configure; item (b) is which interval a click outside a detected drop queries (design §10).

- **(a) The panel's settings and the config-file rule.**
  - **1. Only the document to load, prefs.txt counting as the player's config file.** `--timeline <document>` and a prefs.txt line; the panel shows the document's drops, so it never disagrees with the CLI.
  - **2. A D-030-style `--config` for the player.** A second settings mechanism in the player.
  - **3. The panel re-detects with settings of its own.** Threshold, window and N in the panel, each a player flag and a config line; its drops can differ from the document's.
  - *Agent's recommendation:* 1. *Why:* It adds the least new mechanism, and the panel and the CLI can't disagree.
- **(b) The interval a click outside a detected drop queries.**
  - **1. The clicked frame alone, [f, f + 1).** Lists the spans covering it (a segment, a break, a request) and the points exactly at it; misses a tag at the segment's start unless f is that frame.
  - **2. The clicked frame's segment of the clicked curve's variant.** Lists everything in that segment, its tags and keyframe included; the frame alone where the variant has no segment map.
  - **3. A window of ±K frames around it.** One more setting, which needs a player flag and a config line.
  - *Agent's recommendation:* 2. *Why:* A segment is the unit in which playlist, SCTE-35 and network events occur, and it needs no setting.

**Agent's recommendation:** (a) 1; (b) 2.

**Why:** It adds the least new mechanism, the panel and the CLI can't disagree, and a click needs no setting.

**Blocks:** WP12, WP13.

#### Q40. The player's tag ruler and ManifestParser's cue defects

*Round 3.*

**Summary.** Q40 decides what happens to the player's GL tag ruler and ManifestParser's cue defects; the agent recommends leaving them unchanged and reported now, and retiring the ruler for the panel's playlist lane with the UI package.

The player's GL tag ruler takes its tags from ManifestParser, which takes EXT-X-CUE-OUT-CONT for a cue-out, drops #EXT-OATCLS-SCTE35, leaves DATERANGE unparsed and doesn't count discontinuity sequences (sources.md §4.2). By reading, the ruler's click never fires and its ticks sit off by the stream's start time (ui.md §1.8, F1 and F2). What happens to them in Phase 3?

- **(a) Left as they are, and reported.** Phase 3's headless work uses a parser of its own; nothing of the player's tag display changes.
- **(b) Fix ManifestParser.** Changes a tested behaviour (tests/test_manifest_parser.cpp:95) and the player's display; WP13 would make it.
- **(c) Retire the GL ruler for the panel's playlist lane.** Once the panel exists: one tag display, on the new parser; changes src/DebugHUD.{h,cpp}.

**Agent's recommendation:** (a) now, (c) with the UI package.

**Why:** The headless work doesn't need them, and the panel's lane replaces the ruler without editing a tested parser.

**Blocks:** WP13.

#### Q42. Differences from the work order with no alternative in Phase 3

*Round 3.*

**Summary.** Q42 asks how the differences from the work order that no option of Phase 3 removes are handled; the agent recommends acknowledging them as stated in the pending-decisions document and in Phase 3's PR, with no document edited to match.

Design §11 lists 14 differences; those no option removes are marked Q42: Mirage has no ABR (item 2), the offline decode drops no frame of its own (item 3), the top rung has no score in top-rung mode (item 11), and FFmpeg 8.1.1 exports no HTTP status (item 12). A repository document is also wrong: docs/VMAF_GAP_REPORT.md:230-231 says the Decoder has a dropped-frame counter, which is the render loop's total_dropped (src/main.cpp:1751). How are they handled?

- **(a) Acknowledged as stated.** Reported in docs/PHASE3_DECISIONS_PENDING.md and Phase 3's PR; no document edited to match.
- **(b) Acknowledged, and the gap report corrected.** As (a), plus a dated correction line in docs/VMAF_GAP_REPORT.md for its factual error, written by WP15b.
- **(c) Some reopened.** The owner names which, and the agent writes options for each.

**Agent's recommendation:** (a).

**Why:** Each follows from decided items or facts of the code, and reporting them is the work order's rule; correcting a Phase 0 document is a separate call the owner can make with (b).

**Blocks:** WP15b.

---

## 13. Process choices (P-entries, pending the owner's confirmation)

28 choices of process and measurement: how something is built, tested or measured, with no effect on what an output says by default. Each is the agent's, pending the owner's confirmation, and is numbered from P-082 when recorded in `docs/DECISIONS.md`. The same text is in `design/p_entries.json`.

| | Choice | Alternatives |
|---|---|---|
| **P-A** Exact arithmetic | Event positions are reference frame indices (int64). PTS are int64 ticks of their stream's time base, recovered from the report's seconds by rounding seconds x den / num and checked against the demux pass's packet PTS (design-evaluation run 04: every frame agreed). Seconds come from libavutil's rational arithmetic (AVRational, av_rescale_q_rnd), as VMAFAlign already uses it (src/VMAFAlign.h:9-11), and are turned into doubles only when written with json::number. | A Rational type of the module's own with 128-bit intermediates (design-library P-B); seconds as doubles with tolerances, which listed boundary events inconsistently (design-evaluation runs 07 and 08; design-library runs axis-ad-float, axis-degraded-float). |
| **P-B** The timeline core's dependency rule | The core files include no libavformat, libavcodec, libvmaf, GL or ImGui header; libavutil is allowed, as VMAFAlign uses it. The adapters (TimelineMedia, SessionFetch) are the only code that opens inputs, and each returns plain structs that a test can build by hand. | A core with no FFmpeg at all and its own CRC, base64 and rationals (design-library P-A, P-H). |
| **P-C** Build wiring | Per-target source lists, as CMakeLists.txt has them (150-194, 233-246, 264-329). CMakeLists.txt is changed only by the wiring packages, one commit per round (WP00a, WP00b, WP00c), each adding, as empty compiling files with their lines, the files whose existence that round's answers or the owner's decisions settle, once those answers are in, and by one wiring commit the lead makes when a conditional package (WP16, WP17) starts; no other package edits it, so parallel packages never conflict there. The edits come one at a time: each wiring package depends on the one before it, and the lead's commit for a conditional package is made while no wiring package is open. | A CMake OBJECT library linked into the three targets (design-library P-A), a pattern the build doesn't use yet; one skeleton commit for every round's files at once, which would wire files whose existence later answers decide; each package editing CMakeLists.txt, which the lead then merges. |
| **P-D** Media playlists read again at build time | The timeline build reads each scored media playlist with vmafladder::read_master (any #EXTM3U text, the analyzer's network limits, src/VMAFLadder.cpp:293-334) and writes its URL, location and SHA-256 into the document; the ladder report is not changed for it. | The ladder report recording each pair's media-playlist SHA-256s, a report change that is the owner's to make (it breaks tests/test_vmaf_report.cpp:909's exact key count); tapping the bytes the hls demuxer read (sources.md §11, choice 3 (b)), whose handle avformat_open_input probes. |
| **P-F** The demux pass | Each scored playlist is opened as DecodeCtx::open() opens an input (the interrupt callback, vmafnet::set_io_limits, vmafsegments::set_hls_options, avformat_find_stream_info, the best video stream; src/VMAFAnalyzer.cpp:166-194), with an io_open/io_close2 recorder of its own (each file in open order with its bytes_read at close), and its packets are read with av_read_frame without decoding. | A decode pass (Q09 (b)); extending SegmentWatch, which changes a tested header (src/VMAFSegments.h). |
| **P-G** Frames to segments | A packet's segment is the one whose bytes, counted in open order with the playlist's own open left out, hold its AVPacket::pos; an input's frames are each segment's packets in PTS order, the segments in read order; every scored frame's PTS is checked against the report's. What a mismatch costs is Q43 (c). | PTS against EXTINF sums (145 and 193 of 200 right, design-library runs map-by-pos-*); segment heads through hls::parse_segment_start (sources.md §11, choice 2 (c)). |
| **P-I** Canonical order and deterministic ids | Events are ordered by (ref_index, ref_end, source, type, variant, id); an id is a string made of the source, the type, the variant and a per-type index; identical inputs give byte-identical documents apart from measured fields (the fetch's times and wall clock). | Insertion order; integer ids, which change when another source's events are added. |
| **P-J** The overlap index | A vector of events sorted by start with a running maximum of ends, built once per timeline, and checked against brute force on random events in the tests. | An interval tree; the documents hold one event per segment and per tag (32 to 44 events for the prototype's 200-frame ladders, design-evaluation run 11). |
| **P-L** SCTE-35 decoding with libavutil | The CRC_32 is checked with av_crc(av_crc_get_table(AV_CRC_32_IEEE), UINT32_MAX, section, length) == 0, as FFmpeg's MPEG-TS demuxer checks sections ([mpegts.c#L463](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/mpegts.c#L463); scte35.md §3.6); base64 through av_base64_decode; hex parsed in the module; the decoder never reads past its input; an encrypted message (encrypted_packet 1) is reported undecoded. | A constexpr CRC table of the module's own, tested against av_crc (design-library P-H). |
| **P-M** SCTE-35 test sections | The tests build each section from field values with a writer in the test, which computes its CRC. The published messages the scte35 survey collected, which stay outside the repository until Q21 is answered, and scte35.md's reference decoder (scte35.md §3.7) serve as a cross-check while the decoder is written. An optional test case reads a local vectors file named by the MIRAGE_SCTE35_VECTORS environment variable and is skipped when it is unset; nothing third-party is committed unless Q21 allows it. | Committing third-party vectors (Q21). |
| **P-N** The session fetch | After the demux passes, the build fetches the session's files: the master first, then each span's files in play order, with the pacing and the playlist fetches of Q44 (f) and (g), one connection per file and the analyzer's network limits (D-036). NetworkLogger is installed for the fetch only and uninstalled after it, which restores FFmpeg's default callback (src/NetworkLogger.cpp:179-185); no other code installs a callback (design/runs/log-callback-grep.txt). Each file's rows are copied with copy_changed_since() and the logger cleared after its close (src/NetworkLogger.h:157-160), so the 4000-row limit (src/NetworkLogger.h:194) evicts nothing; rows are tagged NetInitiator::Playback; times are steady microseconds from the session's start plus one UTC anchor; fetch URLs are the report's, or the same paths rebased onto --fetch-base. run_timeline() and run_attribute() set FFmpeg's log level and initialize its network as tools/mirage_vmaf.cpp:155-156 does, since they are dispatched before those lines. Retries, redirects, pacing and playlist fetches, which change the network events, are Q44 (c), (d), (f) and (g); revision 2 moved the last two out of this entry. | NetworkLogger installed for the whole process; a new initiator value, which changes src/NetworkLogger.h; the rows copied once at the end, which could lose a long session's first requests to the row limit. |
| **P-O** A delay rule and a redirect rule in tests/HttpFileServer.h | The test server gains a rule that holds a path's response head for a set time, and one that answers a path with a redirect to another, keeping its hold and cut rules (tests/HttpFileServer.h:31-36). | A second server class. |
| **P-Q** The synthetic ladders' generator, and how often it runs | testgen's prototype (testgen/src/testgen.cpp) is ported to tests/SyntheticLadder.{h,cpp}, compiled with optimization in every build type, since generation took 16.941 s unoptimized against 2.022 to 2.313 s optimized (testgen.md §3.4); each ladder is made once per process, as tests/test_vmaf_ladder.cpp:192-216 makes its fixtures; the generator's layout is the ground truth. ctest runs each Catch2 test case in its own process (review-adversarial/runs/ctest-registration.txt), so the acceptance cases are sections of one test case, which makes each of its ladders once per ctest run; the composite example and WP10's determinism check are test cases of their own, for seven generations per ctest run under Q34 (d) (design §9.5). | A CTest fixture (FIXTURES_SETUP, with FIXTURES_REQUIRED set on the acceptance tests through the TEST_SPEC and PROPERTIES arguments of Catch2 v3.7.1's catch_discover_tests, https://github.com/catchorg/Catch2/blob/v3.7.1/extras/Catch.cmake#L29-L34) that writes the ladders once per ctest run into the build tree: fewer generations, but one directory that two ctest runs of a build tree would share, against the per-process rule of D-032 and P-036; the prototype as is; a generation per test case. |
| **P-R** Acceptance assertions independent of open choices | The acceptance tests assert reference frame ranges, URIs, media sequence numbers, event types and offsets in frames, with detection parameters pinned inside the measured region (T = 6, W = 50, N = 12, the prototype's values, inside design-evaluation run 05's region), not the tool's defaults; the seconds of the axis the owner picks are checked in WP01's unit tests. | Assertions on the defaults, which wait for Q26 and change with it. |
| **P-S** The composite example's delay | In ctest the composite example uses a delay of its own, under the analyzer's 5 s network limit (D-036), and asserts a measured duration at least the delay and status 200; the work order's 2.3 s is used only in the eval harness. | 2.3 s in ctest. |
| **P-T** Tests that run programs, and test tags | The acceptance and CLI tests run mirage_vmaf as a program with posix_spawn through MIRAGE_VMAF_PATH, as tests/test_vmaf_ladder.cpp:286-310 does; the new tests are tagged [timeline], [scte35], [session], [attribution] and [acceptance]. | In-process calls only, which skip the CLI and the files a user gets. |
| **P-U** Keeping the media | With MIRAGE_KEEP_MEDIA=<dir> the acceptance and composite test cases keep their ladders, reports and documents, each test case under <dir>/<its name>/, so that two test processes under ctest -j never write one directory; the eval harness and the UI's scripted QA run read them there (ui.md §5.3 asks for such a switch). | A generator program under tools/; one shared directory, which two processes would write at once. |
| **P-V** The subcommands' option tables | Each subcommand has an option table of its own in TimelineCli, built as vmafcli's is (one row: the option, its config setting, its help; src/VMAFCli.cpp:26-35, 92-124), with D-030's name = value config file, one per invocation, recorded by path and SHA-256, the command line replacing the file; vmafcli's parser is unchanged, since its parse() and read_settings() are written against its own kOptions and Args (src/VMAFCli.cpp:195-257, 273-344). | vmafcli's parser made generic for the subcommands (design-library P-G), which changes tested code. |
| **P-W** Exit statuses and writing | The subcommands exit as mirage_vmaf does (tools/mirage_vmaf.cpp:7-17): 0 written, 1 a failure (an unreadable input, a hash mismatch, an input refused under Q43 (a) or (b), no document left at --out), 2 bad arguments or config file; documents are written through a temporary file renamed over --out, as vmafreport::write_file does (src/VMAFReport.h:149). | None proposed. |
| **P-Y** The JSON reader's form | A document tree with objects kept in their order, duplicate keys refused (as the tests' reader refuses them, tests/test_vmaf_report.cpp:104), a nesting limit, and numbers read with std::from_chars so that they round-trip with json::number; its memory and time on the largest committed ladder report are measured before the panel loads such reports. | A streaming reader from the start (Q32 (b)). |
| **P-Z** Hashes, and the digest against redaction | Hashes are computed at the edges with filehash::sha256_hex; the core returns the canonical text to hash; the scores digest leaves out every field the eval scripts redact (eval/ladder/pool_check.py:117-118, eval/consistency/ladder_check.py:25-27), which a test checks by redacting a report as they do. | Hashing inside the core. |
| **P-AD** The attribute subcommand's text layout | One block per drop: its curve, reference frames, interval in seconds, baseline, minimum, mean and magnitudes; then one line per event, grouped by source in §3.1's order, with its offsets in seconds and in frames, as design-evaluation run 10's prototype printed it. Which format is the default is Q31 (a). | JSON only. |
| **P-AF** The evaluation harness's records | eval/timeline/ in standard-library Python, each record with cmd.txt and environment.txt that give the commit by git rev-parse HEAD, as P-006's practice is. | None proposed. |
| **P-AG** Shared declarations | WP00a writes the declarations that round-1 packages running in parallel share, once the answers they encode are in (Q02, Q03, Q04, Q07), and WP00b those of the sessions (Q11, Q12); each module's own functions and option structs are written by the package that implements it. After each, a change to a type another package uses goes through the lead, who merges it before the packages that need it. | Each package declaring what it needs, merged later; the declarations written before the owner's answers, which would present the agent's recommendations as made. |
| **P-AH** Worktrees and merges | Each package is built in a worktree branched from vmaf/phase-3 (D-066) and merged by the lead in design §14's order; each round's answers are recorded by the lead in docs/DECISIONS.md and marked in docs/PHASE3_DECISIONS_PENDING.md before that round's packages start; nothing is pushed until the owner says. | None proposed. |
| **P-AI** Checking the schema file in ctest | A subset JSON Schema validator in the test (type, required, properties, items, enum, additionalProperties) checks the writer's output against docs/schema/mirage-timeline-1.schema.json; no dependency is added. | A Python validator, a new dependency for standard-library-only eval tooling (schema.md, open issue 3); key tests only, leaving the schema file unchecked. |
| **P-AJ** Axis helpers copied, not moved | TimelineView gets its own copies of nice_step and axis_label (src/NetInspectorAlign.cpp:69-107, in an anonymous namespace that ends at :109), and src/NetInspectorAlign.cpp stays unchanged. | Moving them into TimelineView and changing NetInspectorAlign.cpp to use them (ui.md §2.5): one definition, but a change to a working file for no behaviour. |
| **P-AK** The quote check of documents and posted text | Before each commit of a Phase 3 document, and of this survey and design when they are committed, and before Phase 3's PR description or any other text about Phase 3 is posted, the agent checks every file and text against the work order's translation, which stays outside the repository (D-013), for runs of six or more words in common; the checker's output, which names files, lines and counts but no shared words, is kept as the record of the commit or the posting, and its prose runs must be 0 (design/runs/quote-check.txt is the first). | Asking the owner whether short quotations are within D-013, which D-060's acceptance of a rendering with none quoted already answers; no check. |

Moved to owner questions in revision 1, because each changes what an output says by default (their letters are not reused):

- **P-E** Live media playlists refused: Q43 (a).
- **P-H** Placement rules: Q43 (d) and Q44 (b).
- **P-K** Where a tag belongs, and one cue per signal: Q44 (e); its first half, a tag belonging to the segment whose URI follows it, is RFC 8216's rule (design §5.1).
- **P-P** Segment-range plans follow one another: Q44 (a).
- **P-X** The ladder report reader's scope: Q43 (b).
- **P-AA** The rank rule: Q43 (e).
- **P-AB** Events in several variants' playlists: Q43 (f).
- **P-AC** Provenance copied into the document: Q04 (a), since the ladder report's provenance copied into the document is part of design §8's layout.
- **P-AE** A drop's interval, and intervals given in seconds: Q43 (g).

---

## 14. Work packages

25 packages. Each is built in a worktree of its own from `vmaf/phase-3` (P-AH), with its unit tests. New files are the package's own; a shared file belongs to one package at a time, the later one depending on the earlier, and `CMakeLists.txt` to the wiring packages alone (P-C). A package's round is the round of answers it waits for (0: none, so it can start now), and it starts only once every question of that round is answered. "Blocked by" lists the questions whose answers change what the package builds: its code, its tests or the form of its output. A question's "Blocks" in §12 is the list of packages naming it here. Both are checked by `design/tools/render.py`, with the order of every shared file's owners. The same text is in `design/work_packages.json`.

| WP | Title | Round | Depends on | Blocked by | Shared files it owns (changed) |
|---|---|---|---|---|---|
| WP15a | The pending-decisions document and the UI plan | 0 | none | none | docs/DECISIONS.md |
| WP00a | Wiring and shared declarations, round 1 | 1 | none | Q01, Q02, Q03, Q04, Q05, Q07, Q08, Q20, Q31, Q32, Q43 | CMakeLists.txt |
| WP01 | Timeline core: model, axis and the overlap query | 1 | WP00a | Q02, Q28, Q29, Q30, Q43 | none |
| WP02 | Drop detection on a curve | 1 | WP00a | Q22, Q23, Q24, Q25, Q26, Q27, Q43 | none |
| WP03 | JSON reader and ladder report reader | 1 | WP00a | Q06, Q32, Q43 | none |
| WP04 | Playlist events, the plain cue tags, and the pairing of every cue | 1 | WP00a | Q04, Q17, Q18, Q19, Q20, Q43 | none |
| WP06 | Media ingest: playlists, the demux pass, frame-to-segment maps | 1 | WP00a | Q08, Q43 | none |
| WP08 | The timeline document and its JSON Schema: the ladder part | 1 | WP01, WP03 | Q01, Q02, Q03, Q04, Q05, Q06, Q07, Q22, Q23, Q24, Q25, Q26, Q27, Q28, Q29, Q30 | src/VMAFReport.h, src/VMAFReport.cpp and tests/test_vmaf_report.cpp (only under Q01 (A)) |
| WP09a | The builder and the CLI subcommands, for a ladder | 1 | WP01, WP02, WP03, WP04, WP06, WP08 | Q01, Q02, Q04, Q06, Q07, Q08, Q19, Q20, Q22, Q24, Q26, Q27, Q28, Q29, Q30, Q31, Q43 | tools/mirage_vmaf.cpp; src/VMAFCli.cpp |
| WP10 | Synthetic ladders (D-069's content) | 1 | WP00a | Q33, Q34, Q35 | none |
| WP11a | §3.2's two acceptance tests | 1 | WP09a, WP10 | Q01, Q02, Q04, Q22, Q23, Q25, Q29, Q31, Q33, Q34, Q35 | none |
| WP00b | Wiring and shared declarations, round 2 | 2 | WP00a, WP01 | Q10, Q11, Q12, Q41, Q44 | CMakeLists.txt; src/VMAFNet.h (a comment only); src/TimelineModel.h (the session declarations, through the lead); src/SessionPlan.h (the session plan's and the fetch's types, through the lead); src/NetworkLogger.h, src/NetworkLogger.cpp and the 19 other files that name NetworkLogger, src/main.cpp, src/Demuxer.cpp, src/ManifestParser.cpp and src/NetInspectorAlign.cpp among them (only under Q41 (b)) |
| WP05 | SCTE-35 decoder and message events | 2 | WP00a, WP04 | Q04, Q14, Q15, Q16, Q18, Q21, Q44 | none |
| WP07 | Sessions, their drops, and the session fetch | 2 | WP00b, WP01, WP02, WP03, WP06 | Q03, Q04, Q07, Q10, Q11, Q12, Q22, Q28, Q41, Q44 | tests/HttpFileServer.h; src/TimelineModel.cpp (a session's curve, after WP01 has merged); src/TimelineQuery.cpp (a session's scope, after WP01 has merged) |
| WP09b | Sessions in the build, the CLI and the document | 2 | WP09a, WP07 | Q01, Q03, Q04, Q05, Q07, Q10, Q11, Q12, Q22, Q44 | src/TimelineBuild.h; src/TimelineBuild.cpp; src/TimelineCli.h; src/TimelineCli.cpp; tests/test_timeline_build.cpp; tests/test_timeline_cli.cpp; src/TimelineDoc.h; src/TimelineDoc.cpp; tests/test_timeline_doc.cpp; docs/schema/mirage-timeline-1.schema.json; docs/TIMELINE_FORMAT.md |
| WP09c | Decoded SCTE-35 messages in the build and the CLI | 2 | WP09b, WP05 | Q15, Q16 | src/TimelineBuild.h; src/TimelineBuild.cpp; src/TimelineCli.h; src/TimelineCli.cpp; tests/test_timeline_build.cpp; tests/test_timeline_cli.cpp |
| WP11b | The composite example (test 3) | 2 | WP09b, WP10 | Q01, Q04, Q07, Q10, Q11, Q22, Q23, Q25, Q44 | none |
| WP00c | Wiring, round 3: the UI | 3 | WP00a, WP00b | Q36 | CMakeLists.txt |
| WP12 | UI loading and math, ImGui-free | 3 | WP00c, WP01, WP03, WP07, WP08, WP09b | Q01, Q02, Q05, Q06, Q36, Q37, Q39 | none |
| WP13 | The timeline panel in the player | 3 | WP12, WP01, WP02, WP08 | Q36, Q37, Q38, Q39, Q40 | src/main.cpp; src/PlayerUI.h; src/PlayerUI.cpp; src/DebugHUD.h and src/DebugHUD.cpp (only under Q40 (c)); src/ManifestParser.h, src/ManifestParser.cpp and tests/test_manifest_parser.cpp (only under Q40 (b)); src/Demuxer.h and src/Demuxer.cpp (only under Q37 (c), with its plan approved) |
| WP14 | The evaluation harness: reproduction from one command | 3 | WP09a, WP09b, WP10, WP11a, WP11b | Q02, Q04, Q07, Q10, Q22, Q23, Q25, Q31 | none |
| WP15b | The differences' wording and the gap report's correction | 3 | WP15a | Q41, Q42 | docs/PHASE3_DECISIONS_PENDING.md (after WP15a has merged); docs/VMAF_GAP_REPORT.md (only under Q42 (b)) |
| WP15c | The closing documents and the PR description | 3 | WP09c, WP13, WP14, WP15b | none | docs/PHASE3_STATUS.md; docs/DECISIONS.md (after WP15a has merged); docs/AI_CONTRIBUTIONS.md; docs/PAPER_METHOD_NOTES.md |
| WP16 | Conditional: what the scorer records, its extra pair, and a decode pass | 3 | WP03, WP06, WP09c | Q01, Q08, Q09, Q10, Q12 | src/VMAFAnalyzer.h; src/VMAFAnalyzer.cpp; src/VMAFReport.h; src/VMAFReport.cpp; tests/test_vmaf_report.cpp; tests/test_vmaf_analyzer.cpp; src/VMAFSegments.h and tests/test_vmaf_segments.cpp (only under Q10 (e)); src/VMAFLadder.h, src/VMAFLadder.cpp and tests/test_vmaf_ladder.cpp (only under Q12 (d)); src/LadderReportRead.cpp and tests/test_ladder_report_read.cpp (after WP03 has merged); src/TimelineBuild.cpp and tests/test_timeline_build.cpp (after WP09c has merged) |
| WP17 | Later: the player's session log | 3 | WP07, WP13 | Q11, Q13 | src/main.cpp (only under Q13 (b), after WP13 has merged; the two never run in parallel); src/Demuxer.cpp (only under Q13 (b)); src/MediaSession.cpp (only under Q13 (b)) |

### WP15a The pending-decisions document and the UI plan

Now, before any answer, since no answer changes them: docs/PHASE3_DECISIONS_PENDING.md with §12's questions by round, §11's differences and §13's P-entries, each pending; docs/plans/timeline-ui.md, design §10 as the written D-007 plan for the owner to approve; §13's P-entries appended to docs/DECISIONS.md from P-082 on, each marked pending (entries only added, D-060). Each gives the work order's requirements in its own words, quoting no passage of it (D-013, D-060). The lead then records the owner's answers as those of each round come in (P-AH).

- **New files:** `docs/PHASE3_DECISIONS_PENDING.md`, `docs/plans/timeline-ui.md`.
- **Changed (owned):** `docs/DECISIONS.md`.
- **Round:** 0.
- **Depends on:** none.
- **Blocked by:** none.
- **Tests:**
  - Before each commit: a cross-reference check of every cited file:line, decision and run record, and the quote check of every committed document against the work order's translation, kept outside the repository, whose output names files and lines only (P-AK).

### WP00a Wiring and shared declarations, round 1

One commit once round 1's answers are in. Every file of design §3.2 whose existence the owner's decisions (D-068, D-069, D-070) or round 1's answers settle, as an empty compiling file: the core (SessionPlan and Scte35 included), TimelineMedia, TimelineCli, tests/SyntheticLadder, and their tests and the two attribution test files; the CMake lines for them, with the core, TimelineMedia, TimelineCli and src/HlsAlign.cpp in mirage_vmaf, the same and the tests in mirage_tests, and tests/SyntheticLadder.cpp compiled with optimization in every build type, as src/VMAFPool.cpp gets its own options (CMakeLists.txt:227); under Q05 (b) also the configure-time step that records the commit, and under Q20 (b) an XML parser linked into mirage_vmaf. Then only the declarations that packages running in parallel share, which encode round 1's answers: TimelineModel.h's Source (Q04), Event and MediaKey (Q02, Q03), Variant with its ranks (Q07), Curve, CurveRef for a pair's curve, Timeline and the declaration of curve_points(); TimelineAxis.h's Axis; LadderReportRead.h's PairScores and LadderScores, which WP06 uses while WP03 writes the reader; TimelineBuild.h's SegmentFrames and VariantMedia, which WP06 returns; PlaylistEvents.h's CueInput, the extension point of D-070's interfaces (design §5.5). Each module's own functions and option structs (QueryOptions, DetectOptions and the like) are written by the package that implements it, which the questions they encode block. No behaviour. Afterwards each package owns its files' contents, a change to a shared type goes through the lead (P-AG), and only the wiring packages edit CMakeLists.txt (P-C).

- **New files:** the empty files of design §3.2 whose existence the owner's decisions or round 1's answers settle: those of round 1's packages, and of round 2's src/SessionPlan.{h,cpp}, src/Scte35.{h,cpp}, src/Scte35Events.{h,cpp}, tests/test_session_plan.cpp, tests/test_scte35.cpp, tests/test_scte35_events.cpp and tests/test_attribution_session.cpp, which D-068 and D-070 settle; and the shared declarations (contents then owned by WP01 to WP11b).
- **Changed (owned):** `CMakeLists.txt`.
- **Round:** 1.
- **Depends on:** none.
- **Blocked by:** Q01, Q02, Q03, Q04, Q05, Q07, Q08, Q20, Q31, Q32, Q43.
- **Tests:**
  - The macos-debug and macos-headless presets build, and ctest passes as before; a compile-only test includes every new header.

### WP01 Timeline core: model, axis and the overlap query

TimelineModel: the canonical order, deterministic string ids, the overlap index on reference frames (a start-sorted vector with a running maximum of ends), and curve_points() for a pair's curve. TimelineAxis: each reference frame's media key; the seconds of the option Q02 picks, with its fallback where the reference has no segment map; the check of design §4.2, which compares (c)'s and (d)'s seconds at every scored reference frame and each frame's PTS with the frame grid of its playlist discontinuity sequence (without a map, with that of its report sequence); and seconds turned into frames for declared durations, margins and intervals given in seconds (Q43 (g)). TimelineQuery: attribute() for a pair's curve, with the half-open overlap and optional margins of Q29, the scope of Q28 for a pair (each rung's copy kept, Q43 (f)), the groups in §3.1's order without ranking (Q30), and offsets in seconds and in frames. A session's curve and a session's scope are WP07's, written in these files once WP00b has declared the session types.

- **New files:** `src/TimelineModel.h`, `src/TimelineModel.cpp`, `src/TimelineAxis.h`, `src/TimelineAxis.cpp`, `src/TimelineQuery.h`, `src/TimelineQuery.cpp`, `tests/test_timeline_model.cpp`, `tests/test_timeline_axis.cpp`, `tests/test_timeline_query.cpp`.
- **Round:** 1.
- **Depends on:** WP00a.
- **Blocked by:** Q02, Q28, Q29, Q30, Q43.
- **Tests:**
  - Canonical order and deterministic ids; the overlap index against brute force on random events; points and spans at exact boundaries; empty intervals.
  - The axis on hand-made reports: PTS that restart at the main content's origin, a forward jump under 10 s, a coarser-grid pair; the seconds of Q02's option and of its fallback; seconds to frames for durations and intervals.
  - The check of design §4.2 on reports and segment maps the test writes in the shapes of design/runs/seconds-check.txt: no warning on the D-069 ad shape with its map, and its run of frames 125 to 199 listed as a fact without one; a missing reference frame (in the last segment too), an ad at 24 and at 30 fps after its discontinuity, and an EXTINF one frame too long each warned at the frames that record gives, the PTS check (2) warning for the first two and not for the third; a reference frame missing at the start of each playlist discontinuity sequence, in the shapes of review-3/runs/seconds-check-sequence-start.txt, warned by (1) alone, from the next segment on; a reference that isn't a playlist warned where its PTS change rate.
  - The scope filter for a pair's curve, the group order, exact offsets, margins.

### WP02 Drop detection on a curve

TimelineDetect: detect() over a curve's points, with the statistic and warm-up of Q23, W and N counted as Q24 says, context-first frames as Q25 says, and the drop record (the interval of Q43 (g), the baseline at the first low point, the minimum, the mean, and the magnitudes of Q27); every parameter recorded; defaults only those Q26 gives; nothing when W and N are absent, as Q26 (b) says. Which curves are detected is Q22: pairs here; sessions in WP07.

- **New files:** `src/TimelineDetect.h`, `src/TimelineDetect.cpp`, `tests/test_timeline_detect.cpp`.
- **Round:** 1.
- **Depends on:** WP00a.
- **Blocked by:** Q22, Q23, Q24, Q25, Q26, Q27, Q43.
- **Tests:**
  - Hand-built series: a step drop; single-frame dips at context starts that N passes over; alternating dips like testgen's busy control; a drop inside the blind start of Q23 (a), not found, as the document then states; gaps on a coarser grid; the baseline unchanged through a drop; the same drops from the same input twice.
  - The shapes of design/runs/detection-semantics-ad.txt rebuilt from the test's own numbers: a context start at a drop's first frame.

### WP03 JSON reader and ladder report reader

JsonRead: a document tree with objects in their order, duplicate keys refused, a nesting limit, numbers by std::from_chars so that they round-trip with json::number, errors with their position (P-Y). LadderReportRead: the reports Q43 (b) admits, others refused with the reason named; settings, master entries, reference, each complete pair's frames, failures, sequences and libvmaf contexts; PTS seconds to ticks; the canonical text of the scores digest of Q06, leaving out every field the eval scripts redact (P-Z).

- **New files:** `src/JsonRead.h`, `src/JsonRead.cpp`, `src/LadderReportRead.h`, `src/LadderReportRead.cpp`, `tests/test_json_read.cpp`, `tests/test_ladder_report_read.cpp`.
- **Round:** 1.
- **Depends on:** WP00a.
- **Blocked by:** Q06, Q32, Q43.
- **Tests:**
  - RFC 8259's grammar, escapes and UTF-8; numbers as json::number writes them (the integer literal 100, exponents); round trip with JsonText; duplicate keys; the nesting limit; error positions.
  - A report written by vmafreport::ladder_to_json from results built in the test, read back field by field; the refusals of Q43 (b) with their reasons.
  - The scores digest unchanged after the test redacts a report as eval/ladder/pool_check.py and eval/consistency/ladder_check.py redact.

### WP04 Playlist events, the plain cue tags, and the pairing of every cue

PlaylistEvents: hls::parse_media_timeline's segments plus one more scan of the text for tags, each attached to the segment whose URI follows it (RFC 8216 §4.3.2); discontinuity, program_date_time, daterange (attributes as RFC 8216 §4.3.2.7 lists them) and every other tag; the plain cue tags of D-070 (EXT-X-CUE-OUT bare or with a duration or attributes, EXT-X-CUE-OUT-CONT in both forms, EXT-X-CUE-IN bare or with an ID) into cue_out, cue_out_cont and cue_in; every embedded message (#EXT-OATCLS-SCTE35's base64, DATERANGE's SCTE35-* hex, EXT-X-CUE-OUT-CONT's SCTE35=) handed on as a CueInput; pair_breaks(), which pairs every cue, from plain tags now and from WP05's decoded messages later, into ad_break with Q17's end rule, so that the pairing has one owner; tags outside D-070's list as Q18 says; a bare discontinuity as Q19 says; a 'period' type reserved for DASH, or under Q20 (b) an MPD's periods and EventStreams read into period events and CueInputs; a live playlist as Q43 (a) says. HlsAlign and ManifestParser unchanged.

- **New files:** `src/PlaylistEvents.h`, `src/PlaylistEvents.cpp`, `tests/test_playlist_events.cpp`.
- **Round:** 1.
- **Depends on:** WP00a.
- **Blocked by:** Q04, Q17, Q18, Q19, Q20, Q43.
- **Tests:**
  - Tags before #EXTINF and between #EXTINF and the URI attach to the same segment; discontinuity numbering; PDT; vendor tags kept raw; a live playlist as Q43 (a) says.
  - The cue forms scte35.md §2 describes, written by the test in those shapes; DATERANGE attributes (quoted, hex, a malformed one kept raw); OATCLS and CUE-OUT-CONT's SCTE35= handed on as CueInputs with their anchors.
  - Pairing of cue-out and cue-in into ad_break, the end from a declared duration without a cue-in, and cues from a stub decoder paired with plain ones; testgen's ad playlist shape (testgen.md §2.3), written by the test.

### WP06 Media ingest: playlists, the demux pass, frame-to-segment maps

TimelineMedia (an adapter, libavformat): each scored playlist read with vmafladder::read_master, with its URL, location and SHA-256 (P-D); the demux pass (P-F) with its own io_open/io_close2 recorder; packets to segments by byte position, frames to packets in presentation order within a segment, and the PTS check against the report for every scored frame (P-G), a mismatch handled as Q43 (c) says; keyframes; corrupt packets, each with the next segment boundary, placed as Q43 (h) says; the reference's PTS for reference frames no pair scored. Under Q08 (a); under Q08 (b) the map and the key flags come from the report instead, and the plan changes as design §14 says.

- **New files:** `src/TimelineMedia.h`, `src/TimelineMedia.cpp`, `tests/test_timeline_media.cpp`.
- **Round:** 1.
- **Depends on:** WP00a.
- **Blocked by:** Q08, Q43.
- **Tests:**
  - HLS streams the test writes with FFmpeg's libraries (as tests/test_vmaf_segments.cpp:46-117 does): byte positions against the writer's layout; B-frames; restarted timestamps with a PTS two segments share; EXT-X-BYTERANGE segments; a missing frame, handled as Q43 (c) says; the same over HTTP from tests/HttpFileServer.h.
  - Continuity counters left unmarked at a splice: the corrupt flags found, each with the boundary that follows it, as Q43 (h) places them.
  - A playlist whose last EXTINF is shorter than its frames (as FFmpeg's hls muxer writes it) maps exactly.

### WP08 The timeline document and its JSON Schema: the ladder part

TimelineDoc: the writer (json::quote and json::number, a temporary file renamed over --out, P-W) and the reader (JsonRead) of design §8's layout under Q04 and Q05, naming its ladder report as Q06 says: the header, the axis (Q02) with its check, the variants (Q07), the events with their wall clock (Q03), the detection record (Q22 to Q26), the overlap record (Q28, Q29) and the attributions with their magnitudes (Q27) and groups (Q30); docs/schema/mirage-timeline-1.schema.json, with §5.9's whole vocabulary; docs/TIMELINE_FORMAT.md, the format's description with the event vocabulary and the mapping to §7; the subset JSON Schema validator in the test (P-AI). Under Q01 (A) the writer puts the timeline and attributions into the ladder report instead, with the report's writer and its tests. The session part is WP09b's.

- **New files:** `src/TimelineDoc.h`, `src/TimelineDoc.cpp`, `tests/test_timeline_doc.cpp`, `docs/schema/mirage-timeline-1.schema.json`, `docs/TIMELINE_FORMAT.md`.
- **Changed (owned):** src/VMAFReport.h, src/VMAFReport.cpp and tests/test_vmaf_report.cpp (only under Q01 (A)).
- **Round:** 1.
- **Depends on:** WP01, WP03.
- **Blocked by:** Q01, Q02, Q03, Q04, Q05, Q06, Q07, Q22, Q23, Q24, Q25, Q26, Q27, Q28, Q29, Q30.
- **Tests:**
  - The writer's keys and types, checked against the schema file by the subset validator; a reader round trip; byte-identical output for the same timeline twice; an unknown schema_version refused; whole numbers written as integer literals and typed 'number' in the schema; detection: null when no detection ran.

### WP09a The builder and the CLI subcommands, for a ladder

TimelineBuild: the merge of design §3.4 for a ladder without a session, with the placement rules of §5.1 and Q43, the vocabulary of Q04, unplaceable anchors in warnings; cues from plain tags paired by WP04's pair_breaks(); the segment maps from the demux pass under Q08 (a), from the report under Q08 (b). TimelineCli: the two subcommands of Q31 with option tables of their own (P-V), their config files, --help, run_timeline() and run_attribute(), which set FFmpeg's log level and initialize its network as tools/mirage_vmaf.cpp:155-156 does (P-N); detection on the curves Q22 names for a ladder, only when W and N are given (Q26 (b)), with W and N counted as Q24 says; the axis of Q02; the JSON and text outputs (P-AD), text by default, with the magnitudes of Q27 and the groups of Q30; under Q19 (c) the inference option; the exit statuses of P-W. tools/mirage_vmaf.cpp dispatches on a first word before vmafcli::parse; src/VMAFCli.cpp gains a paragraph in usage() naming the subcommands, after the existing synopsis lines, which tests/test_vmaf_cli.cpp:161 pins.

- **New files:** `src/TimelineBuild.h`, `src/TimelineBuild.cpp`, `src/TimelineCli.h`, `src/TimelineCli.cpp`, `tests/test_timeline_build.cpp`, `tests/test_timeline_cli.cpp`.
- **Changed (owned):** `tools/mirage_vmaf.cpp`; `src/VMAFCli.cpp`.
- **Round:** 1.
- **Depends on:** WP01, WP02, WP03, WP04, WP06, WP08.
- **Blocked by:** Q01, Q02, Q04, Q06, Q07, Q08, Q19, Q20, Q22, Q24, Q26, Q27, Q28, Q29, Q30, Q31, Q43.
- **Tests:**
  - The merge on hand-built inputs: each source's events placed by design §5.1's rules, unplaceable anchors in warnings, the same timeline from the same inputs twice.
  - Option and config parsing: precedence of the command line, unknown and repeated options, required options, --help; an events-only build without W and N; one end-to-end run of each subcommand as a program on a small fixture (exit statuses, outputs); tests/test_vmaf_cli.cpp passes unchanged.

### WP10 Synthetic ladders (D-069's content)

tests/SyntheticLadder.{h,cpp}, ported from testgen's prototype (testgen/src/testgen.cpp): the degraded and the ad ladders with Q33's encoder, Q34's timestamp variants and Q35's content, three rungs of eight 1 s segments at 25 fps, BANDWIDTH measured from the segments, the layout as ground truth; each ladder made once per process, which under ctest means once per Catch2 test case (P-Q); MIRAGE_KEEP_MEDIA keeps them, each test case under its own subdirectory (P-U).

- **New files:** `tests/SyntheticLadder.h`, `tests/SyntheticLadder.cpp`, `tests/test_synthetic_ladder.cpp`.
- **Round:** 1.
- **Depends on:** WP00a.
- **Blocked by:** Q33, Q34, Q35.
- **Tests:**
  - In one test case: one ladder made twice, byte-identical; its segments, frames and tags against its layout; BANDWIDTH in rung order; the encoder used recorded in the output.

### WP11a §3.2's two acceptance tests

tests/test_attribution_acceptance.cpp: one Catch2 test case with a section per case, so that ctest runs it in one process and each ladder is made once (P-Q): test 1, the degraded segment; test 2, the ad boundary, in Q34's variants. Each runs mirage_vmaf --ladder, mirage_vmaf timeline and mirage_vmaf attribute as programs (P-T), reads the document back and asserts as design §9.2 says, with pinned parameters (P-R). It needs no session, no fetch and no SCTE-35 decoder: D-069's ladders carry plain cue tags only.

- **New files:** `tests/test_attribution_acceptance.cpp`.
- **Round:** 1.
- **Depends on:** WP09a, WP10.
- **Blocked by:** Q01, Q02, Q04, Q22, Q23, Q25, Q29, Q31, Q33, Q34, Q35.
- **Tests:**
  - Test 1: one drop on the degraded rung, over its degraded segment's reference frames, whose playlist group holds that segment by URI and media sequence number; no drop on the control rung.
  - Test 2 in each timestamp variant: one drop on the low-quality ad rung, over the ad's reference frames; its scte35 group holds the cue_out at offset 0 frames and an ad_break spanning the drop; its playlist group the discontinuity at offset 0; no drop on the control rung.

### WP00b Wiring and shared declarations, round 2

One commit once round 2's answers are in: SessionFetch's files and test as empty files with their CMake lines, under Q10 (a) or (b); under Q10 (a), src/NetworkLogger.cpp added to mirage_vmaf, and the two comments that would then be wrong or misleading reworded: CMakeLists.txt:229-232 (mirage_vmaf's ladder scoring still reads playlists itself; the timeline fetch uses the network logger) and src/VMAFNet.h:8-9 (mirage_vmaf then links the network logger); the session declarations, encoding Q11, Q12 and Q44 (d), added through the lead (P-AG) to headers WP00a created: Span, Session, Timeline::sessions and CurveRef's session kind in src/TimelineModel.h; PlanSpan, SessionPlan, RequestRow and Request in src/SessionPlan.h, whose contents WP07 then owns. Under Q41 (b), first and as a commit of its own, NetworkLogger renamed throughout, under the owner's ruling under D-007 or an approved plan, since src/Demuxer.cpp and src/main.cpp are among the 21 files that name it (design/runs/networklogger-users.txt).

- **New files:** the empty files of SessionFetch and its test.
- **Changed (owned):** `CMakeLists.txt`; src/VMAFNet.h (a comment only); src/TimelineModel.h (the session declarations, through the lead); src/SessionPlan.h (the session plan's and the fetch's types, through the lead); src/NetworkLogger.h, src/NetworkLogger.cpp and the 19 other files that name NetworkLogger, src/main.cpp, src/Demuxer.cpp, src/ManifestParser.cpp and src/NetInspectorAlign.cpp among them (only under Q41 (b)).
- **Round:** 2.
- **Depends on:** WP00a, WP01.
- **Blocked by:** Q10, Q11, Q12, Q41, Q44.
- **Tests:**
  - The presets build and ctest passes as before; the compile-only test includes the new headers.
  - Under Q41 (b), the same after the rename, tests/test_network_logger.cpp included.

### WP05 SCTE-35 decoder and message events

Scte35: a splice_info_section from base64 (av_base64_decode) or hex; the CRC_32 by av_crc (P-L); splice_insert, time_signal, break_duration, splice_time with pts_adjustment added mod 2^33, the descriptor loop with unknown descriptors skipped by length, segmentation_descriptor with its UPID bytes kept; never reads past its input; encrypted messages reported undecoded. Scte35Events: every CueInput decoded, classed as Q15 says (with cancellations), with the CRC policy of Q16; placed by its anchor kind as design §5.1 says, a splice time on the carrying variant's own frames within the carrying playlist's discontinuity sequence, with splice_point events and every anchor in the payload as Q14 says, and a warning when an anchor falls outside the tag's segment; a message in a CUE-OUT's segment merged into that cue as Q44 (e) says; the decoded cues returned to WP04's pair_breaks().

- **New files:** `src/Scte35.h`, `src/Scte35.cpp`, `src/Scte35Events.h`, `src/Scte35Events.cpp`, `tests/test_scte35.cpp`, `tests/test_scte35_events.cpp`.
- **Round:** 2.
- **Depends on:** WP00a, WP04.
- **Blocked by:** Q04, Q14, Q15, Q16, Q18, Q21, Q44.
- **Tests:**
  - Sections built by a writer in the test (P-M): splice_insert out and in, immediate, break_duration with auto_return, time_signal with segmentation descriptors and sub-segments, a descriptor whose identifier isn't CUEI, splice_command_length 0xFFF, alignment stuffing, the pts_adjustment wrap mod 2^33, CRC good and bad, truncation at every byte, the encrypted flag, cancel indicators; base64 and hex input.
  - The CRC against av_crc on the same bytes; an optional case that reads a local vectors file named by MIRAGE_SCTE35_VECTORS, skipped when it is unset.
  - Event mapping: classes, the CRC policy, anchors and their warnings, pairing of message-only cues through WP04's pair_breaks(), a CUE-OUT-CONT's message decoded.
  - Placement: a splice time on a distorted variant whose PTS origin differs from the reference's lands on the right reference frame; a section anchored by media PTS, as an in-segment reader would give it, placed through the same interface; a period-time anchor sent to the warnings.

### WP07 Sessions, their drops, and the session fetch

SessionPlan: the plan format of Q11, its checks (unknown entries; gaps and overlaps as Q44 (a) says), the spans on reference frames through the pairs, variant_span and variant_switch events with the rung names of Q07, and session_drops(): the pairs' drops (WP02's detect()) clipped to the spans that played them, under Q22 and design §6.3's three rules. In WP01's files, after WP01 has merged: curve_points() for a session's curve, at each frame the score of the variant played there (D-068), with top-rung spans as Q12 says and gaps on coarser grids (src/TimelineModel.cpp); and attribute()'s scope for a session's curve under Q28: the played variant's events in each span, the reference's and the session's own (src/TimelineQuery.cpp). SessionFetch (an adapter): the fetch of P-N through NetworkLogger's manual tracking, under Q10 and with Q41's name, in the order and pacing of Q44 (f) and (g), each file's rows copied and the logger cleared after its close, retries as Q44 (c) says and redirects as Q44 (d) says; the rows becoming playlist_request and segment_request events placed as Q44 (b) says, with the wall clock of Q03. tests/HttpFileServer.h gains a delay rule and a redirect rule (P-O). Its tests of the session's curve and scope are in its own tests/test_session_plan.cpp, so that WP01's test files keep one owner.

- **New files:** `src/SessionPlan.h`, `src/SessionPlan.cpp`, `src/SessionFetch.h`, `src/SessionFetch.cpp`, `tests/test_session_plan.cpp`, `tests/test_session_fetch.cpp`.
- **Changed (owned):** `tests/HttpFileServer.h`; src/TimelineModel.cpp (a session's curve, after WP01 has merged); src/TimelineQuery.cpp (a session's scope, after WP01 has merged).
- **Round:** 2.
- **Depends on:** WP00b, WP01, WP02, WP03, WP06.
- **Blocked by:** Q03, Q04, Q07, Q10, Q11, Q12, Q22, Q28, Q41, Q44.
- **Tests:**
  - Plan parsing and its errors; spans from segment ranges; switch events with both ranks.
  - The session's curve through curve_points(): an unscored top-rung span and a lower-rate variant's gaps; attribute() on a session's curve: the played variant's events in each span, the reference's and the session's own, and no other rung's.
  - The work order's example on a hand-built timeline: a cue-out, a switch between two numbered rungs of a hand-built five-rung ladder and a request of 2.3 s, all the test's inputs, returned in their groups with their offsets.
  - Session drops under design §6.3's rules: a clipped piece of N points or fewer; pieces of two pairs that meet at a switch; the baseline a piece reports.
  - The fetch over HttpFileServer: status 200 and the file's bytes; a delayed segment measured at least the delay (under the analyzer's 5 s limit, D-036); a 404 and a cut body recorded as failures; a redirected file as one event with its chain; the order and pacing of Q44 (f) and (g); rows tagged playback; the logger cleared after each file and uninstalled afterwards; the UTC anchor and session-relative wall clock.

### WP09b Sessions in the build, the CLI and the document

After WP09a and WP07 have merged: TimelineBuild gains the session (spans, switches, the fetch's requests); TimelineCli gains --session, --fetch and --fetch-base, and session drops (Q22); TimelineDoc, its test, the schema file and docs/TIMELINE_FORMAT.md gain the session part of design §8 (session_plan, sessions with their spans and scored flags, the network and player events' fields with their wall clock, Q03). It needs no SCTE-35 decoder: D-069's ad ladder carries plain cue tags only (testgen.md §2.3), so the composite example doesn't wait for WP05.

- **Changed (owned):** `src/TimelineBuild.h`; `src/TimelineBuild.cpp`; `src/TimelineCli.h`; `src/TimelineCli.cpp`; `tests/test_timeline_build.cpp`; `tests/test_timeline_cli.cpp`; `src/TimelineDoc.h`; `src/TimelineDoc.cpp`; `tests/test_timeline_doc.cpp`; `docs/schema/mirage-timeline-1.schema.json`; `docs/TIMELINE_FORMAT.md`.
- **Round:** 2.
- **Depends on:** WP09a, WP07.
- **Blocked by:** Q01, Q03, Q04, Q05, Q07, Q10, Q11, Q12, Q22, Q44.
- **Tests:**
  - The merge with a session on hand-built inputs; the session options and their config settings; the document's session part against the schema file and in a round trip.

### WP09c Decoded SCTE-35 messages in the build and the CLI

After WP09b and WP05 have merged: TimelineBuild gains the decoded messages (WP05's events, returned to WP04's pairing); TimelineCli gains --cue-types (Q15) and --bad-crc (Q16). The document needs no change for them, since WP08's schema holds §5.9's whole vocabulary. It follows WP09b because both change the builder and the CLI, and it is on neither the acceptance path nor the composite example's.

- **Changed (owned):** `src/TimelineBuild.h`; `src/TimelineBuild.cpp`; `src/TimelineCli.h`; `src/TimelineCli.cpp`; `tests/test_timeline_build.cpp`; `tests/test_timeline_cli.cpp`.
- **Round:** 2.
- **Depends on:** WP09b, WP05.
- **Blocked by:** Q15, Q16.
- **Tests:**
  - The merge with decoded messages on hand-built inputs: a message merged into its cue, a message-only cue paired, a bad CRC kept or dropped; the two options and their config settings.

### WP11b The composite example (test 3)

tests/test_attribution_session.cpp: the work order's example on a scripted session with a delayed segment, as design §9.3 says, in a test case of its own (its ad ladder made once in its process, P-Q). It runs the three programs as WP11a's tests do (P-T), with the test's own delay (P-S).

- **New files:** `tests/test_attribution_session.cpp`.
- **Round:** 2.
- **Depends on:** WP09b, WP10.
- **Blocked by:** Q01, Q04, Q07, Q10, Q11, Q22, Q23, Q25, Q44.
- **Tests:**
  - Test 3: the session drop lists the cue_out (scte35), a variant_switch between the planned entries (player), and a segment_request for the delayed segment with a duration at least the delay and status 200 (network).

### WP00c Wiring, round 3: the UI

Under Q36's approval, after WP00b has merged, since both edit CMakeLists.txt (P-C): the UI's files (TimelineLoad, TimelineView, TimelineMatch and their tests; TimelinePanel) as empty files with their CMake lines: the core, the UI math, the panel and src/JsonText.cpp in the player (which doesn't compile JsonText.cpp today, CMakeLists.txt:150-194), the UI math and its tests in mirage_tests, which links no ImGui (D-031).

- **New files:** the empty files of WP12 and WP13.
- **Changed (owned):** `CMakeLists.txt`.
- **Round:** 3.
- **Depends on:** WP00a, WP00b.
- **Blocked by:** Q36.
- **Tests:**
  - The macos-debug and macos-headless presets build, and ctest passes as before.

### WP12 UI loading and math, ImGui-free

TimelineLoad: the panel's loader, off the main thread with a stop token: reads a document and the ladder report it names, checks both hashes (Q06), builds the Timeline, a session's curve through WP07's curve_points() included, and hands an immutable snapshot over once. TimelineView: view state with zoom about a point, pan and fit, x to seconds on Q02's axis, the min/max envelope pyramid (a one-frame dip survives), hit testing for the lowest frame near the cursor, marker clustering; nice_step and axis_label copied from src/NetInspectorAlign.cpp:69-107, which stays unchanged (P-AJ). TimelineMatch: matching a document to the open master by SHA-256, else by URL, with the entry checks of ui.md §2.3; the playhead (the frame on screen, its pair's frame, its reference frame); the seek decision of ui.md §2.4 under Q37; the interval a click outside a drop queries (Q39 (b)). tests/test_hls_seek_behaviour.cpp turns ui.md's seek probe into a ctest case on playlists the test writes.

- **New files:** `src/TimelineLoad.h`, `src/TimelineLoad.cpp`, `src/TimelineView.h`, `src/TimelineView.cpp`, `src/TimelineMatch.h`, `src/TimelineMatch.cpp`, `tests/test_timeline_load.cpp`, `tests/test_timeline_view.cpp`, `tests/test_timeline_match.cpp`, `tests/test_hls_seek_behaviour.cpp`.
- **Round:** 3.
- **Depends on:** WP00c, WP01, WP03, WP07, WP08, WP09b.
- **Blocked by:** Q01, Q02, Q05, Q06, Q36, Q37, Q39.
- **Tests:**
  - The loader on documents and reports the test writes: matching hashes accepted, a changed report refused, a redacted report accepted by its scores digest, an unknown version refused; the snapshot handed over once; a second load cancelling the first.
  - ui.md §5.1: view math; the envelope keeps a one-frame dip; hit testing; the click's interval; matching, including the cases where ManifestParser and VMAFLadder enumerate or resolve entries differently; playhead mapping across sequences; the seek decision for continuous, restarted and after-ad frames.
  - ui.md §5.2: FFmpeg 8.1.1's seek behaviour the decision relies on, pinned on playlists the test writes.

### WP13 The timeline panel in the player

TimelinePanel (player only), as design §10 plans it: starts TimelineLoad and draws its snapshot: the curves, the event lanes, the detected drops and the playhead; a click picks the lowest frame near the cursor, shows its attribution, from the document inside a drop and through the core's query API on WP12's interval elsewhere, and seeks through s_seek_to only when the seek decision says the frame is reachable; the toggle, the View menu, the overlays popover, Esc, and the QA flags --timeline, --ui-timeline and --timeline-report. Under Q37 (b), with its plan approved, the render loop's request to play to a frame and pause; under Q37 (c), with its plan approved, another way to position the hls demuxer; under Q40 (b), ManifestParser's cue defects fixed and its test changed; under Q40 (c), the GL ruler retired for the panel's playlist lane.

- **New files:** `src/TimelinePanel.h`, `src/TimelinePanel.cpp`.
- **Changed (owned):** `src/main.cpp`; `src/PlayerUI.h`; `src/PlayerUI.cpp`; src/DebugHUD.h and src/DebugHUD.cpp (only under Q40 (c)); src/ManifestParser.h, src/ManifestParser.cpp and tests/test_manifest_parser.cpp (only under Q40 (b)); src/Demuxer.h and src/Demuxer.cpp (only under Q37 (c), with its plan approved).
- **Round:** 3.
- **Depends on:** WP12, WP01, WP02, WP08.
- **Blocked by:** Q36, Q37, Q38, Q39, Q40.
- **Tests:**
  - Through WP12's unit tests; the scripted QA run of ui.md §5.3, outside ctest; the manual checks of ui.md §5.4.

### WP14 The evaluation harness: reproduction from one command

eval/timeline/: run_all.sh <build dir> <out> runs the acceptance cases with MIRAGE_KEEP_MEDIA, builds and queries their documents, and runs the composite example against a local delaying server with the example's 2.3 s; check.py compares every answer with the generator's layout, excluding wall-clock fields and checking measured durations as at least the injected delay; each record keeps cmd.txt and environment.txt with the commit (P-AF).

- **New files:** `eval/timeline/README.md`, `eval/timeline/run_all.sh`, `eval/timeline/check.py`, `eval/timeline/slow_server.py`, `eval/timeline/tests/test_check.py`.
- **Round:** 3.
- **Depends on:** WP09a, WP09b, WP10, WP11a, WP11b.
- **Blocked by:** Q02, Q04, Q07, Q10, Q22, Q23, Q25, Q31.
- **Tests:**
  - check.py against fake documents, with the standard library's unittest, as eval/ladder/tests/ does.

### WP15b The differences' wording and the gap report's correction

Once the answers to Q41 and Q42 are in: design §11's differences in the wording those answers settle, in the list of differences of docs/PHASE3_DECISIONS_PENDING.md, from which WP15c's PR description takes them; under Q42 (b), a dated correction line in docs/VMAF_GAP_REPORT.md for its factual error (design §11). Each gives the work order's requirements in its own words, quoting no passage of it (D-013, D-060), and neither is edited to match the implementation; differences are listed.

- **Changed (owned):** docs/PHASE3_DECISIONS_PENDING.md (after WP15a has merged); docs/VMAF_GAP_REPORT.md (only under Q42 (b)).
- **Round:** 3.
- **Depends on:** WP15a.
- **Blocked by:** Q41, Q42.
- **Tests:**
  - Before each commit: the cross-reference check and the quote check of P-AK, the checker's output kept as the record.

### WP15c The closing documents and the PR description

Last, once every package that is built has merged: those it depends on and, through them, every other package of design §14 but WP16 and WP17, which the lead adds to its dependencies when the owner's answers start them. The Phase 3 rows of docs/AI_CONTRIBUTIONS.md; docs/PAPER_METHOD_NOTES.md once there is something measured to state; dated lines in docs/DECISIONS.md's pending entries where the implementation found more (D-060); docs/PHASE3_STATUS.md completed item by item with the output that shows it; and Phase 3's PR description, with the differences in WP15b's wording and the acceptance outputs pasted in. Every document and the PR description give the work order's requirements in their own words, quoting no passage of it (D-013, D-060), and none is edited to match the implementation; differences are listed.

- **Changed (owned):** `docs/PHASE3_STATUS.md`; docs/DECISIONS.md (after WP15a has merged); `docs/AI_CONTRIBUTIONS.md`; `docs/PAPER_METHOD_NOTES.md`.
- **Round:** 3.
- **Depends on:** WP09c, WP13, WP14, WP15b.
- **Blocked by:** none.
- **Tests:**
  - Before each commit, and before the PR description or any other text about Phase 3 is posted: the cross-reference check and the quote check of P-AK, the checker's output kept as the record.

### WP16 Conditional: what the scorer records, its extra pair, and a decode pass

Only when the owner picks an option that changes the scorer or adds a decode pass, after the owner's ruling under D-007 or approval of its written plan where the option needs one; frames, pairing and scores unchanged in every case. In DecodeCtx: under Q09 (a), decode_error_flags, AV_FRAME_FLAG_CORRUPT and avcodec_send_packet's errors; under Q08 (b), each frame's segment and key flag; under Q01 (C), the per-variant events; under Q10 (e), the scoring reads, through a per-input recorder in SegmentWatch's hooks, labelled scoring-paced. Under Q12 (d), VMAFLadder scores the top rung against itself as one more pair. The ladder report carries what is recorded as sparse per-pair lists; after WP03 has merged, LadderReportRead reads the new keys, optional for older reports; after WP09c has merged, TimelineBuild turns them into events (decode_error; segment_request with origin scoring; the segment map and keyframes). Under Q09 (b) instead: a decode pass in an adapter of its own, src/TimelineDecode.{h,cpp}, wired by the lead when the package starts (P-C), its errors made decode_error events through TimelineBuild. Under Q08 (b) or Q01 (C) the part for that option moves into round 1, as design §14 says.

- **New files:** src/TimelineDecode.h and src/TimelineDecode.cpp (only under Q09 (b)).
- **Changed (owned):** `src/VMAFAnalyzer.h`; `src/VMAFAnalyzer.cpp`; `src/VMAFReport.h`; `src/VMAFReport.cpp`; `tests/test_vmaf_report.cpp`; `tests/test_vmaf_analyzer.cpp`; src/VMAFSegments.h and tests/test_vmaf_segments.cpp (only under Q10 (e)); src/VMAFLadder.h, src/VMAFLadder.cpp and tests/test_vmaf_ladder.cpp (only under Q12 (d)); src/LadderReportRead.cpp and tests/test_ladder_report_read.cpp (after WP03 has merged); src/TimelineBuild.cpp and tests/test_timeline_build.cpp (after WP09c has merged).
- **Round:** 3.
- **Depends on:** WP03, WP06, WP09c.
- **Blocked by:** Q01, Q08, Q09, Q10, Q12.
- **Tests:**
  - A damaged segment, made as sources.md's corrupt-segment run made one, records its errors, and a clean one records none; every score, pairing and pooled value unchanged on the committed records' inputs; the report's new keys, with tests/test_vmaf_report.cpp:909's exact key count updated; the events they become, on hand-built reports.

### WP17 Later: the player's session log

Only under Q13 (b), with its D-007 plan approved: logging-only hooks in switch_video, read_loop, video_decode_loop and on_present that write Q11's format with origin 'player': the rung, the demuxer's take-over point, the frames dropped, the requests. Under Q13 (c) instead: a reader of the player's existing switch-trace text into Q11's format, with no change to the player. Its files are wired by the lead when the package starts (P-C).

- **New files:** `src/SessionLog.h`, `src/SessionLog.cpp`.
- **Changed (owned):** src/main.cpp (only under Q13 (b), after WP13 has merged; the two never run in parallel); src/Demuxer.cpp (only under Q13 (b)); src/MediaSession.cpp (only under Q13 (b)).
- **Round:** 3.
- **Depends on:** WP07, WP13.
- **Blocked by:** Q11, Q13.
- **Tests:**
  - The log, or under Q13 (c) the trace of a scripted --qa-switch run, read back through SessionPlan's reader; a scripted --qa-switch run outside ctest.

### Order and the critical path

1. Now: WP15a, the pending-decisions document, the UI plan and the P-entries, which wait for no answer.
2. Round 1, once the owner has answered its questions and the lead has recorded the answers (P-AH): WP00a first and alone, since it owns `CMakeLists.txt` and writes the shared declarations.
3. Then in parallel: WP01, WP02, WP03, WP04, WP06 and WP10.
4. WP08 once WP01 and WP03 have merged.
5. WP09a once WP01, WP02, WP03, WP04, WP06 and WP08 have merged.
6. WP11a, §3.2's two acceptance tests, once WP09a and WP10 have merged.
7. Round 2, once its questions are answered: WP00b once WP00a and WP01 have merged (under Q41 (b) the rename first); WP05 once WP04 has; WP07 once WP00b, WP01, WP02, WP03 and WP06 have; WP09b once WP09a and WP07 have; WP11b, the composite example, once WP09b and WP10 have; WP09c once WP09b and WP05 have.
8. Round 3, once its questions are answered: WP00c under Q36, once WP00b has merged; WP12 once WP00c, WP01, WP03, WP07, WP08 and WP09b have; WP13 after WP12; WP17 only after WP13 (both own `src/main.cpp`) and WP07; WP16 whenever the owner's ruling or approved plan allows it, after WP03, WP06 and WP09c; WP14 once WP11a and WP11b have merged; WP15b once Q41 and Q42 are answered; WP15c last, once every package that is built has merged.

The critical path to the two acceptance tests: WP00a, WP01, WP02, WP03, WP04, WP06, WP08, WP09a, WP10 and WP11a. Each of them depends only on packages of that list, and every question that blocks one of them is in round 1 (both checked by `design/tools/render.py`); D-069's ladders carry plain cue tags only (testgen.md §2.3), so the path needs neither the SCTE-35 decoder nor sessions. The composite example's path, to WP11b, adds WP00b, WP07, WP09b and WP11b, all of round 2; the SCTE-35 decoder (WP05) and the decoded messages (WP09c) are on neither path (checked), and nor are WP15a, WP00c, WP12, WP13, WP14, WP15b, WP15c, WP16 and WP17.

The path assumes the agent's recommendations for Q01 and Q08, under which the scorer is unchanged. Under Q08 (b) the analyzer's recording of each frame's segment and key flag, WP16's part for it, moves into round 1 ahead of WP06, which then reads the map from the report, and the acceptance tests wait for the owner's ruling under D-007 or approved plan; under Q01 (C) the same holds for the per-variant events, which WP04 and WP06 would then read from the report; under Q01 (A) WP08 writes into the ladder report, whose writer and tests it then owns. The lead would re-plan those packages before round 1 starts.

Packages of round 0, which wait for no answer and can start now: WP15a. The questions that block three packages or more: Q04 (WP00a, WP04, WP08, WP09a, WP11a, WP05, WP07, WP09b, WP11b, WP14); Q01 (WP00a, WP08, WP09a, WP11a, WP09b, WP11b, WP12, WP16); Q22 (WP02, WP08, WP09a, WP11a, WP07, WP09b, WP11b, WP14); Q02 (WP00a, WP01, WP08, WP09a, WP11a, WP12, WP14); Q07 (WP00a, WP08, WP09a, WP07, WP09b, WP11b, WP14); Q43 (WP00a, WP01, WP02, WP03, WP04, WP06, WP09a); Q10 (WP00b, WP07, WP09b, WP11b, WP14, WP16); Q11 (WP00b, WP07, WP09b, WP11b, WP17); Q23 (WP02, WP08, WP11a, WP11b, WP14); Q25 (WP02, WP08, WP11a, WP11b, WP14); Q44 (WP00b, WP05, WP07, WP09b, WP11b); Q03 (WP00a, WP08, WP07, WP09b); Q05 (WP00a, WP08, WP09b, WP12); Q06 (WP03, WP08, WP09a, WP12); Q08 (WP00a, WP06, WP09a, WP16); Q12 (WP00b, WP07, WP09b, WP16); Q28 (WP01, WP08, WP09a, WP07); Q29 (WP01, WP08, WP09a, WP11a); Q31 (WP00a, WP09a, WP11a, WP14); Q20 (WP00a, WP04, WP09a); Q24 (WP02, WP08, WP09a); Q26 (WP02, WP08, WP09a); Q27 (WP02, WP08, WP09a); Q30 (WP01, WP08, WP09a); Q36 (WP00c, WP12, WP13); Q41 (WP00b, WP07, WP15b).

---

## 15. Risks

- **R1.** The byte-position join was measured on MPEG-TS from one generator only (sources.md §2; design-library runs map-by-pos-*; design-evaluation runs 03 and 04). fMP4 with EXT-X-MAP, ID3-timestamped audio, open GOPs, decoders that output fewer frames than packets, and D-011's ladders: MEASUREMENT PENDING. The PTS check (P-G) makes a failure visible; what the variant then loses is Q43 (c).
- **R2.** The demux pass reads every segment again: for a remote ladder a second download (time: MEASUREMENT PENDING), and a server could serve other bytes than it served the scorer; VOD only is in scope, and the playlists' SHA-256s and the PTS check catch what changes the frames.
- **R3.** NetworkLogger's status, request and first-byte times come from FFmpeg 8.1.1's TRACE and DEBUG log wording (sources.md §3.1); another FFmpeg version can change it. WP07's test pins it, so a change fails a test rather than a field.
- **R4.** FFmpeg's log callback is process-global (sources.md §9: one thread's replacement redirected another's lines). The fetch must run with nothing else logging through FFmpeg; the timeline build scores nothing and installs NetworkLogger only around the fetch (P-N).
- **R5.** NetworkLogger's manual tracking over HTTP in a headless process was not run by any survey: the prototype fetch parsed the same TRACE line with its own code (design-evaluation run 06), and the sources survey's NetworkLogger probe used its hooks (sources.md §3.1). MEASUREMENT PENDING until WP07's test.
- **R6.** The fetch is not a player: one file at a time, unpaced under Q44 (f) 1, on localhost in the tests, with an injected delay. Its times say what one sequential client got, and the paper must say that the 2.3 s is injected (P-S).
- **R7.** Detection defaults chosen on synthetic content could mislead on real video; W and N stay required until the D-011 measurement of Q26.
- **R8.** Top-rung mode hides a defect present in the top rung too (D-002's limit; testgen.md §5), and mezzanine mode can't score a stitched ladder against a one-file mezzanine (76.92% of frames failed, testgen.md §3.5).
- **R9.** ctest runs each Catch2 test case in its own process (review-adversarial/runs/ctest-registration.txt), so a ladder is generated once in each test case that uses it: seven generations per ctest run under Q34 (d), with P-Q's single acceptance test case (design §9.5). The prototype took 16.941 s per ladder unoptimized and 2.022 to 2.313 s optimized (testgen.md §3.4); the ported generator's time and the tests' time in a Debug build are MEASUREMENT PENDING.
- **R10.** Generated bytes may differ on another machine or x264 build (only one machine was used, testgen.md §3.3); the tests compare behaviour, not hashes.
- **R11.** Seconds, not events, are off where the clocks of Q02 (c) and (d) part: after one simulated missing frame (d) was 0.040 s early, after an ad re-timed at 24 to 60 fps 0.083 to 1.167 s off, and after one EXTINF simulated a frame too long (c) was 0.040 s late (design/runs/seconds-check.txt). Design §4.2's check warned in each of these cases and on none of the four ladders checked; without a segment map a playlist reference's PTS runs are listed as facts only, and how often D-011's references trip the check is MEASUREMENT PENDING. A reference frame missing at the start of a playlist discontinuity sequence puts both clocks a frame early within its segment, where neither test of the check warns, and (d) alone from the next segment on, where (1) warns but can't tell it from an EXTINF a frame too long (review-3/runs/seconds-check-sequence-start.txt).
- **R12.** The player can't seek to the frames of an ad whose timestamps restart at 0.040 s (ui.md §2.4); an ad that restarts at the main content's origin, as D-069's does, was not probed (MEASUREMENT PENDING). Q37 decides what the panel does there.
- **R13.** The JSON reader's memory and time on a full ladder report (27969051 bytes of text for the advanced sample, schema.md §3.3): MEASUREMENT PENDING (P-Y).
- **R14.** Published SCTE-35 examples are often invalid (18 of 79 have a bad CRC or don't decode, scte35.md §5), so the policies of Q14 to Q17 change what attributions show on real streams.
- **R15.** Parallel packages share declarations: the types of WP00a and WP00b may need changes once packages start, which P-AG routes through the lead; a late change can stall the packages that use it.
- **R16.** Whether the player's PTS for a frame of a variant opened through the master equal mirage_vmaf's for the media playlist opened alone, the 33-bit MPEG-TS wrap included: MEASUREMENT PENDING (sources.md §12). The panel's playhead depends on it.
- **R17.** Without libx264 the tests fall back to MPEG-2 (Q33 (c)): other content, recorded in the output, with a smaller drop (62.022 against 96.923 to 97.671, testgen.md §3.1). The pinned parameters detected it on the degraded ladder and on the ad from the main content's origin (design-evaluation run 05); the ad from 1 s and the continuous ad with MPEG-2 are MEASUREMENT PENDING.
- **R18.** Segment-range plans assume the variants' segments line up in time; a ladder whose rungs are cut differently gives plans with gaps or overlaps, refused under Q44 (a). Whether D-011's ladders line up: MEASUREMENT PENDING.
- **R19.** SCTE-35 inside segments (stream type 0x86, or ID3 as in the one public VOD stream found, scte35.md §8) and DASH's EventStream are interfaces only (D-070): their cue input is declared (design §5.5) and tested with a PTS-anchored section, but no reader is built, so a stream that signals only there gives no SCTE-35 events in Phase 3.
- **R20.** The scores digest depends on the eval scripts' redaction rules (P-Z); a change to those rules breaks the match with committed copies until the digest's field list follows it.
- **R21.** Q23 (a)'s warm-up: no point is low before W/2 points that weren't low exist, so a degradation in a curve's first W/2 points is never found (at W = 50, the whole first segment of a D-069 ladder, testgen.md §3.1). The document records the warm-up, and WP02's test states the miss.
- **R22.** How NetworkLogger rows a redirected fetch is from reading src/NetworkLogger.cpp:595-688, not from a run; Q44 (d) joins the rows of one fetched file into one event. If the logger rows a redirect otherwise, WP07's redirect test fails rather than a field.

---

## 16. Open issues

- **MEASUREMENT PENDING**: the demux pass's time on real ladders, local and over HTTP; the byte-position join on fMP4 with EXT-X-MAP, ID3-timestamped audio, open GOPs and D-011's ladders; the document's size on real ladders; the JSON reader's memory and time on the largest committed report; the ported generator's time and the acceptance tests' time in a Debug build (§9.5); the spread of the fetch's times between runs; the detection parameters' behaviour on D-011's ladders (§6.3); how often D-011's references trip the axis check (R11); whether the player's PTS equal `mirage_vmaf`'s for the same frame (R16); decode errors from H.264 and HEVC decoders, since only MPEG-2's were observed (sources.md §12); whether FFmpeg's demuxers deliver an SCTE-35 or timed-ID3 stream through `mirage_vmaf`'s input settings (scte35.md §10, item 6); the continuous-timestamp ad ladder of Q34 (c), not yet generated or scored; the ad from 1 s and the continuous ad with MPEG-2 (Q33); a seek into an ad that restarts at the main content's origin (Q37).
- **Not run by any survey or by this design**: `NetworkLogger`'s manual tracking over HTTP in a headless process (R5), a redirect included (Q44 (d), R22); a paced fetch (Q44 (f) 2 and 3); the player itself (ui.md's findings F1, F2 and F6 are by reading).
- **The scte35 survey's open issues stand**: SCTE 35-1 2023r2 and SCTE 35-2 2026 unread (scte.org returned HTTP 403); the PDF's table text from the agent's own extractor; one public VOD stream with SCTE-35, generated on request by a vendor demo.
- **Fixtures made with FFmpeg's hls muxer** write `TARGETDURATION:0` and a last EXTINF shorter than its frames (sources.md §12): WP06's tests that use one should check their EXTINF against their frames; testgen's generator writes its playlists itself (testgen.md §2.3).
- **Q02, Q25 and Q29 together decide the acceptance tests' assertions on offsets in seconds**; the tests assert offsets in frames (P-R), and the seconds are checked in WP01's unit tests once Q02 is answered.
- **The survey directory is committed without** its generated media, binaries, copies of FFmpeg's source, third-party downloads and extracts, and the SCTE-35 messages it collected (Q21); this design cites none of them for a fact (Conventions).

---

## Appendix A. Runs this design cites

**This design's runs** (`<out>/design/runs/`, each made by `design/tools/run.sh`):

| Record | Command (from the record) | What it shows |
|---|---|---|
| `detection-semantics-ad.txt` | in `<out>/testgen`: `python3 -I -B <out>/design/tools/detection_semantics.py reports/ad-x264.json reports/ad-x264-a.layout.json` | on testgen's ad ladder report, with the prototype detector at T = 6, W = 50, N = 12: where `v180`'s drop starts when the first pair of each libvmaf context is treated like any other, left out of the curve, or left out of the baseline only, and whether the cue-out's frame lies in it; a session's drops for two scripted plans, detected on the session's curve with its own baseline, and as the pairs' drops clipped to the played spans |
| `axis-check.txt` | in `<out>/testgen`: `python3 -I -B <out>/design/tools/axis_check.py` on the reports and layouts of `ad-x264`, `ad-mpeg2`, `ad-x264-origin1` and `degraded-x264`, with `--drop 40` | revision 1's record: the first version's check of Q02 (d)'s seconds (labelled "round-1 wording" in the record) against revision 1's check of steps, with the layouts as the reference's segment map; (c)'s seconds against (d)'s; the same on the first report with reference frame 40 removed (simulated) |
| `seconds-check.txt` | in `<out>/testgen`: `python3 -I -B <out>/design/tools/seconds_check.py` on the same four reports and layouts, on the mezzanine-mode reports `degraded-x264-mezzanine` and `ad-x264-mezzanine` without a map, with `--drop 40 180`, `--retime 75 124 25 24 30 50 60` and `--extinf 1 1` | revision 2's check of §4.2, (1) and (2), with and without the segment map, beside revision 1's check of steps and the second review's comparison at segment starts; the largest difference of (d) from (c); the same on simulated copies of the first report: reference frame 40 or 180 removed, the ad's frames 75 to 124 at 25, 24, 30, 50 and 60 fps with their EXTINF following, and segment 1's EXTINF one frame longer |
| `networklogger-users.txt` | in `<repo>`: `grep -rl -e NetworkLogger src tools tests CMakeLists.txt`, sorted and counted | the 21 files that name `NetworkLogger`, which Q41 (b)'s rename would edit |
| `log-callback-grep.txt` | in `<repo>`: `grep -rn -e av_log_set_callback -e av_log_set_level -e avformat_network_init src tools tests` | which code installs FFmpeg's log callback, sets its level and initializes its network |
| `question-lengths.txt` | in `<out>`: the second review's `review-2/tools/question_lengths.py` on `design/owner_questions.json` | every question's, option's and summary's count of sentences against L4's ranges |
| `package-check.txt` | in `<out>`: the second review's `review-2/tools/package_check.py` on `design/work_packages.json` | every file two packages name, and whether one of them depends on the other |
| `privacy-check.txt` | in `<out>`: the second review's `review-2/tools/privacy_check.py` on `design.md` and every file under `design/` but itself and `quote-check.txt`, which are made after it | counts of local paths, e-mail addresses and the local account's identity words, without the values |
| `repo-state.txt` | in `<repo>`: `git --no-optional-locks log -1` and `status --short --branch` | `<repo>`'s commit and status, read without changing them |
| `render-check.txt` | in `<out>`: `render.py` writing into a pipe that `cmp` compares with `design.md` | that `design.md` is byte-identical to the render of its sources, and that the renderer's consistency checks passed |
| `quote-check.txt` | in `<out>`: the quote checker on `design.md` and every file under `design/` but itself, against the work order's translation kept outside the repository | each file's count of passages shared with the translation, by lines, without the shared words |

`design/tools/run.sh <name> <dir> <command…>` writes the record (working directory, exact command, start and end in UTC, full output, exit status), with local paths replaced by `<out>`, `<repo>` and `<scratch>`. The three Python tools of `design/tools/` that read testgen's reports and layouts in `<out>/testgen/reports/` (`axis_check.py`, `detection_semantics.py`, `seconds_check.py`) write nothing but their records; `axis_check.py` is kept as revision 1 left it, since its record and the second review's `rate-change-check` were made with it.

**The first review's runs** (`<out>/review-adversarial/runs/`), cited by name: `axis-d-check` (the first version's check applied to testgen's reports), `ctest-registration` (how ctest runs the Catch2 test cases), `switch-t-sweep` (a switch alone under Q22 (b), as T changes). **The second review's runs** (`<out>/review-2/runs/`), cited by name: `rate-change-check` (revision 1's check on the ad's frames re-timed at other rates, simulated). **The third review's runs** (`<out>/review-3/runs/`), cited by name: `seconds-check-sequence-start` (revision 2's check with the first frame of each of the ad ladder's playlist discontinuity sequences removed, simulated), `vector-status` (the CRC and decode status of the collected messages, from the scte35 survey's record).

**The two designs' runs**, cited by name:

| Where | Runs | What they show |
|---|---|---|
| `<out>/design-library/runs/` | `map-by-pos-ad-v360`, `-ad-v180`, `-ad-origin1-v360`, `-degraded-v270` (from the `logprobe-*` records of the same directory) | packets to segments by byte position, 200 of 200 on four rungs; by PTS against EXTINF, 145 and 193 of 200 |
| | `axis-ad`, `axis-ad-origin1`, `axis-degraded`, `axis-ad-float`, `axis-degraded-float` | the four axes on testgen's ladders, in exact ticks and in doubles |
| `<out>/design-evaluation/runs/` | `03_segmap`, `04_join_check` | the demux pass on 15 playlists; the join with every frame's PTS checked |
| | `05_detection_sweep` | three detectors over W, T and N on 21 pair curves against the generator's ground truth |
| | `06_session_fetch` | a scripted session's 11 files fetched from a local server that answers one segment 2.3 s late |
| | `07_attribute_seconds_as_doubles`, `08_attribute_ticks`, `09_axes`, `10_attribute_final`, `11_doc_sizes`, `12_repro_check` | the prototype's attributions with doubles and with ticks; the boundary frames on three axes; the end-to-end results; the documents' sizes; the reproduction of runs 07, 08 and 10 |

**The surveys' runs and records**: `<out>/runs/` (sources.md, Appendix A), `<out>/scte35_evidence/runs/` (scte35.md), `<out>/testgen/logs/` (testgen.md), `<out>/schema-survey/runs/` (schema.md), `<out>/experiments/hls-seek-probe/run-output.txt` (ui.md).

---

## Appendix B. API sketch

In the repository's style: namespaces of free functions and structs, errors as strings, no exceptions. Names are proposals.

```cpp
// src/TimelineModel.h — the timeline's data (core: C++20 and libavutil only).
namespace timeline {
enum class Source : std::uint8_t { Vmaf, Network, Playlist, Scte35, Player, Decoder };
inline constexpr std::size_t kSourceCount = 6;
std::string_view source_name(Source);                 // "vmaf", "network", ...

struct MediaKey {                                     // the reference frame an event sits at
    std::int32_t sequence = 0;                        // the report's reference sequence (D-057)
    std::optional<std::int64_t> pts;                  // ticks of the reference's time base
};
struct Value;                                         // null, bool, int64, double, string, list, object
using Payload = std::vector<std::pair<std::string, Value>>;
struct Event { /* §4.3 */ };

struct Variant {
    int index = -1, pair = -1;                        // pair -1: the reference
    std::vector<std::size_t> entries;                 // master entries naming it
    std::string url, playlist_sha256, playlist_location;
    std::int64_t bandwidth = 0;  std::optional<std::string> resolution, codecs;
    int rank_from_top = 0, rank_from_bottom = 0;      // Q07, Q43 (e)
    std::string segment_map = "ok";                   // or "refused: <why>" (Q43 (c))
};
struct Curve {
    int variant = -1;                                 // a pair's distorted variant
    std::vector<std::int64_t> ref_index;  std::vector<double> vmaf;
    std::vector<std::int64_t> context_first;          // reference frames starting a libvmaf context
};
struct CurveRef { enum class Kind : std::uint8_t { Pair, Session } kind = Kind::Pair; int index = 0; };
// Declared by WP00b in round 2, once Q11 and Q12 are answered, with CurveRef's Session kind:
struct Span { std::int64_t ref_index = 0, ref_end = 0; int variant = -1; bool scored = true; };
struct Session { std::string id, name, wallclock_anchor_utc; std::vector<Span> spans; };

struct Timeline {
    Axis axis;                                        // src/TimelineAxis.h
    std::vector<Variant> variants;
    std::vector<Curve>   curves;                      // one per pair
    std::vector<Event>   events;                      // canonical order (P-I)
    std::vector<Session> sessions;                    // added by WP00b in round 2
    std::vector<std::string> warnings;
    // Indices of the events meeting [a, b), margins included (P-J, Q29).
    std::vector<std::size_t> overlapping(std::int64_t a, std::int64_t b) const;
};
// Declared by WP00a; defined in src/TimelineModel.cpp, a pair's by WP01, a session's,
// derived from the pairs' (§4.4), by WP07.
Curve curve_points(const Timeline&, CurveRef);
}

// src/TimelineAxis.h — media keys and seconds (Q02).
namespace timeline {
struct Axis {
    std::string  kind;                                // Q02's option
    std::string  clock;                               // axis.seconds: "playlist-time" or "frame-grid"
    AVRational   time_base{}, frame_duration{};       // the reference's
    std::int64_t frames = 0;                          // reference frames
    std::vector<MediaKey> keys;                       // per reference frame
    std::string  checked;                             // "segment-map" or "report-only" (§4.2)
    std::vector<std::string> warnings;                // the check's runs, (1) and (2) (§4.2)
    double seconds(std::int64_t ref_index) const;     // for output only
    std::int64_t frame_at_or_after(AVRational seconds) const;   // first in index order (§4.2)
};
// The reference's segment map and playlist (EXTINF, discontinuities) when it has them.
Axis build_axis(const LadderScores&, const VariantMedia* reference,
                const hls::MediaTimeline* reference_playlist, std::string_view kind);
}

// src/LadderReportRead.h — a ladder report, read (Q43 (b)).
namespace timeline {
struct PairScores {
    std::size_t index = 0;  std::vector<std::size_t> entries;
    std::string ref_url, dis_url;  AVRational ref_time_base{}, dis_time_base{}, ref_rate{}, dis_rate{};
    std::vector<std::int64_t> ref_index, dis_index, ref_pts, dis_pts;   // pts in ticks
    std::vector<double> vmaf;
    std::vector<std::int64_t> context_first;          // pair indices
    std::vector<Failure> failures;                    // alignment.failures
    std::vector<Sequence> ref_sequences, dis_sequences;
};
struct LadderScores { /* settings, master entries, reference, pairs */ };
LadderScores read_ladder_report(std::string_view text, std::string& err);
std::string scores_text(const LadderScores&);         // the digest's canonical text (Q06, P-Z)
}

// src/PlaylistEvents.h — a media playlist's events, the cue interface, the pairing (WP04).
namespace timeline {
struct PlaylistTag { int segment = -1; int line = 0; std::string name, value; };

// D-070's interfaces: a cue message from any carrier, with the anchor its carrier gives (§5.5).
enum class CueCarrier : std::uint8_t { PlaylistTag, DashEventStream, TsSection, Id3Priv };
struct PlaylistAnchor { int variant = -1; std::int64_t segment = 0; int line = 0; };
struct MediaPtsAnchor { int variant = -1; std::int64_t pts = 0; AVRational time_base{};
                        std::optional<std::int64_t> segment; };     // on the carrying variant's clock
struct PeriodAnchor   { std::string period; std::int64_t presentation_time = 0, timescale = 1; };
struct CueInput {
    CueCarrier carrier = CueCarrier::PlaylistTag;
    std::variant<PlaylistAnchor, MediaPtsAnchor, PeriodAnchor> anchor;
    std::string form;                                 // "OATCLS", "DATERANGE SCTE35-OUT", "CUE-OUT-CONT SCTE35", ...
    std::vector<std::uint8_t> bytes;                  // the splice_info_section
};

struct PlaylistEvents {
    hls::MediaTimeline timeline;                      // src/HlsAlign.h
    std::vector<PlaylistTag> tags;
    std::vector<Event> cues;                          // cue_out, cue_out_cont, cue_in of the plain tags
    std::vector<CueInput> messages;                   // every embedded message, for Scte35Events
    std::string error;                                // a live playlist, say (Q43 (a))
};
PlaylistEvents parse_playlist_events(const std::string& text, const std::string& base_url, int variant);
std::vector<Event> playlist_events(const PlaylistEvents&, int variant, const CueOptions&);  // Q18-Q20
// Every cue, from plain tags and from decoded messages, paired into ad_break (Q17).
std::vector<Event> pair_breaks(const std::vector<Event>& cues, const CueOptions&);
}

// src/Scte35.h, src/Scte35Events.h — the decoder and the message events (WP05).
namespace scte35 {
struct SpliceInfo { /* scte35.md §3's fields, raw and with pts_adjustment applied; crc_valid */ };
std::optional<std::vector<std::uint8_t>> from_base64(std::string_view);   // av_base64_decode
std::optional<std::vector<std::uint8_t>> from_hex(std::string_view);      // "0xFC..."
SpliceInfo decode(std::span<const std::uint8_t> section);                  // never reads past it
}
namespace timeline {
struct MessageEvents { std::vector<Event> cues, splice_points, others; std::vector<std::string> warnings; };
// Decodes, classes (Q15, Q16), merges into plain cues (Q44 (e)) and places every anchor kind (§5.1).
MessageEvents scte35_events(const std::vector<CueInput>&, std::vector<Event>& plain_cues,
                            const std::vector<VariantMedia>&, const LadderScores&, const CueOptions&);
}

// src/SessionPlan.h — sessions and their fetched requests (WP07; types declared by WP00b).
namespace timeline {
struct PlanSpan { std::size_t entry = 0; std::int64_t first_seq = 0, last_seq = 0; };
struct SessionPlan { std::string name; std::vector<PlanSpan> spans; };
struct RequestRow { /* one NetworkLogger row: URL, status, status text, times, bytes, error */ };
struct Request { std::string file; std::vector<RequestRow> chain; };      // one fetched file (Q44 (d))
SessionPlan parse_session_plan(std::string_view json, std::string& err);   // Q11, Q44 (a)
std::vector<Drop> session_drops(const Timeline&, int session, const DetectOptions&);   // Q22, §6.3
}

// src/TimelineBuild.h — the merge.
namespace timeline {
struct SegmentFrames { std::int64_t seq = 0; std::int64_t first_index = 0, frames = 0, bytes = 0; };
struct VariantMedia {                                 // from the demux pass (or the report, Q08 (b))
    std::vector<SegmentFrames> segments;
    std::vector<std::int64_t> keyframes, corrupt;     // frame indices of the input
    std::vector<std::int64_t> pts;                    // per frame, ticks
    std::string refused;                              // why the map was refused (Q43 (c))
};
struct BuildInputs {
    LadderScores scores;
    std::vector<PlaylistEvents> playlists;            // parallel to the variants
    std::vector<VariantMedia>   media;
    std::optional<SessionPlan>  plan;  std::vector<Request> requests;     // round 2, added by WP09b
    std::string axis_kind;  CueOptions cues;
};
Timeline build(const BuildInputs&);                   // warnings in Timeline::warnings
}

// src/TimelineQuery.h, src/TimelineDetect.h — §6.1. Written by WP01 and WP02, not by WP00a,
// since their options encode Q23 to Q29.
namespace timeline {
struct QueryOptions { AVRational margin_before{0, 1}, margin_after{0, 1}; /* scope, Q28 */ };
struct DetectOptions {
    double threshold = 0;                             // Q26: 6 only if the owner says so
    std::int64_t window = 0, min_points = 0;          // Q23, Q24: required until Q26 says otherwise
};
struct Drop { Interval interval; std::int64_t first_point = 0, last_point = 0;
              double baseline = 0, min = 0, mean = 0; };
std::vector<Drop> detect(const Curve&, const DetectOptions&);
Attribution attribute(const Timeline&, CurveRef, Interval, const QueryOptions&);
}

// src/TimelineDoc.h — §8.
namespace timeline {
struct DocInfo { /* report path and hashes, provenance copied, config file, plan, playlists, parameters */ };
std::string to_json(const Timeline&, const std::vector<Attribution>&, const DocInfo&);
bool from_json(std::string_view text, Timeline&, std::vector<Attribution>&, DocInfo&, std::string& err);
}

// Adapters (libavformat): src/TimelineMedia.h, src/SessionFetch.h
namespace timeline {
struct PlaylistText { std::string url, location, sha256, text, error; };   // vmafladder::read_master
PlaylistText read_playlist(const std::string& url, std::stop_token);
VariantMedia read_media(const std::string& playlist_url, const PairScores* check, bool reference,
                        std::stop_token, std::string& err);              // the demux pass (P-F, P-G)
// The session's files in play order (P-N), with the pacing and playlist fetches of Q44 (f), (g).
std::vector<Request> fetch_session(const std::vector<std::string>& urls, std::stop_token);
}

// UI math (ImGui-free; mirage, mirage_tests): src/TimelineLoad.h (WP12).
namespace timeline {
struct Snapshot { Timeline timeline; std::vector<Attribution> attributions; DocInfo info; };
class Loader {                                        // the panel's loading off the main thread
public:
    void start(std::string document_path);            // cancels a load in progress
    std::shared_ptr<const Snapshot> take();           // once, when ready; null before
    std::string error() const;                        // a hash mismatch, an unknown version, ...
};
}
```
