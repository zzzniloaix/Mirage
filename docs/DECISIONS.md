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

