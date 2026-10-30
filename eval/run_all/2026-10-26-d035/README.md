# eval/run_all.sh, every step, at bbc669d

`eval/run_all.sh <out_dir> --jobs 4`, every step, the four over every frame included, from a new clean worktree of `bbc669d`, the last commit after D-035 to D-038 that changes code or scripts, with its output directory and `TMPDIR` outside the repository (docs/DECISIONS.md P-066). Its command, programs and steps are in `run_all.md`, the comparison with the committed outputs in `rerun_check.md`.

**Result**: every step but two found everything as committed, among them `acceptance-full`, `pooling-full` and the new `negotiation-clips` and `negotiation-cases`. `defaults-full` did not: `gear4_vs_gear5` has every verdict and per-frame score of both sides as committed, and the other three pairs failed while the machine slept, or with a process that had stopped making progress (below). `pooling-defaults-full`, which reads `defaults-full`'s reports, ran on them and differs from its committed outputs. The two steps were run again on their own (docs/DECISIONS.md P-066).

Besides the files every record in `eval/run_all/` keeps, this one has:

- `consistency/defaults-full/plain/`: the files of the failed `defaults-full` run that show why it failed, at the paths the run wrote them under `<out_dir>`: the run's `delta.md`, `delta.json`, `environment.txt`, `run.cmd.txt` and `pairs.txt`, and of the three failed pairs their `cmd.txt`, `delta.md`, `delta.json`, `mirage_vmaf`'s stdout and both programs' stderr logs. Not the pairs' JSON outputs, nor the pair that ran as committed.
- `power_log.txt`: the machine's sleep and wake events during the run, from `pmset -g log`, in UTC.
- `stall_check.txt`: the CPU time and state of the one scoring process still running when the run was looked at, sampled twice 120 s apart, the time its stderr log was last written, and how it was stopped.

**What happened** (the files above):

- The machine went to sleep because its lid was closed, on battery, at 2026-10-27T00:03:35Z ("Clamshell Sleep", `power_log.txt`), woke at 00:08:42Z when the lid was opened and slept again from 00:09:00Z; it woke for good at 17:06:51Z, when the lid was opened. In between it woke only for the system's maintenance ("DarkWake").
- The `ffmpeg` of `gear2_vs_gear5` last wrote its stderr log at 2026-10-26T23:59:41Z, 3 min 54 s before that sleep; at 2026-10-27T17:24:57Z and 17:26:57Z it had used 0.04 s of CPU time in 120 s (`stall_check.txt`). It was stopped with SIGTERM, and a second SIGTERM 10 s later, and exited with status 255 (`gear2_vs_gear5/delta.md`). The `ffmpeg` command of the delta tool sets no read time limit (`gear2_vs_gear5/cmd.txt`).
- `mirage_vmaf` reads its network inputs with 5 s limits since D-036 (`src/VMAFNet.h`). The stderr logs of its three failed pairs hold TLS reads that timed out ("IO error: Operation timed out", 4 lines in `gear3_vs_gear5`, 4 in `gear2_vs_gear5`, 20 in `gear1_vs_gear5`) and, in `gear1_vs_gear5`, segments that failed to open ("Failed to open segment"). Its reports scored 52442, 4425 and 15639 frame pairs of the 53946 of each pair, and each warned that more frames than the threshold failed to pair, most of them after a discontinuity, where D-028's rule stops pairing (`mirage.stdout.txt`).
- FFmpeg's side: `gear3_vs_gear5` scored all 53946 frames; `gear1_vs_gear5` 52184, its log with connections reset by the peer ("IO error: Connection reset by peer"); `gear2_vs_gear5`'s, stopped, had written 4562 frames to its libvmaf log as it exited (`rerun_check.md`).
- The times the tools record count only the time the machine was awake (`2026-10-24-full/README.md`). `run_all.md` gives 6615.3 s in all, for a run whose start and end are 18 h 47 min apart.

`SHA256SUMS` covers every file here.
