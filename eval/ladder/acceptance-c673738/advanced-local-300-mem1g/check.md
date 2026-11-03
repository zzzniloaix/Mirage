# The ladder at several pool sizes: PASS

- Ladder: `<ladder>`, the first 300 frame pairs of each pair (`cmd.txt`).
- PASS when every run exits 0 with a complete report, each pair's frames (indices, timestamps, scores at full precision) and pooled values are the same in every run, and with --memory-limit 1G, 1073741824 bytes, every report gives that limit and neither its peak resident memory nor /usr/bin/time -l's maximum resident set size is over it.

| --jobs | exit | status | pool size | most at once | pair memory at each admission (report) | memory limit | memory before the run (report) | peak resident (report) | max RSS (time -l) | wall s |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 0 | complete | 1 | 1 | - | 1073741824 | 18530304 | 306544640 | 307625984 | 22.767 |
| default | 0 | complete | 11 | 3 | pair 1: 254459904; pair 2: 254459904 | 1073741824 | 18497536 | 874905600 | 874905600 | 18.413 |

| pair | distorted | frames | same at --jobs default |
|---|---|---|---|
| 0 | `<ladder_dir>/v5/prog_index.m3u8` | 300 | yes |
| 1 | `<ladder_dir>/v8/prog_index.m3u8` | 300 | yes |
| 2 | `<ladder_dir>/v7/prog_index.m3u8` | 300 | yes |
| 3 | `<ladder_dir>/v6/prog_index.m3u8` | 300 | yes |
| 4 | `<ladder_dir>/v4/prog_index.m3u8` | 300 | yes |
| 5 | `<ladder_dir>/v3/prog_index.m3u8` | 300 | yes |
| 6 | `<ladder_dir>/v2/prog_index.m3u8` | 300 | yes |
