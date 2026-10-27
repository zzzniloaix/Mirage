# The memory limit at 300 frames, before the fix of 60c1f7b: the limit didn't hold

`python3 -I -B eval/ladder/pool_check.py <mirage_vmaf> <out_dir> --ladder <copy_dir>/master.m3u8 --frames 300 --jobs 1,0 --memory-limit 1G`, with `eval/ladder/pool_check.py` of `048a9ac`, which checks each run's peak against the limit, and the `mirage_vmaf` built in a new clean worktree of `1cc1ab0` (`build.txt`; the build's logs are in `../advanced-local-300/`), on the local copy of Apple's advanced sample (`eval/streams/local-advanced-2026-10-26/`); 2026-10-27, from 18:37:20Z to 18:38:08Z, on battery power (P-071).

**Result** (`check.md`): FAIL. Both runs are complete and every pair is the same in both, but at the default size, 11 workers, 7 pairs ran at once and the process's peak resident memory, 2011676672 bytes by its report and by `/usr/bin/time -l`, is over the limit of 1073741824 bytes. The 7 pairs started from 0.000086 to 0.000342 s after the run began (`jobs-default/report.json.xz`, `started_s`): every worker asked to start a pair at once, before any pair had allocated what it uses, and the rule of `1cc1ab0` took a pair to use the memory gained since the run began, then next to none. `60c1f7b` changes the rule (P-072).

The report of that run also warns that "pair 7 of 7 took the process over the memory limit on its own: 1331806208 bytes of resident memory". The warning is made when one pair runs; then the six others had ended, and 1331806208 bytes is more than four times one pair's peak in M2 (`eval/phase2-survey/m2-m3/record.txt`), so the resident memory then still counted memory the ended pairs had used.

`SHA256SUMS` covers every file here.
