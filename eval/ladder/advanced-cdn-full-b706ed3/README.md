# §2's acceptance over every frame, from Apple's CDN, with D-053's margin: the peak under 1 GiB

`python3 -I -B eval/ladder/pool_check.py <mirage_vmaf> <out_dir> --ladder https://devstreaming-cdn.apple.com/videos/streaming/examples/img_bipbop_adv_example_ts/master.m3u8 --frames 0 --jobs 0 --memory-limit 1G`, with `eval/ladder/pool_check.py` of `4587c2d` and the `mirage_vmaf` built in a new clean worktree of `b706ed3` (`build.txt`, `configure.log`, `build.log`; the build of `../advanced-local-300-mem1g-b706ed3/`), whose rule counts each pair, running or new, at what a pair uses plus 25% (D-053): D-049's run over every frame at the default size, which reads Apple's CDN as its check, repeated with D-053's margin (P-075). 2026-10-27, from 21:33:16Z to 22:08:55Z, on AC power (`run.log`), with no sleep or wake event in `pmset`'s log in between.

**Result** (`check.md`): PASS. The run is complete: all 7 pairs of the advanced sample's master, every frame, 36000 frame pairs for each of the 4 rungs at 60 fps and 18000, on the coarser grid, for each of the 3 at 30 fps, with no frame failing to pair (`jobs-default/report.json.xz`). At the default size, 11 workers, at most 2 pairs ran at once, and the process peaked at 730497024 bytes, by its report and by `/usr/bin/time -l`, under the limit of 1073741824. The run took 2129.9 s.

- Pairs 0 and 1 started 0 and 3.1 s into the run, pairs 2 and 3 at 616.9 and 619.2 s, as the first two ended, and pairs 4, 5 and 6 one at a time, each when the one before ended (`started_s`, `ended_s`).
- The report's `pair_memory`, 705085440 bytes, is what the rule measured a pair to use at its largest: more than twice one pair's peak over every frame from Apple's CDN, 345407488 bytes (`../advanced-cdn-pair-full/record.txt`), as the resident memory still counts memory of ended pairs (`../advanced-local-300-mem1g-b706ed3/README.md`).
- With D-051's 10%, the same run peaked 2.1% over the limit, with 3 pairs at once (`../advanced-cdn-full-af17121/`).

`SHA256SUMS` covers every file here.
