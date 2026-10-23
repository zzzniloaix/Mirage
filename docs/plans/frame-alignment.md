# Plan: pairing frames by timestamp (work order §1.4)

- **Status**: a plan for the owner to approve (D-007). Nothing in it is implemented.
- **Open owner decisions it depends on**: #7, the frame-alignment failure threshold and whether exceeding it warns or refuses (D-004), and the choices listed in "Decisions this plan needs" below. Where this plan names a choice, it is a proposal, and the agent's recommendations are marked as such.
- **Written by**: the agent (Claude), from the code at the commit that adds this file and from the FFmpeg 8.1.1 sources (`ffmpeg-8.1.1.tar.xz` from ffmpeg.org, whose SHA-256 is the one in Homebrew's ffmpeg-full formula; P-013). Statements about FFmpeg below come from reading those sources, not from runs; the tests in "Tests" turn them into committed measurements.

## What the work order asks

§1.4: pair reference and distorted frames by PTS instead of assuming that the n-th frame of one is the n-th of the other; handle frame-rate mismatches, dropped frames and duplicated frames; report how many frames failed to align; warn when the failure rate exceeds a threshold (the work order suggests 1%) and never produce a score silently. Acceptance: a test stream with frames dropped on purpose, for which the tool reports the failed frames instead of a wrong score. §7's output has `alignment { paired, failed, failure_rate }` per variant.

## What happens today

- `VMAFAnalyzer::analyze_pair` (`src/VMAFAnalyzer.cpp`) takes the next decoded frame of each input and scores the two as a pair, in the order the decoders output them, until either input ends. The GUI (`start`, `start_manifest`) and `mirage_vmaf` share this loop.
- `mirage_vmaf` records each pair's best-effort PTS in seconds, as the streams have them (P-006), but nothing uses them for pairing.
- So a frame missing from one input shifts every later pair by one frame, and the analyzer scores the shifted pairs without a warning. The same holds for a duplicated frame, a frame-rate mismatch and inputs that start at different times. This comes from reading the code; the dropped-frame test below measures it (MEASUREMENT PENDING).
- The §5 delta tool compares the two sides by position and warns when a `mirage_vmaf` pair has two different PTS (P-009). On bipbop's rungs every pair has equal PTS (`pairs with different PTS: 0` in every pair report of `eval/consistency/diag-after-300/plain/`).

## What FFmpeg does, which the §5 comparison measures against

From `libavfilter/vf_libvmaf.c` and `libavfilter/framesync.{c,h}`:

- The libvmaf filter syncs its inputs with framesync's dual-input mode. Input 0, the distorted video in the §5 graph, is the main input: every distorted frame makes one frame event. Input 1, the reference, is matched to it with the default timestamp mode: the reference frame with the nearest timestamp that is lower than or equal to the event's.
- Distorted frames before the reference's first frame have no reference frame and pass through unscored (`do_vmaf` returns the frame when there is no reference frame).
- When the reference ends first, its last frame is repeated for the remaining distorted frames (`eof_action` defaults to `repeat`). Scoring ends with the distorted input.
- Consequences: a distorted frame that is missing gives one score fewer, and the other frames stay paired correctly; a reference frame that is missing makes the distorted frame at its time pair with the previous reference frame, which is scored without a warning; a distorted input at a lower frame rate than the reference is scored at its own frame rate; one at a higher rate reuses reference frames.
- Before the filter graph, `ffmpeg` shifts each input file's timestamps by that file's start time unless `-copyts` is given (`fftools/ffmpeg_demux.c`, `ts_offset`), so the inputs are aligned at their starts, not by their absolute PTS. For formats that may jump in time (`AVFMT_TS_DISCONT`, which the HLS and MPEG-TS demuxers set), it also removes timestamp jumps larger than its `dts_delta_threshold` option (`fftools/ffmpeg_opt.c`).
- FFmpeg's libvmaf log numbers its frames (`frameNum`) by event, without timestamps.

## Design (proposal)

### Timestamps

- Each decoded frame's `best_effort_timestamp`, kept in its stream's time base and compared after `av_rescale_q()` to a common one, so that equal timestamps compare equal without floating point.
- Measured from each input's start, as `ffmpeg` does without `-copyts`: the container's `start_time`, or the first frame's timestamp when the container has none. Option: absolute PTS (see "Decisions").
- A frame without a timestamp cannot be placed and counts as an alignment failure.

### Matching rule

Two candidates, both on top of the same timestamps:

- **A, the reference's timeline.** §3.1 measures across variants on the reference variant's timeline. Every reference frame is a slot. The distorted frame whose time falls within a tolerance of the slot's time, proposed as half the reference's frame duration, fills it. A slot no distorted frame fills is an unmatched reference frame (a dropped distorted frame, or a gap). A distorted frame that fills no slot is an extra distorted frame (a duplicated timestamp, a higher frame rate, or frames outside the reference's span). Only filled slots are scored.
- **B, FFmpeg's framesync rule.** Every distorted frame is scored against the reference frame at or just before its time, as above. It gives FFmpeg's scores frame for frame, also when frames are missing, but scores mismatched pairs; to say so, a pair whose two times differ by more than the tolerance would count as a failure while still being scored.

The difference matters only where inputs don't line up: for inputs whose frames all line up, such as bipbop's rungs in `diag-after-300/`, both rules give today's pairs and today's scores.

### What a failure does to libvmaf

libvmaf computes its motion feature from the reference frame it was given before (`integer_motion`, and `integer_motion2` also from the one after; `mirage_vmaf --help`). Under rule A, a skipped reference frame makes the next scored pair's motion span the gap. Options: accept that and report it; start a new libvmaf context after every gap, so that each run of contiguous pairs is scored on its own (each run's first frame then has motion 0, like a stream's first frame: `eval/pooling/README.md`); or feed the skipped reference frame against a stand-in distorted frame and leave its score out. FFmpeg under rule B repeats the previous reference frame where one is missing, so that frame's motion is computed against an identical picture.

### Frame-rate mismatches

With rule A, a distorted input at half the reference's frame rate fills every other slot, so half the reference frames are unmatched and the failure rate is far above any threshold. Options: (a) treat it as failures and let the threshold decide; (b) detect the mismatch from the streams' frame rates and score on the coarser of the two grids, counting only frames missing from that grid as failures; (c) resample the distorted input to the reference's rate by repeating frames, which is what rule B amounts to.

### Duplicated frames

- Two distorted frames with the same timestamp: the first fills the slot, the second counts as a duplicate (extra distorted frame).
- A picture repeated under its own new timestamp, as encoders and frame-rate converters do, can't be told from timestamps; it is scored, and the repetition shows in its score. Detecting it from the pixels is outside this plan.

### Discontinuities

- In HLS, `EXT-X-DISCONTINUITY` may come with timestamps that jump or restart. A timestamp that goes backwards, or jumps further than a limit, in either input would be a discontinuity of that input.
- Options: (a) stop pairing at the first discontinuity and report the rest of the input as failed; (b) re-anchor both inputs after a discontinuity they share, matching the n-th discontinuity of one input with the n-th of the other; (c) remove jumps the way `ffmpeg` does, which is needed for §5 parity on such streams; (d) place frames on the playlist's timeline (segment durations and discontinuity sequence, which `src/HlsAlign.h`'s `MediaTimeline` already builds), which needs the playlists, not only the decoded streams.
- bipbop 16x9 has no discontinuities, so this needs constructed test streams (below) or D-011's ladders.

### Accounting

The report gets, per scored pair, what it has today, and an `alignment` object:

- `rule`, `tolerance`, `timestamp_origin`;
- `paired`: scored pairs; `unmatched_reference`, `extra_distorted`, `duplicate_distorted`, `no_timestamp`, `discontinuities`;
- `failed` and `failure_rate`, with a definition the owner picks (#7), such as failed / (paired + failed), or unmatched reference frames over all reference frames;
- `threshold` and whether it was exceeded;
- a list of the failures, each with its input, index and timestamp, so that §3 can place them on the timeline.

### Threshold and warning (#7)

- The threshold and the failure-rate definition are the owner's (#7); the work order suggests 1%.
- Whatever the threshold, failures are always counted and reported, as the work order rules out silent scores.
- Above the threshold, either warn (exit status 0, a warning in the report and on stderr) or refuse (no report and exit status 1, as P-006 does for an incomplete run, or a report flagged as not usable). The GUI would show the warning or refusal in the VMAF panel's error line, as it shows `result.error` today.

### Where the code changes

- A new pure module, for example `src/VMAFAlign.{h,cpp}`, that takes the two inputs' frame timestamps as they are decoded and says, step by step, which frames to pair, skip or count; no FFmpeg types beyond `AVRational`, so that it is unit-tested on its own.
- `analyze_pair` keeps a short queue of decoded frames on each side, bounded so that one input running far ahead can't fill memory, and asks the aligner what to do. This changes the scoring loop that the GUI uses as well, which is why D-007 asks for this plan first.
- `VMAFResult` and `src/VMAFReport.h` get the `alignment` fields and the failure list.
- The delta tool compares by timestamp instead of by position: FFmpeg's log has no timestamps, so FFmpeg's side gets a `showinfo` filter after libvmaf, which passes on the distorted frame of each event, and its log lines give each `frameNum`'s timestamp. A pair passes only if both sides scored the same timestamps, and differences in what each side counted as failed are reported.

## Tests

Unit tests of the aligner (Catch2, no media): timestamp sequences for equal streams, a dropped distorted frame, a dropped reference frame, a duplicated distorted timestamp, a distorted input at half and at double the reference's rate, a later and an earlier start, a backward jump and a forward jump, and frames without timestamps; each with the expected pairs, skips and counts.

Analyzer tests on generated clips (`tests/test_vmaf_analyzer.cpp` makes uncompressed Matroska clips the same way):

- **The work order's dropped-frame stream**: the reference clip, and a distorted copy with one frame removed and the other frames' timestamps kept. Today: every pair after the removed frame has two different PTS. With the plan: one unmatched reference frame, every other pair with equal PTS, the failure counted and the threshold check applied; and, away from the gap, the scores of the run of the full clip against itself.
- A distorted copy with one frame twice at the same timestamp; one at half the frame rate; one whose first frames are cut, timestamps kept; MPEG-TS clips whose timestamps restart midway.

Delta runs on synthetic clips, like `eval/consistency/color_cases.py`: the same cases made with FFmpeg's `select`, `setpts` and `fps` filters and scored by both sides, committed like `color-cases-before/`, so that the report shows what each side does with them. And the plain run of `diag-after-300/` again, to show that streams whose frames line up keep their pairs and scores.

## Decisions this plan needs

1. Approval of timestamp pairing in the analyzer that the GUI and `mirage_vmaf` share (D-007).
2. The matching rule: A, B, or A for the scores with B's pairing reported next to it for §5.
3. The timestamp origin: each input's start, as `ffmpeg` does, or absolute PTS.
4. Frame-rate mismatches: (a), (b) or (c) above.
5. Discontinuities: (a), (b), (c) or (d) above.
6. #7: the failure-rate definition, the threshold, and warn or refuse above it, with the exit status and the GUI's behavior.
7. What a gap does to libvmaf's motion features: accept, restart per contiguous run, or stand-in frames.
8. The report's `alignment` fields and failure list, ahead of §7's schema.

**Agent's recommendation**: rule A, timestamps from each input's start, (b) for frame-rate mismatches, (a) for discontinuities until D-011's ladders show what real discontinuities look like, a libvmaf context restarted after each gap so that no motion feature spans one, and failures always listed; the threshold and warn-or-refuse are left to the owner (#7). Rule A follows the reference variant's timeline, which §3.1 uses for attribution, and never scores a pair whose frames don't match; where it differs from FFmpeg's rule B, the delta tool would report the difference instead of hiding it.
