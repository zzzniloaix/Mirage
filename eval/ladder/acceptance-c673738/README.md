# §2's acceptance checks with the program of Phase 2's PR, at c673738

The three checks of §2's acceptance (docs/DECISIONS.md D-049), rerun on 2026-10-30 after D-063's rerun had ended, with `eval/ladder/pool_check.py` and the `mirage_vmaf` built in a new clean worktree of `c673738` (`build.txt`, `configure.log`, `build.log`), whose code and scripts are those of `e8e3a10`, the code Phase 2's PR brings (P-080). On macOS 26.6.2, on AC power, from 10:57:15Z to 11:31:59Z (`run.log`). Each check ran as its committed record ran it:

- `advanced-local-300/`: `pool_check.py <mirage_vmaf> <out_dir> --ladder <copy_dir>/master.m3u8 --frames 300`, the pool sizes 1, 2, 4 and the default, as `../advanced-local-300/`.
- `advanced-local-300-mem1g/`: the same with `--jobs 1,0 --memory-limit 1G`, as `../advanced-local-300-mem1g-b706ed3/`.
- `advanced-cdn-full/`: `--ladder <Apple's CDN master> --frames 0 --jobs 0 --memory-limit 1G`, every frame, as `../advanced-cdn-full-b706ed3/`.

Besides them:

- `copy.md` and `copy_check.txt`: the local copy of Apple's advanced sample that the first two read, made again with `eval/streams/local_copy.py --segments 2`, and its check against the committed copy's checksums: all 25 of its files have the SHA-256s of `eval/streams/local-advanced-2026-10-26/SHA256SUMS`.
- `comparison.txt`: each run compared with the committed record of the same check, every field of every frame and the pooled values, by a script of the agent's, with the script.
- `runner.sh` and `run.log`: the script that ran the checks, and its log.
- `power_log.txt`: the machine's sleep and wake events during the checks, from `pmset -g log`, in UTC: none.

**Result**: all three PASS (each `check.md`), and every pair's frames and pooled values are those of the committed records: 8400 frames in the four runs at 300 frames, 4200 in the two with the limit and 198000 over every frame (`comparison.txt`). The program has the SHA-256 `454db2ed6be26319975df2130b53fc4fd5a60ed6660cbdfef6278ba88dc8435b` (`build.txt`), that of D-063's rerun (`../../run_all/2026-10-30-d063/`).

- **Pool sizes** (`advanced-local-300/check.md`): at the sizes 1, 2, 4 and the default, 11, at which all 7 pairs ran at once, the peaks were 305266688, 604913664, 1187807232 and 2082406400 bytes and the wall times 23.304, 15.614, 12.099 and 12.061 s.
- **1 GiB at 300 frames** (`advanced-local-300-mem1g/check.md`): at the default size 3 pairs ran at once; the limit let pairs 1 and 2 start at a measure of 254459904 bytes a pair, with 18497536 bytes before the run, and the process peaked at 874905600 bytes, under the limit of 1073741824. The committed run peaked at 913162240 bytes, with 3 pairs at once.
- **1 GiB over every frame from Apple's CDN** (`advanced-cdn-full/check.md`, `jobs-default/report.json.xz`): complete, all 7 pairs, every frame, in 1882.86 s. Pairs 0, 1 and 2 started 0, 1.250 and 2.519 s into the run, and the other four one at a time once those had ended (`started_s`, `ended_s`). The limit let pairs 1 and 2 start at a measure of 269713408 bytes a pair, with 26935296 bytes before the run (`pool.pair_memory`, `pool.baseline`), so that for three pairs it counted 26935296 + 3 × 1.25 × 269713408 = 1038360576 bytes (`vmafladder::admits()`). The process peaked at 1068269568 bytes, by its report and by `/usr/bin/time -l`: 29908992 bytes above what the rule counted, and 5472256 bytes, 0.51%, under the limit. The committed run of `b706ed3` had at most 2 pairs at once and peaked at 730497024 bytes; its report gives the rule's measure only at its largest, 705085440 bytes, not at each start, which D-058 added.
- What else ran isn't recorded beyond `run.log`'s load averages, 13.18, 14.34 and 23.43 as the run over every frame started, after the rerun of D-063, and 5.28, 5.99 and 10.13 as it ended. The wall times depend on that load, and on the power source, and don't compare with the committed runs' (`../advanced-local-300/` ran on battery power).

`SHA256SUMS` covers every file here.
