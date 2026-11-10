# Phase 3: decisions pending for the owner

What is open for the owner on `vmaf/phase-3`, §3 of the VMAF work order (the unified event timeline), after the owner's decisions of 2026-10-30 that started it (`docs/DECISIONS.md` D-066 to D-070). Nothing of Phase 3 is implemented. The survey and the design behind these questions are summarized in `docs/PHASE3_SURVEY.md`, and their records, the design (`eval/phase3-survey/design.md`) and its three reviews among them, are in `eval/phase3-survey/`. The decisions are the owner's (hard rule 3 of the work order); every recommendation below is labeled as the agent's, and none is a decision. Every number comes from a committed record or a cited source, named next to it; where there is none, it says MEASUREMENT PENDING. The state of each item of §3 is in `docs/PHASE3_STATUS.md`.

## Open

44 questions, Q01 to Q44, in three rounds: 27 in round 1, the answers the work packages on the path to §3.2's two acceptance tests need before they start; 9 in round 2, those of sessions, the session fetch, the decoded SCTE-35 messages and the composite example; 8 in round 3, those of the UI, the conditional and later packages, and the differences to report. A question's round is the earliest round of the packages it blocks, and a round's packages start once all of its questions are answered (`eval/phase3-survey/design.md` §12, §14). Q26, Q39, Q43 and Q44 are answered item by item, an item's options numbered: "Q43 (c) 2". The questions marked D-007 (Q01, Q08, Q09, Q10, Q13, Q36, Q37 and Q41) have an answer that is, or includes, an approval or a ruling under D-007. "Blocks" names the work packages that wait for the answer; the packages are listed at the end. After the questions come the differences from the work order to report, and the agent's proposed process choices, to be recorded from P-082 on.

The questions are the design's (`eval/phase3-survey/design/owner_questions.json`), rendered by `eval/phase3-survey/tools/render_pending.py` with the design's paths written as the repository has them and nothing else changed. They are as the design's revision 3 left them, after the third review's seven minor issues (`eval/phase3-survey/review-3/review.md`; `eval/phase3-survey/design/revision-3.md`). The owner's answers are marked here as each round's come in. Round 1's 27 questions were answered on 2026-11-02, every one as the agent recommends (D-071 to D-081); each is marked below. Round 2's 9 were answered on 2026-11-06, every one as the agent recommends, with Q47 (D-103 to D-107); each is marked below. Round 3 is open. Building round 1's first packages raised three more questions, Q45 to Q47, after round 3 below: the owner answered Q45 and Q46 on 2026-11-04 as the agent recommends (D-086, D-087), and Q47 joins round 2. Building WP08, WP08b and WP09a raised Q48 to Q57, which the owner answered on 2026-11-05 and 2026-11-06, each as the agent recommends (D-092, D-099, D-100), and WP08c two more, Q58 and Q59, which the owner answered on 2026-11-06 as the agent recommends (D-101). Building round 2's first packages, WP00b, WP05 and WP07, raised eight more, Q60 to Q67, answered on 2026-11-10, each as the agent recommends (D-108, D-109): WP09b puts in the answers to Q60 to Q62, and WP05b, a follow-up of WP05's, those to Q63 to Q67, as WP08b and WP08c put in the answers on WP08.

### Round 1: the path to §3.2's two acceptance tests (27 questions)

| # | Question | Agent's recommendation | Blocks | D-007 |
|---|---|---|---|---|
| Q01 | Where the timeline and its attributions are written | (B) | WP00a, WP08, WP09a, WP11a, WP09b, WP11b, WP12, WP16 | yes |
| Q02 | The single time base: where events sit, how they are compared, and the seconds of offsets | (c), falling back to (d) where the reference has no segment map, with `eval/phase3-survey/design.md` §4.2's check; (d) alone a close second | WP00a, WP01, WP08, WP09a, WP11a, WP12, WP14 |  |
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
| Q22 | Which curves automatic detection runs on | (c), with `eval/phase3-survey/design.md` §6.3's three rules | WP02, WP08, WP09a, WP11a, WP07, WP09b, WP11b, WP14 |  |
| Q23 | The trailing baseline's statistic and warm-up | (a) | WP02, WP08, WP11a, WP11b, WP14 |  |
| Q24 | Units of W and N, and points without a score | (a) | WP02, WP08, WP09a |  |
| Q25 | Frames that start a libvmaf context (D-037) | (a) | WP02, WP08, WP11a, WP11b, WP14 |  |
| Q26 | Detection's defaults, and when the timeline subcommand detects | (a) 1; (b) 2 | WP02, WP08, WP09a |  |
| Q27 | What drop_magnitude measures | (c) | WP02, WP08, WP09a |  |
| Q28 | Which variants' events an attribution lists | (a) | WP01, WP08, WP09a, WP07 |  |
| Q29 | Which events overlap an interval | (c) | WP01, WP08, WP09a, WP11a |  |
| Q30 | Ordering and ranking of the events an attribution lists | (a) | WP01, WP08, WP09a |  |
| Q31 | The CLI subcommands: shape, names and options | (a), with `eval/phase3-survey/design.md` §7's option set | WP00a, WP09a, WP11a, WP14 |  |
| Q32 | The JSON reader | (a), moving to (b) if a full report's load needs it | WP00a, WP03 |  |
| Q33 | The encoder of the acceptance tests | (c) | WP10, WP11a |  |
| Q34 | The ad's timestamps in acceptance test 2 | (d) | WP10, WP11a |  |
| Q35 | The rest of the synthetic content | (a) | WP10, WP11a |  |
| Q43 | The ladder timeline's defaults: eight choices that change its output | (a) 1; (b) 1; (c) 1; (d) 1; (e) 1; (f) 1; (g) 1; (h) 1 | WP00a, WP01, WP02, WP03, WP04, WP06, WP09a |  |

#### Q01. Where the timeline and its attributions are written

*Round 1. **D-007**: the answer is, or includes, an approval or a ruling under D-007.*

**Summary.** Q01 decides whether the timeline's events and attributions go into the ladder report or into a document of their own; the agent recommends a separate timeline document that names its ladder report by hash and copies no scores.

**Question.** §7 places `timeline` and `attributions` in mirage_vmaf's one JSON output, while D-068 scores a ladder once and attributes many sessions against it. Where are the timeline's events and the attributions written? Option (C) needs the analyzer to record events while scoring, which under D-007 is the owner's to rule on, or to approve as a written plan.

**Options.**

- **(A) In the ladder report.** Top-level `timeline` and `attributions` next to `pairs`, as §7 lists them, with no JSON reader needed. But each new session means scoring again (the full advanced ladder took 1882.86 s, `eval/phase3-survey/schema.md` §3.3) or rewriting the scored file, and per-frame lines are already 99.91% of a full report (same section).
- **(B) A separate timeline document, naming the ladder report by hash.** One ladder report and at most one session per document; scores are read from the report, not copied, so the scoring run's output and every committed comparison stay as they are. The prototype's documents for 200-frame ladders were 8577 to 14470 bytes (`eval/phase3-survey/design-evaluation/runs/11_doc_sizes.txt`); it needs a JSON reader, a hash rule that survives redaction (Q06) and a reported difference from §7.
- **(B') As (B), with the session's per-frame curve copied.** The document reads without its report, but it grows with the frames (36000 entries for one 60 fps pair of Apple's advanced sample, `eval/phase3-survey/schema.md` §3.3) and repeats scores the report holds.
- **(C) Per-variant events in the report, the session in a document.** The per-variant events come from the scoring run itself, but the report changes (a key under `pairs[]` breaks the exact key count at `tests/test_vmaf_report.cpp:909`) and the analyzer must record them, a D-007 matter. That recording would be WP16's and would move into round 1, ahead of the packages that read it (`eval/phase3-survey/design.md` §14).

**Agent's recommendation.** (B).

**Answered** on 2026-11-02: as the agent recommends (`docs/DECISIONS.md` D-072; D-071).

**Why.** It keeps D-068's one scoring for many sessions, leaves the committed records and their checks valid, and keeps per-frame data in the report; its costs (a JSON reader, a hash rule, a reported difference) are each one bounded package.

**Blocks.** WP00a, WP08, WP09a, WP11a, WP09b, WP11b, WP12, WP16.

#### Q02. The single time base: where events sit, how they are compared, and the seconds of offsets

*Round 1.*

**Summary.** Q02 decides on what clock events are placed and compared and in which seconds offsets are printed; the agent recommends placing and comparing on reference frames and printing the reference's playlist time, falling back to the frame index times the frame duration where the reference has no segment map, with the two compared at every frame wherever both exist and the frame-index seconds everywhere as a close second.

**Question.** §3.1 asks for one time base keyed by media PTS, the reference's timeline, but where an ad's timestamps restart at the main content's origin raw PTS repeat: 50 PTS values each name two reference frames in testgen's ad ladder (`eval/phase3-survey/design-evaluation/runs/09_axes.txt`). Under (b) to (d) events are placed and compared on reference frames and keep their media key (the report's sequence and the PTS), so the events an interval lists are the same and only the printed seconds differ, apart from durations given in seconds (`eval/phase3-survey/design.md` §4.2). Wherever the reference has a segment map, `eval/phase3-survey/design.md` §4.2's check computes both (c)'s and (d)'s seconds at every frame and warns where they differ by more than half a frame, so the choice between them is which to print there, and what to print where there is no map. On what clock are events placed and compared, and in which seconds are offsets printed?

**Options.**

- **(a) Raw reference PTS for everything.** The work order's wording, nothing derived. On the ad ladder it lists main_000.ts and main_001.ts for the ad's drop (`eval/phase3-survey/design-library/runs/axis-ad.txt`), and offsets step back by 2.960 s at the ad's first frame (`eval/phase3-survey/design-evaluation/runs/09_axes.txt`).
- **(b) Reference frames; seconds of the PTS, the report's sequences laid end to end.** Needs only the ladder report. A report sequence breaks only at a backward step or a forward jump of more than 10 s (`src/VMAFAlign.h:114`), so smaller jumps stay in the seconds: the 2 s ad break spans 5.000 s (`eval/phase3-survey/design-evaluation/runs/09_axes.txt`).
- **(c) Reference frames; seconds of the reference's playlist time.** Each frame at its segment's EXTINF sum plus its PTS offset in the segment: equal to (d) on the four ladders checked, and right through a missing frame or a change of rate, after which (d) was 0.040 s and 0.083 to 1.167 s off (simulated, `eval/phase3-survey/design/runs/seconds-check.txt`), except within a segment that lost its playlist discontinuity sequence's first frame, where both were a frame early (`eval/phase3-survey/review-3/runs/seconds-check-sequence-start.txt`). It is off instead where an EXTINF doesn't match its segment's frames, which RFC 8216 §4.3.2.1 advises against (0.040 s after one EXTINF simulated a frame too long, same run). It needs the reference's segment map, so a reference without one (a mezzanine, a refused map) falls back to (d).
- **(d) Reference frames; seconds = frame index times the frame duration.** Defined from the report for every reference, a mezzanine included, and equal to (c) on the four ladders checked (`eval/phase3-survey/design/runs/seconds-check.txt`). It is off after a missing frame or a change of rate, by the amounts under (c), and right where an EXTINF is off; `eval/phase3-survey/design.md` §4.2's check, which needs the segment map, warns at the frames where it leaves (c) by more than half a frame.

**Agent's recommendation.** (c), falling back to (d) where the reference has no segment map, with `eval/phase3-survey/design.md` §4.2's check; (d) alone a close second.

**Answered** on 2026-11-02: as the agent recommends (`docs/DECISIONS.md` D-073; D-071).

**Why.** Frames make the events an interval lists exact and independent of the seconds (`eval/phase3-survey/design-evaluation/runs/07_attribute_seconds_as_doubles.txt` and `08_attribute_ticks.txt` showed doubles listing boundary events inconsistently). Since `eval/phase3-survey/design.md` §4.2's check computes both wherever the reference has a segment map, the choice is which seconds to print where they differ: (c) stays right after a missing frame or a change of rate (from the next segment on if the frame missing is a sequence's first), and RFC 8216 §4.3.2.3 puts changes of encoding parameters at discontinuities such as an ad's, while (c) is otherwise off only where an EXTINF departs from its frames; (d) gives every reference and both reference modes one definition, the better choice if the owner expects rounded or integer EXTINF.

**Blocks.** WP00a, WP01, WP08, WP09a, WP11a, WP12, WP14.

#### Q03. Which events have a wall clock, and on what clock

*Round 1.*

**Summary.** Q03 decides what an event's wall clock holds; the agent recommends a wall clock only for observed events (the session fetch now, the player's log later), as microseconds of one steady clock from a UTC anchor sampled at the session's start, and none for offline events.

**Question.** §3.1 records a wall clock beside media time without saying which clock. Offline facts (scores, playlist tags, decoder facts) were observed at no particular time, while a session fetch's requests were. What does an event's wall clock hold?

**Options.**

- **(a) The observation time of observed events only.** When a fetch's request was made, later also the player's log; null for offline events; no common origin stated.
- **(b) (a), as microseconds from one UTC anchor per session.** One steady clock from the session's start plus a UTC time sampled then (`eval/phase3-survey/sources.md` §11, choice 7 (c)); a segment's EXT-X-PROGRAM-DATE-TIME stays in its payload. Monotonic and comparable within a session; offline events, and so every event of an acceptance test, have none (`eval/phase3-survey/design.md` §11, item 5).
- **(c) A date from EXT-X-PROGRAM-DATE-TIME for offline events too.** Every event under a PDT gets a date, but a PDT is a program date, not an observation, and many playlists have none.

**Agent's recommendation.** (b).

**Answered** on 2026-11-02: as the agent recommends (`docs/DECISIONS.md` D-073; D-071).

**Why.** It records only what was observed, on one monotonic clock; NetworkLogger already keeps steady microseconds and maps them to wall time (`src/NetworkLogger.h:189-192`).

**Blocks.** WP00a, WP08, WP07, WP09b.

#### Q04. The timeline document's layout and event vocabulary, as its first schema version

*Round 1.*

**Summary.** Q04 decides the layout and event vocabulary of the timeline document's first schema version; the agent recommends `eval/phase3-survey/design.md` §8 and §5.9's layout, which keeps §7's names where §7 has the field and adds spans, groups by source, offsets, parameters and the report's provenance.

**Question.** `eval/phase3-survey/design.md` §8 proposes the document's layout and §5.9 each source's event types, with §7's field names where §7 has them. Which becomes the document's schema version 1?

**Options.**

- **(a) As `eval/phase3-survey/design.md` §8 and §5.9 propose.** §7's `timeline[]` and `attributions[]` names, extended with reference-frame positions, spans, ids and seconds; `overlapping_events` an array of groups in §3.1's source order, each event with offsets in seconds and frames; the parameters and the ladder report's provenance copied (formerly P-AC, whose alternative was the report's hashes only); scores not copied. It files the aligner's unpaired frames under `vmaf`, not under the decoder as §3.1 groups dropped frames, a difference to report (`eval/phase3-survey/design.md` §11, item 3).
- **(b) §7's literal shapes only.** `pts` and `payload` only, `overlapping_events` a flat list, the rest in payloads: closest to §7's text, but spans, groups and offsets then rest on payload conventions the schema doesn't state.
- **(c) (a), declared provisional until Phase 5.** The same layout, its names to be revisited when §7's report schema is decided; readers must expect a version 2.

**Agent's recommendation.** (a).

**Answered** on 2026-11-02: as the agent recommends (`docs/DECISIONS.md` D-072; D-071).

**Why.** It keeps §7's names and adds what the example and the reproduction need; a prototype of this shape answered the example (`eval/phase3-survey/design-evaluation/runs/10_attribute_final.txt`).

**Blocks.** WP00a, WP04, WP08, WP09a, WP11a, WP05, WP07, WP09b, WP11b, WP14.

#### Q05. Versioning and provenance of the timeline document

*Round 1.*

**Summary.** Q05 decides what versioning and provenance the new document carries; the agent recommends schema_version and a JSON Schema file now, leaving tool_version and git_commit to Phase 5, with the commit recorded by the eval tooling as P-006 has it.

**Question.** §7 (Phase 5) wants `schema_version`, a JSON Schema file, `tool_version` and `git_commit`; the build has no version (`CMakeLists.txt:2`), and P-006 has the eval tooling record the commit when it runs. What does the new document carry?

**Options.**

- **(a) `schema_version` and a schema file now; the rest with Phase 5.** An integer version from 1, as six eval scripts version their outputs (`eval/phase3-survey/schema.md` §1.7); the schema under `docs/schema/`; the commit recorded by the eval tooling (P-006).
- **(b) (a), plus `tool_version` and `git_commit` now.** The commit from a configure-time `git rev-parse` (a CMake step of WP00a), which can go stale between configure and run, the reason P-006 records it at run time; changes P-006.
- **(c) `schema_version` only, the schema file with Phase 5.** Less work now; the paper can't cite a schema for the new document until then.
- **(d) Neither until Phase 5.** Readers tell layouts apart by their keys, as for the 12 committed mirage_vmaf report layouts (`eval/phase3-survey/schema.md` §1.8).

**Agent's recommendation.** (a).

**Answered** on 2026-11-02: as the agent recommends (`docs/DECISIONS.md` D-072; D-071).

**Why.** A new format costs nothing to version from its first day, its schema can be cited, and P-006's confirmed rule for the commit stays.

**Blocks.** WP00a, WP08, WP09b, WP12.

#### Q06. How a timeline document names its ladder report

*Round 1.*

**Summary.** Q06 decides how a document identifies the ladder report it was built from; the agent recommends both the SHA-256 of the report's bytes and the SHA-256 of a canonical digest of what attribution uses, which survives the redaction of committed copies.

**Question.** Committed copies of ladder reports are redacted (local paths replaced), so the hash of the bytes mirage_vmaf wrote doesn't match a committed copy (`eval/phase3-survey/schema.md` §1.8). How does a document name its report?

**Options.**

- **(a) SHA-256 of the report's bytes.** Exact identity of the file; fails against a redacted, committed copy.
- **(b) SHA-256 of a canonical digest of what attribution uses.** Each pair's index and entries, each frame's indices, PTS and score, and the settings that hold no path. Redaction leaves it unchanged; a canonical form to define and test (P-Z).
- **(c) Both.** The file it was built from, and a redaction-proof check against the committed record.

**Agent's recommendation.** (c).

**Answered** on 2026-11-02: as the agent recommends (`docs/DECISIONS.md` D-072; D-071).

**Why.** Each covers what the other can't, and both are cheap to compute when the report is read.

**Blocks.** WP03, WP08, WP09a, WP12.

#### Q07. How rungs are named in outputs

*Round 1.*

**Summary.** Q07 decides how outputs name a rung; the agent recommends the master entry index and URI as its identity, with its BANDWIDTH ranks from the top and from the bottom printed as labels.

**Question.** The work order's example names a switch between two numbered rungs, but the ladder report has no rung numbers: entries are numbered in the master's order (`src/VMAFLadder.h:88`). How does Phase 3 name a rung? Which BANDWIDTH ranks a playlist that several entries name is Q43 (e).

**Options.**

- **(a) The master entry index and URI only.** Exact as the master gives them; no number to read at a glance.
- **(b) Plus an ascending BANDWIDTH rank (1 = lowest).** A switch from 5 to 3 then reads as down; the ranks change when a rung is added.
- **(c) Plus a descending rank (1 = highest).** A switch from 5 to 3 then reads as up; the ranks change when a rung is added.
- **(d) Entry and URI as identity, both ranks as labels.** Unambiguous whatever convention a reader assumes; longer text (`eval/phase3-survey/design-evaluation/runs/10_attribute_final.txt` prints both).

**Agent's recommendation.** (d).

**Answered** on 2026-11-02: as the agent recommends (`docs/DECISIONS.md` D-075; D-071).

**Why.** The identity never changes, and printing both ranks imposes no reading of the example that the work order doesn't state.

**Blocks.** WP00a, WP08, WP09a, WP07, WP09b, WP11b, WP14.

#### Q08. Where each scored frame's segment, and the keyframes, come from

*Round 1. **D-007**: the answer is, or includes, an approval or a ruling under D-007.*

**Summary.** Q08 decides how each scored frame is mapped to its segment and where keyframes come from; the agent recommends a demux-only pass in the timeline build, which leaves the scorer and its reports unchanged.

**Question.** Placing playlist, SCTE-35 and network events needs each scored frame's segment, and §3.1 asks for keyframes; today no frame is mapped to a segment (`eval/phase3-survey/sources.md` §2). Who records them? Option (b) changes the analyzer's reading of its inputs, which needs the owner's ruling under D-007, or a written plan the owner approves.

**Options.**

- **(a) A demux-only pass in the timeline build.** Each scored playlist read again without decoding, packets placed by byte position and checked against the report's PTS; keyframes and corrupt flags from packet flags. Exact on every playlist probed (200 of 200 frames on each of 15 playlists, `eval/phase3-survey/design-evaluation/runs/03_segmap.txt` and `04_join_check.txt`); scorer and report unchanged; every segment read again (time on real ladders: MEASUREMENT PENDING).
- **(b) The analyzer records them while scoring.** No second read, exact by construction; but the analyzer's reading and the report change (a key under `pairs[]`, counted at `tests/test_vmaf_report.cpp:909`), older reports lack it, and it is a D-007 and D-068 matter. The acceptance tests would then wait for that change, WP16's, and its ruling or plan (`eval/phase3-survey/design.md` §14).
- **(c) PTS against EXTINF sums, no extra read.** FFmpeg's own rule ([hls.c#L1916-L1945](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/hls.c#L1916-L1945)); right for 145 of 200 packets on the ad ladders and 193 of 200 on the degraded one (`eval/phase3-survey/design-library/runs/map-by-pos-*.txt`), and it can't place a PTS two segments share.

**Agent's recommendation.** (a).

**Answered** on 2026-11-02: as the agent recommends (`docs/DECISIONS.md` D-074; D-071).

**Why.** It was exact on every playlist probed, leaves §2's scorer and its committed comparisons untouched, and its PTS check makes a failure visible; what a failure costs is Q43 (c).

**Blocks.** WP00a, WP06, WP09a, WP16.

#### Q17. An ad break's end when several sources give a duration

*Round 1.*

**Summary.** Q17 decides where an ad break ends when its tags and messages give several durations; the agent recommends ending it at its cue-in when there is one, else at its start plus the first of a declared order of durations, keeping every value in the payload.

**Question.** A break's duration can come from EXT-X-CUE-OUT's value, DATERANGE's DURATION, PLANNED-DURATION or END-DATE, break_duration, segmentation_duration, or the cue-in's position. Which places the break's end?

**Options.**

- **(a) The cue-in, else a declared order.** Every value kept; without a cue-in, the tag's own duration (CUE-OUT's value, DATERANGE's DURATION or END-DATE), then PLANNED-DURATION, break_duration and segmentation_duration. On testgen's ad ladder the cue-in gave 2.000 s, equal to EXT-X-CUE-OUT:2.000 (`eval/phase3-survey/design-evaluation/runs/10_attribute_final.txt`).
- **(b) The tag's duration first.** Google Video Stitcher's rule (`eval/phase3-survey/scte35.md` §4.4).
- **(c) The message's break_duration first.** The standard's value; absent from tag-only cues.

**Agent's recommendation.** (a).

**Answered** on 2026-11-02: as the agent recommends (`docs/DECISIONS.md` D-076; D-071).

**Why.** The cue-in is where the media changes back, and keeping every value shows any disagreement.

**Blocks.** WP04.

#### Q18. SCTE-35 signalling beyond D-070's list

*Round 1.*

**Summary.** Q18 decides whether Phase 3 reads SCTE-35 tags that D-070 doesn't list; the agent recommends D-070's list only, the other tags kept as raw playlist tag events.

**Question.** Sources also use #EXT-X-SCTE35 (the standard's own HLS tag), #EXT-X-SPLICEPOINT-SCTE35 and Adobe's #EXT-X-CUE (`eval/phase3-survey/scte35.md` §2.6 and §9). Does Phase 3 read them?

**Options.**

- **(a) D-070's list only.** The others kept as raw `tag` events and listed as such; a difference to report.
- **(b) Add EXT-X-SCTE35 and EXT-X-SPLICEPOINT-SCTE35.** Their base64 messages through the same decoder; a little more parsing and tests.
- **(c) Add all three.** EXT-X-CUE carries attributes, no message.

**Agent's recommendation.** (a).

**Answered** on 2026-11-02: as the agent recommends (`docs/DECISIONS.md` D-076; D-071).

**Why.** D-070 decided the list; the others are visible as tags and can be added later at little cost.

**Blocks.** WP04, WP05.

#### Q19. A bare EXT-X-DISCONTINUITY as an ad boundary

*Round 1.*

**Summary.** Q19 decides whether a discontinuity without cue tags is taken as an ad boundary; the agent recommends never inferring one, so that it stays a discontinuity event.

**Question.** AWS MediaTailor's stitched playlists drop the cue tags and keep only EXT-X-DISCONTINUITY (`eval/phase3-survey/testgen.md` §5). Is a discontinuity without cue tags an ad boundary on the timeline?

**Options.**

- **(a) Never inferred.** A `discontinuity` event only.
- **(b) Inferred.** An `ad_break` from pairs of discontinuities; helps with stitched streams, but it is an inference the paper would have to defend.
- **(c) Inference as an option, off by default.** Available for stitched streams; one more option.

**Agent's recommendation.** (a).

**Answered** on 2026-11-02: as the agent recommends (`docs/DECISIONS.md` D-076; D-071).

**Why.** The evaluation mustn't claim SCTE-35 where none was signalled, and the attribution still lists the discontinuity at the drop (`eval/phase3-survey/design-evaluation/runs/10_attribute_final.txt`).

**Blocks.** WP04, WP09a.

#### Q20. DASH period boundaries

*Round 1.*

**Summary.** Q20 decides what Phase 3 does with DASH; the agent recommends an interface only: a reserved period event type and `eval/phase3-survey/design.md` §5.5's cue interface, with no DASH reader.

**Question.** §3.1 lists DASH period boundaries, but mirage_vmaf scores HLS ladders only (`src/VMAFLadder.h:15-22`), and D-070 leaves DASH's EventStream as an interface. What does Phase 3 do with DASH?

**Options.**

- **(a) An interface only.** A `period` type reserved and the cue interface of `eval/phase3-survey/design.md` §5.5 declared, with no reader; reported as a difference.
- **(b) Parse an MPD's periods and EventStreams.** Events for a DASH presentation of a scored HLS ladder's content, read by WP04 with an XML parser that mirage_vmaf would then link (WP00a); the join between the two presentations is unmeasured.
- **(c) DASH ladder scoring.** Outside §2's scope.

**Agent's recommendation.** (a).

**Answered** on 2026-11-02: as the agent recommends (`docs/DECISIONS.md` D-076; D-071).

**Why.** Without DASH scoring, DASH events would reach no curve.

**Blocks.** WP00a, WP04, WP09a.

#### Q22. Which curves automatic detection runs on

*Round 1.*

**Summary.** Q22 decides which curves automatic detection runs on and how a session's drops are found; the agent recommends each pair's curve, with a session's drops taken as its pairs' drops clipped to the spans that played them, under `eval/phase3-survey/design.md` §6.3's three rules.

**Question.** §3.2 compares the score with the variant's own trailing baseline, and D-068 defines a session's curve as the score of the variant played at each frame. Which curves does detection run on, and how are a session's drops found? `eval/phase3-survey/design.md` §6.3 gives (c)'s three rules, with their alternatives: for pieces of N points or fewer, for drops that meet at a switch, and for the baseline a piece reports.

**Options.**

- **(a) Each scored pair's curve only.** Both acceptance tests are of this kind; a session adds events but no drops.
- **(b) The pairs' curves, and the session's curve with its own trailing baseline.** A switch can become a drop depending on the rung gap against T: on the switch-down plan, a drop at T = 3, 4 and 5 and none at T = 5.5, 6, 6.5, 7 and 8 (`eval/phase3-survey/review-adversarial/runs/switch-t-sweep.txt`). The composite drop's baseline was 97.681, v270's level (`eval/phase3-survey/design/runs/detection-semantics-ad.txt`).
- **(c) The pairs' curves, and session drops as the pairs' drops clipped to the spans that played each variant.** Each session point is compared with its own variant's baseline, so a switch alone is never a drop, and there is no second detector. The composite drop: frames 75 to 124, v180's baseline 91.692, magnitude 69.455 (`eval/phase3-survey/design/runs/detection-semantics-ad.txt`).
- **(d) The session's curve only.** Misses drops in variants no session plays, the two acceptance tests' among them.

**Agent's recommendation.** (c), with `eval/phase3-survey/design.md` §6.3's three rules.

**Answered** on 2026-11-02: as the agent recommends (`docs/DECISIONS.md` D-077; D-071).

**Why.** It applies the work order's per-variant baseline to sessions too and still finds the composite drop, while under (b) whether a switch is a drop changes between T = 5 and T = 5.5 on one plan, a fragility the paper would have to explain.

**Blocks.** WP02, WP08, WP09a, WP11a, WP07, WP09b, WP11b, WP14.

#### Q23. The trailing baseline's statistic and warm-up

*Round 1.*

**Summary.** Q23 decides how the trailing baseline is computed and from when it is used; the agent recommends the median of the last W points that weren't low, used once W/2 such points exist, which leaves the first W/2 points of every curve unexamined.

**Question.** The work order doesn't define the sliding baseline. Which statistic over the last W points, and from when is it used?

**Options.**

- **(a) Median of the last W points that weren't low, used once W/2 such points exist.** Exact on the four acceptance curves at every point of the measured grid, T from 3 to 40 and N from 1 to 24 (`eval/phase3-survey/design.md` §2; `eval/phase3-survey/design-evaluation/runs/05_detection_sweep.txt`), and a drop never lowers its own baseline. Its blind start: the first W/2 points of every curve can't be low, at W = 50 the whole first segment of a D-069 ladder (25 frames, `eval/phase3-survey/testgen.md` §3.1).
- **(b) Mean of the same points.** Also exact on the acceptance curves on the same grid (`eval/phase3-survey/design-evaluation/runs/05_detection_sweep.txt`); less robust to outliers; the same blind start.
- **(c) Median of the last W points, low or not.** Follows a long drop down: the drops found were shorter than the truth at W = 10 to 50 and exact only at W = 100 (`eval/phase3-survey/design-evaluation/runs/05_detection_sweep.txt`).
- **(d) One median per curve.** No window and no blind start; blind to slow changes of level.

**Agent's recommendation.** (a).

**Answered** on 2026-11-02: as the agent recommends (`docs/DECISIONS.md` D-077; D-071).

**Why.** It was exact across the measured grid, is robust to outliers, and a drop doesn't pull its baseline down; its blind start is written in the document's detection record and tested (WP02).

**Blocks.** WP02, WP08, WP11a, WP11b, WP14.

#### Q24. Units of W and N, and points without a score

*Round 1.*

**Summary.** Q24 decides whether W and N count curve points, reference frames or seconds, and what happens at gaps; the agent recommends counting points of the curve and skipping gaps.

**Question.** A 30 fps variant's curve has a point at every other reference frame of a 60 fps reference (`eval/phase3-survey/schema.md` §3.1), and a session's curve has no points on unscored spans. In what are W and N counted, and what happens at gaps?

**Options.**

- **(a) Points of the curve, gaps skipped.** Nothing invented at gaps; the same N covers more time on a coarser curve.
- **(b) Reference frames, gaps holding the last score.** The same N is the same time on every curve; scores are invented at gaps.
- **(c) Seconds of the axis, gaps skipped.** Uniform in time; N is no longer a count of frames.

**Agent's recommendation.** (a).

**Answered** on 2026-11-02: as the agent recommends (`docs/DECISIONS.md` D-077; D-071).

**Why.** The work order counts frames, and nothing is invented at the gaps.

**Blocks.** WP02, WP08, WP09a.

#### Q25. Frames that start a libvmaf context (D-037)

*Round 1.*

**Summary.** Q25 decides whether detection treats the first pair of each libvmaf context, which scores low, specially; the agent recommends no special case, which keeps the ad's drop starting on its cue-out's frame.

**Question.** The first pair of every libvmaf context scores low (D-037): frame 75, where testgen's ad starts a new context, is the control rung v270's only frame more than 6 points below its median (`eval/phase3-survey/testgen.md` §3.6, item 7). How does detection treat these frames?

**Options.**

- **(a) No special case.** N of 1 or more passes over their single-frame dips, and the ad's drop starts at frame 75 with the cue-out's frame inside (`eval/phase3-survey/design/runs/detection-semantics-ad.txt`).
- **(b) Left out of the curve.** As D-037's second minimum leaves them out; the ad's drop then starts at frame 76 and the cue-out's frame 75 falls before it, so the strict overlap no longer lists the cue (same run).
- **(c) Left out of the baseline only.** They can be low but never enter the baseline; the same drop as (a) on this ladder (same run).

**Agent's recommendation.** (a).

**Answered** on 2026-11-02: as the agent recommends (`docs/DECISIONS.md` D-077; D-071).

**Why.** (b) moves an ad's drop off its cue by one frame, N of 1 or more already ignores single-frame dips, and (c) adds a rule that changed nothing measured.

**Blocks.** WP02, WP08, WP11a, WP11b, WP14.

#### Q26. Detection's defaults, and when the timeline subcommand detects

*Round 1.*

**Summary.** Q26 decides the shipped defaults of T, W and N and when the timeline subcommand runs detection; the agent recommends T = 6 by default with W and N required, and detection only when W and N are given, so that a build without them writes events only and says so.

**Question.** §3.2 suggests a threshold of 6 VMAF points; W and N have no suggested value, nothing measured on real video sets them, and the acceptance tests pin their own (P-R). Item (a) is the defaults; item (b) is when `timeline` detects, since with W and N required an events-only build would otherwise fail (`eval/phase3-survey/design.md` §7).

**Items**, each answered on its own:

- **(a) The defaults of T, W and N.**
  - **1. T = 6 by default; W and N required until measured.** The work order's value for T; W and N must be given.
  - **2. All three set now from the synthetic sweep.** Synthetic content, one source picture, 200 frames per pair: `eval/phase3-survey/design-evaluation/runs/05_detection_sweep.txt` shows a wide region where the acceptance cases pass, not good defaults for real video.
  - **3. All three required.** No default until a measurement.
  - *Agent's recommendation:* 1. *Why:* What would set W and N is detection over D-011's ladders with injected degradations (`eval/phase3-survey/design.md` §6.3); the longest run of low points in testgen's busy control was 3 (`eval/phase3-survey/design-evaluation/runs/05_detection_sweep.txt`).
- **(b) When `timeline` detects.**
  - **1. Always, W and N required.** `timeline` without `--window` and `--min-points` fails, even when only events are wanted.
  - **2. Only when W and N are given.** Otherwise the document has `detection: null` and no attributions, and a line on stderr says why.
  - **3. A `--detect on|off` option, default on.** Explicit; W and N required when on; one more option.
  - *Agent's recommendation:* 2. *Why:* Events-only builds work without a new option, and both the document and stderr say that no detection ran.

**Agent's recommendation.** (a) 1; (b) 2.

**Answered** on 2026-11-02: as the agent recommends (`docs/DECISIONS.md` D-077; D-071).

**Why.** T has the work order's value and W and N have none, while the tests don't depend on defaults; and events-only builds then need no new option.

**Blocks.** WP02, WP08, WP09a.

#### Q27. What drop_magnitude measures

*Round 1.*

**Summary.** Q27 decides what an attribution's drop_magnitude measures; the agent recommends both depths: the baseline minus the drop's minimum as drop_magnitude, and the baseline minus its mean as mean_drop.

**Question.** §7 names an attribution's `drop_magnitude` without defining it. What does it measure?

**Options.**

- **(a) Baseline minus the drop's minimum.** The worst frame's depth.
- **(b) Baseline minus the drop's mean.** The drop's average depth.
- **(c) Both.** (a) as `drop_magnitude`, (b) as `mean_drop`.

**Agent's recommendation.** (c).

**Answered** on 2026-11-02: as the agent recommends (`docs/DECISIONS.md` D-077; D-071).

**Why.** Both are cheap and answer different questions; §7's name keeps the first. Under Q23 (a) the baseline is the same at every point of a drop (`eval/phase3-survey/design.md` §6.3).

**Blocks.** WP02, WP08, WP09a.

#### Q28. Which variants' events an attribution lists

*Round 1.*

**Summary.** Q28 decides which variants' events an attribution of a curve lists; the agent recommends the curve's own inputs: for a pair, its distorted variant's and the reference's events; for a session, the played variant's in each span, the reference's and the session's own.

**Question.** Every rung's playlist can carry the same tags (all three rungs of testgen's ad ladder carry the cue tags), and a query without scope lists each copy (`eval/phase3-survey/design-evaluation/runs/09_axes.txt`). Which variants' events does an attribution of a curve list?

**Options.**

- **(a) The curve's own inputs.** For a pair, its distorted variant's and the reference's; for a session, the played variant's in each span, the reference's, and the session's own. The prototype of `eval/phase3-survey/design-evaluation/runs/10_attribute_final.txt` did this for pairs; for its session it listed only the played variant's and the session's own, so (a) adds the reference's copies there (`eval/phase3-survey/design.md` §6.4).
- **(b) Every scored variant's events.** Shows that a cue is in every rung; three copies of each tag on a three-rung ladder.
- **(c) The distorted input's only.** Leaves out the reference's segments and tags.

**Agent's recommendation.** (a).

**Answered** on 2026-11-02: as the agent recommends (`docs/DECISIONS.md` D-078; D-071).

**Why.** It keeps another rung's events out of a curve's answer and keeps the reference, whose timeline every curve is on.

**Blocks.** WP01, WP08, WP09a, WP07.

#### Q29. Which events overlap an interval

*Round 1.*

**Summary.** Q29 decides what overlapping an interval means for points and spans; the agent recommends strict half-open overlap on reference frames, with optional margins before and after that are zero by default.

**Question.** §3.2 returns the events that overlap a drop's interval in time. Events are points (a tag, a switch) or spans (a segment, a break, a request). Which events overlap?

**Options.**

- **(a) Strict half-open overlap on reference frames.** A point at frame f meets [a, b) when a ≤ f < b, a span [s, e) when s < b and e > a. On the ad ladder the cue-in at the drop's end isn't listed, but the ad_break span is, with its end offset of +2.000 s (`eval/phase3-survey/design-evaluation/runs/10_attribute_final.txt`).
- **(b) (a) plus points exactly at the end, flagged at_end.** Lists the cue-in at the drop's end, and with it every boundary event there: the next segment, its keyframe, its discontinuity.
- **(c) (a) plus optional margins before and after, zero by default.** At its default it is (a); a margin finds causes that precede their effect, a slow fetch or the corrupt flag one frame before the ad (Q43 (h)); one more pair of options.
- **(d) Closed intervals.** Touching ends count, at both ends.

**Agent's recommendation.** (c).

**Answered** on 2026-11-02: as the agent recommends (`docs/DECISIONS.md` D-078; D-071).

**Why.** By default it is overlap made exact, and the margins meet the one need for more without changing default outputs.

**Blocks.** WP01, WP08, WP09a, WP11a.

#### Q30. Ordering and ranking of the events an attribution lists

*Round 1.*

**Summary.** Q30 decides whether an attribution ranks its events by likely relevance; the agent recommends no ranking: groups in §3.1's source order and events by offset within a group.

**Question.** §3.2 asks for the events by source, with offsets, and says nothing of their order. Should an attribution rank its events by likely relevance?

**Options.**

- **(a) No ranking.** Groups in §3.1's source order (vmaf, network, playlist, scte35, player, decoder), events by offset within a group.
- **(b) Ranked by a heuristic.** Distance from the drop's start, or overlap fraction: a causal heuristic needing an evaluation of its own.
- **(c) A likely-cause field.** An inference the paper would have to defend.

**Agent's recommendation.** (a).

**Answered** on 2026-11-02: as the agent recommends (`docs/DECISIONS.md` D-078; D-071).

**Why.** The claim is overlap; a ranking is a causal model that would need its own evaluation.

**Blocks.** WP01, WP08, WP09a.

#### Q31. The CLI subcommands: shape, names and options

*Round 1.*

**Summary.** Q31 decides the shape, names and options of the CLI subcommands; the agent recommends two first-word subcommands of mirage_vmaf, timeline to build and attribute to query, with `eval/phase3-survey/design.md` §7's options and text output by default.

**Question.** §3.2 asks for a CLI subcommand; mirage_vmaf has none, and today an argument that isn't an option is an error (`src/VMAFCli.cpp:288`). What are the subcommands, and their options (`eval/phase3-survey/design.md` §7)?

**Options.**

- **(a) Two first-word subcommands, timeline and attribute.** timeline builds, detects and writes a document, doing the I/O once; attribute queries a document without reading media; the bare options still score. With `eval/phase3-survey/design.md` §7's options, attribute printing text by default and JSON with `--format json`.
- **(b) One subcommand that builds and queries.** Every query reads every segment and fetches the session again.
- **(c) A separate program, mirage_timeline.** As D-017 separated mirage_vmaf from the player; one more binary to build, package and test.
- **(d) A third form of mirage_vmaf's options.** `--timeline` beside `--ref/--dis` and `--ladder`: the parser's forms grow, and a query's options mix with scoring's.

**Agent's recommendation.** (a), with `eval/phase3-survey/design.md` §7's option set.

**Answered** on 2026-11-02: as the agent recommends (`docs/DECISIONS.md` D-079; D-071).

**Why.** The build does the I/O once, queries are offline, cheap and deterministic, and existing command lines keep their meaning.

**Blocks.** WP00a, WP09a, WP11a, WP14.

#### Q32. The JSON reader

*Round 1.*

**Summary.** Q32 decides whether Phase 3's JSON reader is written in the repository or taken as a dependency; the agent recommends a document-tree reader written in the repository, moving to streaming only if a measurement of a full report needs it.

**Question.** src/ writes JSON by hand and reads none (`src/JsonText.h:1-21`); Phase 3 reads ladder reports, plans and documents, and a full ladder report is 27969051 bytes of text (`eval/phase3-survey/schema.md` §3.3). Written in the repository, or a dependency?

**Options.**

- **(a) Written in the repository, building a document tree.** No new dependency, as D-030 chose for the config file; its memory on full reports: MEASUREMENT PENDING.
- **(b) Written in the repository, streaming large arrays.** Bounded memory; more code.
- **(c) A header-only library through FetchContent.** Less code; a new dependency in D-031's list.

**Agent's recommendation.** (a), moving to (b) if a full report's load needs it.

**Answered** on 2026-11-02: as the agent recommends (`docs/DECISIONS.md` D-079; D-071).

**Why.** It follows D-030's choice of no new dependency and the repository's own writers; streaming is added only if a measurement says it is needed.

**Blocks.** WP00a, WP03.

#### Q33. The encoder of the acceptance tests

*Round 1.*

**Summary.** Q33 decides which encoder makes the synthetic ladders of the acceptance tests; the agent recommends libx264 where it is present and FFmpeg's MPEG-2 encoder otherwise, the encoder recorded in the test's output.

**Question.** D-069's ladders are encoded within the tests through FFmpeg's libraries, and an acceptance test that skips shows nothing. Which encoder?

**Options.**

- **(a) libx264, the tests skipped where it is absent.** Real HLS ladders' codec, a CODECS value, B-frames, and the same x264 commit in Homebrew and the Docker image (`eval/phase3-survey/testgen.md` §3.3); but a skipped acceptance test shows nothing.
- **(b) FFmpeg's MPEG-2 encoder.** In every FFmpeg build; no HLS CODECS value; a smaller drop (62.022 against 96.923 to 97.671, `eval/phase3-survey/testgen.md` §3.1).
- **(c) libx264 when present, else MPEG-2.** The same assertions, the encoder recorded. With MPEG-2 the pinned parameters passed on the degraded ladder and the ad from the main origin (`eval/phase3-survey/design-evaluation/runs/05_detection_sweep.txt`); the ad from 1 s ran with libx264 only and the continuous ad not at all, so those two with MPEG-2 are MEASUREMENT PENDING.

**Agent's recommendation.** (c).

**Answered** on 2026-11-02: as the agent recommends (`docs/DECISIONS.md` D-080; D-071).

**Why.** Both encoders gave clear drops confined to the intended frames, with the same attributions, where they were run (`eval/phase3-survey/design-evaluation/runs/10_attribute_final.txt`), and the tests then run in every build.

**Blocks.** WP10, WP11a.

#### Q34. The ad's timestamps in acceptance test 2

*Round 1.*

**Summary.** Q34 decides which timestamp arrangement the ad of acceptance test 2 uses; the agent recommends all three (restarting at the main content's origin as the acceptance test proper, from 1 s, and continuous), each generated once in the acceptance test's process.

**Question.** The ad's PTS can restart at the main content's origin (PTS repeat across the splice), start at another origin (a jump of more than 10 s, where the report's sequences match the playlist's), or continue the main content's. Which does acceptance test 2 use?

**Options.**

- **(a) Restart at the main content's origin.** The hard case for the time key; testgen's default (`eval/phase3-survey/testgen.md` §3.6, items 4 and 5).
- **(b) From 1 s.** The report's sequences match the playlist's (`eval/phase3-survey/testgen.md` §3.5).
- **(c) Continuous.** The only arrangement in which `eval/phase3-survey/ui.md`'s probe reached the ad by a seek (`eval/phase3-survey/ui.md` §2.4); not yet generated or scored.
- **(d) All three, (a) as the acceptance test proper.** Three ad ladders made in the acceptance test's process; their time in a Debug build: MEASUREMENT PENDING (`eval/phase3-survey/design.md` §9.5).

**Agent's recommendation.** (d).

**Answered** on 2026-11-02: as the agent recommends (`docs/DECISIONS.md` D-080; D-071).

**Why.** (a) is the case the time key must handle, and (b) and (c) cover the aligner's other case and the UI's seekable one.

**Blocks.** WP10, WP11a.

#### Q35. The rest of the synthetic content

*Round 1.*

**Summary.** Q35 decides the remaining choices of D-069's synthetic content as one package; the agent recommends testgen's measured choices: the fractal-noise source, a calm ad, continuity counters marked at the splice, no binary SCTE-35 in the ladders, the generator compiled with optimization, and top-rung mode.

**Question.** `eval/phase3-survey/testgen.md` §4 lists other choices for D-069's content: the source picture, the ad's content, the continuity counters at the splice, binary SCTE-35 forms in the ladders, the generator's speed in a Debug build, and the reference mode. How are they settled?

**Options.**

- **(a) testgen's measured recommendations as one package.** The fractal-noise source (the ramp's ladder fails D-002's BANDWIDTH order, `eval/phase3-survey/testgen.md` §2.2), the calm ad (the busy one nears the threshold at the normal quantizer, `eval/phase3-survey/testgen.md` §3.2), counters marked at the splice, no binary SCTE-35 in the ladders (the decoder's tests build sections instead), the generator optimized in every build type (P-Q), and top-rung mode.
- **(b) Item by item.** The owner answers each of `eval/phase3-survey/testgen.md` §4's items on its own.
- **(c) The existing tests' ramp and MPEG-2 helpers.** Reuses code, but the ramp's measured BANDWIDTH puts v270 above v360, so the ladder is refused (`eval/phase3-survey/testgen.md` §2.2).

**Agent's recommendation.** (a).

**Answered** on 2026-11-02: as the agent recommends (`docs/DECISIONS.md` D-080; D-071).

**Why.** Each item was measured to give a clear, deterministic drop confined to the intended frames (`eval/phase3-survey/testgen.md` §3).

**Blocks.** WP10, WP11a.

#### Q43. The ladder timeline's defaults: eight choices that change its output

*Round 1.*

**Summary.** Q43 decides eight defaults of the ladder timeline that change its output, and the agent recommends each as follows: live media playlists refused, only 75cf6e0's report layout read, a variant's segment map dropped on any PTS mismatch, an unpaired frame placed at the next paired frame, a rung ranked by the highest BANDWIDTH of its entries, each rung's copy of a tag kept, a drop ending at the next point of its curve, and a corrupt packet placed at its own frame with the next segment boundary named.

**Question.** The first version of the design made seven of these choices as process choices (P-E, P-X, P-G, P-H, P-AA, P-AB, P-AE), and (h) answers the first review's case of a corrupt packet at a splice; each changes what the timeline outputs by default, so each is the owner's. Each item has its options and the agent's recommendation and can be accepted or changed on its own; `eval/phase3-survey/design.md` §5.1, §5.2 and §6.3 give the context.

**Items**, each answered on its own:

- **(a) A live media playlist (formerly P-E).**
  - **1. Refused.** A media playlist without EXT-X-ENDLIST that isn't PLAYLIST-TYPE:VOD (hls::MediaTimeline::live(), `src/HlsAlign.h:69`) fails the build, naming it.
  - **2. A warning only.** The build goes on with what the playlist held when it was read.
  - *Agent's recommendation:* 1. *Why:* Live streams are outside the work order's scope, and a partial playlist would give a partial timeline without saying so in its events.
- **(b) Which ladder reports are read (formerly P-X).**
  - **1. The layout of 75cf6e0 on only.** Older layouts refused, naming the missing key; a pair report (--ref/--dis) refused; relative paths taken from the current directory, as mirage_vmaf takes its inputs.
  - **2. Readers for the other layouts too.** mirage_vmaf's committed reports fall into 12 layouts, one of them 75cf6e0's (`eval/phase3-survey/schema.md` §1.8); the records of Phases 1 and 2 then get timelines, at the cost of a reader per layout.
  - **3. 1, and a pair report read as a ladder of one pair.** A pair gets a timeline, without master or playlist events.
  - **4. 1, with relative paths taken from the report's directory.** A report moved together with its media still resolves, unlike mirage_vmaf, which takes its inputs from the current directory.
  - *Agent's recommendation:* 1. *Why:* D-057 added the discontinuity sequences the media key needs, 75cf6e0's layout has every key (8 committed files, `eval/phase3-survey/schema.md` §1.8), and the acceptance tests make new reports.
- **(c) A variant whose frames don't all match the demux pass (formerly the end of P-G).**
  - **1. No segment map for the variant.** Its segment-bound events (segments, tags, cues, requests) are left out, with a warning: one PTS mismatch costs the whole variant.
  - **2. The map kept for the frames that match.** More events survive, with a warning for each mismatch; a partly wrong map can place events off by the mismatch.
  - **3. The variant's segments placed by PTS against EXTINF, flagged.** Every event placed, some wrongly: 145 and 193 of 200 packets right on testgen's ladders (`eval/phase3-survey/design-library/runs/map-by-pos-*.txt`).
  - *Agent's recommendation:* 1. *Why:* No mismatch occurred on any probed playlist (`eval/phase3-survey/design-evaluation/runs/04_join_check.txt`), and an event placed on a wrong frame would be a silently wrong attribution.
- **(d) Where an unpaired frame sits (formerly part of P-H).**
  - **1. At the next paired frame of its input, by index.** A reference frame that failed to pair sits at itself.
  - **2. At the nearest reference frame by PTS.** Ambiguous where PTS repeat across a splice (`eval/phase3-survey/design-evaluation/runs/09_axes.txt`).
  - *Agent's recommendation:* 1. *Why:* Index order is unambiguous where PTS repeat.
- **(e) Which BANDWIDTH ranks a playlist that several master entries name (formerly P-AA).**
  - **1. The highest among its entries.** As the top rung is chosen (`src/VMAFLadder.h:33-35`), so the reference's rank from the top is 1.
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
  - **1. At the flagged packet's frame, the next segment boundary in its payload.** FFmpeg's fact unaltered. On testgen's ladder with unmarked counters the flags fell on frames 74 and 122 (`eval/phase3-survey/design.md` §5.2), so the ad's drop [75, 125) lists only the second, unless a margin (Q29) is given.
  - **2. At the first frame of the next segment, when a discontinuity precedes it.** The flag then sits at the boundary whose counter restart caused it; an inference from the stream's structure, wrong for a counter error inside a segment.
  - **3. Both, as two events.** The fact and the inferred boundary each listed; two events for one flag.
  - *Agent's recommendation:* 1. *Why:* It records what FFmpeg reported and names the boundary for the reader; D-069's acceptance ladders mark their counters (Q35) and have no such flags.

**Agent's recommendation.** (a) 1; (b) 1; (c) 1; (d) 1; (e) 1; (f) 1; (g) 1; (h) 1.

**Answered** on 2026-11-02: as the agent recommends (`docs/DECISIONS.md` D-081; D-071).

**Why.** Each recommended default keeps what the data shows, places nothing it can't place exactly, and says in the document what it left out.

**Blocks.** WP00a, WP01, WP02, WP03, WP04, WP06, WP09a.

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

#### Q10. Where a scripted session's per-segment HTTP timing comes from

*Round 2. **D-007**: the answer is, or includes, an approval or a ruling under D-007.*

**Summary.** Q10 decides where a scripted session's per-segment HTTP timing comes from; the agent recommends a sequential fetch of the session's files after scoring, timed by the player's NetworkLogger compiled into mirage_vmaf.

**Question.** D-068 gives a session the HTTP timing of its segments but no source for a scripted one, and the example needs a segment request's duration; the scoring reads are paced by decoding and scoring (`eval/phase3-survey/sources.md` §3.3). Without a session a ladder has no network events, both acceptance tests included (`eval/phase3-survey/design.md` §11, item 6). Which source? Option (e) changes the analyzer's input reading, which needs the owner's ruling under D-007 or a written plan, and D-068 keeps the decode path unchanged.

**Options.**

- **(a) A fetch of the session's files, timed by NetworkLogger.** The master, each played playlist and segment in play order, as Q44 (f) and (g) say; NetworkLogger compiled into mirage_vmaf unchanged and installed for the fetch only (`eval/phase3-survey/design.md` §3.3, §5.7), giving status, request sent, first byte, end and bytes. A prototype measured 2.313 s for a segment served 2.3 s late (`eval/phase3-survey/design-evaluation/runs/06_session_fetch.txt`); it is not a player's fetch, and it relies on FFmpeg 8.1.1's log wording.
- **(b) The same fetch, timed by avio alone.** No log parsing; the status only as the class of a failed open's AVERROR_HTTP_* ([http.c#L592-L608](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/http.c#L592-L608)), so 200 and 206 are not told apart.
- **(c) Declared in the plan.** Deterministic, but it measures nothing: the example's 2.3 s becomes an input, which makes the answer circular.
- **(d) None for scripted sessions.** Timing only from the player's log, later; Phase 3 can't answer the example's network part.
- **(e) (a), plus what the scoring reads saw.** A per-input recorder in SegmentWatch's hooks, labelled scoring-paced (`eval/phase3-survey/sources.md` §11, choice 5 (a)), so that every ladder gets network events. Its times are paced by scoring, the report changes and ladders must be scored again: a D-007 and D-068 matter, and WP16's work.

**Agent's recommendation.** (a).

**Answered** on 2026-11-06: as the agent recommends (`docs/DECISIONS.md` D-104 (a); D-103).

**Why.** It uses the component §3.1 names, gives every field §3.1 lists, and a test can make one segment slow with a delayed local server. (e) gives ladders without sessions network events, but paced by scoring they don't show how long a player would wait.

**Blocks.** WP00b, WP07, WP09b, WP11b, WP14, WP16.

#### Q11. The session plan's format

*Round 2.*

**Summary.** Q11 decides the scripted session plan's file format; the agent recommends versioned JSON whose spans are master entries with their first and last media sequence numbers.

**Question.** A scripted session (D-068) says which variant plays over each span of media. A plan is a list, which D-030's config file can't hold (each setting is given once, `src/VMAFCli.cpp:224`), so it is a file of its own, recorded by path and SHA-256. Which format, and in which unit?

**Options.**

- **(a) Versioned JSON, spans as segment ranges.** `{entry, first_seq, last_seq}` in play order: exact, no snapping rule, the fetched segments follow directly, and HLS switches at segment boundaries. The player's log can later add observed fields to the same format.
- **(b) Versioned JSON, spans in seconds on the axis.** Closer to D-068's span of media time, but it needs a snapping rule, and a rule for fetching around a switch inside a segment.
- **(c) Text lines `<entry> <first>-<last>`.** The simplest to write; a second format for the player's log later.
- **(d) The player's `--qa-switch` syntax.** Already known (`src/main.cpp:553-556`), but its times are wall seconds after the first frame, and `v<height>` is ambiguous when two playlists share a height.

**Agent's recommendation.** (a).

**Answered** on 2026-11-06: as the agent recommends (`docs/DECISIONS.md` D-105 (a); D-103).

**Why.** Segment ranges are exact for HLS and give the fetch list directly; JSON reuses the reader Phase 3 needs anyway.

**Blocks.** WP00b, WP07, WP09b, WP11b, WP17.

#### Q12. A session's spans that play the reference in top-rung mode

*Round 2.*

**Summary.** Q12 decides what a session's curve holds where its plan plays the top rung, which top-rung mode doesn't score; the agent recommends leaving those spans unscored and marked, with no detection there.

**Question.** In top-rung mode the top rung is the reference and has no score (`src/VMAFLadder.h:36-38`). What does a session's curve hold where its plan plays the top rung?

**Options.**

- **(a) Unscored.** No points and no detection there; the document marks the span `scored: false`.
- **(b) Taken as 100.** Assumed, not measured; a switch down from it looks like a drop of 100 minus the next score.
- **(c) Sessions require mezzanine mode.** Every variant is scored, but a stitched ladder can't be scored against a one-file mezzanine (76.92% of frames failed, `eval/phase3-survey/testgen.md` §3.5).
- **(d) The top rung scored against itself once.** Measured, at the cost of one more pair in §2's run (WP16), for a curve that says nothing about encoding.

**Agent's recommendation.** (a).

**Answered** on 2026-11-06: as the agent recommends (`docs/DECISIONS.md` D-105 (b); D-103).

**Why.** It claims nothing that wasn't measured and needs no change to §2.

**Blocks.** WP00b, WP07, WP09b, WP16.

#### Q14. The anchor that places an SCTE-35 cue

*Round 2.*

**Summary.** Q14 decides which anchor places a cue that carries an SCTE-35 message; the agent recommends the tag's position (the segment it precedes), with a decoded splice time kept as its own splice_point event and every anchor recorded.

**Question.** A cue can be placed by its tag's position, by a DATERANGE's START-DATE through PROGRAM-DATE-TIME, or by its message's splice time; vendors disagree, and several published messages carry no splice time (`eval/phase3-survey/scte35.md` §4). D-069's ladders carry plain tags, which every option places by position (`eval/phase3-survey/testgen.md` §2.3). Which anchor places cues that carry a message?

**Options.**

- **(a) Position, with the other anchors recorded.** Cue events at the segments their tags precede; a splice time becomes a `splice_point` event, placed on the carrying variant's own frames (`eval/phase3-survey/design.md` §5.1); a warning when an anchor falls outside the tag's segment. Segment-accurate; the rule MediaTailor, Video Stitcher and broadpeak.io state (`eval/phase3-survey/scte35.md` §4.1).
- **(b) Splice time when the message has one, else position.** The standard's splice point, frame-accurate where present; absent from many published messages, and an upstream restamp without a pts_adjustment update misplaces it.
- **(c) START-DATE through PDT for DATERANGE, else position.** The HLS rule for DATERANGE, but it needs a PDT; in Wowza's published example tag and date are 13.8 s apart (`eval/phase3-survey/scte35.md` §4.2).
- **(d) A configurable precedence.** Every anchor recorded and one declared order (`eval/phase3-survey/scte35.md` suggests splice time, START-DATE, position), with a warning when they disagree; one more option to defend and test.

**Agent's recommendation.** (a).

**Answered** on 2026-11-06: as the agent recommends (`docs/DECISIONS.md` D-106 (a); D-103).

**Why.** Position is where stitched media changes segment and every form has it, while the frame-accurate time stays visible as its own event.

**Blocks.** WP05.

#### Q15. Which decoded SCTE-35 messages count as cue-outs and cue-ins, and cancellations

*Round 2.*

**Summary.** Q15 decides which decoded SCTE-35 messages count as break starts and ends, and whether cancellations are kept; the agent recommends splice_insert by its out-of-network flag and time_signal with the break, advertisement, placement-opportunity and ad-block segmentation types, every other message kept as a non-break event and cancellations as their own events.

**Question.** splice_insert says out or in by its out_of_network_indicator; for time_signal the segmentation_type_ids that count are a choice, and sources' defaults differ (`eval/phase3-survey/scte35.md` §4.4). Which set, and are cancelled events kept?

**Options.**

- **(a) Breaks, advertisements, placement opportunities and ad blocks.** time_signal with types 0x22-0x23, 0x30-0x37 and 0x44-0x47 (SCTE 35 2023r1 Table 23); every other message a `message` event and a cancellation a `cancel` event; the set configurable (`--cue-types`).
- **(b) splice_insert only.** Every time_signal a non-break message; misses segmentation-only signalling.
- **(c) MediaPackage v1's default set.** splice_insert plus provider and distributor advertisements and placement opportunities (`eval/phase3-survey/scte35.md` §4.4); one vendor's choice.
- **(d) Every Start/End pair of Table 23.** Programs, chapters and the rest become breaks too.

**Agent's recommendation.** (a).

**Answered** on 2026-11-06: as the agent recommends (`docs/DECISIONS.md` D-106 (b); D-103).

**Why.** It covers the break hierarchy the standard defines and keeps every other message on the timeline, so a program or chapter boundary can still be attributed.

**Blocks.** WP05, WP09c.

#### Q16. An SCTE-35 message whose CRC_32 fails

*Round 2.*

**Summary.** Q16 decides what happens to an SCTE-35 message with a bad CRC_32; the agent recommends keeping it, marked crc_valid: false with a warning, with an option to drop such messages.

**Question.** Of 79 published messages collected, 9 have a bad CRC_32, 1 of them a library's intended negative test (`eval/phase3-survey/scte35.md` §5; `eval/phase3-survey/scte35_evidence/runs/check_vectors.txt`; `eval/phase3-survey/review-3/runs/vector-status.txt`; the survey's summary, `eval/phase3-survey/scte35.md` §0 item 5, gives 3 in error). What does Phase 3 do with such a message?

**Options.**

- **(a) Kept, marked.** `crc_valid: false` and a warning; still attributable; `--bad-crc drop` drops them.
- **(b) Dropped.** A corrupted message isn't trusted; its tag still shows by position.
- **(c) Kept silently.** No signal of the doubt.

**Agent's recommendation.** (a).

**Answered** on 2026-11-06: as the agent recommends (`docs/DECISIONS.md` D-106 (c); D-103).

**Why.** Vendors and the RFCs publish such messages, and an event the attribution omits can't explain a drop.

**Blocks.** WP05, WP09c.

#### Q21. Which SCTE-35 test vectors may be committed

*Round 2.*

**Summary.** Q21 decides which published SCTE-35 messages Phase 3's tests may commit; the agent recommends committing only sections the tests build themselves, until the owner settles the licences.

**Question.** The published messages collected carry different terms (`eval/phase3-survey/scte35.md` §5): scte35-go is Apache-2.0 but includes copies of SCTE's samples, SCTE's PDF and several vendors' pages are All Rights Reserved, some pages state no licence, and of threefive's two repositories the documentation of superkabuki/threefive_is_scte35 is GPL-2.0 and the example playlists and smoke test of futzu/threefive are MIT, each by GitHub's detection. Which may Phase 3's tests commit to GPL-3.0-or-later Mirage?

**Options.**

- **(a) Self-made only.** Sections the test builds from field values, with their CRC; the decoder checked against the published vectors during development, outside the repository.
- **(b) Plus scte35-go's own test cases and futzu/threefive's examples.** Apache-2.0 for scte35-go's cases, its copies of SCTE's samples left out, and MIT for futzu/threefive's 6 messages, for which their source gives no decode, so a test could check only that they decode with a valid CRC (`eval/phase3-survey/scte35.md` §5).
- **(c) Plus the standard's samples and the RFCs' examples.** SCTE's All Rights Reserved; the RFC examples under the IETF Trust's terms.
- **(d) Plus vendor documentation examples.** Pages without a licence or All Rights Reserved, and threefive's GPL-2.0 documentation.

**Agent's recommendation.** (a), until the owner settles the licences, as with D-008.

**Answered** on 2026-11-06: as the agent recommends (`docs/DECISIONS.md` D-107; D-103).

**Why.** It needs no licence judgement, and the layout is still checked during development against the 61 published messages that decode with a valid CRC and no disagreement with their sources (`eval/phase3-survey/scte35.md` §5).

**Blocks.** WP05.

#### Q41. The component's name: the work order's NetworkDebugger against the code's NetworkLogger

*Round 2. **D-007**: the answer is, or includes, an approval or a ruling under D-007.*

**Summary.** Q41 decides how the difference between the work order's name NetworkDebugger and the code's NetworkLogger is handled, linking NetworkLogger into mirage_vmaf being Q10 (a)'s; the agent recommends keeping the code's name and reporting the difference.

**Question.** §3.1 names the network source NetworkDebugger, an av_log interceptor, while the code's component is NetworkLogger, which also hooks io_open and io_close2 and is built into the player and mirage_tests only (`eval/phase3-survey/sources.md` §10, item 1). Whether mirage_vmaf links it for the session fetch is Q10 (a); this question is the name only, and where Q10's answer isn't (a) only option (a)'s report applies. How is the difference handled? Option (b) edits `src/Demuxer.cpp` and `src/main.cpp`, the decode and render path, so choosing it includes the owner's ruling under D-007 that a rename is outside it, or the approval of a written plan.

**Options.**

- **(a) Keep the code's name and report the difference.** NetworkLogger keeps its name and its tests, and `eval/phase3-survey/design.md` §11, item 1 reports the difference in the design's words.
- **(b) Rename it throughout.** Matches the work order's word, but edits the 21 files that name it, `CMakeLists.txt`, `src/Demuxer.cpp` and `src/main.cpp` among them (`eval/phase3-survey/design/runs/networklogger-users.txt`), and the documents, for a name; WP00b would make it a commit of its own before WP07.
- **(c) A headless wrapper named after the work order.** The session fetch calls a NetworkDebugger wrapper around NetworkLogger: two names for one component.

**Agent's recommendation.** (a).

**Answered** on 2026-11-06: as the agent recommends (`docs/DECISIONS.md` D-104 (b); D-103).

**Why.** The component exists and is tested under its name, and the work order's rule is to report a difference, not to hide it.

**Blocks.** WP00b, WP07, WP15b.

#### Q44. Sessions, the fetch and decoded messages: seven choices that change their output

*Round 2.*

**Summary.** Q44 decides seven defaults of sessions, the session fetch and decoded SCTE-35 messages that change their output, and the agent recommends each as follows: plans with gaps or overlaps refused, requests placed on their segment's media span or at the first span that plays their playlist, no retries in the fetch, one event per fetched file with its redirect chain in the payload, one cue for a message in an EXT-X-CUE-OUT's segment, a fetch one file at a time without pacing, and each played variant's playlist fetched once.

**Question.** Earlier versions of the design made six of these choices as process choices (P-P, P-H, P-K, and three parts of P-N: its retries, its pacing and its playlist fetches), and (d) answers the first review's case of a redirected fetch; each changes what a session's timeline or a cue outputs, so each is the owner's. Each item has its options and the agent's recommendation and can be accepted or changed on its own; `eval/phase3-survey/design.md` §5.5, §5.6 and §5.7 give the context.

**Items**, each answered on its own:

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
  - **2. As the scorer's demuxer retries.** Up to kSegMaxRetry = 10 more tries (`src/VMAFSegments.h:28`), each try its own row in the event's payload.
  - **3. An option, default none.** One more option to record and test.
  - *Agent's recommendation:* 1. *Why:* The fetch records what one sequential client got at the time, and its events carry origin fetch, so a failure there says nothing about the scores.
- **(d) A fetch that is redirected.**
  - **1. One event per fetched file.** NetworkLogger starts a row at each request line, a redirect's included (`src/NetworkLogger.cpp:601`, by reading, not run); the event's times run from the first row's start to the last row's end, its status is the last row's, and the chain is in its payload.
  - **2. One event per row.** Each request of the chain listed on its own.
  - *Agent's recommendation:* 1. *Why:* A player waits for the whole chain, and the example's duration is a segment's, not a request's.
- **(e) A message in the same segment as an EXT-X-CUE-OUT (formerly P-K).**
  - **1. That cue's message.** The two make one cue_out event, with the decoded message in its payload.
  - **2. Each its own cue.** Two cue_out events for AWS-style pairs of OATCLS and CUE-OUT (`eval/phase3-survey/scte35.md` §2.4).
  - *Agent's recommendation:* 1. *Why:* They are one signal written twice; two cue-outs would claim two breaks.
- **(f) The fetch's pacing (formerly part of P-N).**
  - **1. One file at a time, unpaced.** Each request starts once the file before it has been read; the fetch is as quick as the server, and an event's wall clock says when the fetch, not a player, made the request.
  - **2. One file at a time, paced to the plan's media time.** Each segment requested when its span reaches it on a clock started with the session, as a player without a buffer would: wall clocks then follow playback, but a build with a session lasts at least the session's media length, 8 s for a session over a whole D-069 ladder (`eval/phase3-survey/testgen.md` §2.3).
  - **3. A buffer like a player's.** Requests run ahead of playback up to a buffer length: a model of a player, whose length and rules would be one more choice for the paper to defend.
  - *Agent's recommendation:* 1. *Why:* A request's duration runs from its request to its last byte under every option, and the events carry origin fetch; pacing would only add the session's media length to every build, and a buffer is a player model.
- **(g) A variant's media playlist in the fetch (formerly part of P-N).**
  - **1. Once, the first time a span plays it.** One playlist_request per played variant, at that span's first frame (item (b)).
  - **2. At the start of every span that plays it.** A switch back to a variant fetches its playlist again: one playlist_request per span.
  - *Agent's recommendation:* 1. *Why:* A VOD playlist (Q43 (a)) doesn't change during a session, so one fetch per variant shows its request without repeating it.

**Agent's recommendation.** (a) 1; (b) 1; (c) 1; (d) 1; (e) 1; (f) 1; (g) 1.

**Answered** on 2026-11-06: as the agent recommends (`docs/DECISIONS.md` D-104 (a) for (c), (d), (f) and (g), D-105 for (a) and (b), D-106 (d) for (e); D-103).

**Why.** Each recommended default records what was planned or observed without inferring more, one signal stays one event, and the fetch is as quick as its server allows.

**Blocks.** WP00b, WP05, WP07, WP09b, WP11b.

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

#### Q09. Decode errors in the timeline

*Round 3. **D-007**: the answer is, or includes, an approval or a ruling under D-007.*

**Summary.** Q09 decides whether and how Phase 3 records decode errors; the agent recommends recording the scoring decode's errors in the analyzer if the owner rules that recording alone is outside D-007 or approves its short plan, and otherwise leaving decode errors out of Phase 3.

**Question.** §3.1 lists decode errors, but today a damaged segment scores to a complete report with no trace of them: 6 frames carried decode_error_flags 4 and scored 0 to 34.6 (`eval/phase3-survey/sources.md` §7). Where do decode errors come from? Option (a) touches the analyzer's decode loop: answering it is a ruling under D-007 that recording alone is outside it, or the approval of a written plan.

**Options.**

- **(a) Recorded by the analyzer while scoring.** The flags and send errors read in DecodeCtx (`src/VMAFAnalyzer.cpp:197-241`), as a sparse per-pair list in the report; no frame, pairing or score changes, and no extra decode. A ladder must be scored again to carry them; WP16 records them and makes them decode_error events.
- **(b) A decode pass in the timeline build.** The scorer untouched; every variant decoded again (time: MEASUREMENT PENDING), in an adapter of its own (WP16), recording a second decode's errors, not the scored one's.
- **(c) Not in Phase 3.** Keyframes and corrupt packets from the demux pass and unpaired frames from the report only; decode errors a difference to report.

**Agent's recommendation.** (a) if the owner so rules or approves the plan; otherwise (c).

**Why.** Only the scoring decode's errors explain the scores being attributed, at no decode cost. Neither acceptance test needs them, so the package is conditional and off the acceptance path.

**Blocks.** WP16.

#### Q13. The player's own session log in Phase 3

*Round 3. **D-007**: the answer is, or includes, an approval or a ruling under D-007.*

**Summary.** Q13 decides whether the player's own session log is part of Phase 3; the agent recommends leaving it out, so that sessions come only from scripted plans, and taking it later with its own D-007 plan.

**Question.** D-068 has sessions from a scripted plan now and later from a log the player records. Recording needs hooks in switch_video, read_loop, video_decode_loop and on_present (`eval/phase3-survey/sources.md` §6), which sit in the render and decode path. Is the log part of Phase 3? Answering (b) asks the agent for a written plan under D-007, for the owner to approve before any code.

**Options.**

- **(a) Not in Phase 3.** Scripted plans only, the plan's format leaving room for the log. The decoder events D-068's limits expect for frames dropped at a switch then don't exist in Phase 3, a difference to report (`eval/phase3-survey/design.md` §11, item 3).
- **(b) In Phase 3, under a D-007 plan.** Logging-only hooks with the rung, the take-over point and the frames dropped; its package shares `src/main.cpp` with the panel's, so the two run one after the other.
- **(c) From the existing switch-trace text.** No change to the player, a reader of it in WP17; but free text without the rung, on logger::uptime()'s clock, which isn't NetworkLogger's (`eval/phase3-survey/sources.md` §6 and §9).

**Agent's recommendation.** (a).

**Why.** Neither acceptance test nor the composite example needs it, and (b) is a render and decode path change the owner should take with its own plan.

**Blocks.** WP17.

#### Q36. The §3.3 UI plan under D-007, and the panel's place

*Round 3. **D-007**: the answer is, or includes, an approval or a ruling under D-007.*

**Summary.** Q36 asks the owner to approve `eval/phase3-survey/design.md` §10 as the written D-007 plan for §3.3's panel and to choose where the panel goes; the agent recommends approving it, docked above the control stack, and building it after the headless packages.

**Question.** `eval/phase3-survey/design.md` §10 (from `eval/phase3-survey/ui.md`) is a written plan for §3.3: an ImGui panel that reads a timeline document and its ladder report, with its loading and math in ImGui-free files tested in ctest. By the agent's reading none of it touches the render or decode path (`eval/phase3-survey/ui.md` §3), and approving it is the approval D-007 asks for. Does the owner approve it, and where does the panel go?

**Options.**

- **(a) Approve, docked above the control stack.** Full width beside the seek bar, lowest in the bottom stack so that it doesn't move when the filmstrip shows; it covers the lower video.
- **(b) Approve, as a tab of the network sidebar.** Docking, resizing and focus handling for free; narrow; tied to N.
- **(c) Approve, as a floating window.** The simplest; it can cover the seek bar, and its position isn't kept.
- **(d) Not in Phase 3.** The headless parts only; §3.3 reported as deferred.

**Agent's recommendation.** (a), built after the headless packages.

**Why.** A timeline belongs beside the seek bar it shares a clock with (`eval/phase3-survey/ui.md` §2.6), and the headless packages carry the paper's claim first.

**Blocks.** WP00c, WP12, WP13.

#### Q37. Clicking a low point inside an ad whose timestamps restart

*Round 3. **D-007**: the answer is, or includes, an approval or a ruling under D-007.*

**Summary.** Q37 decides what the panel does when the user clicks inside an ad that no seek can reach; the agent recommends saying the frame isn't reachable and offering a seek to the segment before the discontinuity, which changes no render or decode code.

**Question.** With FFmpeg 8.1.1's hls demuxer, no seek reached the frames of an ad whose timestamps restart at 0.040 s: by PTS the seek fails and the player's clamp sends it to the stream's start, and by playlist position the ad is skipped (`eval/phase3-survey/ui.md` §2.4, measured). An ad that restarts at the main content's origin, as D-069's does, was not probed (MEASUREMENT PENDING). What does the panel do on such a click? Options (b) and (c) change the render loop or Demuxer, each needing a written plan under D-007.

**Options.**

- **(a) It says the frame isn't reachable by a seek.** And offers a seek to the start of the segment before the discontinuity, from which playing reaches the frame; no render or decode change; a difference from §3.3, reported.
- **(b) Play to the frame, then pause.** A new kind of request in the render loop (`src/main.cpp`).
- **(c) Another way to position the hls demuxer.** A change of `src/Demuxer.cpp`.

**Agent's recommendation.** (a) for Phase 3.

**Why.** It touches no render or decode path; (b) or (c) can follow with their own plan.

**Blocks.** WP12, WP13.

#### Q38. The timeline panel's toggle key

*Round 3.*

**Summary.** Q38 decides which key, if any, toggles the timeline panel; the agent leaves it to the owner's preference and suggests L if a key is wanted.

**Question.** The letters bound to nothing are B, E, J, L, P, R, S, U, X, Y and Z (`eval/phase3-survey/ui.md` §1.5). Which, if any, toggles the timeline panel?

**Options.**

- **(a) L.** Reads as line, the timeline.
- **(b) E.** Reads as events.
- **(c) No key.** The View menu, the overlays popover and --ui-overlays only.

**Agent's recommendation.** The owner's preference; L if a key is wanted.

**Why.** A key binding is a matter of taste; L is the mnemonic one.

**Blocks.** WP13.

#### Q39. The panel's settings, and what a click outside a drop queries

*Round 3.*

**Summary.** Q39 decides what the panel lets the user configure and which interval it queries for a click outside a detected drop; the agent recommends only the document to load, with prefs.txt as the player's config file, and the clicked frame's segment as the interval.

**Question.** The work order wants each setting on the command line and in a config file; the player's settings file is prefs.txt, key=value, written by the app and ignored in snapshot runs (`eval/phase3-survey/ui.md` §2.6, D9). Item (a) is what the panel lets the user configure; item (b) is which interval a click outside a detected drop queries (`eval/phase3-survey/design.md` §10).

**Items**, each answered on its own:

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

**Agent's recommendation.** (a) 1; (b) 2.

**Why.** It adds the least new mechanism, the panel and the CLI can't disagree, and a click needs no setting.

**Blocks.** WP12, WP13.

#### Q40. The player's tag ruler and ManifestParser's cue defects

*Round 3.*

**Summary.** Q40 decides what happens to the player's GL tag ruler and ManifestParser's cue defects; the agent recommends leaving them unchanged and reported now, and retiring the ruler for the panel's playlist lane with the UI package.

**Question.** The player's GL tag ruler takes its tags from ManifestParser, which takes EXT-X-CUE-OUT-CONT for a cue-out, drops #EXT-OATCLS-SCTE35, leaves DATERANGE unparsed and doesn't count discontinuity sequences (`eval/phase3-survey/sources.md` §4.2). By reading, the ruler's click never fires and its ticks sit off by the stream's start time (`eval/phase3-survey/ui.md` §1.8, F1 and F2). What happens to them in Phase 3?

**Options.**

- **(a) Left as they are, and reported.** Phase 3's headless work uses a parser of its own; nothing of the player's tag display changes.
- **(b) Fix ManifestParser.** Changes a tested behaviour (`tests/test_manifest_parser.cpp:95`) and the player's display; WP13 would make it.
- **(c) Retire the GL ruler for the panel's playlist lane.** Once the panel exists: one tag display, on the new parser; changes `src/DebugHUD.{h,cpp}`.

**Agent's recommendation.** (a) now, (c) with the UI package.

**Why.** The headless work doesn't need them, and the panel's lane replaces the ruler without editing a tested parser.

**Blocks.** WP13.

#### Q42. Differences from the work order with no alternative in Phase 3

*Round 3.*

**Summary.** Q42 asks how the differences from the work order that no option of Phase 3 removes are handled; the agent recommends acknowledging them as stated in the pending-decisions document and in Phase 3's PR, with no document edited to match.

**Question.** `eval/phase3-survey/design.md` §11 lists 14 differences; those no option removes are marked Q42: Mirage has no ABR (item 2), the offline decode drops no frame of its own (item 3), the top rung has no score in top-rung mode (item 11), and FFmpeg 8.1.1 exports no HTTP status (item 12). A repository document is also wrong: `docs/VMAF_GAP_REPORT.md:230-231` says the Decoder has a dropped-frame counter, which is the render loop's total_dropped (`src/main.cpp:1751`). How are they handled?

**Options.**

- **(a) Acknowledged as stated.** Reported in `docs/PHASE3_DECISIONS_PENDING.md` and Phase 3's PR; no document edited to match.
- **(b) Acknowledged, and the gap report corrected.** As (a), plus a dated correction line in `docs/VMAF_GAP_REPORT.md` for its factual error, written by WP15b.
- **(c) Some reopened.** The owner names which, and the agent writes options for each.

**Agent's recommendation.** (a).

**Why.** Each follows from decided items or facts of the code, and reporting them is the work order's rule; correcting a Phase 0 document is a separate call the owner can make with (b).

**Blocks.** WP15b.

## Questions raised by the implementation

Questions that the packages met and the design doesn't settle, each of whose answers changes an output. They continue the design's numbering.

#### Q45. A variant refused under D-081 (c): its keyframes and corrupt packets

*Round 1.* Raised by WP06.

**Question.** D-081 (c) leaves a variant whose frames don't all match the demux pass without a segment map and its segment-bound events. Its keyframes and corrupt packets are placed by the pass's frame order, which the report contradicts. Are they kept?

- **1. Left out too.** The refused variant's media holds only the reason; nothing is placed that can't be placed exactly.
- **2. Kept, placed by the pass's frame order.** They may sit off their frames.

**Agent's recommendation.** 1.

**Answered** on 2026-11-04: 1, as the agent recommends (`docs/DECISIONS.md` D-086).

#### Q46. Two cue-outs before a cue-in

*Round 1.* Raised by WP04.

**Question.** A playlist can have a second `EXT-X-CUE-OUT` before any `EXT-X-CUE-IN`, as Google Video Stitcher's early notice (`X-TYPE="EABN"`) before the cue-out proper. How are the ad breaks paired?

- **1. They don't nest.** The first cue_out ends at its declared duration, or makes no break with a warning; the second pairs with the next cue_in.
- **2. Each with the next cue_in.** Both breaks end at the same cue_in, one spanning the other, as the design's §5.5 reads literally.

**Agent's recommendation.** 1.

**Answered** on 2026-11-04: 1, as the agent recommends (`docs/DECISIONS.md` D-087).

#### Q47. An `EXT-X-DATERANGE` with a malformed attribute and an SCTE-35 message

*Round 2.* Raised by WP04's review; it decides what the SCTE-35 decoder (WP05) receives.

**Question.** WP04 treats an `EXT-X-DATERANGE` as malformed when its attribute list doesn't parse or an attribute isn't of the type RFC 8216 §4.3.2.7 gives it (Wowza's unquoted `START-DATE`, `eval/phase3-survey/scte35.md` §2.5): the tag is kept raw with the reason, and none of its `SCTE35-*` messages is handed on. A `DATERANGE` without `ID` or `START-DATE`, which the RFC requires, is not refused, and the attribute-list reader accepts blanks around `,` and lowercase letters or `_` in names, which RFC 8216 §4.2 doesn't allow (`src/PlaylistEvents.cpp`). Which rule applies?

- **1. Strict.** A malformed `DATERANGE` hands on no message, and one without `ID` or `START-DATE` is malformed too.
- **2. Lenient for the messages.** Each `SCTE35-*` attribute that is itself a well-formed hexadecimal sequence is handed on with a warning, even when another attribute is malformed.
- **3. As it is now.** Malformed tags hand on nothing; `ID` and `START-DATE` aren't required.

**Agent's recommendation.** 2: the messages are what D-070 asks Phase 3 to read, and a vendor's malformed date says nothing about its message; the warning keeps the doubt visible.

**Answered** on 2026-11-06: 2, as the agent recommends (`docs/DECISIONS.md` D-106 (e); D-103).

**Blocks.** WP05.

#### Q48. The axis's per-frame values where a frame isn't a whole number of ticks

*Round 1.* Raised by WP08's review.

**Question.** WP08 writes each reference frame's playlist-time ticks and media key, which design §8 doesn't list but `attribute` needs without the media, as stretches of frames with a whole step. Where a frame isn't a whole number of ticks (24000/1001 and 60000/1001 fps on MPEG-TS's 90 kHz clock), the PTS step alternates, the stretches cover 4 or 2 frames, and the document grows with frames, past what one value per frame would take (the review's probe, not committed). How are they written?

- **1. Stretches with a rational step and a phase.** The k-th frame's value is first + floor((phase + k × num) / den): exact, and the document grows with segments at every common rate.
- **2. One value per frame.** The simplest; the document grows with frames.
- **3. Only what the ladder report doesn't hold.** The axis is restored from the document and the report together, so `attribute` would always need `--report`.
- **4. As built.** Stretches with a whole step.

**Agent's recommendation.** 1.

**Answered** on 2026-11-05: 1, as the agent recommends (`docs/DECISIONS.md` D-092 (a)).

#### Q49. Which number a pair has in the document

*Round 1.* Raised by WP08's review.

**Question.** WP08 writes `variants[].pair` and a curve's `index` as the pair's position among the report's complete pairs. The report's own `pairs[].index` is the plan's and skips the number of a pair that failed, so the two differ once a pair fails. Which number do the document and `--curve pair:<k>` use?

- **1. The report's `pairs[].index`.** The one a reader of the report sees; the code maps it to the position.
- **2. The position among the complete pairs.** As built.

**Agent's recommendation.** 1.

**Answered** on 2026-11-05: 1, as the agent recommends (`docs/DECISIONS.md` D-092 (b)).

#### Q50. Values a variant doesn't have

*Round 1.* Raised by WP08's review.

**Question.** WP08 writes null for the bandwidth, both ranks and the playlist of a variant without master entries (a mezzanine reference) or one that isn't a rung. A mezzanine reference has no segment map either, but WP08's reader takes only `"ok"` or `"refused: …"`, so its document says `"ok"`. How are these written?

- **1. Null, the segment map too.**
- **2. Null, the segment map `"none"`.**
- **3. As built.** Null, the segment map `"ok"`.

**Agent's recommendation.** 1: the document writes only what its inputs show.

**Answered** on 2026-11-05: 1, as the agent recommends (`docs/DECISIONS.md` D-092 (c)).

#### Q51. WP08's other forms, and how strict its reader is

*Round 1.* Raised by WP08's review.

**Question.** Forms design §8 leaves open, each of which changes the output, as WP08 built them: `axis.runs` for the runs of §4.2's check; a curve as `{"kind", "index"}`; rationals as `"num/den"` strings; `overlap.scope` `"curve-inputs"`; the runs' tests `"clocks"`, `"playlist-grid"` and `"report-grid"`; the stretches' member names; `detection.curves` with `session_rules` null; all six groups of overlapping events, empty ones included; and a strict reader (every key required, unknown keys refused, every value in seconds recomputed), so that any addition takes a new `schema_version`. Are they kept?

- **1. All as built.**
- **2. Each asked on its own.**

**Agent's recommendation.** 1.

**Answered** on 2026-11-05: 1, as the agent recommends (`docs/DECISIONS.md` D-092 (d)).

#### Q52. Playlist-time ticks in a unit finer than the PTS clock

*Round 1.* Raised by WP08b.

**Question.** At 24000/1001 and 60000/1001 fps on a 90 kHz clock, build_axis() keeps the playlist-time ticks in a unit 8 and 4 times finer than the PTS, so a PTS rounded by one tick moves them by 8 or 4; D-092 (a)'s formula moves by 0 or 1 a frame, so those ticks still start a stretch every 4 or 2 frames. How are they written?

- **1. A whole multiplier on the floor term**, a new member of the stretches: all three kinds of values grow with segments.
- **2. Per segment, as following the keys' PTS**, the axis's own rule, with the formula as fallback.
- **3. As built.** They grow with frames at those rates.
- **4. One value per frame**, for those ticks only.

**Agent's recommendation.** 1.

**Answered** on 2026-11-05: 1, as the agent recommends (`docs/DECISIONS.md` D-099 (a)).

#### Q53. WP08b's encoding details

*Round 1.* Raised by WP08b.

**Question.** Three choices that change a document's bytes but no value read back: which step and phase hold frames that several would (the longest stretch, the frame duration on a tie, the smallest phase, 0/1 for one frame); whole steps for the keys of an axis without a frame duration; and a variant without a playlist whose segment map isn't Variant's default, written as given for the reader to refuse. Are they kept?

- **1. All as built.**
- **2. Each asked on its own.**

**Agent's recommendation.** 1.

**Answered** on 2026-11-05: 1, as the agent recommends (`docs/DECISIONS.md` D-099 (b)).

#### Q54. WP09a's choices

*Round 1.* Raised by WP09a's review.

**Question.** Eight choices WP09a made where the design is silent, each of which changes an output: the layout of `attribute --format json`; how ranks and labels are counted; where a segment ends; the payload names of the vmaf and decoder-side events; attribute's defaults (margins, `--auto`'s parameters, drop numbering, which curves are answered); exit statuses; how finely warnings are given; and the forms of the options record and the config file. Are they kept as built?

- **1. All as built.**
- **2. Each asked on its own.**

**Agent's recommendation.** 1.

**Answered** on 2026-11-05: 1, as the agent recommends (`docs/DECISIONS.md` D-099 (c)).

#### Q55. PTS rounded to a clock coarser than their time base

*Round 1.* Raised by WP08b's review.

**Question.** 90 kHz PTS of a millisecond timestamp, as an FLV, RTMP or Matroska source remuxed into MPEG-TS without re-encoding gives, follow no step WP08b's writer tries, so the keys and the playlist-time ticks start a stretch every few frames (the review's probe, not committed). How is such a reference written?

- **1. The writer also tries a coarser clock**: a scale above 1 that divides every difference from a stretch's first value. Only the writer changes; schema version 1, every value exact.
- **2. As built**, the limit stated in the format document.

**Agent's recommendation.** 1.

**Answered** on 2026-11-06: 1, as the agent recommends (`docs/DECISIONS.md` D-100 (a)).

#### Q56. A sequence at another frame rate than the axis's

*Round 1.* Raised by WP08b's review.

**Question.** An ad at another frame rate after its discontinuity, which design §4.2 expects, follows neither the axis's frame duration nor a whole step where its frame isn't a whole number of PTS ticks, so it starts a stretch every few frames (the review's probe, not committed). How is it written?

- **1. The writer also tries the step the stretch's own first values give**, after the axis's frame duration. Only the writer changes; schema version 1, every value exact.
- **2. As built**, the limit stated in the format document.

**Agent's recommendation.** 1.

**Answered** on 2026-11-06: 1, as the agent recommends (`docs/DECISIONS.md` D-100 (b)).

#### Q57. Two frame durations that hold the same frames, and the scales the reader takes

*Round 1.* Raised by WP08b's review.

**Question.** Two choices that change a document's bytes, or what is refused, but no value read back: where the frame duration in the axis's ticks with scale 1 and the frame duration in PTS ticks with a scale hold the same frames, the one tried first (scale 1); and the reader taking any scale from 1 on both kinds of stretch, scale 1 alone on frames without a PTS. Are they kept?

- **1. Both as built.**
- **2. Each asked on its own.**

**Agent's recommendation.** 1.

**Answered** on 2026-11-06: 1, as the agent recommends (`docs/DECISIONS.md` D-100 (c)).

#### Q58. A sequence at another frame rate whose PTS were rounded to a coarser clock

*Round 1.* Raised by WP08c's review.

**Question.** An ad at 24000/1001, 30000/1001 or 60000/1001 fps whose PTS are on a millisecond clock, a frame of 1001/24, 1001/30 or 1001/60 ms, follows none of the steps D-100's writer tries over all its frames: D-100 (a)'s step is the axis's frame duration, and D-100 (b)'s steps, from windows of 2 to 8 of the stretch's first values, are exact only for a step whose denominator is at most 8. So the ad's keys and playlist-time ticks start a stretch every one or two dozen of its frames; the content's stretches, and those of an ad on the 90 kHz clock, are not affected (the review's probe, not committed). Every value still comes back exactly. How is it written?

- **1. As built**, the limit stated in the format document, as WP08c states it.
- **2. D-100 (b) also tries longer windows**, so that the steps of the common frame rates up to 60 fps on a millisecond or a 1/600 s clock, whose denominators go up to 100, are found, at up to that many steps per stretch. Only the writer changes; schema version 1, every value exact.

**Agent's recommendation.** 1: the case needs a coarser clock and an ad at another frame rate at once, the stretches grow with the ad's frames only, and the limit is stated; 2 has the writer try many more steps for every stretch to cover it.

**Answered** on 2026-11-06: 1, as the agent recommends (`docs/DECISIONS.md` D-101 (a)).

#### Q59. WP08c's other choices

*Round 1.* Raised by WP08c and its review.

**Question.** Choices that change a document's bytes, or what is refused, but no value read back:

- (a) D-100 (b)'s steps are tried for every window of 2 to 8 values, and, where the stretch's first differences share a factor, in units of it with that factor as the scale (an ad at 60000/1001 fps needs the scale 2 on the playlist-time ticks of 30000/1001 fps content), a combination neither part of D-100 states;
- (b) the factor is taken from a stretch's first 8 differences and, where D-100 (a)'s step stops at a value whose difference from the first it doesn't divide, refined to the factor the two share (the review's fix: from the first 8 alone, PTS on a 1/600 s clock at 24000/1001, 30000/1001 and 60000/1001 fps grew with their frames);
- (c) the order: the frame durations, the whole step, then D-100's steps, which are taken only where they hold more frames, so that a stretch that WP08b's steps hold keeps WP08b's form (the review's fix);
- (d) a step of D-100's that is a whole number isn't tried, and an axis without a frame duration keeps whole steps alone (D-099 (b));
- (e) at a value off every step, one of D-100's steps can hold it with the frames around it in a step no frame rate has (11263/3 over 4 frames in two tests of `tests/test_timeline_doc.cpp`), and among D-100 (b)'s steps the shortest window wins a tie;
- (f) D-092 (b) as built: a curve read back is placed at its pair's rank among the variants' numbers, where `attach_curves()` puts the report's curve of that pair; `build()` and `attach_curves()` refuse pairs out of the plan's order, and `build()` a `pairs[].index` beyond an int, neither of which the ladder report reader gives; a curve reference that names no pair is written null, which the reader refuses.

Are they kept?

- **1. All as built.**
- **2. Each asked on its own.**

**Agent's recommendation.** 1.

**Answered** on 2026-11-06: 1, as the agent recommends (`docs/DECISIONS.md` D-101 (b)).

#### Q60. A session fetch of local files

*Round 2.* Raised by WP07's review.

**Question.** Without `--fetch-base`, a session over a ladder scored from local files fetches those files, and NetworkLogger's row of a local file ends at its open: its first byte and its end are the open's end, and its bytes are those read (`src/NetworkLogger.cpp:498-509`; `src/SessionFetch.h`). A local segment's request then lasts as long as its open, whatever the file's size (the review's probe, not committed). Design §7 turns the fetch on whenever a session is given, and neither D-104 nor design §5.7 says what a fetch of local files measures. What does `timeline` do?

- **1. As built**, the format document saying that a local file's duration is its open's.
- **2. Refused**: exit status 2 when a session's files aren't http or https URLs and no `--fetch-base` is given, the message naming `--fetch-base` and `--fetch off`.
- **3. Skipped**, with a warning: the session has no network events.
- **4. The fetch times a local file's read itself**, its row ending after the read.

**Agent's recommendation.** 2: the network events then come from network requests alone, and an argument the fetch can't honour is refused, as D-099 (c) refuses arguments the document can't satisfy; under 4, rows that NetworkLogger didn't time would sit beside those it did.

**Answered** on 2026-11-10: 2, as the agent recommends (`docs/DECISIONS.md` D-108 (a)).

#### Q61. A switch between two spans of one entry

*Round 2.* Raised by WP07's review.

**Question.** Where two consecutive spans of a plan name the same entry, `session_events()` puts a variant_switch at their boundary whose two rungs are the same (`tests/test_session_plan.cpp` pins it), so the session's attributions list a player event where nothing changed. Design §5.6 puts a switch at each span boundary after the first, wording written for spans that change variant, and no decision covers the case. The drops already take such spans as one stretch, as D-077's clipping to the spans that played each variant reads. Is it kept?

- **1. As built.**
- **2. No switch where the entry doesn't change**; two entries that name one playlist still switch, the master's entry being what changes.
- **3. Such plans refused.**

**Agent's recommendation.** 2: an event that changes nothing can't explain a drop, and a plan may well split one entry's segments into spans.

**Answered** on 2026-11-10: 2, as the agent recommends (`docs/DECISIONS.md` D-108 (b)).

#### Q62. The session's other choices

*Round 2.* Raised by WP00b, WP07 and WP07's review.

**Question.** Choices that change a document's bytes, or what is refused:

- (a) a session whose files weren't fetched (`--fetch off`, design §7) has no UTC anchor, and its document writes `sessions[].wallclock_anchor_utc` null, which design §8 doesn't mark "| null";
- (b) the anchor's text is RFC 3339 UTC to the microsecond, with the zone `Z`, the unit its wall clock counts in (D-073);
- (c) a plan span through a segment the timeline doesn't have, not in its playlist or not placed by the build, refuses the whole plan, naming the span and the segment, as the build places nothing it can't place exactly (D-081); placing the span from its placed segments instead would need the playlist events in `fetch_files()`;
- (d) the plan file's layout beyond D-105 (a)'s spans: `schema` `"mirage-session-plan"`, `schema_version` 1, a `name`, which may be empty, and `spans`, every member required and any other refused, as strict as D-092 (d)'s document reader;
- (e) round 2's payloads, until WP09b's schema fixes them: a variant_span's rung flat (entry, uri, bandwidth, resolution and both ranks), with first_seq, last_seq and scored; a variant_switch's from and to as rungs, with its origin, its `Event::variant` the variant played from it, as design §6.4's prototype lists it; a request's url, range, status, status_text, bytes, start, first_byte, end, duration, failed, error, origin and its chain of rows; times in seconds from the session's start, null where a row didn't reach them;
- (f) `--fetch-base` takes each URL's path after the master's directory, as design §9.3's local server of the ladder's directory needs it, and a URL outside that directory refuses the whole fetch.

Are they kept?

- **1. All as built.**
- **2. Each asked on its own.**

**Agent's recommendation.** 1.

**Answered** on 2026-11-10: 1, as the agent recommends (`docs/DECISIONS.md` D-108 (c)).

#### Q63. Several messages of one signal in one segment, with no `EXT-X-CUE-OUT`

*Round 2.* Raised by WP05's review.

**Question.** D-106 (d) makes a message in the same segment as an `EXT-X-CUE-OUT` that cue's message. Where several cue-out messages carry one signal in one segment and there is no plain `EXT-X-CUE-OUT`, each is a cue_out of its own, and `pair_breaks()` makes an ad_break of each (D-087), with no warning: an OATCLS and an `EXT-X-DATERANGE` of one splice_insert, for example, or the `EXT-X-DATERANGE` tags that RFC 8216 §4.3.2.7.1 writes for one section with several segmentation types, each carrying the whole section (the review's probe, not committed). How are they placed?

- **1. As built.**
- **2. One cue_out per signal**: the cue-out messages of one variant and one segment that share their section's bytes, their splice_event_id or their segmentation_event_id make one cue_out, and one splice_point per splice time; the others' records go into its payload.
- **3. One cue_out per segment**, whatever its messages.

**Agent's recommendation.** 2: it is D-106 (d)'s reason, one signal written twice, where two cue-outs would claim two breaks; 3 would also merge two signals that only share a segment.

**Answered** on 2026-11-10: 2, as the agent recommends (`docs/DECISIONS.md` D-109 (a)).

#### Q64. An `EXT-X-DATERANGE` with both SCTE35-OUT and SCTE35-IN

*Round 2.* Raised by WP05's review.

**Question.** RFC 8216 §4.3.2.7.1 maps a splice out/in pair to one or more tags of one ID, so one tag may carry both messages. Such a tag hands on its two messages at one line, in the tag's order; with SCTE35-OUT first, that is a cue_out and a cue_in at one frame, of which `pair_breaks()` makes an ad_break of no frames, with no warning, and the tag's DURATION ends nothing (the review's probe, not committed). Under D-078 a span of no frames meets an interval only where the interval starts before its frame and ends after it, so a drop that starts with the ad, with no margin, lists the tag's cue_out and cue_in but not their ad_break. How is it placed?

- **1. As built.**
- **2. The SCTE35-IN in the tag of its SCTE35-OUT makes no cue_in of its own**: its record goes into the cue_out, and the break ends at the tag's DURATION or END-DATE, by D-076.
- **3. Its cue_in placed at its END-DATE**, or START-DATE plus DURATION, through the playlist's `EXT-X-PROGRAM-DATE-TIME`.

**Agent's recommendation.** 2: the tag declares its break's duration, which D-076 then uses; 3 places an event by its date, which D-106 (a) chose not to do.

**Answered** on 2026-11-10: 2, as the agent recommends (`docs/DECISIONS.md` D-109 (b)).

#### Q65. A message repeated in each `EXT-X-CUE-OUT-CONT`

*Round 2.* Raised by WP05's review.

**Question.** Some packagers repeat a break's message in each `EXT-X-CUE-OUT-CONT`'s SCTE35= (MediaLive's "SCTE 35 enhanced" output and Wowza, `eval/phase3-survey/scte35.md` §2.2). Every decoded message with a splice time gives a splice_point (design §5.5), so each repeat gives one at the same frame, each with the warning of a splice time outside its tag's segment, or outside its sequence in a stitched ad (the review's probe, not committed). Is it kept?

- **1. As built.**
- **2. A CUE-OUT-CONT's own message makes no splice_point and no anchor warning**; it is still decoded into that tag's payload.
- **3. One splice_point per variant, splice time and event id**, whatever carries it.

**Agent's recommendation.** 2: a CONT's message repeats its break's, whose splice time the break's own message already places, and a repeat's splice time lies before its tag's segment by its nature, so the warning tells nothing.

**Answered** on 2026-11-10: 2, as the agent recommends (`docs/DECISIONS.md` D-109 (c)).

#### Q66. Message durations that no decimal holds

*Round 2.* Raised by WP05's review.

**Question.** A message's break_duration and segmentation_duration count 90 kHz ticks; the payload writes them in seconds, as numbers, which `pair_breaks()` reads as the decimal their shortest text gives (`exact_seconds()` in `src/PlaylistEvents.cpp`). A tick count that 9 doesn't divide has no exact decimal, so a cue_out whose only duration is such a value, with no cue_in, makes no ad_break, with the warning that no AVRational holds it, against D-076's order of durations. For a whole number of frames at 24, 30 or 30000/1001 fps, a frame of 3750, 3000 or 3003 ticks, each with one factor of 3, that is every count that isn't a multiple of 3 (`tests/test_scte35_events.cpp` pins the case). How is it written?

- **1. As built**, the limit stated in the format document.
- **2. A member `<name>_ticks` beside each such duration**, its 90 kHz ticks as a whole number, which `pair_breaks()` reads before the seconds; the seconds stay as they are.
- **3. No new member**: `pair_breaks()` reads the seconds back as the nearest whole number of 90 kHz ticks.

**Agent's recommendation.** 2: every value stays exact and D-076's order holds for every duration; 3 would guess the clock from a number.

**Answered** on 2026-11-10: 2, as the agent recommends (`docs/DECISIONS.md` D-109 (d)).

#### Q67. WP05's other choices

*Round 2.* Raised by WP05 and its review.

**Question.** Choices that change the events, or what is refused:

- (a) the levels of SCTE 35's hierarchy of advertising segments (§10.3.3.7), a Break above its Placement Opportunities and Ad Blocks, count alike: under D-087's rule a Break that declares a duration then makes an ad_break around those of the levels below it, where D-087 says that no ad_break spans another, and a Break that declares none makes no ad_break, with a warning, its Break End ending the break of the advertisement open at it; and of several segmentation descriptors of a cue's role, the first that gives a duration, in the loop's order, declares its segmentation_duration;
- (b) base64 only with its padding (RFC 4648), and a value that starts with 0x or 0X read as hex;
- (c) a section whose table_id isn't 0xFC or whose protocol_version isn't 0 left undecoded, and a message that doesn't decode a warning only, with no event;
- (d) a time_signal with an End and a Start type gives a cue_in, then a cue_out, at one frame, and a cue_in that joins an `EXT-X-CUE-OUT` takes that cue's line where its own is later or null, so that it pairs whichever of the two tags comes first;
- (e) only cue-out messages merge into a plain cue of their segment, not a cue-in message beside an `EXT-X-CUE-IN`;
- (f) a splice time before its sequence's first frame is outside the sequence, and a media PTS with no segment is searched only where the playlist has one sequence;
- (g) none of a malformed `EXT-X-DATERANGE`'s other attributes is read beside its well-formed SCTE35-* ones (D-106 (e)), and an SCTE35-IN's in point comes from its own tag alone, its END-DATE or its START-DATE plus DURATION, none taken from an earlier tag of its ID;
- (h) whether each type of SCTE 35's Table 23 starts or ends a break is taken from the type's name;
- (i) the payloads' layout and member names (`src/Scte35Events.h`).

Are they kept?

- **1. All as built.**
- **2. Each asked on its own.**

**Agent's recommendation.** 1. Of these, (a) is the closest: the alternatives, time_signal cues paired by segmentation_event_id within their level, or cues from the outermost level present alone, the inner levels' messages becoming `message` events, would pair otherwise than D-087's rule does, or need a rule for which level is outermost where breaks differ.

**Answered** on 2026-11-10: 1, as the agent recommends (`docs/DECISIONS.md` D-109 (e)); its (a) is recorded there as an exception to D-087.

## Differences from the work order, to report

The design lists 14 differences between what Phase 3 would build and what the work order's §3 asks (`eval/phase3-survey/design.md` §11). None of them is removed by editing a document, as the work order's constraints require: each is reported here and in the description of Phase 3's PR (WP15c, in the wording WP15b settles), and both pass the quote check before they are committed or posted (P-AK). Each names the question that settles it; "Q42" marks those that no option of Phase 3 removes, which Q42 asks the owner how to handle.

1. **The network component's name.** The work order calls it NetworkDebugger, an `av_log` interceptor; the code's `NetworkLogger` also hooks `io_open`/`io_close2` and is built into the player and `mirage_tests` only (`eval/phase3-survey/sources.md` §10, item 1). Under Q10 (a) it is compiled into `mirage_vmaf` and times a fetch, not a playback (Q10); how the two names are handled is Q41.
2. **ABR and the player's switches.** Mirage has no ABR (D-068); switches come from scripted plans, and the player's log is Q13 (Q42).
3. **The decoder's facts.** The offline decode drops no frame of its own; frames a decoder never output are the report's unpaired frames, filed under `vmaf`, where §3.1 groups dropped frames with the decoder (Q04 (a), Q43 (d)); frames the player drops at a switch are not in Phase 3's scores (D-068's limits), and under Q13 (a) Phase 3 has no decoder events for them either; decode errors only under Q09 (a) or (b) (Q09, Q13, Q42).
4. **The key.** PTS alone repeat (`eval/phase3-survey/design-evaluation/runs/09_axes.txt`); events sit on reference frames with (sequence, PTS) as their key, and offsets are seconds of the axis, the reference's playlist time under Q02 (c) or frames times the frame duration under (d), not PTS differences (Q02).
5. **The wall clock.** Only observed events have one (Q03 (b)), so offline events, all of an acceptance test's among them, have none (Q03).
6. **Network events need a session.** A ladder built without a session has no network events, both acceptance tests included (Q10; Q10 (e) would add scoring-paced ones).
7. **DASH.** `mirage_vmaf` scores HLS ladders only; DASH's period boundaries are a reserved type and its `EventStream` an interface, as D-070 has it (Q20).
8. **Splice points.** Several published messages carry no splice time, and the HLS-only tags none, so a cue's place is often only its tag's position (`eval/phase3-survey/scte35.md` §9, item 3); splice points are their own events where a time exists (Q14).
9. **D-070's list** omits `#EXT-X-SCTE35`, `#EXT-X-SPLICEPOINT-SCTE35` and Adobe's `#EXT-X-CUE` (Q18); the one public VOD stream found carries SCTE-35 in its segments as ID3, not as stream type 0x86 (`eval/phase3-survey/scte35.md` §9, item 2).
10. **Rung numbers.** The example's switch between numbered rungs assumes a numbering the report doesn't have (Q07).
11. **The top rung in top-rung mode** has no score, so a session's spans on it have none, and an ad low in every rung can't show as a drop in that mode (Q12; D-002's limit; Q42).
12. **The HTTP status** isn't exported by FFmpeg 8.1.1's API ([http.c#L180-L206](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/http.c#L180-L206)); it comes from `av_log` text through `NetworkLogger`, or as a class from a failed open (Q10, Q42).
13. **§7's single output, and §7's attribution shape.** Under Q01 (B) the timeline and attributions are a separate document; attributions group events by source, give offsets and carry the detection parameters, beyond §7's flat list; `tool_version` and `git_commit` wait for Phase 5 under Q05 (a) (Q01, Q04, Q05).
14. **§3.3's seek from a clicked low point** doesn't reach the frames of an ad whose timestamps restart at 0.040 s with FFmpeg 8.1.1 (measured: `eval/phase3-survey/ui.md` §2.4, from `experiments/hls-seek-probe/run-output.txt`); an ad that restarts at the main content's origin, as D-069's does, was not probed (MEASUREMENT PENDING) (Q37).

A repository document found wrong, reported and not edited: `docs/VMAF_GAP_REPORT.md:230-231` says the Decoder has a dropped-frame counter; it is the render loop's `total_dropped` (`src/main.cpp:1751`; `eval/phase3-survey/sources.md` §12) (Q42; under Q42 (b) WP15b adds a dated correction line to it).

## The agent's proposed process choices, to be recorded from P-082 on

28 choices of process and measurement: how something is built, tested or measured, with no effect on what an output says by default (`eval/phase3-survey/design/p_entries.json`; `eval/phase3-survey/design.md` §13). They are the agent's, pending the owner's confirmation. Each is recorded in `docs/DECISIONS.md`, from P-082 on, when the first package that uses it starts, marked pending until the owner confirms it, and later lines are only added (D-060; P-081 says why not all at once); the design's letters are its own.

| | Choice | Alternatives |
|---|---|---|
| **P-A** Exact arithmetic | Event positions are reference frame indices (int64). PTS are int64 ticks of their stream's time base, recovered from the report's seconds by rounding seconds x den / num and checked against the demux pass's packet PTS (`eval/phase3-survey/design-evaluation/runs/04_join_check.txt`: every frame agreed). Seconds come from libavutil's rational arithmetic (AVRational, av_rescale_q_rnd), as VMAFAlign already uses it (`src/VMAFAlign.h:9-11`), and are turned into doubles only when written with json::number. | A Rational type of the module's own with 128-bit intermediates (`eval/phase3-survey/design-library.md`, P-B); seconds as doubles with tolerances, which listed boundary events inconsistently (`eval/phase3-survey/design-evaluation/runs/07_attribute_seconds_as_doubles.txt` and `08_attribute_ticks.txt`; `eval/phase3-survey/design-library/runs/axis-ad-float.txt`, `axis-degraded-float.txt`). |
| **P-B** The timeline core's dependency rule | The core files include no libavformat, libavcodec, libvmaf, GL or ImGui header; libavutil is allowed, as VMAFAlign uses it. The adapters (TimelineMedia, SessionFetch) are the only code that opens inputs, and each returns plain structs that a test can build by hand. | A core with no FFmpeg at all and its own CRC, base64 and rationals (`eval/phase3-survey/design-library.md`, P-A, P-H). |
| **P-C** Build wiring | Per-target source lists, as `CMakeLists.txt` has them (150-194, 233-246, 264-329). `CMakeLists.txt` is changed only by the wiring packages, one commit per round (WP00a, WP00b, WP00c), each adding, as empty compiling files with their lines, the files whose existence that round's answers or the owner's decisions settle, once those answers are in, and by one wiring commit the lead makes when a conditional package (WP16, WP17) starts; no other package edits it, so parallel packages never conflict there. The edits come one at a time: each wiring package depends on the one before it, and the lead's commit for a conditional package is made while no wiring package is open. | A CMake OBJECT library linked into the three targets (`eval/phase3-survey/design-library.md`, P-A), a pattern the build doesn't use yet; one skeleton commit for every round's files at once, which would wire files whose existence later answers decide; each package editing `CMakeLists.txt`, which the lead then merges. |
| **P-D** Media playlists read again at build time | The timeline build reads each scored media playlist with vmafladder::read_master (any #EXTM3U text, the analyzer's network limits, `src/VMAFLadder.cpp:293-334`) and writes its URL, location and SHA-256 into the document; the ladder report is not changed for it. | The ladder report recording each pair's media-playlist SHA-256s, a report change that is the owner's to make (it breaks `tests/test_vmaf_report.cpp:909`'s exact key count); tapping the bytes the hls demuxer read (`eval/phase3-survey/sources.md` §11, choice 3 (b)), whose handle avformat_open_input probes. |
| **P-F** The demux pass | Each scored playlist is opened as DecodeCtx::open() opens an input (the interrupt callback, vmafnet::set_io_limits, vmafsegments::set_hls_options, avformat_find_stream_info, the best video stream; `src/VMAFAnalyzer.cpp:166-194`), with an io_open/io_close2 recorder of its own (each file in open order with its bytes_read at close), and its packets are read with av_read_frame without decoding. | A decode pass (Q09 (b)); extending SegmentWatch, which changes a tested header (`src/VMAFSegments.h`). |
| **P-G** Frames to segments | A packet's segment is the one whose bytes, counted in open order with the playlist's own open left out, hold its AVPacket::pos; an input's frames are each segment's packets in PTS order, the segments in read order; every scored frame's PTS is checked against the report's. What a mismatch costs is Q43 (c). | PTS against EXTINF sums (145 and 193 of 200 right, `eval/phase3-survey/design-library/runs/map-by-pos-*.txt`); segment heads through hls::parse_segment_start (`eval/phase3-survey/sources.md` §11, choice 2 (c)). |
| **P-I** Canonical order and deterministic ids | Events are ordered by (ref_index, ref_end, source, type, variant, id); an id is a string made of the source, the type, the variant and a per-type index; identical inputs give byte-identical documents apart from measured fields (the fetch's times and wall clock). | Insertion order; integer ids, which change when another source's events are added. |
| **P-J** The overlap index | A vector of events sorted by start with a running maximum of ends, built once per timeline, and checked against brute force on random events in the tests. | An interval tree; the documents hold one event per segment and per tag (32 to 44 events for the prototype's 200-frame ladders, `eval/phase3-survey/design-evaluation/runs/11_doc_sizes.txt`). |
| **P-L** SCTE-35 decoding with libavutil | The CRC_32 is checked with av_crc(av_crc_get_table(AV_CRC_32_IEEE), UINT32_MAX, section, length) == 0, as FFmpeg's MPEG-TS demuxer checks sections ([mpegts.c#L463](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/mpegts.c#L463); `eval/phase3-survey/scte35.md` §3.6); base64 through av_base64_decode; hex parsed in the module; the decoder never reads past its input; an encrypted message (encrypted_packet 1) is reported undecoded. | A constexpr CRC table of the module's own, tested against av_crc (`eval/phase3-survey/design-library.md`, P-H). |
| **P-M** SCTE-35 test sections | The tests build each section from field values with a writer in the test, which computes its CRC. The published messages the scte35 survey collected, which stay outside the repository until Q21 is answered, and `eval/phase3-survey/scte35.md`'s reference decoder (`eval/phase3-survey/scte35.md` §3.7) serve as a cross-check while the decoder is written. An optional test case reads a local vectors file named by the MIRAGE_SCTE35_VECTORS environment variable and is skipped when it is unset; nothing third-party is committed unless Q21 allows it. | Committing third-party vectors (Q21). |
| **P-N** The session fetch | After the demux passes, the build fetches the session's files: the master first, then each span's files in play order, with the pacing and the playlist fetches of Q44 (f) and (g), one connection per file and the analyzer's network limits (D-036). NetworkLogger is installed for the fetch only and uninstalled after it, which restores FFmpeg's default callback (`src/NetworkLogger.cpp:179-185`); no other code installs a callback (`eval/phase3-survey/design/runs/log-callback-grep.txt`). Each file's rows are copied with copy_changed_since() and the logger cleared after its close (`src/NetworkLogger.h:157-160`), so the 4000-row limit (`src/NetworkLogger.h:194`) evicts nothing; rows are tagged NetInitiator::Playback; times are steady microseconds from the session's start plus one UTC anchor; fetch URLs are the report's, or the same paths rebased onto --fetch-base. run_timeline() and run_attribute() set FFmpeg's log level and initialize its network as `tools/mirage_vmaf.cpp:155-156` does, since they are dispatched before those lines. Retries, redirects, pacing and playlist fetches, which change the network events, are Q44 (c), (d), (f) and (g); revision 2 moved the last two out of this entry. | NetworkLogger installed for the whole process; a new initiator value, which changes `src/NetworkLogger.h`; the rows copied once at the end, which could lose a long session's first requests to the row limit. |
| **P-O** A delay rule and a redirect rule in `tests/HttpFileServer.h` | The test server gains a rule that holds a path's response head for a set time, and one that answers a path with a redirect to another, keeping its hold and cut rules (`tests/HttpFileServer.h:31-36`). | A second server class. |
| **P-Q** The synthetic ladders' generator, and how often it runs | testgen's prototype (`eval/phase3-survey/testgen/src/testgen.cpp`) is ported to `tests/SyntheticLadder.{h,cpp}`, compiled with optimization in every build type, since generation took 16.941 s unoptimized against 2.022 to 2.313 s optimized (`eval/phase3-survey/testgen.md` §3.4); each ladder is made once per process, as `tests/test_vmaf_ladder.cpp:192-216` makes its fixtures; the generator's layout is the ground truth. ctest runs each Catch2 test case in its own process (`eval/phase3-survey/review-adversarial/runs/ctest-registration.txt`), so the acceptance cases are sections of one test case, which makes each of its ladders once per ctest run; the composite example and WP10's determinism check are test cases of their own, for seven generations per ctest run under Q34 (d) (`eval/phase3-survey/design.md` §9.5). | A CTest fixture (FIXTURES_SETUP, with FIXTURES_REQUIRED set on the acceptance tests through the TEST_SPEC and PROPERTIES arguments of Catch2 v3.7.1's catch_discover_tests, https://github.com/catchorg/Catch2/blob/v3.7.1/extras/Catch.cmake#L29-L34) that writes the ladders once per ctest run into the build tree: fewer generations, but one directory that two ctest runs of a build tree would share, against the per-process rule of D-032 and P-036; the prototype as is; a generation per test case. |
| **P-R** Acceptance assertions independent of open choices | The acceptance tests assert reference frame ranges, URIs, media sequence numbers, event types and offsets in frames, with detection parameters pinned inside the measured region (T = 6, W = 50, N = 12, the prototype's values, inside `eval/phase3-survey/design-evaluation/runs/05_detection_sweep.txt`'s region), not the tool's defaults; the seconds of the axis the owner picks are checked in WP01's unit tests. | Assertions on the defaults, which wait for Q26 and change with it. |
| **P-S** The composite example's delay | In ctest the composite example uses a delay of its own, under the analyzer's 5 s network limit (D-036), and asserts a measured duration at least the delay and status 200; the work order's 2.3 s is used only in the eval harness. | 2.3 s in ctest. |
| **P-T** Tests that run programs, and test tags | The acceptance and CLI tests run mirage_vmaf as a program with posix_spawn through MIRAGE_VMAF_PATH, as `tests/test_vmaf_ladder.cpp:286-310` does; the new tests are tagged [timeline], [scte35], [session], [attribution] and [acceptance]. | In-process calls only, which skip the CLI and the files a user gets. |
| **P-U** Keeping the media | With MIRAGE_KEEP_MEDIA=<dir> the acceptance and composite test cases keep their ladders, reports and documents, each test case under <dir>/<its name>/, so that two test processes under ctest -j never write one directory; the eval harness and the UI's scripted QA run read them there (`eval/phase3-survey/ui.md` §5.3 asks for such a switch). | A generator program under tools/; one shared directory, which two processes would write at once. |
| **P-V** The subcommands' option tables | Each subcommand has an option table of its own in TimelineCli, built as vmafcli's is (one row: the option, its config setting, its help; `src/VMAFCli.cpp:26-35, 92-124`), with D-030's name = value config file, one per invocation, recorded by path and SHA-256, the command line replacing the file; vmafcli's parser is unchanged, since its parse() and read_settings() are written against its own kOptions and Args (`src/VMAFCli.cpp:195-257, 273-344`). | vmafcli's parser made generic for the subcommands (`eval/phase3-survey/design-library.md`, P-G), which changes tested code. |
| **P-W** Exit statuses and writing | The subcommands exit as mirage_vmaf does (`tools/mirage_vmaf.cpp:7-17`): 0 written, 1 a failure (an unreadable input, a hash mismatch, an input refused under Q43 (a) or (b), no document left at --out), 2 bad arguments or config file; documents are written through a temporary file renamed over --out, as vmafreport::write_file does (`src/VMAFReport.h:149`). | None proposed. |
| **P-Y** The JSON reader's form | A document tree with objects kept in their order, duplicate keys refused (as the tests' reader refuses them, `tests/test_vmaf_report.cpp:104`), a nesting limit, and numbers read with std::from_chars so that they round-trip with json::number; its memory and time on the largest committed ladder report are measured before the panel loads such reports. | A streaming reader from the start (Q32 (b)). |
| **P-Z** Hashes, and the digest against redaction | Hashes are computed at the edges with filehash::sha256_hex; the core returns the canonical text to hash; the scores digest leaves out every field the eval scripts redact (`eval/ladder/pool_check.py:117-118`, `eval/consistency/ladder_check.py:25-27`), which a test checks by redacting a report as they do. | Hashing inside the core. |
| **P-AD** The attribute subcommand's text layout | One block per drop: its curve, reference frames, interval in seconds, baseline, minimum, mean and magnitudes; then one line per event, grouped by source in §3.1's order, with its offsets in seconds and in frames, as `eval/phase3-survey/design-evaluation/runs/10_attribute_final.txt`'s prototype printed it. Which format is the default is Q31 (a). | JSON only. |
| **P-AF** The evaluation harness's records | `eval/timeline/` in standard-library Python, each record with cmd.txt and environment.txt that give the commit by git rev-parse HEAD, as P-006's practice is. | None proposed. |
| **P-AG** Shared declarations | WP00a writes the declarations that round-1 packages running in parallel share, once the answers they encode are in (Q02, Q03, Q04, Q07), and WP00b those of the sessions (Q11, Q12); each module's own functions and option structs are written by the package that implements it. After each, a change to a type another package uses goes through the lead, who merges it before the packages that need it. | Each package declaring what it needs, merged later; the declarations written before the owner's answers, which would present the agent's recommendations as made. |
| **P-AH** Worktrees and merges | Each package is built in a worktree branched from vmaf/phase-3 (D-066) and merged by the lead in `eval/phase3-survey/design.md` §14's order; each round's answers are recorded by the lead in `docs/DECISIONS.md` and marked in `docs/PHASE3_DECISIONS_PENDING.md` before that round's packages start; nothing is pushed until the owner says. | None proposed. |
| **P-AI** Checking the schema file in ctest | A subset JSON Schema validator in the test (type, required, properties, items, enum, additionalProperties) checks the writer's output against `docs/schema/mirage-timeline-1.schema.json`; no dependency is added. | A Python validator, a new dependency for standard-library-only eval tooling (`eval/phase3-survey/schema.md`, open issue 3); key tests only, leaving the schema file unchecked. |
| **P-AJ** Axis helpers copied, not moved | TimelineView gets its own copies of nice_step and axis_label (`src/NetInspectorAlign.cpp:69-107`, in an anonymous namespace that ends at :109), and `src/NetInspectorAlign.cpp` stays unchanged. | Moving them into TimelineView and changing NetInspectorAlign.cpp to use them (`eval/phase3-survey/ui.md` §2.5): one definition, but a change to a working file for no behaviour. |
| **P-AK** The quote check of documents and posted text | Before each commit of a Phase 3 document, and of the survey and the design when they are committed, and before Phase 3's PR description or any other text about Phase 3 is posted, the agent checks every file and text against the work order's translation, which stays outside the repository (D-013), for runs of six or more words in common; the checker's output, which names files, lines and counts but no shared words, is kept as the record of the commit or the posting, and its prose runs must be 0 (`eval/phase3-survey/design/runs/quote-check.txt` is the first). | Asking the owner whether short quotations are within D-013, which D-060's acceptance of a rendering with none quoted already answers; no check. |

9 more were moved into questions by the design's first revision, because each changes what an output says by default:

- **P-E** Live media playlists refused: Q43 (a).
- **P-H** Placement rules: Q43 (d) and Q44 (b).
- **P-K** Where a tag belongs, and one cue per signal: Q44 (e); its first half, a tag belonging to the segment whose URI follows it, is RFC 8216's rule (`eval/phase3-survey/design.md` §5.1).
- **P-P** Segment-range plans follow one another: Q44 (a).
- **P-X** The ladder report reader's scope: Q43 (b).
- **P-AA** The rank rule: Q43 (e).
- **P-AB** Events in several variants' playlists: Q43 (f).
- **P-AC** Provenance copied into the document: Q04 (a), since the ladder report's provenance copied into the document is part of `eval/phase3-survey/design.md` §8's layout.
- **P-AE** A drop's interval, and intervals given in seconds: Q43 (g).

## The work packages the questions block

25 packages (`eval/phase3-survey/design/work_packages.json`; `eval/phase3-survey/design.md` §14). A package's round is the round of answers it waits for, 0 meaning none; "Blocked by" lists the questions whose answers change what it builds, as the design gives them.

| WP | Title | Round | Depends on | Blocked by |
|---|---|---|---|---|
| WP15a | The pending-decisions document and the UI plan | 0 | none | none |
| WP00a | Wiring and shared declarations, round 1 | 1 | none | Q01, Q02, Q03, Q04, Q05, Q07, Q08, Q20, Q31, Q32, Q43 |
| WP01 | Timeline core: model, axis and the overlap query | 1 | WP00a | Q02, Q28, Q29, Q30, Q43 |
| WP02 | Drop detection on a curve | 1 | WP00a | Q22, Q23, Q24, Q25, Q26, Q27, Q43 |
| WP03 | JSON reader and ladder report reader | 1 | WP00a | Q06, Q32, Q43 |
| WP04 | Playlist events, the plain cue tags, and the pairing of every cue | 1 | WP00a | Q04, Q17, Q18, Q19, Q20, Q43 |
| WP06 | Media ingest: playlists, the demux pass, frame-to-segment maps | 1 | WP00a | Q08, Q43 |
| WP08 | The timeline document and its JSON Schema: the ladder part | 1 | WP01, WP03 | Q01, Q02, Q03, Q04, Q05, Q06, Q07, Q22, Q23, Q24, Q25, Q26, Q27, Q28, Q29, Q30 |
| WP09a | The builder and the CLI subcommands, for a ladder | 1 | WP01, WP02, WP03, WP04, WP06, WP08 | Q01, Q02, Q04, Q06, Q07, Q08, Q19, Q20, Q22, Q24, Q26, Q27, Q28, Q29, Q30, Q31, Q43 |
| WP10 | Synthetic ladders (D-069's content) | 1 | WP00a | Q33, Q34, Q35 |
| WP11a | §3.2's two acceptance tests | 1 | WP09a, WP10 | Q01, Q02, Q04, Q22, Q23, Q25, Q29, Q31, Q33, Q34, Q35 |
| WP00b | Wiring and shared declarations, round 2 | 2 | WP00a, WP01 | Q10, Q11, Q12, Q41, Q44 |
| WP05 | SCTE-35 decoder and message events | 2 | WP00a, WP04 | Q04, Q14, Q15, Q16, Q18, Q21, Q44 |
| WP07 | Sessions, their drops, and the session fetch | 2 | WP00b, WP01, WP02, WP03, WP06 | Q03, Q04, Q07, Q10, Q11, Q12, Q22, Q28, Q41, Q44 |
| WP09b | Sessions in the build, the CLI and the document | 2 | WP09a, WP07 | Q01, Q03, Q04, Q05, Q07, Q10, Q11, Q12, Q22, Q44 |
| WP09c | Decoded SCTE-35 messages in the build and the CLI | 2 | WP09b, WP05 | Q15, Q16 |
| WP11b | The composite example (test 3) | 2 | WP09b, WP10 | Q01, Q04, Q07, Q10, Q11, Q22, Q23, Q25, Q44 |
| WP00c | Wiring, round 3: the UI | 3 | WP00a, WP00b | Q36 |
| WP12 | UI loading and math, ImGui-free | 3 | WP00c, WP01, WP03, WP07, WP08, WP09b | Q01, Q02, Q05, Q06, Q36, Q37, Q39 |
| WP13 | The timeline panel in the player | 3 | WP12, WP01, WP02, WP08 | Q36, Q37, Q38, Q39, Q40 |
| WP14 | The evaluation harness: reproduction from one command | 3 | WP09a, WP09b, WP10, WP11a, WP11b | Q02, Q04, Q07, Q10, Q22, Q23, Q25, Q31 |
| WP15b | The differences' wording and the gap report's correction | 3 | WP15a | Q41, Q42 |
| WP15c | The closing documents and the PR description | 3 | WP09c, WP13, WP14, WP15b | none |
| WP16 | Conditional: what the scorer records, its extra pair, and a decode pass | 3 | WP03, WP06, WP09c | Q01, Q08, Q09, Q10, Q12 |
| WP17 | Later: the player's session log | 3 | WP07, WP13 | Q11, Q13 |
