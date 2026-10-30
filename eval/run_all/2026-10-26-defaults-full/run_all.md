# eval/run_all.py run

From 2026-10-26T21:20:40Z to 2026-10-26T22:06:00Z, 2719.8 s of wall time in all; repository at 1f7d42d23dc5fefa33409d5624941247e00e9d67. Command:

    eval/run_all.sh <out_dir> --only defaults-full,pooling-defaults-full --jobs 4

| step | command | exit status | wall time (s) |
|---|---|---|---|
| build head | `git -C <repo> worktree add --detach <worktree> 1f7d42d23dc5fefa33409d5624941247e00e9d67` | 0 | 0.6 |
| build head | `cmake --preset macos-release` | 0 | 43.8 |
| build head | `cmake --build --preset macos-release --target mirage_vmaf` | 0 | 3.0 |
| defaults-full | `eval/consistency/run_delta.sh <out_dir>/builds/head/mirage_vmaf <out_dir>/consistency/defaults-full/plain --pairs eval/consistency/pairs/bipbop16x9.txt --scaler bicubic --model vmaf_v0.6.1 --threads 4 --threshold 0.01 --jobs 4 --ffmpeg /opt/homebrew/opt/ffmpeg-full/bin/ffmpeg --ffprobe /opt/homebrew/opt/ffmpeg-full/bin/ffprobe` | 0 | 2666.9 |
| pooling-defaults-full | `/opt/homebrew/opt/python@3.14/bin/python3.14 -I -B eval/pooling/pooling.py --delta-run <out_dir>/consistency/defaults-full/plain --check-pooled --out <out_dir>/pooling/defaults-full/pooling` | 0 | 2.0 |

Wall time per step (s): defaults-full 2666.9, pooling-defaults-full 2.0
