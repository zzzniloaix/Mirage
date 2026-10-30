# eval/run_all.py run

From 2026-10-23T14:29:36Z to 2026-10-23T15:14:43Z, 2707.1 s of wall time in all; repository at e0beb49d42f3331681fad2d76a282ddf95fd8d7e. Command:

    eval/run_all.sh <out_dir> --only pooling-full --jobs 4

| step | command | exit status | wall time (s) |
|---|---|---|---|
| build head | `git -C <repo> worktree add --detach <worktree> e0beb49d42f3331681fad2d76a282ddf95fd8d7e` | 0 | 0.3 |
| build head | `cmake --preset macos-release` | 0 | 85.3 |
| build head | `cmake --build --preset macos-release --target mirage_vmaf` | 0 | 2.0 |
| acceptance-full | `eval/consistency/run_delta.sh <out_dir>/builds/head/mirage_vmaf <out_dir>/consistency/acceptance-full/plain --pairs eval/consistency/pairs/bipbop16x9.txt --scaler bilinear --model vmaf_v0.6.1 --threads 4 --threshold 0.01 --jobs 4 --ffmpeg /opt/homebrew/opt/ffmpeg-full/bin/ffmpeg --ffprobe /opt/homebrew/opt/ffmpeg-full/bin/ffprobe` | 0 | 2614.8 |
| pooling-full | `/opt/homebrew/opt/python@3.14/bin/python3.14 -I -B eval/pooling/pooling.py --delta-run <out_dir>/consistency/acceptance-full/plain --out <out_dir>/pooling/acceptance-full/pooling` | 0 | 1.7 |

Wall time per step (s): acceptance-full 2614.8, pooling-full 1.7
