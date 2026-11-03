# The ladder at several pool sizes: PASS

- Ladder: `<ladder>`, the first 300 frame pairs of each pair (`cmd.txt`).
- PASS when every run is complete and each pair's frames (indices, timestamps, scores at full precision) and pooled values are the same in every run.

| --jobs | exit | status | pool size | most at once | peak resident (report) | max RSS (time -l) | wall s |
|---|---|---|---|---|---|---|---|
| 1 | 0 | complete | 1 | 1 | 304529408 | 305610752 | 33.146 |
| 2 | 0 | complete | 2 | 2 | 581894144 | 582926336 | 20.173 |
| 4 | 0 | complete | 4 | 4 | 1111425024 | 1111425024 | 13.558 |
| default | 0 | complete | 11 | 7 | 2009759744 | 2009759744 | 14.553 |

| pair | distorted | frames | same at --jobs 2 | same at --jobs 4 | same at --jobs default |
|---|---|---|---|---|---|
| 0 | `<ladder_dir>/v5/prog_index.m3u8` | 300 | yes | yes | yes |
| 1 | `<ladder_dir>/v8/prog_index.m3u8` | 300 | yes | yes | yes |
| 2 | `<ladder_dir>/v7/prog_index.m3u8` | 300 | yes | yes | yes |
| 3 | `<ladder_dir>/v6/prog_index.m3u8` | 300 | yes | yes | yes |
| 4 | `<ladder_dir>/v4/prog_index.m3u8` | 300 | yes | yes | yes |
| 5 | `<ladder_dir>/v3/prog_index.m3u8` | 300 | yes | yes | yes |
| 6 | `<ladder_dir>/v2/prog_index.m3u8` | 300 | yes | yes | yes |
