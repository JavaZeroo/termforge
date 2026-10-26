# 文档目录

当前是有限 Windows 本地终端原型。先读现状，再沿路标选择任务；研究和历史记录用于证明结论，不代表当前 TODO。

| 问题 | 唯一维护入口 |
|---|---|
| 已经能做什么，离目标差在哪里？ | [功能与差异矩阵](FEATURES.md) |
| 最终目标是什么，如何分阶段完成？ | [五个路标，远到近](PLAN.md) |
| 用户确认了什么，哪些决策仍待确认？ | [ADR-0006：方向与决策权限](adr/0006-confirmed-project-direction.md) |
| R2 分几步交付，当前先做什么、怎样验收？ | [R2 交付路线与验收](milestones/R2-local-terminal.md) |
| 已验收的基础文字交互合同是什么？ | [R1 基础文字交互](milestones/R1-local-text.md) |
| 字体、网格和真实显示如何验收？ | [R2 字体与显示合同](milestones/R2-font-display.md) |
| 哪项可开工，谁阻塞谁？ | [GitHub 跟踪索引](TRACKING.md) |
| 已完成结论由什么支持？ | [证据索引](EVIDENCE.md) |
| 外部核心候选如何构建和验证？ | [Alacritty 固定候选实验](../experiments/candidates/alacritty/README.md) |
| 当前可下载什么版本？ | [Alpha 1 发布说明](releases/v0.1.0-alpha.1.md)、[发行验证](releases/v0.1.0-alpha.1-validation.json) |
| 如何构建、运行与贡献？ | [贡献指南](../CONTRIBUTING.md)、[本地 shell](../experiments/shell/README.md) |
| 逆向什么、如何测量？ | [研究工具](../research/README.md)、[UI 探针](../research/ui/README.md)、[ConPTY 中断输入机制](research/conpty-ctrl-c-input.md)、[性能协议](../benchmarks/PROTOCOL.md) |
| 为什么采用当前技术和限制？ | [ADR-0001](adr/0001-evidence-before-stack.md)、[ADR-0002](adr/0002-automation-first-investigation.md)、[ADR-0003](adr/0003-core-reference-and-reuse.md)、[ADR-0004](adr/0004-presentation-backend.md)、[ADR-0005](adr/0005-physical-row-resize.md) |
| 哪些材料可以进入完整开源项目？ | [来源与公开边界](PROVENANCE.md)、[字体来源](../assets/fonts/README.md) |
| 旧 M0/G1、批次审查和规格去哪里了？ | [历史归档及迁移表](archive/README.md) |

维护分工：FEATURES 说明已合并能力和配置边界；PLAN 维护 R1–R5 范围；当前路标维护交付顺序与验收；TRACKING 只作 issue 导航；GitHub 保存实时状态、阻塞和执行清单；EVIDENCE 索引固定来源。新增事实进入研究/结果，历史审查进入归档，不再在多个计划文件中追加同一段过程。
