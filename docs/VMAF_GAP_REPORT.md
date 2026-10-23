# Mirage VMAF 差距报告（Phase 0）

- 日期：2026-10-21
- 依据工作单：`mirage-vmaf-spec-agent.md`
- 勘察对象：commit `a4e02895d2d9c51c620e6c140f5566944c80ec71`（分支 `fix/high-speed-playback` 的 HEAD）。2026-10-23 改写历史后，这个提交是 `1bab7a3104cf9149300eafd3d3deb7ae4a9c6f69`，文件树 `61765e415978c5a9735a3a6a3e245c52d0e6be99` 不变（见 `docs/DECISIONS.md` D-012）
- 撰写：agent（Claude），由 owner 审阅

**范围说明**
- 主工作区里另有未提交的 FairPlay/AVFoundation 改动。它们不碰 VMAF 打分代码，只改了 `main.cpp` 里 `--vmaf` 自动启动的分支（多了"AVFoundation 会话不跑 VMAF"的判断）和 DebugHUD 的混合方式。本报告不讨论这些改动，基线也用 HEAD 编译。
- 报告中的行号都指 HEAD 版本。每个数字或版本号都能在 `eval/baseline/` 的原始输出里找到出处：
  - `code_facts.txt`：代码位置与行数；
  - `environment.txt`：依赖版本与机器信息；
  - `bipbop16x9/`：基线运行的原始输出。

---

## 一句话结论

现有实现能做的是：
- 对 HLS ladder 以**最高带宽 variant 为参考**，串行地逐个打分；
- 或者对单个文件和一份参考文件打分；
- 输出逐帧分数，以及 mean / min / p5，还有一份简陋的 JSON。

它没有做到的是：
- 按 PTS 对齐帧；
- 可配置的模型和 scaler；
- 版本与配置进输出；
- 一致性校验；
- 统一时间轴；
- benchmark；
- 许可证与可复现打包。

论文的立论点 §3 目前只有零散的数据源，各自分开，没有统一的数据结构和查询 API。

**此外，基线发现现有分数与 FFmpeg 的 libvmaf 滤镜对不上。** 640x360 那一档前 300 帧，逐帧绝对差平均 5.2798，见下文"现状基线"。§5 这道闸门今天过不去。

---

## 1. VMAF 相关代码：文件、行数、架构

| 位置 | 内容 | 规模（HEAD） |
|---|---|---|
| `src/VMAFAnalyzer.h` | `VMAFResult`、`VMAFAnalyzer` 声明 | 75 行 |
| `src/VMAFAnalyzer.cpp` | 解码、libvmaf 打分、JSON 导出 | 433 行 |
| `src/PlayerUI.cpp` `draw_vmaf_results` | "VMAF Analysis" ImGui 窗口：表格 + 第一条结果的逐帧曲线 | L1399–1563 |
| `src/DebugHUD.cpp` `draw_vmaf_panel` | 右下角 GL 小面板（V 键） | L908–1109 |
| `src/PlayerUI.cpp` 菜单 | Analyze → "VMAF — Single File…" / "VMAF — Manifest Variants" | — |
| `src/main.cpp` | `--vmaf [ref]` 参数、自动启动（L1096–1108）、完成后自动导出（L1826–1834）、面板接线、导出按钮（L3180–3184、L3422–3425） | 共 80 行含 "vmaf" |
| `src/MediaSession.h` | `VMAFAnalyzer vmaf_analyzer;`（L82）：分析器属于单个媒体会话 | — |
| `CMakeLists.txt` | `pkg_check_modules(VMAF REQUIRED IMPORTED_TARGET libvmaf)`，链接 `PkgConfig::VMAF` | — |

**架构**

- `VMAFAnalyzer` 在自己的 `std::jthread` 里工作。
- 对每一对（参考，失真），它用 `avformat_open_input` 直接打开两个 URL，各建一个解码上下文（`DecodeCtx`），软件解码各自最佳的视频流。
- 两路帧都经 `sws_scale` 转成参考分辨率的 8 bit YUV420P（`SWS_BILINEAR`，L152、L157），拷进 `VmafPicture` 后逐帧调用 `vmaf_read_pictures`。
- 任一路到 EOF 就结束。然后 flush，用 `vmaf_score_at_index` 取逐帧分数，用 `vmaf_score_pooled` 取 mean / min，p5 在 C++ 里手工计算。
- 结果存在加锁的 vector 里，UI 每帧复制一份快照。

## 2. libvmaf：库链接还是 shell out

- **以库形式链接，直接调用 C API**：
  - 头文件 `#include <libvmaf/libvmaf.h>`；
  - 调用 `vmaf_init` / `vmaf_model_load` / `vmaf_read_pictures` / `vmaf_score_*`；
  - 动态链接 `/opt/homebrew/opt/libvmaf/lib/libvmaf.3.dylib`（`otool -L`，见 `environment.txt`）。
- `VMAFAnalyzer` 里没有 `system` / `popen` 一类调用。
- 这一条符合 §1.1 的"禁止 shell out"。

## 3. libvmaf 版本、模型、可配置性

**版本**

| 来源 | 版本号 |
|---|---|
| Homebrew 安装的包 `libvmaf` | 3.1.0 |
| 库自报 `vmaf_version()` | 3.0.0 |
| `vmaf --version` | 3.0.0 |
| `libvmaf.pc` | 3.0.0 |
| `version.h` 的 `VMAF_API_VERSION` | 3.0.0 |

三处自报的版本号与包版本不一致。§7 的 `libvmaf_version` 记哪个，需要 owner 定（见文末决策清单）。

**模型**
- 写死为 `vmaf_model_load(&model, &model_cfg, "vmaf_v0.6.1")`（L199），按版本字符串加载内置模型，`flags = VMAF_MODEL_FLAGS_DEFAULT`。
- 不可配置，输出里也没有模型标识。
- 本机 `/opt/homebrew/opt/libvmaf/share/libvmaf/model/` 下有 `vmaf_v0.6.1.json`、`vmaf_v0.6.1neg.json`、`vmaf_4k_v0.6.1.json` 等文件，但代码不用。

**其他固定参数**
- `n_threads = 4`（L188）
- `n_subsample = 1`
- `log_level = NONE`

这些都不可配置，也不进输出。

## 4. Ladder 全量打分与参考源

**支持。** 入口有两个：
- `start_manifest(variants)`（`VMAFAnalyzer.cpp` L325–377）；
- 菜单 "Manifest Variants" 和 `--vmaf`（不带参数）都走这里。

做法：
1. 把 master 里的所有 `EXT-X-STREAM-INF` 按 `BANDWIDTH` 降序排序，第一个作为参考（相当于 §1.2 的模式 B，top-rung）。
2. 其余每个条目各和它打一次分。
3. 单线程、串行、一对接一对。

问题：
- **按 `EXT-X-STREAM-INF` 条目打分，不按视频播放列表去重。**
  - 有的 ladder 让同一视频播放列表配不同音频组，同一播放列表因此会被重复打分。例如 Apple advanced 示例：24 个条目，指向 8 个视频播放列表，见 `eval/baseline/inputs/`。
  - 参考播放列表的其他条目也会和参考本身对打。
- 纯音频条目（无 `RESOLUTION`）同样会被尝试，打不开时记为错误。
- 每一对都重新解码一遍参考，N 个条目就把参考解码 N−1 遍。
- 打开 variant 时没有挂 NetworkLogger 的 hook，这些请求在网络面板里显示为 initiator "other"。也没有设置 `timeout` / `rw_timeout` / `reconnect` 这些选项（播放路径的 `Demuxer` 设置了），网络异常时只能按 FFmpeg 默认行为处理。
- 整段 VOD 从头解码到尾，没有时间窗选项。
- 参考源模式 A（mezzanine）**只在单文件模式下存在**：当前打开的文件对一份手选文件打分（`start(ref, dis)`）。ladder 不能对外部参考打分。
- 打开的是 HLS master 时，`--vmaf <ref>` 里的参考文件**会被忽略**，照样走 top-rung 的 ladder 模式。原因是 `main.cpp` L1099–1105 先判断 manifest 有没有 variant，有就直接调 `start_manifest`。
- 输出里没有 `reference_mode`。

## 5. 逐帧分数，以及是否与播放时间轴对齐

**有逐帧分数**：`VMAFResult::per_frame`，按帧序号存放。

**不对齐 PTS**：
- 参考与失真按**解码顺序的序号**一一配对：L216–217 各取下一帧，L260 用 `frame_idx++` 作索引。
- 不读也不存 PTS。
- 先结束的一路决定总帧数。
- 帧率不一致、丢帧、重复帧都不检测，也不报告。帧率不同的 ladder（例如 Apple advanced 示例同时有 30 和 60 fps 的条目），按代码逻辑会静默错位出分。这一点是读代码得出的，没有实测，§1.4 的验收会构造用例验证。

**与播放时间轴的关系**：
- UI 曲线上的播放位置标记按 `pos_frac × N` 取帧（`PlayerUI.cpp` L1553），其中 `pos_frac = 当前 PTS / 时长`。
- 这等于假设打分帧均匀铺满整个时长，只是近似。
- 曲线只画第一条完成的结果。

## 6. JSON 导出与 schema

**有**：`write_json(path)`（L392–433），手写拼接字符串。结构如下：

```
{ "results": [ { "label", "ref", "dis", "bandwidth", "resolution",
                 "vmaf_mean", "vmaf_min", "vmaf_p5", "per_frame": [ … ] }
               | { …, "error" } ] }
```

问题：
- 没有以下字段：
  - schema 版本；
  - 工具版本、git commit、libvmaf 版本；
  - 模型、参考模式、scaler、环境；
  - 逐帧的 PTS。
- 字符串没有做 JSON 转义：URL 或 label 里出现 `"` 或 `\` 就会产生非法 JSON。
- `"resolution"` 写的是**参考**的分辨率（L145 `result.width = ref_w`），而 `"label"` 写的是失真 variant 的分辨率。字段名有误导性。
- **输出路径是 `open_path + ".vmaf.json"`**，自动导出（main.cpp L1830）和导出按钮（L3182、L3423）都是这个写法。
  - 打开的是 URL 时，它是一个相对路径，形如 `https:/<host>/<path>/master.m3u8.vmaf.json`，只有当前目录下恰好存在这些子目录时才能写成功。
  - 调用方在 `write_json` 返回 false 时**仍然打印** "VMAF report written to …"（L1831–1832，L3183–3184，L3424–3425）。
  - 基线运行为了拿到真实输出，预先建了这些目录，见 `docs/DECISIONS.md` P-004。
- 只在分析完成时（`--vmaf`）或按导出按钮时才写。

## 7. 打分与播放的并发关系

结构上**不在渲染线程打分**：
- 打分在独立的 `std::jthread` 里进行，有自己的 demux / decode 上下文，不和播放共用解码器。

但需要说明：
- 这是对 variant 文件的**离线**整段打分，不是对"正在播放的帧"打分。
- 没有帧队列，没有背压策略。统计只有一个 `progress()` 浮点数。
- 打分与播放争 CPU 的影响没有测过（**MEASUREMENT PENDING**）。
- 按 V 打开后（GL 小面板和 ImGui 窗口由同一个开关控制），渲染线程每帧会调用两次 `results()`（`main.cpp` L2996、L3163），每次在锁内复制全部结果，包括每条的逐帧 vector。开销随帧数增长，没有测过（**MEASUREMENT PENDING**）。
- 分析器是 `MediaSession` 的成员。打开另一个源时会话被销毁，分析被取消，结果丢失。

## 8. 现有测试对 VMAF 的覆盖

**没有。**
- `tests/` 下没有任何文件提到 VMAF（`grep -rli vmaf tests` 无输出）。
- `VMAFAnalyzer.cpp` 不在 `mirage_tests` 的源文件列表里。

## 9. LICENSE

**仓库里没有 LICENSE 或 COPYING 文件**（`git ls-files` 无匹配）。按工作单 §8，这一条**直接不满足投稿要求**。

依赖的许可证（来自各依赖自带的 LICENSE 文件和 Homebrew 元数据）：

| 依赖 | 接入方式 | 许可证 |
|---|---|---|
| FFmpeg（实际链接 **ffmpeg-full 8.1.1**，见下） | Homebrew，动态链接 | **GPL-3.0-or-later**：该构建的 configure 选项含 `--enable-gpl --enable-version3`，并带 `libx264` / `libx265` / `libzimg` / `libvmaf`，没有 `--enable-nonfree`（`environment.txt`）。Homebrew 元数据给的许可证也是 GPL-3.0-or-later |
| libvmaf 3.x | Homebrew | BSD-2-Clause-Patent |
| GLFW 3.4 | Homebrew | Zlib |
| Dear ImGui v1.91.9 | FetchContent | MIT |
| miniaudio（`master`） | FetchContent | public domain 或 MIT-0，二选一 |
| stb（`master`） | FetchContent | MIT 或 public domain，二选一 |
| glad2 v2.0.8 | FetchContent | 生成器本身为 MIT；生成代码与 Khronos 头文件的条款见 glad 的 LICENSE |
| pugixml v1.14 | FetchContent | MIT |
| nativefiledialog-extended v1.2.1 | FetchContent | Zlib |
| Catch2 v3.7.1（只用于测试） | FetchContent | BSL-1.0 |

**需要 owner 决定（可能还需公司法务）**
- Mirage 自身采用什么许可证。CFP 举的例子是 GPLv2、LGPLv2、BSD。
- 选定的许可证与"链接 GPLv3+ 的 FFmpeg 构建"之间如何兼容，二进制分发时适用什么条款。
- 仓库的 `origin` 是 github.com 上的 `zzzniloaix/Mirage`（SSH 别名 `github-zzzniloaix` 解析到 `github.com`，2026-10-23 用 `ssh -G` 核对）。初稿写成了公司 GitHub，有误，已更正。仓库 2026-04-03 起公开，2026-10-23 改为私有，投稿前需要重新公开。作为开源项目发布是否需要走公司的流程，由 owner 确认。

## 10. 工作单 §1–§8 逐节状态

| 节 | 状态 | 已有 | 缺什么 |
|---|---|---|---|
| §1.1 libvmaf 3 集成 | 部分实现 | 直接链接 C API | 运行时报告版本与模型标识；可选模型（v0.6.1 / neg / 4k）；自定义模型路径 |
| §1.2 参考源双模式 | 部分实现 | ladder 只有 top-rung；mezzanine 只在单文件模式 | ladder 的 mezzanine 模式；`reference_mode` 字段；两种模式分开出报告 |
| §1.3 分辨率对齐 | 部分实现 | 失真升采样到参考分辨率 | scaler 写死为 bilinear，不可配，也不进输出 |
| §1.4 帧对齐 | 未实现 | — | PTS 配对；帧率不一致、丢帧、重复帧的处理；失败帧数与阈值告警 |
| §1.5 分数池化 | 部分实现 | mean、min、p5 | harmonic_mean、p1/p10/p25/p50；p5 现在是不插值的 nearest-rank（`sorted[n*5/100]`，L284），定义需要明确 |
| §2 ladder 编排 | 部分实现 | 解析 master 的 BANDWIDTH / RESOLUTION / CODECS（ManifestParser）；自动选最高带宽为参考；取消时已完成的结果保留 | 线程池与并发度；内存上限；按视频播放列表去重；discontinuity 与 segment 时长不一致的处理；UI 里的取消按钮（目前只在新开分析或销毁时取消） |
| §3.1 时间轴模型 | 部分实现（数据源零散，没有统一模型） | 见下方 | 以 PTS 为主键的统一事件结构；SCTE-35；ABR/切换事件；VMAF 逐帧分数上时间轴 |
| §3.2 归因查询 | 未实现 | — | 查询 API、CLI 子命令、自动检出、两个归因测试用例 |
| §3.3 UI 呈现 | 部分实现 | 单条逐帧曲线；网络侧栏的时间线与对齐视图；manifest tag 的时间刻度条（仅当打开的是媒体播放列表时才有 tag） | 缩放 / 拖拽时间轴、点击跳帧并显示归因、多 variant 叠加、事件标记叠在分数曲线上 |
| §4 并发打分 | 部分实现 | 独立线程、独立解码器（离线打分） | 对播放帧打分、有界队列、三种背压策略、实时统计、掉帧测量 |
| §5 一致性校验 | 未实现 | 本机 ffmpeg-full 带 `--enable-libvmaf`，可作对照工具（只用于校验，不进实现）。Phase 0 的诊断对照已发现差异，见"现状基线" | 校验工具、delta 报告；查明并消除现有差异 |
| §6 benchmark | 未实现 | 播放侧已有 QA 工具（`--av-report`、`--net-report`），与 VMAF 无关 | bench / sweep 模式、资源统计、环境元数据 |
| §7 JSON schema | 部分实现 | 上面第 6 条的那份 JSON | 版本化 schema 文件，以及 §7 列出的全部字段 |
| §8 可复现性打包 | 未实现 | 部分依赖钉了 tag | 见下方 |

**§3.1 现有的数据源**（逐项都要接进统一时间轴）

- **NetworkLogger**
  - 每个 HTTP 请求有开始、发出、首字节、结束、字节数、状态码。时间用 steady clock 微秒，可以换算成 wallclock。
  - 不以媒体 PTS 为键。
- **ManifestParser**
  - 能解析 `EXT-X-DISCONTINUITY`、`DISCONTINUITY-SEQUENCE`、`PROGRAM-DATE-TIME`、`CUE-OUT` / `CUE-IN`、未知的 `EXT-X-*` tag，以及 DASH 的 Period / Event。
  - 只解析它打开的那份播放列表。打开 HLS master 时拿不到 segment 级事件，因为这些 tag 只出现在媒体播放列表里。
  - 不解析 `EXT-X-DATERANGE` 的 SCTE-35 属性，也不解码 splice_info。
- **HlsAlign**（`MediaTimeline`）
  - 能把媒体播放列表的每个 segment 映射到播放列表时间，带 discontinuity 与 PDT，并检查 segment 内部时间戳。
  - 用于网络侧栏的对齐视图。
- **播放器切换**
  - Mirage 不做 ABR。用户手动切换 variant 时只写日志文本，不产生结构化事件。
- **解码器**
  - 有丢帧计数；`KeyframeIndex` 记录关键帧位置。

**§8 的具体缺口**

- 没有 LICENSE（见第 9 条）。
- **构建依赖没有钉死，文档也不一致**：
  - miniaudio 和 stb 的 FetchContent 用的是 `GIT_TAG master`。
  - Homebrew 依赖不钉版本。本机装的是 ffmpeg / ffmpeg-full 8.1.1、libvmaf 3.1.0、GLFW 3.4，而 Homebrew 当前的 formula 已经是 ffmpeg 9.0.2、libvmaf 3.2.1、glfw 3.5.1（`environment.txt`）。干净机器今天照 README 安装，FFmpeg 会是另一个大版本。
  - README 和 CLAUDE.md 写的是 `brew install ffmpeg`，而 `CMakePresets.json` 用 `PKG_CONFIG_PATH` 指向 **ffmpeg-full**。按 README 在干净机器上安装，链接到的会是另一个 FFmpeg 构建。本机两个都装了，版本都是 8.1.1，但 configure 选项不同。
- **本机当前无法从零构建**：
  - glad 在构建时用 Python 生成 GL loader。
  - 本机 Homebrew Python 3.14.7 的 `pyexpat` 扩展链接的是系统的 `/usr/lib/libexpat.1.dylib`，并且需要系统库里没有的 `_XML_SetAllocTrackerActivationThreshold`，所以 `import pyexpat` 失败。干净构建因此在这一步失败。原始错误见 `eval/baseline/environment.txt` 和 `eval/baseline/build_failure_python314.log`。Python 3.13 没有这个问题。
  - 基线是用 scratch venv 绕过去的（DECISIONS P-003）。
  - 可以考虑把 glad 的生成结果放进仓库，或者钉死生成环境。属于 Phase 1 的范围，待 owner 决定。
- 只有 macOS preset，没有 Linux 构建说明，没有 Docker / Nix，没有 `eval/run_all.sh`，没有 `eval/streams.md`。
- 根目录的 `vcpkg.json` 列了 ffmpeg 和 glfw3，没有 libvmaf，也不钉版本。CMake 构建不用它。

---

## 现状基线（Apple bipbop 16x9，2026-10-21）

全过程与全部原始输出都在 `eval/baseline/`，说明见 `eval/baseline/README.md`。

**跑通了。** 构建和输出路径各绕行了一处（P-003、P-004），现有代码一行没改。

**结果**
- 参考：gear5，1920x1080，`BANDWIDTH=1924009`。
- 数据来源：`bipbop16x9/report.vmaf.json` 与 `summary.txt`。

| 失真（现有 label） | 帧数 | mean | min | p5 | JSON 里的 `resolution` |
|---|---|---|---|---|---|
| 1280x720  1.0 Mbps | 53946 | 74.5632 | 69.7469 | 73.8895 | 1920x1080 |
| 960x540  0.9 Mbps | 53946 | 53.2220 | 45.1244 | 52.0560 | 1920x1080 |
| 640x360  0.6 Mbps | 53946 | 33.0648 | 20.0523 | 31.4198 | 1920x1080 |
| 416x234  0.3 Mbps | 53946 | 1.1455 | 0.0000 | 0.0000 | 1920x1080 |
| 0x0  0.0 Mbps（gear0，纯音频） | — | — | — | — | 记为错误 `cannot open distorted: …/gear0/prog_index.m3u8` |

**资源**
- 数据来源：`bipbop16x9/time_l.txt`，即 `/usr/bin/time -l` 的输出。

| 项 | 数值 |
|---|---|
| 墙钟 | 3172.96 s |
| user | 12899.81 s |
| sys | 188.51 s |
| maximum resident set size | 640106496 bytes |
| peak memory footprint | 812352904 bytes |

- 这些是**整个播放器进程**的数字：从启动、边播放边打分，一直到报告写出后 2 s。不是"单纯打分"的数字，现有工具也没法把两者分开测。

**从输出里直接看到的问题**

1. `"resolution"` 一栏全是参考的分辨率 1920x1080，与第 6 条的分析一致。
2. 纯音频条目进入了打分列表，被记为错误。它的 label 是 "0x0  0.0 Mbps"：`BANDWIDTH=41457` 按 Mbps 保留一位小数显示为 0.0。
3. 有分数的四档，**min 都等于第一帧的分数**（`summary.txt` 的 `first_frame`）。也就是说，现有的 min 池化取的就是第一帧。§1.5 定义池化时需要考虑这一点。
4. 416x234 这一档：
   - 整段没有一帧超过 5 分（max 4.6752）。
   - 时序上没有对出错的迹象：各档帧率都是 30000/1001，起始时间都是 10.0 s，开头 12 帧的 PTS 序列完全相同（`diagnostics/ffprobe_rungs.txt`）；从头解码、序号为 300 的那一帧（序号从 0 计）在两档上都显示帧计数 000300（`diagnostics/frame_n300_*.png`）。
   - 但两档的画面不是同一张图：叠加文字各写各的分辨率和 gear 号，低档里分辨率测试栅格已经糊成灰块。
   - 所以这条 ladder 的低档不是"参考画面的缩放编码"。它适合当功能基线，不适合当绝对质量的测量对象。论文 evaluation 选流时需要注意（决策 14）。

**诊断性对照：现有分数与 FFmpeg libvmaf 不一致**

这不是 §5 的实现，只是在 Phase 0 判断"基线的分数能不能信"。

对照条件：
- 工具：FFmpeg 8.1.1（ffmpeg-full）的 `libvmaf` 滤镜。
- 模型 `vmaf_v0.6.1`，`n_threads=4`。
- 失真经 `scale=1920:1080:flags=bilinear`，参考原样。
- 范围：前 300 帧。
- 两边用的是**同一个** `libvmaf.3.dylib`（`diagnostics/libvmaf_used_by_ffmpeg.txt`）。
- 滤镜的输入顺序是 #0 失真、#1 参考，调用的是 `vmaf_read_pictures(ref, dist)`，和 Mirage 一致。
- 命令和原始日志在 `diagnostics/`；对比由 `tools/compare_first_frames.py` 完成，输出在 `diagnostics/compare_first300.txt`。

| 失真 | Mirage 前 300 帧均值 | FFmpeg 均值 | 逐帧 \|Δ\| 最大 | 逐帧 \|Δ\| 平均 | \|Δ\| > 0.01 的帧 |
|---|---|---|---|---|---|
| 640x360 | 33.8328 | 28.5529 | 5.6747 | 5.2798 | 300 / 300 |
| 416x234 | 1.8845 | 0.0061 | 3.8838 | 1.8784 | 290 / 300 |

- 每一帧都是 Mirage 更高：带符号的平均差与绝对平均差相等。
- §5 的目标是"默认配置下逐帧绝对差 < 0.01"。**现状达不到。**
- 原因还没定位。打分库相同，模型相同，参考帧相同（同尺寸同格式）。前 300 帧的配对也相同：开头的 PTS 序列一致，序号 300 的帧计数一致。剩下已知的差别是**失真帧的缩放实现**：
  - Mirage：`sws_getContext(..., SWS_BILINEAR, ...)` 加 `sws_scale`；
  - 对照：`scale` 滤镜，`flags=bilinear`。
  - 这只是候选原因，要在 §5 里逐项排除，排除后才能下结论。
- 这说明工作单把 §5 排在 §3 之前是对的：在一致性问题查清之前，**现有实现产出的分数（包括上表）都不应当用作论文数据**。

---

## 需要 owner 决定的事项

按工作单硬约束 3，以下各项都等 owner 拍板。agent 不会自行选择。

1. **范围确认**：Phase 1 从哪里开始，§8 和 §1 / §5 的先后顺序。
2. **§1.2 参考源默认值**：mezzanine 还是 top-rung。
3. **§1.3 scaler 默认值**：工作单建议 bicubic；现在的代码是 bilinear。
4. **默认模型**：v0.6.1、neg 还是 4k。另外，按分辨率自动选模型是否算默认行为。
5. **位深处理**：现在一律转成 8 bit YUV420P，10 bit / HDR 内容会被截断成 8 bit 再打分。这会影响分数。
6. **池化定义**：百分位用 nearest-rank 还是插值；harmonic mean 用 libvmaf 自带的还是自己算。
7. **帧对齐失败阈值**：工作单建议 1%。超过阈值时是只告警，还是拒绝出分。
8. **§4 背压策略默认值**：三选一。
9. **版本号记录**：libvmaf 包版本 3.1.0，自报 3.0.0，两个都记还是只记一个。
10. **许可证**：Mirage 用什么许可证，FFmpeg 构建怎么选（第 9 条）。
11. **构建策略**：glad 生成结果是否入库；FFmpeg 依赖统一成 ffmpeg 还是 ffmpeg-full；Linux 支持的范围。
12. **分支与 PR**：VMAF 工作基于哪个分支、向哪里提 PR。现在的 `fix/high-speed-playback` 比 origin 多 2 个未推送的提交，工作区还有未提交的 FairPlay 改动。
13. **会改动渲染或解码路径的重构**：例如 §4 对播放帧打分、把分析器移出 `MediaSession`。这类重构一律先报方案。
14. **evaluation 用哪些 ladder**：bipbop 16x9 的低档不是参考画面的缩放编码（见"现状基线"）。绝对质量评估和 §3 的归因测试都需要"同一源素材编出的 ladder"，可以自己用公开源片编码，也可以挑合适的公开流。
15. **对待 Phase 0 基线分数的态度**：在 §5 查清差异之前，基线分数只当"现状记录"，不进论文。是否认可这个处理，需要你确认。
