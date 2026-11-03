# The ladder at several pool sizes: FAIL

- Ladder: `<ladder>`, the first 300 frame pairs of each pair (`cmd.txt`).
- PASS when every run is complete and each pair's frames (indices, timestamps, scores at full precision) and pooled values are the same in every run.

| --jobs | exit | status | pool size | most at once | memory limit | peak resident (report) | max RSS (time -l) | wall s |
|---|---|---|---|---|---|---|---|---|
| 1 | 0 | complete | 1 | 1 | 1073741824 | 303955968 | 304824320 | 30.692 |
| default | 0 | complete | 11 | 7 | 1073741824 | 2011676672 | 2011676672 | 17.784 |

| pair | distorted | frames | same at --jobs default |
|---|---|---|---|
| 0 | `<ladder_dir>/v5/prog_index.m3u8` | 300 | yes |
| 1 | `<ladder_dir>/v8/prog_index.m3u8` | 300 | yes |
| 2 | `<ladder_dir>/v7/prog_index.m3u8` | 300 | yes |
| 3 | `<ladder_dir>/v6/prog_index.m3u8` | 300 | yes |
| 4 | `<ladder_dir>/v4/prog_index.m3u8` | 300 | yes |
| 5 | `<ladder_dir>/v3/prog_index.m3u8` | 300 | yes |
| 6 | `<ladder_dir>/v2/prog_index.m3u8` | 300 | yes |

Problems:
- jobs 0: the report's peak resident memory, 2011676672, is over the memory limit, 1073741824
- jobs 0: /usr/bin/time -l's maximum resident set size, 2011676672, is over the memory limit, 1073741824
