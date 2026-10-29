# Phase 2: decisions pending for the owner

What is open for the owner on `vmaf/phase-2`, §2 of the VMAF work order, after the decisions of 2026-10-26 on the survey (`docs/DECISIONS.md` D-042 to D-049) and of 2026-10-27 on what the measurements found (D-050 to D-053), and after the audit of both branches of 2026-10-28, which the owner asked for (`docs/AI_CONTRIBUTIONS.md`), and the owner's answers of 2026-10-29 to the questions it raised (D-054 to D-061). The decisions are the owner's (hard rule 3 of the work order); every recommendation below is labeled as the agent's. Every number comes from a committed output, named next to it; where there is none, it says MEASUREMENT PENDING. The state of each item of §2 is in `docs/PHASE2_STATUS.md`.

## Open

Nothing is open for the owner's decision: on 2026-10-29 the owner answered the questions of the audit of 2026-10-28 and confirmed P-067 to P-077 (`docs/DECISIONS.md` D-054 to D-061), in "Decided on 2026-10-29" below; the questions of 2026-10-27, decided, follow those.

**For information.** With a memory limit, once the first pairs had ended the others ran one at a time in each run at 1 GiB over 300 frame pairs but the first, in which all 7 started at once (`eval/ladder/advanced-local-300-mem1g-1cc1ab0/`), and in the run over every frame with D-051's 10%; in the run over every frame with D-053's 25%, the next two pairs ran together once the first two had ended, and only the last three one at a time (`eval/ladder/advanced-cdn-full-b706ed3/README.md`). The report's `pair_memory` at 300 frames was more than three times one pair's peak, so at times the resident memory still counted memory of pairs that had ended (`eval/ladder/advanced-local-300-mem1g-b706ed3/README.md`; P-074). Which of the process's memory measures drops when a pair ends, and when, is MEASUREMENT PENDING.

### The reviews' asks that touch decisions (for information)

The audit's reviews, a video engineer's and a UI designer's, asked for changes that touch decided items (the audit's report, which isn't committed, §8.4 and §9). Options are written for them only if the owner asks:

- a forward jump in one input taken as a gap: pairing by time, with a new libvmaf context, and the 10 s threshold an option (§8.4, rank 1; D-046, D-050);
- the temporal origin of inputs at different frame rates checked, by a search for the offset (§8.4, rank 2; D-028);
- the pool's default size from the cores and libvmaf's threads, with `--threads` and `--subsample`, and M4 measured (§8.4, rank 8; D-044);
- the memory measured per pair, in a process of its own, or presented as a heuristic (§8.4, rank 7; D-045, D-051, D-053, and the question on `pool.pair_memory` below, which D-058 decided);
- HDR and Dolby Vision inputs refused unless forced (§8.4, rank 9; D-021);
- a report with `schema_version`, `tool_version` and `git_commit` (§8.4, rank 9; §9.2, item 3; §9.4; the work order's §7).

## Decided on 2026-10-29 (D-054 to D-061)

The owner's answers to the questions of the audit of 2026-10-28, and the confirmation of P-067 to P-077 (`docs/DECISIONS.md` D-054 to D-061: D-054 adopted every recommendation labeled as the agent's here as of `d9c690a`), each item with the text it had when it was decided, as the record of its options, and a note at its start on what was decided.

### A segment the hls demuxer skips (D-036; P-066)

**Decided** (2026-10-29): D-055, options 2 and 3: the analyzer gives FFmpeg's hls demuxer `seg_max_retry` 10, as the player's demuxer has it, and a segment the demuxer still goes on without fails the pair; option 4 later.

**What the code and the records show.** D-036 gives each network read 5 s (`src/VMAFNet.h`). When a segment's request waits longer, FFmpeg's hls demuxer goes on to the next segment: its `seg_max_retry` is 0 unless set (`ffmpeg -h demuxer=hls`), and the analyzer sets none (`DecodeCtx::open()` in `src/VMAFAnalyzer.cpp`), where the player's demuxer sets 10 (`src/Demuxer.cpp`). D-036's option 2 said a failed read makes the demuxer skip the segment (`docs/PHASE1_DECISIONS_PENDING.md`, "A network read without a time limit"). The read returns no error, so `vmafreport::incomplete()` finds nothing wrong: the pair ends complete, with exit 0, and so does a ladder run. In D-049's run over every frame from Apple's CDN a read timed out once, and FFmpeg's retry with a new connection recovered it (`eval/ladder/advanced-cdn-full-af17121/jobs-default/stderr.txt`, lines 8 to 10).

**Why it matters.** A segment lost in one input shows only as frames that don't pair, which warn above 1% (D-028); since each input's times are measured from its own start (D-028), a first segment lost in one input would offset every pair of it. A segment lost in both inputs at the same place leaves no failure: the frames on either side pair, in one libvmaf context across the hole. No committed record has a skipped segment; what one does to a pair's scores is MEASUREMENT PENDING.

**Question.** What should a skipped segment do to a pair, and to a run?

**Options.**

1. As now, D-036's option 2: the segment is skipped, and only the pairing's counts can show it.
2. Retries, as the player's demuxer has them: `seg_max_retry` 10 on the analyzer's HLS inputs, so that a segment is tried again before it is skipped. This reopens D-036, which chose option 2 over the player's options; FFmpeg's side of the §5 check would still skip.
3. A skipped segment as a read error: the pair is incomplete, so it fails, and a ladder run fails (D-047). FFmpeg reports the skip only in its log, so the analyzer would catch it there, or in the demuxer's `io_open` callback, through which every segment is opened.
4. A gap in both inputs' timestamps at the same place reported: counted and warned of in the pairing's record, with a new libvmaf context after it.

**Agent's recommendation.** Options 2 and 3 together: a segment is tried again as the player tries it, and one that is still lost fails the pair, so that no report is complete without it; option 4 later, for gaps that are in the source itself. Tests on a local server that holds a segment in the middle and a first segment follow the decision.

### The options of an rtmp input (D-036; P-066)

**Decided** (2026-10-29): D-056, option 2.

**What the code says.** `vmafnet::set_io_limits()` gives every http(s), rtmp and rtsp URL FFmpeg's `timeout` and `rw_timeout`, 5 s (`src/VMAFNet.h`), the rule of `src/FfOpen.cpp`, which `src/Demuxer.cpp` follows; a test asserts both values for an rtmp URL (`tests/test_vmaf_analyzer.cpp`, "vmafnet: network URLs get the read limits, local inputs none"). For FFmpeg 8.1.1's own rtmp protocol, `timeout` is the "Maximum timeout (in seconds) to wait for incoming connections" and "Implies -rtmp_listen 1" (`ffmpeg -h protocol=rtmp`): with it an input listens on its URL's host and port instead of connecting to them, and the 5 s isn't a limit on its reads. No committed record has an rtmp input; what `mirage_vmaf` does with one is MEASUREMENT PENDING.

**Question.** Which options should an rtmp input get?

**Options.**

1. As now: `timeout` and `rw_timeout` for every network input, as D-036 words it.
2. `rw_timeout` only for rtmp, which implies no listening, in `src/VMAFNet.h`, `src/FfOpen.cpp` and `src/Demuxer.cpp` alike; `--help`, P-066's statement of the limits and the test follow, and a test on a local rtmp server checks it end to end.
3. No rtmp input in `mirage_vmaf`: refused, as a protocol it doesn't support.

**Agent's recommendation.** Option 2: D-036's intent, that a read that waits longer fails, then holds for rtmp, and the player keeps the analyzer's rule.

### Frames missing at a sequence's start, and the reasons of the frames D-050 counts (D-050; P-073)

**Decided** (2026-10-29): D-057: (a) option 2, and option 3 once a committed case shows an offset; (b) option 1, with a test of an input that ends.

**What the code says.** Each discontinuity sequence after the first is measured from its own first frame (D-046), and D-050 compares only where the inputs' sequences before it end (`src/VMAFAlign.h`, "Jumps at different places").

- When one input lacks the first frames after a jump that is at the same place in both inputs, the two sequences still pair, offset by the frames the one lacks. The frames that don't pair warn only above 1% (D-028), and nothing in the report shows the offset. No test or committed record has such a case: how often it happens, and its effect on the scores, is MEASUREMENT PENDING.
- Two kinds of frames D-050 counts keep D-046's reasons. The other input's frame within a frame of the coarser input of the first input's jump pairs with nothing, an unmatched reference or extra distorted frame, before pairing stops (P-073; `tests/test_vmaf_align.cpp`, "a jump in one input alone stops the pairing"). Where one input ends instead of jumping, pairing doesn't stop, and the other input's frames count as unmatched or extra, not as after a discontinuity.

**Questions.** (a) Should the pairing check, or the report show, where each pair of sequences starts? (b) Should those frames keep D-046's reasons?

**Options.**

- (a)
  1. As now, the limitation stated in `--help` and the method notes.
  2. The report gives each input's sequences, where each starts and how many frames it has, so that an offset shows; no pair changes.
  3. The pairing compares the k-th sequences' first timestamps as the inputs give them as well: where they differ by more than a frame of the coarser input, the input that starts later lacks frames, which fail, and the sequences pair from where both have frames. It relies on RFC 8216's matching timestamps in every rung (§6.2.4), as D-050 does.
- (b)
  1. D-046's reasons, as P-073 implements them.
  2. After a discontinuity, as D-028's rule counts them.

**Agent's recommendation.** (a) Option 2 now, which changes no score and shows an offset where there is one; option 3 once a committed case shows one. (b) Option 1, which P-073 states, with a test of an input that ends.

### What `pool.pair_memory` gives, and what `--memory-limit` is (D-045, D-051, D-053; P-074)

**Decided** (2026-10-29): D-058: (a) option 3; (b) option 1.

**What the records show.** The rule measures a pair as the process's resident memory gained since the run began, shared out among the pairs running, and keeps the largest measure (`vmafladder::admits()`); the report gives that as `pool.pair_memory` (P-074). At 1 GiB over 300 frame pairs it was more than three times one pair's peak, since the resident memory still counted memory of pairs that had ended (`eval/ladder/advanced-local-300-mem1g-b706ed3/README.md`; "For information" above). D-053 has the report give what the rule measured "so that a run shows how far its pairs went beyond it", which a measure kept at its largest can't show. The limit decides when a pair starts; running pairs aren't stopped, and the peak can pass it: with D-051's margin the run over every frame from Apple's CDN peaked 2.1% over 1 GiB (`eval/ladder/advanced-cdn-full-af17121/`; P-073). `--help` states the rule (`7ee3724`).

**Questions.** (a) What should `pool.pair_memory` give? (b) Should `--memory-limit` stay a rule for starting pairs, or become a cap?

**Options.**

- (a)
  1. As now, the largest measure.
  2. The measure of the first pairs once they had warmed up, P-074's alternative.
  3. Each admission's measure, with the pair it let start, in the report's pool section.
- (b)
  1. A rule for starting pairs, as D-045 to D-053 have it.
  2. A cap: a running pair that would take the process over it paused, or stopped and started again later.
  3. Each pair in a process of its own, whose memory is measured and limited on its own, which the audit's reviews asked for.

**Agent's recommendation.** (a) Option 3: each admission's measure is what D-053 asks the field to show, and the largest can be read from it. (b) Option 1: with D-053's margin the run over every frame stayed under the limit (P-075), and a cap would stop pairs that have done much of their work.

### A memory limit when the memory can't be read (D-045; P-067)

**Decided** (2026-10-29): D-059, option 3.

**What the code says.** `vmafproc::resident_bytes()` gives 0 when the process's memory can't be read, when `task_info()` fails on macOS or `/proc/self/statm` can't be read on Linux (`src/VMAFProcess.cpp`). With a limit set, `admits()` then lets a pair start once none is warming up, and the warning of a pair over the limit on its own isn't given (`src/VMAFLadder.cpp`); neither the report nor stderr says that the limit wasn't applied. One assertion tests it (`tests/test_vmaf_ladder.cpp`, "vmafladder::admits: the memory limit's rule"), and no D- or P-entry records it.

**Question.** What should a run with a memory limit do when its memory can't be read?

**Options.**

1. As now: pairs start as the warm-up allows, and nothing says so.
2. As now, with a warning on stderr and in the report's pool warnings.
3. Refused: a run whose memory can't be read before its first pair fails then; a reading that fails later holds the next pair back until one succeeds.

**Agent's recommendation.** Option 3: a run that was given a limit doesn't run without one.

### Whether a pending P-entry may grow (P-073, P-074)

**Decided** (2026-10-29): D-060, option 1.

**What the history shows.** While they were pending, P-073 got a bullet on the unit tests in `61f711c` and one on `a07_restart` in `4587c2d`, and P-074's Result a sentence in `9a72fed`; each added fact rests on a committed record or says MEASUREMENT PENDING. `docs/DECISIONS.md` says the agent's entries "stay marked as pending until the owner confirms them", and not whether their text may change meanwhile. The corrections of 2026-10-28 and 2026-10-29 are lines of their own, `- **Correction** (date): …`, before an entry's **Who**.

**Question.** May a pending P-entry's text change before the owner confirms it?

**Options.**

1. Append-only: what is found later goes in dated lines, Correction or Status, and the text above them stays as written.
2. Frozen: what is found later goes in new P-entries only.
3. Free to change until it is confirmed, git keeping the history.

**Agent's recommendation.** Option 1, the corrections' form: an entry shows what was chosen and, dated, what was found after it, in one place.

### The streams record's checksums (D-049; P-069)

**Decided** (2026-10-29): D-060, option 1.

**What the record shows.** `eval/streams/local-advanced-2026-10-26/SHA256SUMS` lists the files of the local copy, media included, which isn't in the repository, so that a copy made again can be checked (`eval/streams/local_copy.py`, `copy.md`). It can't be checked in the record's own directory, and `copy.json` and `copy.md` are in no checksum file; the SHA256SUMS of the record of 2026-10-23 next to it lists that record's own files (`eval/streams/fetched-2026-10-23/`).

**Question.** Should the record get checksums that can be checked in place? Committed records are never edited, and an addition to a record's directory is the owner's call.

**Options.**

1. As now, with `eval/streams.md` saying what the record's SHA256SUMS checks, and how.
2. A second file in the record, its own files' checksums under a name of its own: an addition to a committed record.
3. A README in the record's directory: an addition too.

**Agent's recommendation.** Option 1: the record stays as committed, and the note goes where the streams are described.

### `docs/PHASE2_SURVEY.md`, a snapshot or updated (D-042)

**Decided** (2026-10-29): D-060, option 1.

**What the file says.** Its header says the text is as the owner decided it, "with the measurements added since"; it still gives M2 and M3 as MEASUREMENT PENDING, and says no committed run has scored the advanced sample. M2 and M3 are in `eval/phase2-survey/m2-m3/record.txt`, and the sample's runs in `eval/ladder/advanced-local-300/` and those after it (P-071; `docs/PHASE2_STATUS.md`).

**Question.** Should the survey stay as it was decided, or follow the measurements?

**Options.**

1. A dated snapshot, as of its last commit, its header pointing to `docs/PHASE2_STATUS.md` for the measurements since.
2. Updated: M2, M3 and the sample's runs where it has them pending.

**Agent's recommendation.** Option 1: it is what D-042 to D-049 were decided on, and the status file has what came after.

### The recommendation line of P-033 to P-065 in Phase 1's pending document (D-041)

**Decided** (2026-10-29): D-060, option 1, in a new commit on `vmaf/phase-1`.

**What the history shows.** `34e6faa`, on `vmaf/phase-1`, moved the package of P-033 to P-065 under "Decided" in `docs/PHASE1_DECISIONS_PENDING.md` without its last line, "**Agent's recommendation**: confirm them all, with the open questions above decided as recommended there." (`b2d2824`), though the file keeps each decided item "with the text it had when it was decided"; the package of P-006 to P-032 kept its line. Neither the commit's message nor `docs/AI_CONTRIBUTIONS.md` says so.

**Question.** Should the line come back?

**Options.**

1. Restored on `vmaf/phase-1`, in a new commit: an addition.
2. A note where it was, saying that `34e6faa` left it out and what it said.
3. As now, with a disclosure in `docs/AI_CONTRIBUTIONS.md`.

**Agent's recommendation.** Option 1, on `vmaf/phase-1` with the other Phase 1 text the audit found, the two citations of "hard rule 4" for the differences list, so that Phase 1's PR carries them.

### The model's name, and the survey's rendering of §2 (D-013)

**Decided** (2026-10-29): D-060: (a) option 2; (b) option 1.

**What the files say.** `docs/PHASE2_SURVEY.md` has the byline "Written by: the agent (Claude)", as `docs/VMAF_GAP_REPORT.md` has, and the agent's P-entries say "**Who**: agent (Claude)"; the hard rules put the AI's involvement in `docs/AI_CONTRIBUTIONS.md`, which names the model. The survey also gives the work order's §2 requirements in its own words ("What §2 asks"), while D-013 keeps the work order out of the repository.

**Questions.** (a) Should files other than `docs/AI_CONTRIBUTIONS.md` name the model? (b) Is the survey's rendering of §2 within D-013?

**Options.**

- (a)
  1. As now: "agent (Claude)" in bylines and **Who** lines.
  2. "agent" only from now on, the model named in `docs/AI_CONTRIBUTIONS.md` alone; what is written stays.
- (b)
  1. Within D-013: the requirements in the survey's words, no passage of the work order quoted.
  2. Shortened to the items' names, their text left to the work order.

**Agent's recommendation.** (a) Option 2, which keeps the AI's involvement in the one file the hard rules name, without editing what is written. (b) Option 1: D-013 keeps the work order's text out, and the survey states what the owner decided on.

### The agent's choices to confirm: P-067 to P-077

**Decided** (2026-10-29): D-061 confirmed P-067 to P-077 as their entries state them.

| Entry | What |
|---|---|
| P-067 | how D-043 to D-048 are implemented: the pool, the memory limit and the end of a run |
| P-068 | how D-046 is implemented: the discontinuity sequences from the timestamps; its statement on `a07_restart` no longer describes the code under D-050 (its correction, P-073) |
| P-069 | the measurements so far: the machine, M5, the local copy of the acceptance ladder and its check |
| P-070 | M5's gaps, and the report's start after a discontinuity |
| P-071 | M2, M3, the acceptance across pool sizes at 300 frames, and the memory limit's runs |
| P-072 | the memory limit's rule: a pair's memory measured once it has warmed up |
| P-073 | how D-050 to D-052 are implemented, and their runs |
| P-074 | how D-053 is implemented, and its run at 300 frames |
| P-075 | D-049's run over every frame with D-053's margin |
| P-076 | the rerun of every committed Phase 1 measurement with the Phase 2 program, cut short by the machine's sleep |
| P-077 | how the audit's first batch is implemented, and the unit tests after it |

**Agent's recommendation**: confirm P-067 to P-077, as D-050, D-051 and D-053 change what P-068 and P-072 implement.

## Decided on 2026-10-27 (D-050 to D-053)

The owner's answers to the agent's questions of 2026-10-27 (`docs/DECISIONS.md` D-050 to D-053), each item with the text it had when it was decided, as the record of its options, and a note at its start on what was decided.

### Discontinuity sequences that don't line up (D-046; P-068, P-070)

**Decided** (2026-10-27): D-050, option 2: the k-th sequences pair only where their jumps are at the same place in both inputs; elsewhere pairing stops, and the frames after count as failed. **Done** in `23d733f` (P-073): in M5's gaps, where the inputs lack different segments, 240 pairs and no pair of frames at different times; where both lack the same segments, all 719 frames pair; M5's discontinuities pair every frame, as with D-046 (`eval/phase2-survey/m5-gaps-d050/`, `m5-d050/`).

**What M5's gaps show** (`eval/phase2-survey/m5-gaps/record.txt`; P-070). A synthetic ladder of two rungs in segments of about 2 s, whose playlists lack six segments in a row, about 12 s, without a tag, as when FFmpeg's hls demuxer skips segments it can't read:

| Case | D-046's pairing (`e5d6e9d`) | D-028's rule (`bbc669d`) |
|---|---|---|
| `both`: the reference lacks its segments 4 to 9, the distorted input its segments 10 to 15 | 359 pairs, of which 119 pair a reference frame with the distorted frame 12.012 s later; 360 unmatched reference and 360 extra distorted frames | 240 pairs; 958 frames after a discontinuity |
| `same`: both lack segments 4 to 9 | 719 pairs, no failure | 240 pairs; 958 frames after a discontinuity |
| `one`: the reference alone lacks them | 240 pairs; 479 unmatched reference and 839 extra distorted frames | 240 pairs; 1318 frames after a discontinuity |

In `both`, the 119 pairs make the second libvmaf context, from reference 21.453367 s with distorted 33.465367 s to reference 25.390633 s with distorted 37.402633 s. The report warns of the failure rate, 66.73%, as for any failures, and nothing in it sets these pairs apart: their mean score, 11.564222, is above that of the 240 others, 9.873372.

**Why.** D-046 pairs the k-th discontinuity sequence of the reference with the k-th of the distorted input, "each measured from its own start". P-068 takes the sequences from the timestamp jumps, as D-046 does where the playlists have no tag, and a jump forward of more than 10 s is one (D-028's threshold). A gap that long in an input's frames is then the start of a sequence, and where the two inputs' gaps are at different places, their k-th sequences start at different times and are paired from their starts. Where only one input has the gap, its later sequence pairs with nothing.

**Where it can happen.** In an HLS ladder that follows RFC 8216, matching content has the same timestamps and the same discontinuity sequence numbers in every variant (§6.2.4: "Matching content in Variant Streams MUST have matching timestamps", and "MUST have matching Discontinuity Sequence Numbers"), so the jumps at its tags are at the same place in every rung, as in M5's streams (`eval/phase2-survey/m5-d046/record.txt`). Jumps at different places come from frames missing in one input, as when FFmpeg's hls demuxer skips segments it couldn't read. In Phase 1's rerun at `bbc669d`, `mirage_vmaf`'s network reads timed out, and its reports of two pairs counted 2 and 7 timestamp discontinuities, where D-028's rule stopped pairing at the first (`eval/run_all/2026-10-26-d035/consistency/defaults-full/plain/gear3_vs_gear5/delta.md` and `gear1_vs_gear5/delta.md` on `vmaf/phase-1`). Where they were in each input is in no committed file.

**Question.** Should D-046's pairing pair the k-th sequences of the two inputs wherever they start, or only where their jumps are at the same place?

**Options.**

1. As now: the k-th sequences pair from their own starts, wherever the jumps are (P-068).
2. The k-th sequences pair only where the jump is at the same place in both inputs: where both inputs' (k − 1)-th sequences end within a frame of the coarser input of each other, in their own times. Where they don't, pairing stops at that sequence and every frame after it counts as failed, as D-028's rule counts the frames after a discontinuity. By the rule, not measured: M5's three cases with a tag, `same` and `one` pair as now, and `both` gives the 240 pairs before the first gap and none after.
3. A jump forward starts no sequence: the input's times go on across it, and its frames pair by timestamp with the other input's frames; only a jump back starts a sequence. RFC 8216's matching timestamps make a ladder's forward jumps the same in every rung. By the rule, not measured: in the gap cases every frame that has a frame of the same time in the other input pairs with it, and M5's `jump` case pairs as now; two inputs that jump forward by different amounts at the same place, 60 s and 20 s, which `tests/test_vmaf_align.cpp` pairs under D-046, would pair nothing after the jump; jumps back at different places would still pair from their starts.
4. Options 3 and 2 together: a jump forward starts no sequence, and a sequence that starts with a jump back pairs only where both inputs jump back at the same place.
5. D-046's wording: the sequences from the playlists' tags, each placed by the durations of the segments before it. FFmpeg's hls demuxer doesn't say which segment a frame came from (P-068), and a segment it skips shifts every later one.

**Agent's recommendation.** Option 2. It keeps D-046 as decided wherever the jumps are at the same place in both inputs, which RFC 8216 makes every jump at a tag in a ladder that follows it, and it pairs no two sequences whose jumps are at different places: where they are, the frames after count as failed and the report warns, as with D-028's rule. Option 4 would keep the pairs after a gap in one input, at the cost of D-046's pairing of jumps of different sizes at the same place.

### The memory limit's peak (D-045; P-071, P-072)

**Decided** (2026-10-27): D-051, option 2 with 10%: each pair, running or new, counted at what a pair uses plus 10%. **Done** in `af17121` (P-073): at 1 GiB on the local copy, 3 pairs at once and a peak of 825720832 bytes, PASS (`eval/ladder/advanced-local-300-mem1g-af17121/`); D-049's run over every frame is P-073's.

**What the runs show.** `pool_check.py --jobs 1,0 --memory-limit 1G` on the local copy of Apple's advanced sample, the first 300 frame pairs of each of its 7 pairs, checks each run's peak against the limit (P-071):

| Program | Rule | Most pairs at once | Peak resident memory, bytes | Record |
|---|---|---|---|---|
| `1cc1ab0` | P-067's | 7 | 2011676672 | `eval/ladder/advanced-local-300-mem1g-1cc1ab0/` |
| `a3d011b` | `60c1f7b`'s | 4 | 1132167168 | `-a3d011b/` |
| `dfed5a3` | `dfed5a3`'s, D-045's with a warm-up | 4 | 1131544576 | `-dfed5a3/` |

The limit is 1073741824 bytes; every run fails the check. With `dfed5a3`'s rule (P-072) the pairs start one after another, each once the one before has scored its first 30 frame pairs, and a pair starts while the resident memory, plus what a pair uses as measured then, stays within the limit, as D-045 has it. The fourth pair fitted when it started; then the four grew, to a peak 5.4% over the limit. A pair alone is at 97% of its 300-frame peak after 30 frame pairs, 284639232 bytes of 293208064 (`warm_up.txt`), and beyond that its peak grows little: 293306368, 294453248 and 295059456 bytes after 300, 450 and 600 frame pairs, and 301056000 over all 720 of the copy (`eval/ladder/advanced-local-pair-memory/record.txt`).

**Memory that stays resident.** After the first four pairs ended, the other three ran one at a time, and the report warns that pair 4 took the process over the limit on its own, at 1095270400 bytes of resident memory, more than three times one pair's peak; the run before the fix warned the same of pair 7 at 1331806208 bytes (`-1cc1ab0/README.md`). The resident memory then still counted memory the ended pairs had used. Which of the process's memory measures drops when a pair ends is MEASUREMENT PENDING.

**Correction** (2026-10-28): the warning's "pair 4 of 7" counts pairs from 1; it is the pair of index 3, which ran alone from 8.399 s to 8.746 s, when the other three of the first wave had ended, not a pair after the first four (P-072's correction).

**Question.** Should the limit hold the peak under it, as D-049's acceptance checks, and how?

**Options.**

1. As now: D-045's rule with the warm-up. The peak can exceed the limit by what the running pairs gain after the last pair started: 5.4% at 1 GiB here. The acceptance's check fails for a limit just above what a whole number of pairs used when the last one started.
2. A margin: each pair counted at what it uses plus a share, such as 10%, the share to be checked by the acceptance's runs.
3. A pair counted at the most one pair has used in the run so far, once a pair has completed alone; before that, as option 1.
4. D-045's alternative: an estimate from the inputs' frame sizes and the queues' depths, before a pair starts.

**Agent's recommendation.** Option 2 with 10%, more than three times the 3% one pair gains after its warm-up in `warm_up.txt`, so that the check of D-049's runs can hold; the runs at 300 frames and over every frame then show whether it does. Separately, measuring which memory measure drops when a pair ends would show whether the pool can run more pairs once the first have ended.

### D-045's default memory limit

**Decided** (2026-10-27): D-052: no default limit, as recommended below.

D-045 sets no default limit until M2 and M3 show what a pair uses. They are measured (`eval/phase2-survey/m2-m3/record.txt`; P-071): one pair of the advanced sample peaked at 292995072 bytes with two 1080p60 inputs and 272957440 with a 1080p60 and a 270p30 input, over 300 frame pairs, and the ladder one pair after another at 304955392 bytes; at the default size, 7 pairs at once peaked at 2009759744 bytes (`eval/ladder/advanced-local-300/`). The machine has 38654705664 bytes of memory (`eval/phase2-survey/machine.txt`).

**Agent's recommendation.** No default limit: at the default size, 11 pairs of this ladder's size would need about 11 times one pair's peak, about 3.2 GB by M2, well under the machine's memory; a limit is for ladders whose pairs are larger, and the user who scores them gives it.

### P-068's departure from D-046's wording

**Decided** (2026-10-27): D-050 keeps the sequences from the timestamp jumps, P-068's, and doesn't read the playlists' tags (its option 5).

D-046 takes the sequences "from the playlists' `EXT-X-DISCONTINUITY` tags where there are any, else from the timestamp jumps"; P-068 takes them from the timestamp jumps alone. Provisional until the owner decides; the question above bears on it (its option 5).

### The memory limit over every frame (D-051; P-073)

**Decided** (2026-10-27): D-053, option 2: the margin becomes 25%, and the report gives what a pair was measured to use; the run over every frame is to be repeated to check the peak. **Done** in `b706ed3` (P-074): at 1 GiB on the local copy, 3 pairs at once and a peak of 913162240 bytes, PASS (`eval/ladder/advanced-local-300-mem1g-b706ed3/`). Over every frame from Apple's CDN, at most 2 pairs at once and a peak of 730497024 bytes, PASS (`eval/ladder/advanced-cdn-full-b706ed3/`; P-075).

**What the run shows.** D-049's run over every frame at the default size, from Apple's CDN, with a limit of 1 GiB and D-051's margin (`eval/ladder/advanced-cdn-full-af17121/`; P-073): the run is complete, every frame of the 7 pairs paired, but 3 pairs ran at once and the process peaked at 1096171520 bytes, 2.1% over the limit of 1073741824. At 300 frame pairs the same limit held, with 3 pairs at once and a peak of 825720832 bytes (`eval/ladder/advanced-local-300-mem1g-af17121/`).

**Why.** D-051 counts each pair at what a pair used after its first 30 frame pairs, plus 10%. A pair uses more over its full length: one pair alone over every frame from Apple's CDN was at 322256 KiB of resident memory 45 s into its run and 336096 KiB 405 s in, and peaked at 345407488 bytes (`eval/ladder/advanced-cdn-pair-full/record.txt`), 21% above one pair's peak after 30 frame pairs of the local copy, 284639232 bytes (`eval/ladder/advanced-local-300-mem1g-dfed5a3/warm_up.txt`). What the rule measured for a pair in the run over every frame isn't in its report.

**Question.** How should the limit count a pair, so that the peak stays under it over every frame?

**Options.**

1. As now, D-051's 10%: over every frame of this ladder the peak was 2.1% over the limit.
2. A larger margin, 25%, which covers the 21% above; the report would also give what the rule measured for a pair, so that a run shows how far its pairs went beyond it. A ladder longer than this one's 10 minutes would gain more: the pair above grew by 13840 KiB between 45 s and 405 s into its run.
3. A count that grows with a pair's length: what a pair uses after its warm-up, plus 10%, plus a growth per frame pair times the frame pairs the pair will score, from its playlists' durations and frame rate; the growth per frame pair to be measured. Each pair's playlists would be read before it starts.
4. D-045's alternative: an estimate from the inputs' frame sizes, the queues' depths and their length.

**Agent's recommendation.** Option 2, with the run over every frame repeated to check it: the smallest change that covers D-049's ladder, and the report's new value shows whether a longer ladder needs option 3.
