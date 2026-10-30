# The ladder at several pool sizes: PASS

- Ladder: `<ladder>`, every frame pair of each pair (`cmd.txt`).
- PASS when every run exits 0 with a complete report, each pair's frames (indices, timestamps, scores at full precision) and pooled values are the same in every run, and with --memory-limit 1G, 1073741824 bytes, every report gives that limit and neither its peak resident memory nor /usr/bin/time -l's maximum resident set size is over it.

| --jobs | exit | status | pool size | most at once | pair memory at each admission (report) | memory limit | memory before the run (report) | peak resident (report) | max RSS (time -l) | wall s |
|---|---|---|---|---|---|---|---|---|---|---|
| default | 0 | complete | 11 | 3 | pair 1: 269713408; pair 2: 269713408 | 1073741824 | 26935296 | 1068269568 | 1068269568 | 1882.86 |

| pair | distorted | frames |
|---|---|---|
| 0 | `https://devstreaming-cdn.apple.com/videos/streaming/examples/img_bipbop_adv_example_ts/v5/prog_index.m3u8` | 36000 |
| 1 | `https://devstreaming-cdn.apple.com/videos/streaming/examples/img_bipbop_adv_example_ts/v8/prog_index.m3u8` | 36000 |
| 2 | `https://devstreaming-cdn.apple.com/videos/streaming/examples/img_bipbop_adv_example_ts/v7/prog_index.m3u8` | 36000 |
| 3 | `https://devstreaming-cdn.apple.com/videos/streaming/examples/img_bipbop_adv_example_ts/v6/prog_index.m3u8` | 36000 |
| 4 | `https://devstreaming-cdn.apple.com/videos/streaming/examples/img_bipbop_adv_example_ts/v4/prog_index.m3u8` | 18000 |
| 5 | `https://devstreaming-cdn.apple.com/videos/streaming/examples/img_bipbop_adv_example_ts/v3/prog_index.m3u8` | 18000 |
| 6 | `https://devstreaming-cdn.apple.com/videos/streaming/examples/img_bipbop_adv_example_ts/v2/prog_index.m3u8` | 18000 |
