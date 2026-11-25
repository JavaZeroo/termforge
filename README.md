# TermForge

**以完整开源、可独立构建的实现复刻 WindTerm 2.7.0，优先完成 Windows 11 x64，最终覆盖 Windows、Linux、macOS。**

An independent, fully open-source terminal project targeting WindTerm 2.7.0 feature, behavior, UI and performance parity. Windows 11 x64 first; Windows, Linux and macOS ultimately. Not affiliated with WindTerm or KingToolbox.

TermForge 处于 Windows 本地终端原型阶段。R1 基础文字交互已在既有 Win32 配置按限定合同验收，当前推进 R2 的“本地终端基础＋已知差异清单”。正式配置由自主 C++ 核心、Qt Widgets 和随包固定源码修订的微软 ConPTY 组成，须显式启用 Qt 构建。R2 最终包、完整 WindTerm 对照及性能等价仍未验收。 [能力矩阵](docs/FEATURES.md) · [R2 交付路线与验收](docs/milestones/R2-local-terminal.md)

首个完整版本以 Windows 11 x64 为目标，后续覆盖 Windows、Linux、macOS。以 WindTerm 2.7.0 的实际行为为准，内部结构可独立设计。Alacritty 保留为差分参考。 [项目方向](docs/adr/0006-confirmed-project-direction.md) · [C++ / Qt](docs/adr/0008-cpp-core-qt-widgets.md) · [ConPTY](docs/adr/0007-bundled-microsoft-conpty.md)

## 开始使用

下载首个 [Windows x64 Alpha 预发布包](https://github.com/JavaZeroo/termforge/releases/tag/v0.1.0-alpha.1)：完整解压 `TermForge-v0.1.0-alpha.1-windows-x64.zip` 后双击 `termforge_shell.exe`，保留同目录的 `shell_clipboard_reader.exe` 和 `assets/fonts/`。运行不需要 Python、CMake 或 Visual C++ Redistributable；包内附版本、源码身份和文件哈希，发布页另附 `SHA256SUMS.txt`。

这是基于 Win32 与系统 ConPTY 的未签名 R1 体验包，验证范围为 Windows 11 本机和 Windows Server 2025 CI。布局保存到 `%LOCALAPPDATA%\TermForge\workspace-v1.layout`。以下快捷键适用于该体验包。 [发布说明](docs/releases/v0.1.0-alpha.1.md)

- `Ctrl+Shift+T` 新建主区会话，`Ctrl+T` 新建底部会话。
- `F2` 重命名当前区域标签，`F11` 扩大底部 Shell；五条分隔线可拖动。
- `Ctrl++` / `Ctrl+-` 调整当前会话文字大小，`Ctrl+0` 恢复 100%，范围 50%–200%。

从源码构建时，默认 Win32 原型的 Visual Studio 产物为 `build/bin/Release/termforge_shell.exe`。正式 Qt 配置使用独立构建目录和固定源码 ConPTY 文件对。 [默认构建](CONTRIBUTING.md) · [Win32 操作与限制](experiments/shell/README.md) · [Qt 构建与运行](src/qt/README.md#构建与运行)

## 项目地图

| 要了解什么 | 唯一入口 |
|---|---|
| 已经做了什么、还缺什么 | [当前能力与目标矩阵](docs/FEATURES.md) |
| 最终目标与五个路标（从远到近） | [总体计划](docs/PLAN.md) |
| 哪些方向已确认、哪些决定仍需确认 | [方向与决策权限](docs/adr/0006-confirmed-project-direction.md) |
| 官方发行平台、架构与声明的最低系统边界 | [2.7.0 平台范围台账](research/windterm-2.7.0-platform-scope.md) |
| R2 分几步交付，每步还缺什么 | [R2 交付路线与验收](docs/milestones/R2-local-terminal.md) |
| 已验收的基础文字交互合同 | [R1 基础文字交互](docs/milestones/R1-local-text.md) |
| GitHub milestones、issues、交付看板与执行顺序 | [任务跟踪](docs/TRACKING.md) · [Project 交付总览](https://github.com/users/JavaZeroo/projects/2) |
| 结论依据、复现命令和全部文档 | [证据索引](docs/EVIDENCE.md) · [文档导航](docs/README.md) |

交付主线：**终端基础能力与性能 → SSH/SFTP 远程工作流 → Windows 高级功能与性能收敛 → Windows 11 x64 完整复刻验收 → Linux/macOS 扩展与全平台验收**。保留 R1–R5 编号，R5 分 Windows 和全平台两个出口；性能工程、原版研究和平台可行性调查持续推进。

## 工程原则与来源

功能先通过正确性验证；性能同时衡量吞吐、交互延迟、CPU、内存、稳定性和多会话公平性。不以丢数据、无界缓冲或积压呈现换取跑分，不预设某种语言或框架必然达标。研究原版公开代码、运行行为与关键内部机制，并比较自主实现和许可明确的组件；主要技术路线、验收门槛和重要差异经用户确认后生效。

原创代码和文档采用 [MIT](LICENSE)，第三方组件保留自身许可。实现不依赖 WindTerm 二进制或专有资源；观察、静态线索和推断分别记录。详见[来源规则](docs/PROVENANCE.md)与[性能规范](benchmarks/README.md)。
