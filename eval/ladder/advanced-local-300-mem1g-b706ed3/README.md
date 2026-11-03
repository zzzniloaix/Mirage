# The memory limit at 300 frames with D-053's margin of 25%

`python3 -I -B eval/ladder/pool_check.py <mirage_vmaf> <out_dir> --ladder <copy_dir>/master.m3u8 --frames 300 --jobs 1,0 --memory-limit 1G`, with `eval/ladder/pool_check.py` and the `mirage_vmaf` built in a new clean worktree of `b706ed3` (`build.txt`, `configure.log`, `build.log`), whose rule counts each pair, running or new, at what a pair uses plus 25% (D-053), and whose report gives what the rule measured a pair to use (`pair_memory`). On the local copy of Apple's advanced sample (`eval/streams/local-advanced-2026-10-26/`), with one pair's peak memory after 1 to 300 frame pairs (`warm_up.txt`); 2026-10-27, the check from 21:21:49Z to 21:22:32Z, on AC power (`run.log`; P-074).

**Result** (`check.md`): PASS. Both runs are complete, every pair is the same in both, and neither run's peak is over the limit. At the default size 3 pairs ran at once, started 0, 0.329 and 0.747 s into the run (`jobs-default/report.json.xz`, `started_s`), and the process peaked at 913162240 bytes, under the limit of 1073741824. After the first three, the other four ran one at a time, each starting when the one before ended.

The report's `pair_memory`, 896270336 bytes, is more than three times one pair's peak in `warm_up.txt`, 292896768 bytes after 300 frame pairs: the rule measures a pair as the memory gained since the run began shared out among the pairs running, and once the first pairs had ended, the process's resident memory still counted memory they had used, shared out among fewer pairs.

`SHA256SUMS` covers every file here.
