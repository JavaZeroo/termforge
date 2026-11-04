# ADR-0010：有界正文保留与独立显示投影

- 状态：已确认实施方向；内容替换与联合验收尚未完成。2026-11-04 另确认隐藏帧返回保留完整正文、接受原版裁剪差异。
- 日期：2026-11-02（2026-11-04 增补确认）
- 决策者：项目负责人（用户）
- 执行范围：[#27](https://github.com/JavaZeroo/termforge/issues/27) / [#29](https://github.com/JavaZeroo/termforge/issues/29) 联合切片；具体证据、责任及切片见[已确认方案](../research/r27-r29-resize-proposal.md)。

## 确认依据

本会话提交的原问：

> 是否按已整理的方案，联合推进 #27/#29：保留现有核心和行身份体系，改为缩放时保留正文、按窗口宽度重新显示，并同步处理历史、选择与查找？这涉及替换现有裁剪逻辑，AGENTS.md 要求重大替换由你确认；尚未查清的原版行为仍先取证。

用户原答：

> 按方案推进（推荐）

问题标识：`["request_user_input_async","call_4555e1f508bf453fb1b545cf8e0d3fb0",0]`。确认来自这一明确答复，不由文档状态、提交、合并或 issue 关闭推导；不重复申请同一方案的批准。

## 决策及理由

保留现有自主 C++ 核心和 Screen owner、行 ID/版本、rank、epoch/revision 体系；在其中以**有界正文保留、独立显示投影及明确的位置迁移**替换物理裁剪式 resize，并联动历史、选择与 Find。Qt Widgets 与随包固定微软 ConPTY 路线不变。

这是对 [#29 既有 C 叠加路线](https://github.com/JavaZeroo/termforge/issues/29#issuecomment-6541164759)的内容职责扩展：[#264](https://github.com/JavaZeroo/termforge/pull/264) 的 ViewMapping 及身份约束保留，但单靠显示映射不能恢复已被 `line.resize` 丢弃的正文，因此本次明确批准改变内容保留与 resize 实现。旧 B 栈 #108–#113 不恢复，也不引入平行内容 owner、产品后端或大依赖。

[原版三阶段样本](../../research/ui/results/2026-11-02-resize-state-reference/README.md)在 `91×20 → 61×20 → 91×20` 中保留 marker 文档位置，可见矩形重排后恢复，DSR 却始终为 `11;2`。[独立 Qt / 固定 ConPTY 诊断](../../experiments/shell/results/2026-11-02-qt-terminal-modes/README.md)则记录 native grid 重排而 core 裁剪的失配。两种运输和几何不混为同一对照；这些事实支持分离正文、显示与协议坐标，不能据此补定尚未知的寻址行为。

局部补搬 B/CD 或附加裁剪缓存不能覆盖软硬行、后续写入、历史淘汰和文字锚点的一致性；继续只裁剪也不能完成保留的 #29 任务。因此选择在现有 owner 内完成可验证的内容与视图分离。

## 隐藏帧返回的保留行为差异（2026-11-04 已确认）

[#311](https://github.com/JavaZeroo/termforge/pull/311) 已登记隐藏主屏纯高度变化的正文保留工程合同。[#312](https://github.com/JavaZeroo/termforge/pull/312) 的离线补充审计显示：原版 WindTerm 2.7.0 在“先缩窄主屏、进入备用屏、改变高度、再返回主屏”的固定七阶段路径上，返回时按**当时的协议宽度**裁剪隐藏主屏行。固定样本由 `89 空格 + uvw`（92 字符）变为 `61 空格 + w`（62 字符），`u`/`v` 与第 61–90 列空格消失，DSR `19;7 → 14;7`。这与当前保留合同不一致，需用户决定。该审计不重跑、不改写原严格失败，`semantic_acceptance=false`。

用户原答（2026-11-04）：

> 保留完整正文，明确接受与 WindTerm 的这项差异

**决定**：维持 #311 的保留方向，产品保留完整正文；不为匹配原版的返回裁剪新增裁剪，也不把该场景改写为“原版一致”。该差异登记为**已确认的产品有意差异**，解除 `hidden-frame-return-retention-vs-observed-clipping` 的人工确认阻塞，不再重复要求用户选择同一问题。

**确认依据**：[决定评论](https://github.com/JavaZeroo/termforge/issues/29#issuecomment-6627911863)、[隐藏主屏高度补充审计](../../research/ui/results/2026-11-02-resize-hidden-height-reference/README.md)。

**四项身份分别保留**：

| 身份 | 内容 | 证据/状态 |
|---|---|---|
| 原版观察 | WindTerm 2.7.0 在上述单例七阶段路径返回时裁剪隐藏行 | [补充审计](../../research/ui/results/2026-11-02-resize-hidden-height-reference/README.md)；旧严格失败保留，未重跑 |
| 产品有意差异 | TermForge 返回主屏保留完整源正文 | 本节；用户 2026-11-04 确认 |
| 工程回归 | `resize_hidden_height_test`（`retained_resize_hidden_height_core`，核心显式入口） | [#311 工程合同](../../experiments/core/results/2026-11-04-hidden-height-retention/README.md)；工程不变量，不是原版 oracle |
| 正式路径验收 | 真实 child → 固定 ConPTY → Qt 路径上的同一场景 | **未完成**：既有 Qt 保留契约只覆盖仅缩宽、同高度（12/12），#311 的 Qt CTest 5/5 只是既有套件回归；留 #29 后续切片 |

**边界**：

- 仅覆盖上述路径与固定样本；不外推到所有隐藏帧、其他尺寸、连续 resize、Unicode/宽字/结合字、保存光标、历史淘汰或备用屏后续输出。
- 不改写原严格失败、[离线补充审计](../../research/ui/results/2026-11-02-resize-hidden-height-reference/README.md)、`semantic_acceptance=false` 或原版隐藏策略分类保持 null 的事实。
- 不改变 [R2 范围及验收合同](../milestones/R2-local-terminal.md)；#27/#29 保持 OPEN，不据此声明完整 resize/reflow、WindTerm 等价或 R2 完成。
- 若正式路径实测未保留完整正文，按新证据修正并重新取证；本确认不豁免工程回归。

## 实施与确认边界

- 按方案的小切片推进：先固定影响实现的原版行为，再实现有界保留/投影及协议交叉，联动 TextDocument、HistoryViewport、ViewMapping、TextSelection、TextSearch，最后在同一 Qt / 固定 ConPTY 配置闭合用户路径。
- resize 后协议写入、光标、备用屏、宽度 1、宽字/结合字、高度变化与连续尺寸等未知行为仍先取证。显示矩形变化不自动改变 DSR；当前尺寸合并风险仍是静态风险，不作为已复现竞态。具体永久数据结构和未观测行为未被本决定预先定案。
- 重要原版差异或原版缺陷修正仍按 [ADR-0006](0006-confirmed-project-direction.md)另行确认；容量口径、有损边界及实现策略须有可审查依据。保留有界性、所有权和身份约束，不能靠无限缓存或复活已淘汰内容取绿。
- [ADR-0005](0005-physical-row-resize.md)保留旧物理行实现与结果身份。本决定授权替换其裁剪路线，不抹去历史失败或把新行为回填为旧样本结果；相应实现与验收完成前，不宣称替换已交付。
- [R2 范围及验收合同](../milestones/R2-local-terminal.md)不变；本决定不关闭 #27/#29，不豁免原失败，不把 Qt 模型接线当完整历史/复制/Find UI 交付，也不批准任何数值性能容差或完整 WindTerm 等价声明。

实现状态继续由责任 issue 和 FEATURES 维护；本文仅记录此次架构决定及其边界。
