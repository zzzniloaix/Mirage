# §2's acceptance over every frame, from Apple's CDN, with a limit of 1 GiB: the peak 2.1% over it

`python3 -I -B eval/ladder/pool_check.py <mirage_vmaf> <out_dir> --ladder https://devstreaming-cdn.apple.com/videos/streaming/examples/img_bipbop_adv_example_ts/master.m3u8 --frames 0 --jobs 0 --memory-limit 1G`, with `eval/ladder/pool_check.py` of `cee3540` and the `mirage_vmaf` built in a new clean worktree of `af17121` (`build.txt`, `configure.log`, `build.log`), whose rule is D-045's with P-072's warm-up and D-051's margin: D-049's run over every frame at the default size, which reads Apple's CDN as its check (P-073). 2026-10-27, from 19:58:15Z to 20:30:10Z, on AC power, with no sleep or wake event in `pmset`'s log in between.

**Result** (`check.md`): FAIL. The run is complete: all 7 pairs of the advanced sample's master, every frame, 36000 frame pairs for each of the 4 rungs at 60 fps and 18000, on the coarser grid, for each of the 3 at 30 fps, with no frame failing to pair (`jobs-default/report.json.xz`). At the default size, 11 workers, 3 pairs ran at once, and the process peaked at 1096171520 bytes, by its report and by `/usr/bin/time -l`, 2.1% over the limit of 1073741824.

- Pairs 0, 1 and 2 started 0, 2.4 and 4.5 s into the run and ended 697.7, 695.0 and 699.4 s into it; the other four ran one at a time, each starting when the one before ended (`started_s`, `ended_s`). The run took 1907.7 s.
- The rule counted each pair at what a pair used after its first 30 frame pairs, plus 10%. The three pairs of the first wave, scored together for about 695 s, reached a peak that the count didn't cover.
- `jobs-default/stderr.txt` holds TLS reads of Apple's CDN that timed out, at D-036's 5 s limit, and the hls demuxer's new connection for `v8/fileSequence27.ts`; every pair is complete with every frame paired.

`SHA256SUMS` covers every file here.
