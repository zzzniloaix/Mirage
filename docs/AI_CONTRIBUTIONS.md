# AI 参与记录

用途：论文 methods 一节 AI 参与披露段落的素材（见 VMAF 工作单硬约束 6；ACM 2026 修订后的政策要求披露 AI 在研究过程中的参与，含编码；AI 辅助写作不再需要披露）。

只记录可核实的事实；AI 参与的具体程度与 owner 的设计、审查工作由 owner 补充或确认。代码注释里不散落 AI 相关说明，集中在本文件。

---

## 仓库历史中的 AI 署名（来自 git，2026-10-21 采集）

采集方式：在分支 `fix/high-speed-playback`（HEAD `a4e0289`）上运行 `git log` 并读取 `Co-Authored-By` trailer。

- 从 HEAD 可达的提交共 25 个，其中 18 个带有 `Co-Authored-By: Claude …` trailer。出现的模型署名有：
  - Claude Sonnet 4.6
  - Claude Opus 4.7
  - Claude Opus 4.7 (1M context)
  - Claude Opus 5.5 (1M context)
- 不带 trailer 的 7 个提交：`4bc5ffc`、`521465d`、`bded21a`、`92e0c4c`、`62f3210`、`79407e4`、`ce4157b`。

**注意：** 没有 trailer 不能证明没有 AI 参与，trailer 只是一种惯例。每个提交的实际参与程度由 owner 确认。

## VMAF 相关代码

- **`src/VMAFAnalyzer.cpp` / `src/VMAFAnalyzer.h`**（打分核心）
  - 只有一个提交：`62f3210`（2026-04-22，"Add VMAF implementation"），没有 AI trailer。
  - **待 owner 确认**：这一提交是否有 AI 参与，参与到什么程度。
- **之后的 VMAF 接线与 UI 改动**
  - 用 `git log -G vmaf` 找出的提交：
    - 无 trailer：`79407e4`
    - 带 Claude trailer：`9b67621`、`eea030f`、`85e59a3`、`cb29189`、`a4e0289`
  - 这些提交改的是界面、面板和会话结构，不是打分逻辑。逐个核对的结论待补。

## VMAF 工作单（本轮）

| 日期 | 阶段 | AI 做了什么 | owner 做了什么 |
|---|---|---|---|
| 2026-10-21 | Phase 0 | Claude Opus 5.5（经 Claude Code）：<br>• 勘察现有 VMAF 代码<br>• 采集环境与依赖信息<br>• 用干净 worktree 编译 HEAD，跑现状基线<br>• 用 FFmpeg libvmaf 对其中两档的前 300 帧做诊断对照，用 ffprobe 和抽帧核对配对<br>• 编写 `eval/baseline/` 下的脚本<br>• 起草 `docs/VMAF_GAP_REPORT.md`、本文件和 `docs/DECISIONS.md` 的流程条目 | 提出工作单。审阅差距报告并确认 Phase 1 范围：待完成 |

Phase 0 不修改任何现有代码，所以 AI 在这一阶段只产出文档、脚本和测量结果。
