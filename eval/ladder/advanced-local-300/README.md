# §2's acceptance across pool sizes, on the local copy of Apple's advanced sample

`python3 -I -B eval/ladder/pool_check.py <mirage_vmaf> <out_dir> --ladder <copy_dir>/master.m3u8 --frames 300`, as of `1cc1ab0`: the ladder of the local copy of Apple's advanced HLS sample (`eval/streams/local-advanced-2026-10-26/`), its top rung `v9` against each of its 7 other video playlists, scored at the pool sizes 1, 2, 4 and the default, the first 300 frame pairs of each pair (D-049; P-071).

- **The program**: `mirage_vmaf` built with the `macos-release` preset in a new clean worktree of `1cc1ab0` on `vmaf/phase-2` (`build.txt`, `configure.log`, `build.log`), SHA-256 `1536b315b003269a8f3fb6dd033604c5613862139d9bb2ffae66d6099de1d03e`.
- **When**: 2026-10-27, from 18:31:31Z to 18:32:53Z, one run at a time, after M2 and M3 (`eval/phase2-survey/m2-m3/record.txt`) and nothing else running, the machine on battery power, with no sleep or wake event in `pmset`'s log from 18:20Z to 18:33Z.
- **Result** (`check.md`): PASS. Every run is complete, and each pair's frames, their indices, timestamps and scores at full precision, and its pooled values are the same at every size.
- The run at the default size had 11 workers and 7 pairs at once, one per pair; its peak resident memory, 2009759744 bytes, is the report's (`pool.peak_resident`) and `/usr/bin/time -l`'s. The peaks at sizes 1, 2 and 4 are 304529408, 581894144 and 1111425024 bytes by the reports.
- The wall times, 33.146, 20.173, 13.558 and 14.553 s, were measured on battery power, one run each.

| File | Contents |
|---|---|
| `cmd.txt` | the four commands, with `<mirage_vmaf>`, `<ladder>`, `<ladder_dir>` and `<out_dir>` for paths |
| `jobs-1/`, `jobs-2/`, `jobs-4/`, `jobs-default/` | each run's report (`report.json.xz`), standard output and error, and `/usr/bin/time -l`'s lines (`time.txt`) |
| `check.md`, `check.json` | the comparison and its verdict |
| `build.txt`, `configure.log`, `build.log` | the build of the program, with `<worktree>` for the worktree's path |
| `SHA256SUMS` | SHA-256 of every file here |
