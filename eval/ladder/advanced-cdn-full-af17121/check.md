# The ladder at several pool sizes: FAIL

- Ladder: `<ladder>`, every frame pair of each pair (`cmd.txt`).
- PASS when every run is complete and each pair's frames (indices, timestamps, scores at full precision) and pooled values are the same in every run.

| --jobs | exit | status | pool size | most at once | memory limit | peak resident (report) | max RSS (time -l) | wall s |
|---|---|---|---|---|---|---|---|---|
| default | 0 | complete | 11 | 3 | 1073741824 | 1096171520 | 1096171520 | 1907.718 |

| pair | distorted | frames |  |
|---|---|---|
| 0 | `https://devstreaming-cdn.apple.com/videos/streaming/examples/img_bipbop_adv_example_ts/v5/prog_index.m3u8` | 36000 |  |
| 1 | `https://devstreaming-cdn.apple.com/videos/streaming/examples/img_bipbop_adv_example_ts/v8/prog_index.m3u8` | 36000 |  |
| 2 | `https://devstreaming-cdn.apple.com/videos/streaming/examples/img_bipbop_adv_example_ts/v7/prog_index.m3u8` | 36000 |  |
| 3 | `https://devstreaming-cdn.apple.com/videos/streaming/examples/img_bipbop_adv_example_ts/v6/prog_index.m3u8` | 36000 |  |
| 4 | `https://devstreaming-cdn.apple.com/videos/streaming/examples/img_bipbop_adv_example_ts/v4/prog_index.m3u8` | 18000 |  |
| 5 | `https://devstreaming-cdn.apple.com/videos/streaming/examples/img_bipbop_adv_example_ts/v3/prog_index.m3u8` | 18000 |  |
| 6 | `https://devstreaming-cdn.apple.com/videos/streaming/examples/img_bipbop_adv_example_ts/v2/prog_index.m3u8` | 18000 |  |

Problems:
- jobs 0: the report's peak resident memory, 1096171520, is over the memory limit, 1073741824
- jobs 0: /usr/bin/time -l's maximum resident set size, 1096171520, is over the memory limit, 1073741824
