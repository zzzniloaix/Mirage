# The memory limit at 300 frames with 60c1f7b's rule: the limit didn't hold

`python3 -I -B eval/ladder/pool_check.py <mirage_vmaf> <out_dir> --ladder <copy_dir>/master.m3u8 --frames 300 --jobs 1,0 --memory-limit 1G`, with the `mirage_vmaf` built in a new clean worktree of `a3d011b` (`build.txt`, `configure.log`, `build.log`), whose rule is `60c1f7b`'s: a pair warms up until it has scored 30 frame pairs, and the pairs warming up count at the memory measured for one pair (P-072). On the local copy of Apple's advanced sample (`eval/streams/local-advanced-2026-10-26/`); 2026-10-27, `warm_up.txt` from 18:49:17Z, the check from 18:49:26Z to 18:50:16Z, on battery power.

**Result** (`check.md`): FAIL. Both runs are complete and every pair is the same in both, but at the default size 4 pairs ran at once and the process peaked at 1132167168 bytes, over the limit of 1073741824. Pairs 1 to 3 started together 0.419 s into the run, when pair 0 had warmed up (`jobs-default/report.json.xz`, `started_s`).

`warm_up.txt`: one pair, the top rung against `v8`, both 1080p60, scored alone for its first 1 to 300 frame pairs; its peak resident memory is 284573696 bytes after 30 frame pairs and 293093376 after 300.

`SHA256SUMS` covers every file here.
