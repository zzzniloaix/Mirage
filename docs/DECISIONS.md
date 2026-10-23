# 决策日志

用途：论文 design rationale 一节的素材，以及 owner 答辩时的依据（见 VMAF 工作单硬约束 4）。

每条记录包含：决策、备选方案、选择理由、谁做的决定、日期。

**设计决策只能由 owner 做出**（工作单硬约束 3）。agent 只能做测量与流程上的选择，这类条目单独列在下面，并标注「待 owner 确认」。

---

## 设计决策（owner）

其余待决定的事项见 `docs/VMAF_GAP_REPORT.md` 的「需要 owner 决定的事项」一节。

### D-001 Phase 1 内部顺序：§5 一致性校验先做

- **决策**
  - 工作单的 Phase 1 = §8 + §1 + §5，按工作单要求作为一个 PR 提交。
  - Phase 1 内部顺序：
    1. §5：做 Mirage 与 FFmpeg libvmaf 滤镜的逐帧 delta 工具，查明 Phase 0 发现的差异（640x360 前 300 帧 |Δ| 均值 5.28，Mirage 每帧都偏高）的原因。同时只做复现运行所需的最小 §8：把 Python 3.13 venv 的 glad 生成方式写成文档（对应 P-003）。
    2. §1：VMAF 核心（PTS 配对、模型 / scaler 可配置等）。
    3. §8：完整的可复现性打包。
    4. 重跑 §5，delta 报告提交进 `eval/`。
  - Phase 2 及以后按工作单的依赖顺序；§5 不通过不进 Phase 2。
- **备选**
  - §8 先行：先钉死构建环境，再做任何测量。代价是 VMAF 正确性要推迟，而且要先定 #10（许可证）和 #11（构建策略）。
  - §1 先行：先完成分析器核心，再用 §5 验证。代价是在一个原因不明的 5 分差异上继续开发，而且要先定 #2–#7。
  - Phase 1 拆成 1a / 1b / 1c 三个 PR。不选：与工作单硬约束 5（一个 Phase 一个 PR）不符。
- **理由**
  - 论文里的所有分数都依赖 §5 通过；工作单也要求 §5 提前做。
  - 差异的原因可能直接决定 #3（scaler 默认值）。
- **已知代价**：§1 的改动可能再次改变分数，所以 §1 完成后要重跑 §5；Phase 1 的 PR 会很大。
- **更正记录**：本条初稿（2026-10-22）把 §1、§8 写成了 Phase 2、Phase 3，与工作单的 phase 划分不符。原因是 agent 提问时选项措辞有误，不是 owner 的决定。owner 看过工作单后确认为现在的写法。
- **谁**：owner（Jiayu Lin）
- **日期**：2026-10-22

### D-002 参考源默认模式：top-rung（模式 B）

- **决策**：§1.2 的默认参考源是 top-rung，即以最高带宽 variant 为参考。mezzanine 参考（模式 A）按工作单要求同样实现，作为可选模式。
- **备选**：默认 mezzanine（模式 A），所有 variant 对外部源素材打分。
- **理由**：owner 选择时未说明理由，待 owner 补充。
- **影响**（agent 记录，供 owner 核对）
  - 默认分数衡量的是 ladder 内部的相对退化，不是绝对质量。论文需要讲清这一点（工作单 §1.2）。
  - 与现有代码的行为一致（现有 ladder 打分只支持 top-rung）。
  - 公开流通常拿不到 mezzanine，默认 top-rung 时公开流可以直接打分。
  - 输出仍须带 `reference_mode` 字段，两种模式的分数不得混在同一组报告里。
  - 默认模式下，top-rung 必须是其他档的同源缩放编码，否则分数没有意义。bipbop 16x9 不满足这一点（见差距报告"现状基线"），所以 #14 的 ladder 选择更重要了。
- **谁**：owner（Jiayu Lin）
- **日期**：2026-10-22

### D-003 scaler 默认值暂不决定，等 §5 诊断结果

- **决策**：§1.3 的默认 scaler 暂不决定。§5 诊断时在 Mirage 和 FFmpeg 两边分别测 bilinear 与 bicubic，owner 看到实测结果后再定。在此之前代码保持现有的 bilinear 不动。
- **备选**
  - 现在定 bicubic（工作单 §1.3 的建议）。
  - 现在定 bilinear（现有代码的行为）。
- **理由**：Phase 0 测出的 5 分差异是在两边都用 bilinear 的条件下得到的，差异不一定来自 scaler 本身。看到 scaler 对分数的实际影响之前定默认值，没有数据依据。
- **谁**：owner（Jiayu Lin），采纳 agent 的建议
- **日期**：2026-10-23

### D-004 #4–#8 推迟到对应章节开工时决定

- **决策**
  - #4 默认模型、#5 位深处理、#6 池化定义、#7 帧对齐失败阈值：§1 开工时决定。
  - #8 背压策略默认值：Phase 4（§4）开工时决定。
  - §5 对比期间沿用现有设置（`vmaf_v0.6.1`、8 bit YUV420P），不为对比改代码。
- **备选**：现在逐条决定。
- **理由**：这几项都会改变分数。和 D-003 一样，等有实测数据时再定。
- **谁**：owner（Jiayu Lin），采纳 agent 的建议
- **日期**：2026-10-23

### D-005 Phase 0 基线分数只当现状记录，不进论文

- **决策**：`eval/baseline/` 里的分数只作为 Phase 0 的现状记录。论文里的数字一律来自 §5 通过之后的运行。
- **备选**：基线分数进论文，例如作为修复前的对照，并写明原因。
- **理由**：Mirage 与 FFmpeg libvmaf 在同等条件下有约 5 分的差异，原因未查明（见差距报告"现状基线"）。
- **谁**：owner（Jiayu Lin），采纳 agent 的建议
- **日期**：2026-10-23

### D-006 libvmaf 版本：包版本与运行时自报版本都记录

- **决策**：JSON 输出和 §5 delta 报告同时记录 libvmaf 的包版本（本机 Homebrew 为 3.1.0）和运行时 `vmaf_version()` 的返回值（本机为 3.0.0）。
- **备选**
  - 只记运行时自报版本（§1.1 的字面要求）。
  - 只记包版本。
- **理由**：两者在本机不一致。都记下来，复现时才能看出用的是哪个构建。
- **谁**：owner（Jiayu Lin），采纳 agent 的建议
- **日期**：2026-10-23

### D-007 触及渲染或解码路径的重构一律先交方案

- **决策**：任何改动渲染或解码路径的重构（例如 §4 对播放帧打分、把分析器移出 `MediaSession`），agent 先写方案交 owner 审批，批准后才改代码。
- **备选**：小的重构直接做，在 PR 里说明；大的再先报方案。
- **理由**：工作单硬约束 3。
- **谁**：owner（Jiayu Lin），采纳 agent 的建议
- **日期**：2026-10-23

### D-008 Mirage 采用 GPL-3.0-or-later

- **决策**：Mirage 的许可证是 GPL-3.0-or-later。LICENSE 文件在 Phase 1 的 §8 部分加入。
- **备选**
  - GPL-2.0-or-later（CFP 举的例子是 GPLv2；与 GPLv3 的 FFmpeg 组合分发时按 GPLv3 生效）。
  - BSD / MIT（源码宽松，但带 ffmpeg-full 的二进制整体仍是 GPLv3）。
  - 等法务或公司开源流程的结论再定。
  - GPLv2-only 不可选：与链接的 GPLv3 FFmpeg 构建不兼容。
- **理由**：Mirage 链接的 ffmpeg-full 8.1.1 是 GPL-3.0-or-later（`--enable-gpl --enable-version3`，见差距报告第 9 条），选同一许可证，源码与二进制分发的条款最简单。
- **未完成**：是否需要法务或公司开源流程确认，由 owner 自行核实。
- **谁**：owner（Jiayu Lin）
- **日期**：2026-10-23

### D-009 FFmpeg 统一为 ffmpeg-full

- **决策**：构建统一使用 Homebrew 的 ffmpeg-full。`CMakePresets.json` 已经指向它，README / CLAUDE.md 中的 `brew install ffmpeg` 在 Phase 1 的 §8 部分改正。
- **备选**
  - 统一为 `ffmpeg`：改 preset 跟 README。§5 对比还需另装带 libvmaf 的 FFmpeg，并要重跑基线。
  - 推迟到 §8 再定。
- **理由**
  - ffmpeg-full 带 libvmaf 滤镜，§5 对比要用。
  - Phase 0 基线就是用 ffmpeg-full 跑的。
  - 与 D-008 的 GPL-3.0-or-later 一致。
- **未决定**：#11 的另外两部分（glad 生成结果是否入库、Linux 支持范围）在 §8 开工时决定。
- **谁**：owner（Jiayu Lin）
- **日期**：2026-10-23

### D-010 先把 fix/high-speed-playback 合入 main，VMAF 工作 PR 到 main

- **决策**
  - 把 `fix/high-speed-playback`（`a4e0289`）快进合入 `main`。
  - VMAF 各 phase 的分支都 PR 到 `main`。`vmaf/phase-0` 本来就基于 `a4e0289`，不需要 rebase。
  - FairPlay 改动留在本地分支 `feat/fairplay-drm`，不推送（owner 2026-10-22 的指示）。
- **备选**
  - PR 到 `fix/high-speed-playback`，`main` 保持不动。
  - 开一个长期的 vmaf 分支，投稿前再合入 `main`。
- **理由**：`main` 落后 `a4e0289` 6 个提交，可以快进。VMAF 的 PR 以 `main` 为目标，diff 里就只有 VMAF 的改动。
- **谁**：owner（Jiayu Lin）
- **日期**：2026-10-23

### D-011 evaluation 用自己编码的 ladder

- **决策**：evaluation 用公开源片自己编码 HLS ladder，源片即 mezzanine。编码脚本和参数进 `eval/`。具体源片与编码参数在开始编码时再由 owner 定。
- **备选**
  - 挑选现成的公开流（DASH-IF、Unified Streaming demo 等）：拿不到 mezzanine，流也可能变动或下线。
  - 两种都用：自编 ladder 做绝对质量与 §3 归因测试，公开流做适用性演示。
- **理由**
  - 各档出自同一源片，满足 D-002（默认 top-rung 参考）的前提，也能跑 mezzanine 模式。
  - 可控、可复现，还能有意做出丢帧、discontinuity 等测试用例。
  - Phase 0 用的 bipbop 16x9 各档是分别渲染的，不适合衡量质量（见差距报告"现状基线"）。
- **谁**：owner（Jiayu Lin）
- **日期**：2026-10-23

### D-012 改写全部历史，提交邮箱统一为 GitHub noreply 地址

- **决策**
  - 所有提交的作者 / 提交者邮箱改为 `217987193+zzzniloaix@users.noreply.github.com`，并强推 `main` 与 `fix/high-speed-playback`。
  - 本仓库以后的提交也用这个地址（仓库级 git 配置）。
- **备选**：只改以后的提交，已推送的历史不动。
- **理由**：不在公开历史里暴露个人邮箱。仓库 2026-04-03 起公开，2026-10-23 改为私有，投稿时还要重新公开。
- **影响**
  - 所有提交的 SHA 都变了。已推送的 25 个提交逐个核对过 tree hash，文件树没有任何变化。
  - 新旧对照：基线提交 `a4e02895d2d9c51c620e6c140f5566944c80ec71` → `1bab7a3104cf9149300eafd3d3deb7ae4a9c6f69`，tree `61765e415978c5a9735a3a6a3e245c52d0e6be99` 不变。差距报告、本日志（P-001、D-010）以及 `eval/` 原始输出里的 `a4e0289` 都指这个提交。原始输出保持原样，不改成新 SHA。
  - `vmaf/phase-0` 的 7 个提交同时按 D-013、D-014 改写，文件树因此有变化。
- **谁**：owner（Jiayu Lin）
- **日期**：2026-10-23

### D-013 工作单不放进仓库

- **决策**：工作单（`mirage-vmaf-spec-agent.md`，"Mirage VMAF Subsystem — Agent Work Order"）只保存在 owner 本地，不入库。之前放在 `docs/spec/` 下的副本已从 `vmaf/phase-0` 的全部历史中移除。仓库里的文档按标题和文件名引用它。
- **备选**
  - 删去不宜公开的段落后入库，并注明删改过。
  - 原样入库。
- **理由**：工作单含有 owner 的个人信息，不适合放进将要公开的仓库。
- **谁**：owner（Jiayu Lin）
- **日期**：2026-10-23

### D-014 Phase 0 原始输出中的本机信息打码

- **决策**：`eval/baseline/` 和差距报告里的本机信息换成占位符，其余内容不变。
  - `<worktree>`：Phase 0 编译基线用的干净 worktree 路径（P-001）。
  - `<repo>`：本仓库在 owner 机器上的路径。
  - `<tmp>`：系统按用户生成的临时目录。
  - `<hostname>`：机器名（`uname -a` 的输出）。
  - `run_baseline.sh` 里写死的临时目录改为读环境变量 `SCRATCH`。
  - 差距报告第 9 条删去了一个不应公开的主机名。
- **涉及文件**：`environment.txt`、`code_facts.txt`、`run_baseline.sh`、`diagnostics/ffmpeg_libvmaf_gear1_vs_gear5_first300.cmd.txt`、`diagnostics/ffmpeg_libvmaf_gear2_vs_gear5_first300.cmd.txt`、`docs/VMAF_GAP_REPORT.md`。分数、日志和其他输出都没有改动。
- **校验和**：`diagnostics/SHA256SUMS` 只更新了两个 cmd 文件的行，其余各行逐字节不变。改写后两个目录的 `shasum -a 256 -c` 全部通过。
  - `ffmpeg_libvmaf_gear1_vs_gear5_first300.cmd.txt`：`92ae786fad941303c5a0128c7a4cda0d14948e039f628663def57e3adbcae66f`（打码前）→ `555e64e875918da3a237b6f790b579d049acfe05158566c50f7a024bab197b3e`
  - `ffmpeg_libvmaf_gear2_vs_gear5_first300.cmd.txt`：`13001dca0964b86eb7651cfa171649d15b4a27f4d2ec47ca1fc6ff6af14bd318`（打码前）→ `2050a5d83277c54a5bd386326cc272dddc50bf3fe47073d0c5bd144f3b9ce939`
- **原件**：未打码的原件保存在 owner 本地的私有备份里，不入库。
- **备选**：原样保留。
- **理由**：机器名和本机路径与复现无关，不应进入公开仓库。
- **谁**：owner（Jiayu Lin）
- **日期**：2026-10-23

---

## 测量 / 流程选择（agent，待 owner 确认）

### P-001 Phase 0 基线用干净 worktree 编译已提交的 HEAD

- **选择**
  - 在 `git worktree add --detach <scratch>/wt-head HEAD` 得到的目录里，用 `cmake --preset macos-release` 编译 commit `a4e0289`。
  - 用编出的二进制跑现有打分功能。
- **备选**
  - 直接用主工作区的 debug 构建。
  - 先 `git stash` 再编译。
- **理由**
  - 主工作区里有未提交的 FairPlay/AVFoundation 改动，不属于勘察对象。worktree 保证测的是一个可引用的 commit，而且不动工作区。
  - 选 release 只为缩短运行时间：libvmaf 与 FFmpeg 都是预编译库，Mirage 自己的代码只做帧搬运和排序，不参与分数计算。这一点是基于代码阅读的推断，没有实测验证。
- **谁**：agent（Claude）
- **日期**：2026-10-21
- **状态**：待 owner 确认

### P-002 基线 ladder 选 Apple bipbop 16x9

- **选择**：`https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/bipbop_16x9_variant.m3u8`
- **备选**
  - Apple advanced 示例 `img_bipbop_adv_example_ts/master.m3u8`。它有 24 个 `EXT-X-STREAM-INF`（bipbop 16x9 是 6 个），指向 8 个视频播放列表，帧率分 30 / 60 两档。现有打分器是串行的，会对每个条目逐一打分，包括指向同一播放列表的重复条目。没有选它当 Phase 0 基线，它更适合以后专门测帧率不一致和重复条目。两个 master 的原文见 `eval/baseline/inputs/`。
  - 其他公开流，留到 Phase 1 的 `eval/streams.md`。
- **理由**
  - Apple 官方托管，地址稳定。
  - 5 个视频 variant 外加 1 个纯音频 variant，正好能看到现有工具遇到纯音频 variant 时的行为。
  - 以上 variant 数与时长都来自对 master 和 variant 播放列表的实际抓取（见 `eval/baseline/README.md`）。
- **谁**：agent
- **日期**：2026-10-21
- **状态**：待 owner 确认

### P-003 glad 代码生成改用 scratch venv（Python 3.13 + jinja2 3.1.6）

- **选择**：worktree 配置时指定 `-DPython_EXECUTABLE=<scratch>/venv-glad/bin/python`。
- **备选**
  - 修复 Homebrew 的 Python 3.14。这会改动机器环境，需要 owner 决定。
  - 复制主仓库已生成的 glad 文件。试过不行：ninja 仍要求重跑生成步骤。
- **理由**
  - 本机 Homebrew Python 3.14.7 的 `pyexpat` 导入失败（`Symbol not found: _XML_SetAllocTrackerActivationThreshold`），导致干净构建在 glad 生成步骤失败。
  - venv 不改系统环境。生成的 `gl.h` / `gl.c` / `khrplatform.h` 与主仓库构建产物逐字节一致（已用 `cmp` 核对）。
- **谁**：agent
- **日期**：2026-10-21
- **状态**：待 owner 确认。这件事本身也是 §8 可复现性的问题，见差距报告。

### P-004 让现有代码能写出 JSON 报告：预先创建 URL 形状的目录

- **选择**：在运行目录下预先创建 `https:/devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/`。
- **备选**
  - 不创建。现有代码写 `<URL>.vmaf.json` 这个相对路径会失败，基线就只剩日志。
  - 改代码。Phase 0 禁止。
- **理由**：不改代码，也能拿到现有导出功能的真实输出。
- **谁**：agent
- **日期**：2026-10-21
- **状态**：待 owner 确认。这个导出路径问题在差距报告里单列。

### P-005 基线分数的诊断性对照：FFmpeg libvmaf 滤镜，前 300 帧，两档

- **选择**
  - 用 ffmpeg-full 8.1.1 的 `libvmaf` 滤镜，对 640x360 和 416x234 两档的前 300 帧打分，再和 Mirage 的逐帧分数比较。
  - 对照条件尽量贴近 Mirage：模型 `vmaf_v0.6.1`，`n_threads=4`，失真用 `scale=1920:1080:flags=bilinear` 放大，参考原样。
- **备选**
  - 不做对照，只报告基线数字。缺点是 416x234 的 1.1455 是否可信就无从判断。
  - 全片对照。太耗时，而且已经超出 Phase 0 的范围，属于 §5。
- **理由**
  - 判断基线分数能不能信，是差距报告必须回答的问题。
  - 选这两档，是因为它们一个是异常档，一个是正常档。
  - 300 帧在 30000/1001 fps 下比第一个 segment 长一点：两档播放列表里第一个 segment 的 `EXTINF` 都是 9.9766（见 `eval/baseline/inputs/`），所以对照范围跨进了第二个 segment。配对是否一致，用序号 300 的帧（序号从 0 计，紧接对照范围之后）核对过：两档的帧计数器都显示 000300。
- **谁**：agent
- **日期**：2026-10-21
- **状态**：待 owner 确认。这一步**不是** §5 的实现。§5 的校验工具、参数和阈值仍由 owner 决定。

