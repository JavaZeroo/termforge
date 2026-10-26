# TermForge

**独立、完整开源地复刻 WindTerm 2.7.0，以可复现证据验证功能、交互和性能。**

An independent, fully open-source terminal project targeting WindTerm 2.7.0 feature, interaction and performance parity. Not affiliated with WindTerm or KingToolbox.

当前是有限 Windows 本地终端原型，**R1 基础文字交互已按限定合同验收，正在推进 R2 可靠本地终端与开发版**。主线已有候选核心路线、首批流式字素、有限多会话和配置恢复；候选核心与实验 ConPTY 后端的联合配置、完整 TUI/输入、真实不同 DPI、性能预算和开发包仍需闭合。现状见[能力矩阵](docs/FEATURES.md)，接续顺序见 [R2 交付路线与验收](docs/milestones/R2-local-terminal.md)。SSH/SFTP 属于 R3，完整 UI 与 WindTerm 性能等价均未验收。`TermForge` 是暂名。

## 开始使用

下载首个 [Windows x64 Alpha 预发布包](https://github.com/JavaZeroo/termforge/releases/tag/v0.1.0-alpha.1)：完整解压 `TermForge-v0.1.0-alpha.1-windows-x64.zip` 后双击 `termforge_shell.exe`，保留同目录的 `shell_clipboard_reader.exe` 和 `assets/fonts/`。运行不需要 Python、CMake 或 Visual C++ Redistributable；包内附版本、源码身份和文件哈希，发布页另附 `SHA256SUMS.txt`。

这是未签名的免安装体验包，尚未达到 R2 日常开发版。已验环境限 Windows 11 本机与 Windows Server 2025 CI；不据此声明 Windows 10/Server 2022 支持。布局仍保存到 `%LOCALAPPDATA%\TermForge\workspace-v1.layout`，并非全数据便携模式。具体边界见[发布说明](docs/releases/v0.1.0-alpha.1.md)。也可按[贡献与构建指南](CONTRIBUTING.md)从源码构建，Visual Studio 入口为 `build/bin/Release/termforge_shell.exe`。

- `Ctrl+Shift+T` 新建主区会话，`Ctrl+T` 新建底部会话。
- `F2` 重命名当前区域标签，`F11` 扩大底部 Shell；五条分隔线可拖动。
- `Ctrl++` / `Ctrl+-` 调整当前会话文字大小，`Ctrl+0` 恢复 100%，范围 50%–200%。

完整操作、自动验证与限制见[本地 shell 说明](experiments/shell/README.md)。默认核心 resize 按物理行裁剪，无 reflow；显式候选构建采用独立的 [resize 与历史合同](experiments/core/CANDIDATE.md)。受控 DPI 测试不等于实际跨显示器验证。

## 项目地图

| 要了解什么 | 唯一入口 |
|---|---|
| 已经做了什么、还缺什么 | [当前能力与目标矩阵](docs/FEATURES.md) |
| 最终目标与五个路标（从远到近） | [总体计划](docs/PLAN.md) |
| 官方发行平台、架构与声明的最低系统边界 | [2.7.0 平台范围台账](research/windterm-2.7.0-platform-scope.md) |
| R2 分几步交付，每步还缺什么 | [R2 交付路线与验收](docs/milestones/R2-local-terminal.md) |
| 已验收的基础文字交互合同 | [R1 基础文字交互](docs/milestones/R1-local-text.md) |
| GitHub milestones、issues 和执行顺序 | [任务跟踪](docs/TRACKING.md) |
| 结论依据、复现命令和全部文档 | [证据索引](docs/EVIDENCE.md) · [文档导航](docs/README.md) |

交付主线：**基础文字交互 → 可靠本地终端 → SSH/SFTP 远程日常版 → 高级功能/跨平台/性能 → 完整 2.7.0 复刻验收**。性能工程和定向逆向贯穿各路标，不把远期整块调查当作近期开发的前置。

## 工程原则与来源

功能先通过正确性验证；性能同时衡量吞吐、交互延迟、CPU、内存、稳定性和多会话公平性。不以丢数据、无界缓冲或积压呈现换取跑分，不预设某种语言或框架必然达标。优先复用许可明确的组件，以测量和可维护性决定产品架构。

原创代码和文档采用 [MIT](LICENSE)，第三方组件保留自身许可。实现不依赖 WindTerm 二进制或专有资源；观察、静态线索和推断分别记录。详见[来源规则](docs/PROVENANCE.md)与[性能规范](benchmarks/README.md)。
