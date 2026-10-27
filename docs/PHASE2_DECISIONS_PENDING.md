# Phase 2: decisions pending for the owner

What is open for the owner on `vmaf/phase-2`, §2 of the VMAF work order, after the decisions of 2026-10-26 on the survey (`docs/DECISIONS.md` D-042 to D-049). The decisions are the owner's (hard rule 3 of the work order); every recommendation below is labeled as the agent's. Every number comes from a committed output, named next to it; where there is none, it says MEASUREMENT PENDING. The state of each item of §2 is in `docs/PHASE2_STATUS.md`.

## Open

### Discontinuity sequences that don't line up (D-046; P-068, P-070)

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

### D-045's default memory limit

D-045 sets no default limit until M2 and M3 are measured: one pair's peak resident memory and wall time, and the ladder's, by a `mirage_vmaf` from before the pool (`eval/phase2-survey/m2_m3_baseline.sh`; MEASUREMENT PENDING).

### P-068's departure from D-046's wording

D-046 takes the sequences "from the playlists' `EXT-X-DISCONTINUITY` tags where there are any, else from the timestamp jumps"; P-068 takes them from the timestamp jumps alone. Provisional until the owner decides; the question above bears on it (its option 5).

## The agent's choices to confirm: P-067 to P-070

| Entry | What |
|---|---|
| P-067 | how D-043 to D-048 are implemented: the pool, the memory limit and the end of a run |
| P-068 | how D-046 is implemented: the discontinuity sequences from the timestamps |
| P-069 | the measurements so far: the machine, M5, the local copy of the acceptance ladder and its check |
| P-070 | M5's gaps, and the report's start after a discontinuity |

**Agent's recommendation**: confirm P-067, P-069 and P-070; P-068 with the question above.
