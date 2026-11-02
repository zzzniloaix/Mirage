# The memory limit at 300 frames with D-051's margin: the peak under the limit

`python3 -I -B eval/ladder/pool_check.py <mirage_vmaf> <out_dir> --ladder <copy_dir>/master.m3u8 --frames 300 --jobs 1,0 --memory-limit 1G`, with the `mirage_vmaf` built in a new clean worktree of `af17121` (`build.txt`, `configure.log`, `build.log`), whose rule is D-045's with P-072's warm-up and D-051's margin: no pair starts while a running pair hasn't scored 30 frame pairs, and a pair starts while the memory before the run, plus what a pair uses as measured and 10% more for each running pair and the new one, stays within the limit. On the local copy of Apple's advanced sample (`eval/streams/local-advanced-2026-10-26/`); 2026-10-27, from 19:55:30Z to 19:56:15Z, on AC power (P-073).

**Result** (`check.md`): PASS. Both runs are complete, every pair is the same in both, and neither run's peak is over the limit. At the default size 3 pairs ran at once, started 0, 0.354 and 0.775 s into the run (`jobs-default/report.json.xz`, `started_s`), and the process peaked at 825720832 bytes, under the limit of 1073741824. After the first three, the other four ran one at a time, each starting when the one before ended.

`SHA256SUMS` covers every file here.
