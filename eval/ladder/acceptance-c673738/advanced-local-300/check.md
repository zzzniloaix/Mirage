# The ladder at several pool sizes: PASS

- Ladder: `<ladder>`, the first 300 frame pairs of each pair (`cmd.txt`).
- PASS when every run exits 0 with a complete report, each pair's frames (indices, timestamps, scores at full precision) and pooled values are the same in every run.

| --jobs | exit | status | pool size | most at once | pair memory at each admission (report) | memory limit | memory before the run (report) | peak resident (report) | max RSS (time -l) | wall s |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | 0 | complete | 1 | 1 | - | - | - | 305266688 | 306364416 | 23.304 |
| 2 | 0 | complete | 2 | 2 | - | - | - | 604913664 | 605962240 | 15.614 |
| 4 | 0 | complete | 4 | 4 | - | - | - | 1187807232 | 1187807232 | 12.099 |
| default | 0 | complete | 11 | 7 | - | - | - | 2082406400 | 2082406400 | 12.061 |

| pair | distorted | frames | same at --jobs 2 | same at --jobs 4 | same at --jobs default |
|---|---|---|---|---|---|
| 0 | `<ladder_dir>/v5/prog_index.m3u8` | 300 | yes | yes | yes |
| 1 | `<ladder_dir>/v8/prog_index.m3u8` | 300 | yes | yes | yes |
| 2 | `<ladder_dir>/v7/prog_index.m3u8` | 300 | yes | yes | yes |
| 3 | `<ladder_dir>/v6/prog_index.m3u8` | 300 | yes | yes | yes |
| 4 | `<ladder_dir>/v4/prog_index.m3u8` | 300 | yes | yes | yes |
| 5 | `<ladder_dir>/v3/prog_index.m3u8` | 300 | yes | yes | yes |
| 6 | `<ladder_dir>/v2/prog_index.m3u8` | 300 | yes | yes | yes |
