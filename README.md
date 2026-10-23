# TermForge

**独立、完整开源地复刻 WindTerm 2.7.0，以可复现证据验证功能、交互和性能。**

An independent, fully open-source terminal project targeting WindTerm 2.7.0 feature, interaction and performance parity. Not affiliated with WindTerm or KingToolbox.

当前是有限 Windows 本地终端原型：已有真实 cmd/ConPTY 输入输出、主区与底部共最多 8 个会话、标签操作、可保存的布局、每会话缩放和受控 DPI 回归。R1 有界历史浏览已按限定合同验收；粘贴与扩展键盘/原生 IME 已接入，正常系统剪贴板路径已有验证，系统边界、实际中文输入和完整文字工作流仍待验收。选择复制、搜索、常用 TUI、SSH/SFTP 和日常开发包尚未完成；尚无与 WindTerm 性能等价的结论。`TermForge` 是暂名。

## 开始使用

按照[贡献与构建指南](CONTRIBUTING.md)构建后，Windows / Visual Studio 入口为 `build/bin/Release/termforge_shell.exe`；分发时保留旁边的 `assets/fonts`。

- `Ctrl+Shift+T` 新建主区会话，`Ctrl+T` 新建底部会话。
- `F2` 重命名当前区域标签，`F11` 扩大底部 Shell；五条分隔线可拖动。
- `Ctrl++` / `Ctrl+-` 调整当前会话文字大小，`Ctrl+0` 恢复 100%，范围 50%–200%。

完整操作、自动验证与限制见[本地 shell 说明](experiments/shell/README.md)。当前 resize 按物理行裁剪，无 reflow；受控 DPI 测试不等于实际跨显示器验证。

## 项目地图

| 要了解什么 | 唯一入口 |
|---|---|
| 已经做了什么、还缺什么 | [当前能力与目标矩阵](docs/FEATURES.md) |
| 最终目标与五个路标（从远到近） | [总体计划](docs/PLAN.md) |
| 下一路标的具体事项、依赖和验收 | [R1 基础文字交互](docs/milestones/R1-local-text.md) |
| GitHub milestones、issues 和执行顺序 | [任务跟踪](docs/TRACKING.md) |
| 结论依据、复现命令和全部文档 | [证据索引](docs/EVIDENCE.md) · [文档导航](docs/README.md) |

交付主线：**基础文字交互 → 可靠本地终端 → SSH/SFTP 远程日常版 → 高级功能/跨平台/性能 → 完整 2.7.0 复刻验收**。性能工程和定向逆向贯穿各路标，不把远期整块调查当作近期开发的前置。

## 工程原则与来源

功能先通过正确性验证；性能同时衡量吞吐、交互延迟、CPU、内存、稳定性和多会话公平性。不以丢数据、无界缓冲或积压呈现换取跑分，不预设某种语言或框架必然达标。优先复用许可明确的组件，以测量和可维护性决定产品架构。

原创代码和文档采用 [MIT](LICENSE)，第三方组件保留自身许可。实现不依赖 WindTerm 二进制或专有资源；观察、静态线索和推断分别记录。详见[来源规则](docs/PROVENANCE.md)与[性能规范](benchmarks/README.md)。
