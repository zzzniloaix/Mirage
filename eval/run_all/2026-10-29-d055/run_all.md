# eval/run_all.py run

From 2026-10-29T22:35:27Z to 2026-10-29T22:37:17Z, 109.8 s of wall time in all; repository at e006f814c28315f514f334335d8f3a32b4ed1a0b. Command:

    eval/run_all.sh <out_dir> --only defaults-300,pooling-defaults-300,ladder-300 --jobs 4

| step | command | exit status | wall time (s) |
|---|---|---|---|
| build head | `git -C <repo> worktree add --detach <worktree> e006f814c28315f514f334335d8f3a32b4ed1a0b` | 0 | 0.9 |
| build head | `cmake --preset macos-release` | 0 | 47.8 |
| build head | `cmake --build --preset macos-release --target mirage_vmaf` | 0 | 3.0 |
| defaults-300 | `eval/consistency/run_delta.sh <out_dir>/builds/head/mirage_vmaf <out_dir>/consistency/defaults-300/plain --frames 300 --pairs eval/consistency/pairs/bipbop16x9.txt --scaler bicubic --model vmaf_v0.6.1 --threads 4 --threshold 0.01 --jobs 4 --ffmpeg /opt/homebrew/opt/ffmpeg-full/bin/ffmpeg --ffprobe /opt/homebrew/opt/ffmpeg-full/bin/ffprobe` | 0 | 37.9 |
| ladder-300 | `/opt/homebrew/opt/python@3.14/bin/python3.14 -I -B eval/consistency/ladder_check.py <out_dir>/builds/head/mirage_vmaf <out_dir>/consistency/ladder-300/top-rung --ladder https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/bipbop_16x9_variant.m3u8 --pairs eval/consistency/pairs/bipbop16x9.txt --frames 300 --scaler bicubic --model vmaf_v0.6.1 --jobs 4` | 0 | 19.3 |
| pooling-defaults-300 | `/opt/homebrew/opt/python@3.14/bin/python3.14 -I -B eval/pooling/pooling.py --delta-run <out_dir>/consistency/defaults-300/plain --check-pooled --out <out_dir>/pooling/defaults-300/pooling` | 0 | 0.3 |

Wall time per step (s): defaults-300 37.9, ladder-300 19.3, pooling-defaults-300 0.3
