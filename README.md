# TermForge

**以完整开源、可独立构建的实现复刻 WindTerm 2.7.0，优先完成 Windows 11 x64，最终覆盖 Windows、Linux、macOS。**

An independent, fully open-source terminal project targeting WindTerm 2.7.0 feature, behavior, UI and performance parity. Windows 11 x64 first; Windows, Linux and macOS ultimately. Not affiliated with WindTerm or KingToolbox.

当前是有限 Windows 本地终端原型，**R1 基础文字交互已按限定合同验收，正在推进 R2 可靠本地终端与开发版**。已有 Win32 原型、有限多会话与配置恢复，以及独立 Alacritty 参考实验。正式路线已确定为自主 C++ 核心、当前采用 Qt Widgets GUI 与随包固定微软 ConPTY；[最小单会话整合](https://github.com/JavaZeroo/termforge/issues/115)和[固定依赖专用 CI](https://github.com/JavaZeroo/termforge/issues/117)已合入 main，核心能力缺口、Qt 工作区迁移、完整 TUI/输入、真实不同 DPI、性能预算和开发包仍需闭合。现状见[能力矩阵](docs/FEATURES.md)，接续顺序见 [R2 交付路线与验收](docs/milestones/R2-local-terminal.md)。SSH/SFTP 属于 R3，完整 UI 与 WindTerm 性能等价均未验收。`TermForge` 是暂名。

方向已经用户确认，依据见 [ADR-0006](docs/adr/0006-confirmed-project-direction.md)：原版行为优先，内部结构不强求一致，但认真研究、借鉴原版；用户已确认[自主 C++ 核心与当前 Qt Widgets GUI 路线](docs/adr/0008-cpp-core-qt-widgets.md)，Windows 本地 Shell 后端采用[随包固定微软 ConPTY](docs/adr/0007-bundled-microsoft-conpty.md)。Alacritty 保留为参考，路线确认与实现验收分开。关键性能指标分别验收，具体允许波动范围后续与用户明确。

## 开始使用

下载首个 [Windows x64 Alpha 预发布包](https://github.com/JavaZeroo/termforge/releases/tag/v0.1.0-alpha.1)：完整解压 `TermForge-v0.1.0-alpha.1-windows-x64.zip` 后双击 `termforge_shell.exe`，保留同目录的 `shell_clipboard_reader.exe` 和 `assets/fonts/`。运行不需要 Python、CMake 或 Visual C++ Redistributable；包内附版本、源码身份和文件哈希，发布页另附 `SHA256SUMS.txt`。

这是未签名的免安装体验包，尚未达到 R2 日常开发版。已验环境限 Windows 11 本机与 Windows Server 2025 CI；不据此声明 Windows 10/Server 2022 支持。布局仍保存到 `%LOCALAPPDATA%\TermForge\workspace-v1.layout`，并非全数据便携模式。具体边界见[发布说明](docs/releases/v0.1.0-alpha.1.md)。也可按[贡献与构建指南](CONTRIBUTING.md)从源码构建，Visual Studio 入口为 `build/bin/Release/termforge_shell.exe`。

- `Ctrl+Shift+T` 新建主区会话，`Ctrl+T` 新建底部会话。
- `F2` 重命名当前区域标签，`F11` 扩大底部 Shell；五条分隔线可拖动。
- `Ctrl++` / `Ctrl+-` 调整当前会话文字大小，`Ctrl+0` 恢复 100%，范围 50%–200%。

完整操作、自动验证与限制见[本地 shell 说明](experiments/shell/README.md)。默认核心 resize 按物理行裁剪，无 reflow；显式候选构建采用独立的 [resize 与历史合同](experiments/core/CANDIDATE.md)。受控 DPI 测试不等于实际跨显示器验证。

## 项目地图

新的[自主 C++ / Qt 会话入口](src/qt/README.md)以显式构建开关提供固定 80×24 的 cmd 标签，可新增、切换、独立关闭并在应用退出前核对会话回收；[限定验证](experiments/shell/results/2026-10-28-qt-tabs/README.md)不继承旧配置能力。当前默认入口和上面的操作说明仍属于既有 Win32 原型。分屏、其他 shell、工作区恢复、完整文字交互及动态 resize 尚待迁移，不能据此声明完整开发版。

| 要了解什么 | 唯一入口 |
|---|---|
| 已经做了什么、还缺什么 | [当前能力与目标矩阵](docs/FEATURES.md) |
| 最终目标与五个路标（从远到近） | [总体计划](docs/PLAN.md) |
| 哪些方向已确认、哪些决定仍需确认 | [方向与决策权限](docs/adr/0006-confirmed-project-direction.md) |
| 官方发行平台、架构与声明的最低系统边界 | [2.7.0 平台范围台账](research/windterm-2.7.0-platform-scope.md) |
| R2 分几步交付，每步还缺什么 | [R2 交付路线与验收](docs/milestones/R2-local-terminal.md) |
| 已验收的基础文字交互合同 | [R1 基础文字交互](docs/milestones/R1-local-text.md) |
| GitHub milestones、issues 和执行顺序 | [任务跟踪](docs/TRACKING.md) |
| 结论依据、复现命令和全部文档 | [证据索引](docs/EVIDENCE.md) · [文档导航](docs/README.md) |

交付主线：**终端基础能力与性能 → SSH/SFTP 远程工作流 → Windows 高级功能与性能收敛 → Windows 11 x64 完整复刻验收 → Linux/macOS 扩展与全平台验收**。保留 R1–R5 编号，R5 分 Windows 和全平台两个出口；性能工程、原版研究和平台可行性调查持续推进。

## 工程原则与来源

功能先通过正确性验证；性能同时衡量吞吐、交互延迟、CPU、内存、稳定性和多会话公平性。不以丢数据、无界缓冲或积压呈现换取跑分，不预设某种语言或框架必然达标。研究原版公开代码、运行行为与关键内部机制，并比较自主实现和许可明确的组件；主要技术路线、验收门槛和重要差异经用户确认后生效。

原创代码和文档采用 [MIT](LICENSE)，第三方组件保留自身许可。实现不依赖 WindTerm 二进制或专有资源；观察、静态线索和推断分别记录。详见[来源规则](docs/PROVENANCE.md)与[性能规范](benchmarks/README.md)。
