# ADR-0007：Windows 正式采用随包固定微软 ConPTY

- 状态：已确认路线；整合与验收未完成
- 日期：2026-10-28
- 决策者：项目负责人（用户）
- 确认依据：在比较系统 ConPTY、随包固定微软 ConPTY 和 WindTerm 2.7.0 使用的 WinPTY 后，用户明确“现在明确 随包固定微软 ConPTY”。

Windows 11 x64 产品的本地 Shell 后端采用随应用分发、版本和来源固定的 Microsoft ConPTY。集中整合和优化这一条产品路线；系统自带 ConPTY 和 WinPTY 可保留为诊断对照，不继续作为并行产品路线扩展。该次决定只选择 PTY 后端；后续自主 C++ 核心与当前 Qt Widgets GUI 的确认见 [ADR-0008](0008-cpp-core-qt-widgets.md)。路线确认不宣布现有实验实现已经可交付。

## 依据与取舍

随包版本可固定行为和升级节奏，已有加载、API 生命周期、包身份及错误处理实现可复用。[固定后端对照](../research/conpty-margin-backends.md)证明它在指定输入下保留了系统版本丢失的滚动区域命令；代价是承担组件分发、升级及兼容回归。这是限定正确性依据，不是性能排名或 WindTerm 等价证明。

WindTerm 2.7.0 官方说明为避免 vi 的非法字符而改用 WinPTY，[已有本地 cmd 观察](../research/windterm-ui-typography-2026-10-23.md)也实际启动了 winpty-agent。用户在知悉该事实后选择不同的内部后端；[ADR-0006](0006-confirmed-project-direction.md)确认的原版行为优先、重要差异确认和性能容差约定继续生效。

## 实施与确认边界

- 复用 [#47](https://github.com/JavaZeroo/termforge/issues/47) / [Draft PR #50](https://github.com/JavaZeroo/termforge/pull/50) 已有固定包接入及限定修复，不另起一套后端。现有 `Microsoft.Windows.Console.ConPTY 1.25.260930003` 可作整合起点；具体版本、宽度 flags 和内部实现没有被本次用户确认永久锁定。
- 保持同包 `conpty.dll` / `OpenConsole.exe`、来源与许可、构建/运行身份检查及完整生命周期；组件缺失或身份错误明确失败，不静默切回另一条产品路径。后续升级固定来源并复验实际行为。
- 合并及切换产品默认前，验证声明配置的完整 shell/TUI、resize/历史、输入、工作区与回收合同；保留内部跳过、旧失败和环境缺口。正式路线与代码是否合入、是否通过验收分别记录。
- 后端选择不自动批准与原版不同的滚动区域或原生控制台行为。#34 / #41 / #56 中的具体原版差异继续留证；按原版优先调查，接受重要差异或修正原版缺陷前取得用户确认。
- 正式终端核心和当前 GUI 路线已由后续 [ADR-0008](0008-cpp-core-qt-widgets.md)确认；具体实现、原版差异与整体验收继续分开。性能数值容差在可靠基线后另行商定。

## 已知差异

按 #41 的裁决，以下差异**书面接受**，作为 R2 的声明边界而非待修缺陷。

- **主／备用屏 margin 的原生可见性**：随包固定 Microsoft ConPTY 在源控制台保留 `2;3r` / `4;5r`，末端恢复 `REGION_C` / `REGION_D` / `BOTTOM_FIXED`；但源控制台 CHAR_INFO 的 margin 仍与系统后端同为不匹配。系统后端的对照、VT-off 反例、旧 passthrough flag 的拒绝及第一方来源见[每屏 margin 的同源 A/B](../research/conpty-margin-backends.md)。
- **边界**：依赖原生 Console API 读取屏幕状态的应用，可能与末端呈现不同步。该差异不阻塞 #27／#28 在 VT／transport／末端层的验收；原生层的证据缺口单独保留，不用末端通过替代。
- **重启条件**：若后续需要原生 API 精确同步（例如依赖 `CONSOLE_SCREEN_BUFFER_INFO` 的应用），由 [#56](https://github.com/JavaZeroo/termforge/issues/56) 重新评估上游修复或可维护 MIT fork，并按 [ADR-0006](0006-confirmed-project-direction.md) 的原版优先原则重新取得用户确认。
