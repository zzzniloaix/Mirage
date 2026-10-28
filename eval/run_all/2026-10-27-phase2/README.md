# eval/run_all.sh, every step, with the Phase 2 program, at 4587c2d

`eval/run_all.sh <out_dir> --jobs 4`, every step, the four over every frame included, from a new clean worktree of `4587c2d` on `vmaf/phase-2`, the head after D-053's implementation, with its output directory and `TMPDIR` outside the repository (docs/DECISIONS.md P-076): the rerun of every committed Phase 1 measurement with the Phase 2 program that `docs/PHASE2_STATUS.md` lists before the Phase 2 PR. Its command, programs and steps are in `run_all.md`, the comparison with the committed outputs in `rerun_check.md`.

**Result**: 25 of the 27 steps found everything as committed, among them `defaults-full`, every verdict and all 53946 per-frame scores of each of its four pairs on Mirage's side at full precision and every libvmaf metric of FFmpeg's logs; the alignment cases made after D-028, now paired under D-050; the ladder check, which `mirage_vmaf --ladder` now scores with the pool; and the negotiation cases. `acceptance-full` did not: the machine went to sleep with its lid closed while it was being scored (below), and `pooling-full`, which reads its reports, differs too. The program the run built from `4587c2d` has the SHA-256 `6348ea3a5fd022e55603b0731b2af2f3a72c29d1b901eb1f133c76c0b7d86f40`, that of the build of `b706ed3` in `eval/ladder/advanced-local-300-mem1g-b706ed3/build.txt`.

Besides the files every record in `eval/run_all/` keeps, this one has:

- `consistency/acceptance-full/plain/`: the files of the failed `acceptance-full` run that show why it failed, at the paths the run wrote them under `<out_dir>`: the run's `delta.md`, `delta.json`, `environment.txt`, `run.cmd.txt` and `pairs.txt`, and of the three failed pairs their `cmd.txt`, `delta.md`, `delta.json`, `mirage_vmaf`'s stdout and both programs' stderr logs. Not the pairs' JSON outputs, nor the pair that ran as committed.
- `power_log.txt`: the machine's sleep and wake events during the run, from `pmset -g log`, in UTC.
- `stall_check.txt`: the CPU time and state of the two scoring processes still running when the run was looked at, sampled twice 120 s apart, the times their stderr logs were last written, and how they were stopped.

**What happened** (the files above):

- `acceptance-full` ran from 2026-10-27T22:43:07Z (`consistency/acceptance-full/plain/delta.md`). The machine went to sleep because its lid was closed at 23:06:13Z ("Clamshell Sleep", `power_log.txt`), woke fully only at 23:15:07Z for a few seconds, and woke for good at 2026-10-28T03:22:04Z, when the lid was opened; in between only the system's maintenance woke it ("DarkWake").
- In `acceptance-full`, `gear3_vs_gear5` is as committed. The `ffmpeg` processes of `gear2_vs_gear5` and `gear1_vs_gear5` last wrote their stderr logs at 23:06:14Z and 23:18:54Z, and after the wake used 0.04 and 0.03 s of CPU time in 120 s; they were stopped with SIGTERM, and a second SIGTERM 10 s later (`stall_check.txt`). The run then went on with its other steps.
- `mirage_vmaf`'s stderr logs of the three failed pairs hold network reads that timed out (6, 12 and 14 lines with "timed out" in `gear4_vs_gear5`, `gear2_vs_gear5` and `gear1_vs_gear5`) and segments that failed to open ("Failed to open segment", 2, 249 and 354 lines). Its reports scored 53217, 6281 and 4725 frame pairs, each with D-028's warning (`mirage.stdout.txt`); in `gear4_vs_gear5`, 1003 frames failed, 63 unmatched reference frames and 940 after a discontinuity, which the Phase 2 program counts once D-050 has stopped the pairing at jumps that aren't at the same place in both inputs.
- FFmpeg's side: `gear4_vs_gear5` scored all 53946 frames, as committed; `gear2_vs_gear5`'s and `gear1_vs_gear5`'s, stopped, had written 4001 and 1721 frames to their libvmaf logs as they exited (`rerun_check.md`).
- The times the tools record count only the time the machine was awake (`../2026-10-24-full/README.md`). `run_all.md` gives 7007.0 s in all, for a run whose start and end are 6 h 7 min apart.

`SHA256SUMS` covers every file here.
