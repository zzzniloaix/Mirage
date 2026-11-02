# The ladder at several pool sizes: PASS

- Ladder: `<ladder>`, the first 300 frame pairs of each pair (`cmd.txt`).
- PASS when every run is complete and each pair's frames (indices, timestamps, scores at full precision) and pooled values are the same in every run.

| --jobs | exit | status | pool size | most at once | pair memory (report) | memory limit | peak resident (report) | max RSS (time -l) | wall s |
|---|---|---|---|---|---|---|---|---|---|
| 1 | 0 | complete | 1 | 1 | - | 1073741824 | 304021504 | 304906240 | 23.591 |
| default | 0 | complete | 11 | 3 | 896270336 | 1073741824 | 913162240 | 913162240 | 19.762 |

| pair | distorted | frames | same at --jobs default |
|---|---|---|---|
| 0 | `<ladder_dir>/v5/prog_index.m3u8` | 300 | yes |
| 1 | `<ladder_dir>/v8/prog_index.m3u8` | 300 | yes |
| 2 | `<ladder_dir>/v7/prog_index.m3u8` | 300 | yes |
| 3 | `<ladder_dir>/v6/prog_index.m3u8` | 300 | yes |
| 4 | `<ladder_dir>/v4/prog_index.m3u8` | 300 | yes |
| 5 | `<ladder_dir>/v3/prog_index.m3u8` | 300 | yes |
| 6 | `<ladder_dir>/v2/prog_index.m3u8` | 300 | yes |
