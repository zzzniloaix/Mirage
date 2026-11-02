# The memory limit at 300 frames with dfed5a3's rule: the peak 5.4% over the limit

`python3 -I -B eval/ladder/pool_check.py <mirage_vmaf> <out_dir> --ladder <copy_dir>/master.m3u8 --frames 300 --jobs 1,0 --memory-limit 1G`, with the `mirage_vmaf` built in a new clean worktree of `dfed5a3` (`build.txt`, `configure.log`, `build.log`), whose rule is D-045's with a warm-up: no pair starts while a running pair hasn't scored 30 frame pairs, and a pair starts while the resident memory plus what a pair uses, as measured then, stays within the limit (P-072). On the local copy of Apple's advanced sample (`eval/streams/local-advanced-2026-10-26/`); 2026-10-27, `warm_up.txt` from 18:57:47Z, the check from 18:57:56Z to 18:58:47Z, on battery power.

**Result** (`check.md`): FAIL. Both runs are complete and every pair is the same in both. At the default size the pairs started one after another, 0, 0.42, 0.9 and 1.521 s into the run, each once the one before had warmed up, and 4 ran at once; the process peaked at 1131544576 bytes, 5.4% over the limit of 1073741824. The rule lets a pair in on what the running pairs use when it starts, and a pair uses more later than at its 30th frame pair: one pair's peak alone is 284639232 bytes after 30 frame pairs and 293208064 after 300 (`warm_up.txt`).

After the first four ended, pairs 4, 5 and 6 ran one at a time, each starting when the one before ended, and the report warns that "pair 4 of 7 took the process over the memory limit on its own: 1095270400 bytes of resident memory". 1095270400 bytes is more than three times one pair's peak, so the resident memory then still counted what the ended pairs had used, as in the run before the fix (`../advanced-local-300-mem1g-1cc1ab0/README.md`).

`SHA256SUMS` covers every file here.
