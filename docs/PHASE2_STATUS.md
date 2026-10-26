# Phase 2 status

Phase 2 of the VMAF work order is its §2, ladder orchestration, on the branch `vmaf/phase-2`, made from `vmaf/phase-1` before Phase 1 is merged (`docs/DECISIONS.md` D-042). This file says, for each item of §2, what the branch does and which committed output shows it, and what remains. Every number comes from a committed output named next to it; where there is none, it says MEASUREMENT PENDING. The survey that preceded the work is `docs/PHASE2_SURVEY.md`; the owner's decisions on it are D-042 to D-049, and the agent's choices in implementing them P-067 to P-069.

| §2 item | Status | Where |
|---|---|---|
| 1. The master's variants, with `BANDWIDTH`, `RESOLUTION` and `CODECS` | done in Phase 1 (D-029) | `src/VMAFLadder.h` |
| 2. The top rung by `BANDWIDTH` | done in Phase 1 (D-029) | `vmafladder::plan()` |
| 3. A bounded pool, its size configurable, by default physical cores minus one | done in `mirage_vmaf --ladder` (D-043, D-044, D-048) | `vmafladder::score()`, `--jobs`, `src/VMAFProcess.h`; P-067 |
| 4. A memory limit that runs fewer pairs at a time | done (D-045); no default limit yet | `--memory-limit`; P-067 |
| 5. Segments of different durations, `EXT-X-DISCONTINUITY` | done: each discontinuity sequence paired on its own (D-046), the sequences found where the timestamps jump, which departs from D-046's wording (P-068); rungs whose segments differ pair as before | `src/VMAFAlign.h`; `eval/phase2-survey/m5/`, `m5-d046/` |
| 6. A cancel that keeps the pairs done | done: SIGINT, SIGTERM or a failed pair stops the run, whose report lists the complete pairs and the others' states (D-047) | `mirage_vmaf --help`; P-067 |
| **Acceptance**: a 6 to 8 rung 1080p ladder, the same scores at pool sizes 1 to N, the peak memory under the limit | the check is written (`eval/ladder/pool_check.py`); the runs on Apple's advanced sample (D-049) are MEASUREMENT PENDING | `eval/streams/local-advanced-2026-10-26/` |

## Measurements

- **M1**, the machine: an Apple M3 Pro with 12 physical cores, 6 performance and 6 efficiency cores (`eval/phase2-survey/machine.txt`).
- **M5**, discontinuities (`eval/phase2-survey/m5/record.txt`): FFmpeg's hls demuxer gives each segment's own timestamps, unchanged at an `EXT-X-DISCONTINUITY`; D-028's rule then stopped pairing at the tag, 30 pairs and the 60 frames after it failed. With D-046 every frame pairs, 60 pairs and no failure (`m5-d046/record.txt`).
- **M2, M3**, one pair's and the ladder's memory and time before the pool: `eval/phase2-survey/m2_m3_baseline.sh`; MEASUREMENT PENDING.

## Tests

The pool, the memory limit with a stand-in for the memory reading, the cancel, a failed pair, the options and the report's new fields (`tests/test_vmaf_ladder.cpp`, `test_vmaf_cli.cpp`, `test_vmaf_report.cpp`); the discontinuity sequences on timestamp sequences and on MPEG-TS clips (`tests/test_vmaf_align.cpp`, `test_vmaf_analyzer.cpp`). The unit test record of Phase 2 is MEASUREMENT PENDING.

## Open for the owner

- P-068: D-046's sequences come from the timestamps, not the playlists' tags; provisional until the owner decides.
- D-045's default memory limit, once M2 and M3 are measured.
- P-067 to P-069, the agent's choices, pending confirmation.

## Before the Phase 2 PR

Phase 1's PR first (hard rule 5: one PR per phase); then the acceptance runs, a unit test record and a rerun of every committed measurement with the Phase 2 program, and this branch brought up to date with Phase 1's.
