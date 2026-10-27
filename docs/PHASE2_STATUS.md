# Phase 2 status

Phase 2 of the VMAF work order is its §2, ladder orchestration, on the branch `vmaf/phase-2`, made from `vmaf/phase-1` before Phase 1 is merged (`docs/DECISIONS.md` D-042). This file says, for each item of §2, what the branch does and which committed output shows it, and what remains. Every number comes from a committed output named next to it; where there is none, it says MEASUREMENT PENDING. The survey that preceded the work is `docs/PHASE2_SURVEY.md`; the owner's decisions on it are D-042 to D-049, and the agent's choices in implementing and measuring them P-067 to P-072. What is open for the owner is in `docs/PHASE2_DECISIONS_PENDING.md`.

| §2 item | Status | Where |
|---|---|---|
| 1. The master's variants, with `BANDWIDTH`, `RESOLUTION` and `CODECS` | done in Phase 1 (D-029) | `src/VMAFLadder.h` |
| 2. The top rung by `BANDWIDTH` | done in Phase 1 (D-029) | `vmafladder::plan()` |
| 3. A bounded pool, its size configurable, by default physical cores minus one | done in `mirage_vmaf --ladder` (D-043, D-044, D-048); on the local advanced sample every pair scores the same at the sizes 1, 2, 4 and the default | `vmafladder::score()`, `--jobs`, `src/VMAFProcess.h`; `eval/ladder/advanced-local-300/`; P-067, P-071 |
| 4. A memory limit that runs fewer pairs at a time | done (D-045), each pair's memory measured once it has warmed up (P-072); at 1 GiB on the local advanced sample 4 pairs ran at once where 7 do without, and the peak was 5.4% over the limit: open for the owner. No default limit; M2 and M3 are measured, and the agent recommends none | `--memory-limit`; `eval/ladder/advanced-local-300-mem1g-*/`; P-067, P-071, P-072 |
| 5. Segments of different durations, `EXT-X-DISCONTINUITY` | done: each discontinuity sequence paired on its own (D-046), the sequences found where the timestamps jump, which departs from D-046's wording (P-068); rungs whose segments differ pair as before. Where the two inputs' jumps are at different places, as gaps from skipped segments can be, the later sequences are paired with each other from their starts, frames 12.012 s apart in M5's gaps (P-070): open for the owner | `src/VMAFAlign.h`; `eval/phase2-survey/m5/`, `m5-d046/`, `m5-gaps/` |
| 6. A cancel that keeps the pairs done | done: SIGINT, SIGTERM or a failed pair stops the run, whose report lists the complete pairs and the others' states (D-047) | `mirage_vmaf --help`; P-067 |
| **Acceptance**: a 6 to 8 rung 1080p ladder, the same scores at pool sizes 1 to N, the peak memory under the limit | on the local copy of Apple's advanced sample, 8 rungs, the first 300 frame pairs: the same scores at the sizes 1, 2, 4 and the default, PASS; the peak under a limit of 1 GiB, FAIL, by 5.4%. The run over every frame at the default size from Apple's CDN (D-049) is MEASUREMENT PENDING | `eval/ladder/pool_check.py`, `eval/ladder/advanced-local-300/`, `advanced-local-300-mem1g-dfed5a3/`; P-071 |

## Measurements

- **M1**, the machine: an Apple M3 Pro with 12 physical cores, 6 performance and 6 efficiency cores (`eval/phase2-survey/machine.txt`).
- **M5**, discontinuities (`eval/phase2-survey/m5/record.txt`): FFmpeg's hls demuxer gives each segment's own timestamps, unchanged at an `EXT-X-DISCONTINUITY`; D-028's rule then stopped pairing at the tag, 30 pairs and the 60 frames after it failed. With D-046 every frame pairs, 60 pairs and no failure (`m5-d046/record.txt`).
- **M5's gaps** (`eval/phase2-survey/m5-gaps/record.txt`; P-070): playlists that lack about 12 s of segments, without a tag. Where both inputs lack the same segments, D-046's pairing pairs all 719 frames, where D-028's rule paired 240; where they lack different ones, it scores 359 pairs, of which 119 pair a reference frame with the distorted frame 12.012 s later, and nothing in the report sets them apart ("Discontinuity sequences that don't line up" in `docs/PHASE2_DECISIONS_PENDING.md`).
- **M2, M3**, before the pool (`eval/phase2-survey/m2-m3/record.txt`; P-071): one pair of the local advanced sample, 300 frame pairs, peaked at 292995072 bytes of resident memory in 4.20 s with two 1080p60 inputs and at 272957440 bytes in 4.19 s against the 480x270 rung at 30 fps; the ladder's 7 pairs one after another at 304955392 bytes in 28.91 s, on battery power.
- **The pool** (`eval/ladder/advanced-local-300/`; P-071): at the sizes 1, 2, 4 and the default, 11, the peaks were 304529408, 581894144, 1111425024 and 2009759744 bytes and the wall times 33.146, 20.173, 13.558 and 14.553 s, one run each on battery power. Each pair keeps libvmaf's 4 threads (D-044), and in M2 a pair used 17.40 s of CPU time in 4.20 s.
- **One pair's memory** (`eval/ladder/advanced-local-pair-memory/record.txt`, the `warm_up.txt` of `advanced-local-300-mem1g-dfed5a3/`): 284639232 bytes after 30 frame pairs, 293208064 after 300, 301056000 over all 720 of the copy.

## Tests

The pool, the memory limit with a stand-in for the memory reading and its rule on its own (`vmafladder::admits`), the cancel, a failed pair, the options and the report's new fields (`tests/test_vmaf_ladder.cpp`, `test_vmaf_cli.cpp`, `test_vmaf_report.cpp`); the acceptance's check with a stand-in for `mirage_vmaf` (`eval/ladder/tests/test_pool_check.py`); the discontinuity sequences on timestamp sequences and on MPEG-TS clips (`tests/test_vmaf_align.cpp`, `test_vmaf_analyzer.cpp`). The unit test record of Phase 2 is MEASUREMENT PENDING.

## Open for the owner

In `docs/PHASE2_DECISIONS_PENDING.md`, with the options and the agent's recommendations:

- Discontinuity sequences whose jumps are at different places in the two inputs (D-046; P-068, P-070).
- P-068: D-046's sequences come from the timestamps, not the playlists' tags; provisional until the owner decides.
- The memory limit's peak, which can exceed the limit by what the running pairs gain after the last one started (D-045; P-071, P-072).
- D-045's default memory limit, now that M2 and M3 are measured: the agent recommends none.
- P-067 to P-072, the agent's choices, pending confirmation.

## Before the Phase 2 PR

Phase 1's PR first (hard rule 5: one PR per phase); then the acceptance runs, a unit test record and a rerun of every committed measurement with the Phase 2 program, and this branch brought up to date with Phase 1's.
