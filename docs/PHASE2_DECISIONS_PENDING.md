# Phase 2: decisions pending for the owner

What is open for the owner on `vmaf/phase-2`, §2 of the VMAF work order, after the decisions of 2026-10-26 on the survey (`docs/DECISIONS.md` D-042 to D-049) and of 2026-10-27 on what the measurements found (D-050 to D-052). The decisions are the owner's (hard rule 3 of the work order); every recommendation below is labeled as the agent's. Every number comes from a committed output, named next to it; where there is none, it says MEASUREMENT PENDING. The state of each item of §2 is in `docs/PHASE2_STATUS.md`.

## Open

### The memory limit over every frame (D-051; P-073)

**What the run shows.** D-049's run over every frame at the default size, from Apple's CDN, with a limit of 1 GiB and D-051's margin (`eval/ladder/advanced-cdn-full-af17121/`; P-073): the run is complete, every frame of the 7 pairs paired, but 3 pairs ran at once and the process peaked at 1096171520 bytes, 2.1% over the limit of 1073741824. At 300 frame pairs the same limit held, with 3 pairs at once and a peak of 825720832 bytes (`eval/ladder/advanced-local-300-mem1g-af17121/`).

**Why.** D-051 counts each pair at what a pair used after its first 30 frame pairs, plus 10%. A pair uses more over its full length: one pair alone over every frame from Apple's CDN was at 322256 KiB of resident memory 45 s into its run and 336096 KiB 405 s in, and peaked at 345407488 bytes (`eval/ladder/advanced-cdn-pair-full/record.txt`), 21% above one pair's peak after 30 frame pairs of the local copy, 284639232 bytes (`eval/ladder/advanced-local-300-mem1g-dfed5a3/warm_up.txt`). What the rule measured for a pair in the run over every frame isn't in its report.

**Question.** How should the limit count a pair, so that the peak stays under it over every frame?

**Options.**

1. As now, D-051's 10%: over every frame of this ladder the peak was 2.1% over the limit.
2. A larger margin, 25%, which covers the 21% above; the report would also give what the rule measured for a pair, so that a run shows how far its pairs went beyond it. A ladder longer than this one's 10 minutes would gain more: the pair above grew by 13840 KiB between 45 s and 405 s into its run.
3. A count that grows with a pair's length: what a pair uses after its warm-up, plus 10%, plus a growth per frame pair times the frame pairs the pair will score, from its playlists' durations and frame rate; the growth per frame pair to be measured. Each pair's playlists would be read before it starts.
4. D-045's alternative: an estimate from the inputs' frame sizes, the queues' depths and their length.

**Agent's recommendation.** Option 2, with the run over every frame repeated to check it: the smallest change that covers D-049's ladder, and the report's new value shows whether a longer ladder needs option 3.

## The agent's choices to confirm: P-067 to P-073

| Entry | What |
|---|---|
| P-067 | how D-043 to D-048 are implemented: the pool, the memory limit and the end of a run |
| P-068 | how D-046 is implemented: the discontinuity sequences from the timestamps |
| P-069 | the measurements so far: the machine, M5, the local copy of the acceptance ladder and its check |
| P-070 | M5's gaps, and the report's start after a discontinuity |
| P-071 | M2, M3, the acceptance across pool sizes at 300 frames, and the memory limit's runs |
| P-072 | the memory limit's rule: a pair's memory measured once it has warmed up |
| P-073 | how D-050 to D-052 are implemented, and their runs |

**Agent's recommendation**: confirm P-067 to P-073, as D-050 and D-051 change what P-068 and P-072 implement.

## Decided on 2026-10-27 (D-050 to D-052)

The owner's answers to the agent's questions of 2026-10-27 (`docs/DECISIONS.md` D-050 to D-052), each item with the text it had when it was decided, as the record of its options, and a note at its start on what was decided.

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
