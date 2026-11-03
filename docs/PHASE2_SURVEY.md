# Phase 2 survey: ladder orchestration (§2)

- Date: 2026-10-26
- Work order: Phase 2, §2 (hard rule 1: survey first, then the owner's decisions, then code)
- Surveyed: commit `d3a7a63` of `vmaf/phase-1`, Phase 1's work, which isn't merged yet; Phase 2 is the branch `vmaf/phase-2`, made from it
- Written by: the agent (Claude), for the owner's review

Every number here comes from a committed output or the code, named next to it; where there is none, it says MEASUREMENT PENDING. The owner's decisions on it are D-042 to D-049 in `docs/DECISIONS.md`; the text below is as the owner decided it, with the measurements added since. It is a snapshot as of `f8781f2`, its last commit before this line, and isn't updated: the measurements since, M2 and M3 and the advanced sample's runs among them, are in `docs/PHASE2_STATUS.md` (D-060).

## What §2 asks

1. Read the HLS master playlist, list its variants, with their `BANDWIDTH`, `RESOLUTION` and `CODECS`.
2. In top-rung mode, take the variant with the highest `BANDWIDTH` as the reference.
3. A bounded pool of threads, its size configurable, by default the number of physical cores minus one.
4. A configurable memory limit: above it, fewer pairs at a time, not running out of memory.
5. Variants whose segments have different durations, and playlists with `EXT-X-DISCONTINUITY`.
6. A cancel that keeps the scores of the variants already done.

**Acceptance**: a ladder of 6 to 8 rungs at 1080p scores to the end; its scores don't change when the pool's size goes from 1 to N; its peak memory stays under the configured limit.

## What Phase 1 already does

| §2 item | Today | What is missing |
|---|---|---|
| 1. Master, variants, attributes | Done in `mirage_vmaf --ladder` (D-029, `src/VMAFLadder.h`): the master's entries with `BANDWIDTH`, `AVERAGE-BANDWIDTH`, `RESOLUTION`, `CODECS` and `FRAME-RATE`; entries that name the same playlist scored once; entries without video skipped and listed | nothing |
| 2. The top rung | Done: the video entry with the highest `BANDWIDTH`, the first of equals with a warning (`vmafladder::plan()`) | nothing |
| 3. A pool | None: `vmafladder::score()` scores one pair after another. Within a pair, libvmaf runs on 4 threads (`kThreads` in `src/VMAFAnalyzer.cpp`), each color conversion on 1 (`kScaleThreads`), and the decoders with FFmpeg's default thread count, which the analyzer doesn't set | the pool, its size, its option (decisions P2-1 to P2-3) |
| 4. Memory limit | None: nothing measures or limits memory | the limit, how it is measured, what it does (P2-4); how much one pair uses is MEASUREMENT PENDING (M2) |
| 5. Segment durations, discontinuities | Partly. The pairing pairs frames by timestamp (D-028), not by segment, so segments of different durations don't matter where the timestamps line up. At a discontinuity, a timestamp that goes back or jumps forward by more than 10 s, pairing stops and the rest of the input fails (D-028, option (a), "until D-011's ladders show what real discontinuities look like"). No committed run has a stream with a discontinuity or with rungs whose segments differ | a rule for discontinuities (P2-5), and test streams with both (M5) |
| 6. Cancel | Partly. In the player, `VMAFAnalyzer::cancel()` stops the analysis and the pairs already scored keep their results (`start_manifest()` stores each pair's result as it ends). `mirage_vmaf --ladder` writes a report only when every pair is complete (P-006, D-029) and has no cancel | what a cancel or a failed pair leaves (P2-7) |

**The machine** (M1, `eval/phase2-survey/machine.txt`): an Apple M3 Pro with 12 physical cores, 6 performance and 6 efficiency cores, and 12 logical ones; the work order's default would be 11 pairs at a time.

**The acceptance ladder.** Apple's advanced HLS sample fits §2's acceptance without D-011's ladders (`eval/streams.md`, "Considered: Apple's advanced HLS sample"): 24 entries naming 8 video playlists, `v2` to `v9`, three of them 1920x1080, at 30 and 60 fps, each 100 segments of 6 s. Its pairs also have the frame-rate mismatch (a 60 fps reference against 30 fps playlists) and the untagged inputs that Phase 1's synthetic cases covered. No committed run has scored it. D-011's own ladders, once chosen, would be a second acceptance.

## The player's ladder analysis

The player's VMAF panel scores a manifest's variants with `VMAFAnalyzer::start_manifest()`: one pair after another on one background thread, the variants from the player's `ManifestParser`, the highest bandwidth as the reference. §2 doesn't say whether the pool is for the player too; scoring while playing is §4 (Phase 4), a decision point the work order leaves to the owner.

## Measurements Phase 2 needs

- **M1**, the machine's cores and memory: `eval/phase2-survey/machine.txt`.
- **M2**, one pair's peak memory and wall time: the Phase 1 program on pairs of the advanced sample, a 1080p reference against a 1080p and against a 270p playlist, the first frames only (`/usr/bin/time -l`). MEASUREMENT PENDING.
- **M3**, the ladder before the pool: `mirage_vmaf --ladder` on the advanced sample with the Phase 1 program, one pair at a time, its wall time and peak memory, as the baseline the pool is compared with. MEASUREMENT PENDING.
- **M4**, only if P2-3 chooses fewer libvmaf threads: whether libvmaf's thread count changes any score. MEASUREMENT PENDING.
- **M5**, test streams with a discontinuity and with rungs whose segments differ, made by `eval/phase2-survey/m5_discontinuities.py`: what FFmpeg's hls demuxer gives the analyzer at an `EXT-X-DISCONTINUITY` (`eval/phase2-survey/m5/record.txt`, made with the release `mirage_vmaf` of `e5a34ea`). Two rungs, two periods of 30 frames each:
  - FFmpeg's hls demuxer gives each segment's own timestamps, unchanged at the tag: where period B's timestamps start again, its first frame is at 1.433367, the time of period A's first; where they start 60 s later, at 61.400000 (ffprobe's frame times through the hls demuxer).
  - At both, D-028's rule finds one discontinuity in each input and stops pairing: 30 pairs, and the 60 frames after the tag, 30 of each input, fail.
  - Where period B's timestamps go on from period A's, the tag changes nothing: 60 pairs, no failure.
  - Rungs whose segments differ, without a discontinuity: 30 pairs, no failure. P2-6 needs nothing more.

## Decisions needed from the owner

Each with its options and the agent's recommendation; the decision is the owner's (hard rule 3).

**P2-1. Where the pool runs.**
1. `mirage_vmaf --ladder` only; the player's panel keeps scoring one pair at a time.
2. Both.

Agent's recommendation: 1. Scoring next to playback is §4's question, which the work order leaves to the owner, and the player's analysis can take the pool once §4 is decided.

**P2-2. What runs at the same time.**
1. Pairs: each worker scores one pair, as a pair is scored today.
2. Frames of one pair across workers.

Agent's recommendation: 1. It changes nothing within a pair, so a pair's scores can't depend on the pool, which is what §2's acceptance checks; 2 would split libvmaf's motion features across workers.

**P2-3. The threads.** The work order's default is physical cores minus one for the pool, and each pair's libvmaf runs on 4 threads.
1. That default, each pair's libvmaf on 4 threads as today: more threads than cores when several pairs run.
2. That default, each pair's libvmaf on 1 thread: a changed setting that could change scores (M4 first), and FFmpeg's side of §5 scores on 4.
3. A default that counts libvmaf's threads, such as cores minus one divided by 4, at least 1.

Agent's recommendation: 1, the work order's default, with the libvmaf threads unchanged, so that Phase 1's §5 parity holds as measured; how the wall time changes with the pool's size is a measurement for Phase 5's benchmarks.

**P2-4. The memory limit: what it measures, and what it does.**
1. The process's resident memory, read from the system while the ladder runs: a pair starts only while the resident memory plus what a pair has used so far stays under the limit; running pairs aren't stopped; a pair that needs more than the limit alone runs alone, with a warning.
2. An estimate per pair from its inputs' frame sizes and the queues' depths, checked before it starts.
3. The default limit: none until given; or a share of the machine's memory, the share chosen when M2 and M3 show what a pair uses.

Agent's recommendation: 1, as it measures what the acceptance checks, with no default limit until M2 and M3 are measured, as D-003 deferred the scaler until §5's data.

**P2-5. Discontinuities.**
1. As D-028: pairing stops at the first discontinuity, and the rest fails.
2. Each discontinuity sequence paired on its own: the k-th sequence of the reference with the k-th of the distorted input, each measured from its own start, a new libvmaf context for each; the sequences taken from the playlists' `EXT-X-DISCONTINUITY` tags, which RFC 8216 (§4.3.3.3, `EXT-X-DISCONTINUITY-SEQUENCE`) has correspond across the variants of a master, else from the timestamp jumps.
3. `ffmpeg`'s correction of timestamp jumps, which makes the timeline continuous (the plan's option (c) of `docs/plans/frame-alignment.md`).

Agent's recommendation: 2, after M5 shows what the analyzer gets at a discontinuity. It changes D-028, which D-040 kept until real discontinuities are measured. FFmpeg's plain §5 graph pairs such inputs by its own rule, so §5 is expected to fail for them, as for the alignment cases (D-040; not measured).

**P2-6. Rungs whose segments have different durations.** Nothing to decide, unless M5 shows timestamps that don't line up across such rungs; then it joins P2-5. M5: they line up, every frame paired.

**P2-7. A cancel, or a pair that fails.**
1. `mirage_vmaf` stops on SIGINT or SIGTERM, or at a pair that fails; the pairs still running are stopped; it writes a report of the pairs that were complete, marked as cancelled or failed, with the pairs not scored listed, and exits with status 1.
2. As today: no report unless every pair is complete (P-006, D-029).

Agent's recommendation: 1, as §2 asks to keep the scores already done; the report says the run is incomplete, so it can't pass for a complete one, P-006's reason.

**P2-8. The report's order.** The pairs in the plan's order, whatever order they finish in, and the pool's size and the memory limit in the report's settings; the run's per-pair start and end times as well.

Agent's recommendation: as stated: the same report whatever the pool's size, apart from the times.

**P2-9. The options.** `--jobs N` and `--memory-limit SIZE`, on the command line and in the config file (D-030; the work order's engineering constraint). The names are the owner's to change.

**P2-10. The acceptance runs.**
1. Apple's advanced sample: the ladder at the pool sizes 1, 2, 4 and the default, the first 300 frames of each pair, every per-frame score compared across them at full precision; one run over every frame at the default size, for the peak memory against the limit.
2. Wait for D-011's ladders.

Agent's recommendation: 1 now, and D-011's ladders when chosen.

**P2-11. Where the acceptance reads the sample from.** Several pairs at once read Apple's CDN at once, and since D-036 a read that waits more than 5 s fails, which would fail a pair for the network rather than the pool.
1. From the CDN, as the Phase 1 runs.
2. From a local copy of the sample's playlists and segments, kept outside the repository, their SHA-256s committed.

Agent's recommendation: 2 for the comparison across pool sizes, so that only the pool changes between runs; 1 for one run, as a check that the CDN gives the same scores.

## A plan once these are decided

1. M2, M3 and M5, committed (M1 is).
2. The pool in `vmafladder::score()`, its options, the memory limit, the cancel and the report; unit tests on generated clips: the same scores at every pool size, a cancel, the memory limit with a stand-in for the memory reading.
3. The discontinuity rule (P2-5) on generated streams, with tests.
4. The acceptance runs (P2-10), committed with their `eval/run_all.sh` steps.
5. The documents, the P-entries and the PR, with the acceptance output pasted (hard rule 5).
