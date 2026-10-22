# Phase 0 现状基线

在一条公开 HLS ladder 上运行 Mirage **现有的** ladder VMAF 打分（`--vmaf`），不改任何代码。另外用 FFmpeg 的 libvmaf 滤镜对其中两档的前 300 帧做了诊断性对照。

这里每个文件都是原始输出，或者是生成这些输出的脚本。`docs/VMAF_GAP_REPORT.md` 里的数字都出自这里。

## 被测对象

| 项 | 值 | 出处 |
|---|---|---|
| 代码 | commit `a4e02895d2d9c51c620e6c140f5566944c80ec71`。在 `git worktree add --detach` 出来的干净目录里用 `cmake --preset macos-release` 编译 | `environment.txt`、`code_facts.txt` |
| 链接的库 | ffmpeg-full 8.1.1 的 `libavformat` / `libavcodec` / `libswscale`；libvmaf（Homebrew 包 3.1.0，库自报 3.0.0）；GLFW 3.4 | `environment.txt`（`otool -L`、`brew list`、`vmaf_version()`） |
| 机器 | Apple M3 Pro，macOS 26.2 | `environment.txt` |
| ladder | `https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/bipbop_16x9_variant.m3u8` | `inputs/`（抓取的 master 与两份 variant 播放列表，抓取时间见 `inputs/fetched_utc.txt`） |

## 怎么跑的

```
eval/baseline/run_baseline.sh <mirage> <ladder-url> <run-dir>       # 与本次运行完全相同的脚本
eval/baseline/finalize_baseline.sh <run-dir> eval/baseline/bipbop16x9
eval/baseline/collect_facts.sh <worktree> <mirage> eval/baseline     # environment.txt、code_facts.txt
```

`run_baseline.sh` 的行为：
- 用一个空的 `HOME` 启动 `mirage <url> --vmaf --qa-no-focus --window 1280x720`，外面套 `/usr/bin/time -l`，并设 6 小时的 alarm 兜底。
- 日志里出现 "VMAF report written to" 后，再等 2 秒结束播放器。现有工具写完报告后会继续播放，自己不会退出，所以 `rc.txt` 是 143（SIGTERM），属于预期。
- 脚本里写死的 `S=` 是本次会话的 scratch 目录，只用来放隔离的 `HOME`。

## 与"照 README 原样运行"的两处偏离

都记录在 `docs/DECISIONS.md` 里。

1. **P-003**：干净构建时 glad 生成步骤用的是 venv 里的 Python 3.13 + jinja2 3.1.6。原因是本机默认的 Homebrew Python 3.14.7 导入 `pyexpat` 失败，原始日志见 `build_failure_python314.log`。生成的 `gl.h` / `gl.c` / `khrplatform.h` 与主仓库构建逐字节一致。
2. **P-004**：现有代码把报告写到 `<URL>.vmaf.json`，相对于工作目录。为了让它写得出来，事先在工作目录下建好了 `https:/devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/`。实际写出的路径见 `bipbop16x9/report_path_written_by_mirage.txt`。

## 文件

| 文件 | 内容 |
|---|---|
| `bipbop16x9/mirage.log` | 播放器 stdout，原样 |
| `bipbop16x9/time_and_stderr.log.xz` | 播放器 stderr（FFmpeg 日志）加 `time -l` 的输出，xz 压缩；解压后与原文件一致，SHA-256 见 `SHA256SUMS` |
| `bipbop16x9/time_l.txt` | 上面那份里 `time -l` 的部分（墙钟、CPU、峰值内存） |
| `bipbop16x9/report.vmaf.json` | 现有代码写出的 JSON，原样复制 |
| `bipbop16x9/summary.txt` | `tools/summarize_report.py` 对 JSON 的逐档汇总 |
| `bipbop16x9/*_utc.txt`、`rc.txt` | 开始时间、看到报告行的时间、结束时间、退出码 |
| `diagnostics/ffmpeg_libvmaf_*_first300.*` | FFmpeg libvmaf 滤镜对 640x360、416x234 两档前 300 帧的命令（`.cmd.txt`）、JSON 日志、stderr |
| `diagnostics/compare_first300.txt` | `tools/compare_first_frames.py`：同样 300 帧上，Mirage 与 FFmpeg 的逐帧差 |
| `diagnostics/ffprobe_rungs.txt` | 各档的流参数与开头 12 帧的 PTS |
| `diagnostics/frame_n300_*.png` | 从头解码后第 300 帧（序号从 0 计）的截图，命令见 `frames_n300.cmd.txt` |
| `inputs/` | 运行当天抓取的播放列表原文 |
| `build_failure_python314.log` | 用默认 Python 做干净构建时失败的原始日志 |
