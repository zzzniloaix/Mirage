# eval/run_all.sh, every step, at a4692a2

`eval/run_all.sh <out_dir> --jobs 4`, every step, the four over every frame included, from a new clean worktree of `a4692a2`, the last commit of the review fixes that changes code or scripts, with its output directory and `TMPDIR` outside the repository (docs/DECISIONS.md P-065). Its command, programs and steps are in `run_all.md`, the comparison with the committed outputs in `rerun_check.md`.

**Result**: every step but two found everything as committed, `acceptance-full` and `pooling-full` included. `defaults-full` did not: two of its four pairs, `gear4_vs_gear5` and `gear3_vs_gear5`, have every verdict and per-frame score of both sides as committed, and the other two failed because the machine went to sleep while they were being scored. `pooling-defaults-full`, which reads `defaults-full`'s reports, exited with status 1 and wrote no output, since those two pairs have none. The two steps were run again on their own (docs/DECISIONS.md P-065).

Besides the files every record in `eval/run_all/` keeps, this one has:

- `consistency/defaults-full/plain/`: the files of the failed `defaults-full` run that show why it failed, at the paths the run wrote them under `<out_dir>`: the run's `delta.md`, `delta.json`, `environment.txt`, `run.cmd.txt` and `pairs.txt`, and of the two failed pairs their `cmd.txt`, `delta.md`, `delta.json` and both programs' stderr logs. Not the pairs' JSON outputs, nor the two pairs that ran as committed.
- `power_log.txt`: the machine's sleep and wake events during the run, from `pmset -g log`, in UTC.
- `stall_check.txt`: the CPU time and state of the four scoring processes of the two failed pairs after the machine woke, sampled twice 120 s apart, the times their stderr logs were last written, and how they were stopped.

**What happened** (the files above):

- `defaults-full` started at 2026-10-26T19:13:27Z (`consistency/defaults-full/plain/delta.md`). With `--jobs 4` the delta tool runs four of its eight scoring processes at a time, in the pairs' order (`eval/consistency/run_delta.py`), so `gear2_vs_gear5` and `gear1_vs_gear5` started when the processes of `gear4_vs_gear5` and `gear3_vs_gear5` had ended.
- At 2026-10-26T19:51:20Z the machine went to sleep because its lid was closed, on battery ("Clamshell Sleep", `power_log.txt`). It woke at 2026-10-26T21:12:21Z, when the lid was opened. In between it woke only for the system's maintenance ("DarkWake"), for 2 and 8 s.
- The stderr logs of the four scoring processes of `gear2_vs_gear5` and `gear1_vs_gear5`, `mirage_vmaf` and `ffmpeg` alike, were last written at 2026-10-26T19:51:16Z and 19:51:17Z. None of them has a line reporting an error, a failure or a time-out: the only lines with the word "error" are `ffmpeg`'s closing statistics ("0 decode errors"), and no segment failed to open. After the machine woke, each process's CPU time rose by 0.05 s in 120 s (`stall_check.txt`): they were waiting, and made no progress.
- They were stopped with SIGTERM at 2026-10-26T21:17:01Z, and the two `ffmpeg` processes, still running 10 s later, with a second SIGTERM (`stall_check.txt`). `mirage_vmaf` exited with status -15 and `ffmpeg` with 255 (`gear2_vs_gear5/delta.md`, `gear1_vs_gear5/delta.md`). `ffmpeg` wrote its libvmaf log as it stopped: it holds 25091 frames of `gear2_vs_gear5` and 21310 of `gear1_vs_gear5`, of which 15 and 14 have other metrics than the committed run (`rerun_check.md`); `mirage_vmaf` wrote no report.
- `src/VMAFAnalyzer.cpp` opens its inputs without FFmpeg's `timeout` and `rw_timeout` options or an interrupt callback, and FFmpeg 8.1.1's TCP reads then wait without a time limit; the delta tool's `ffmpeg` command gives no such option either (`gear2_vs_gear5/cmd.txt`). This is the open item "A network read without a time limit" in `docs/PHASE1_DECISIONS_PENDING.md` (P-062).
- The times the tools record count only the time the machine was awake (`2026-10-24-full/README.md`). `run_all.md` gives 6912.5 s in all, for a run whose start and end are 3 h 16 min apart.

`SHA256SUMS` covers every file here.
