# 每屏 margin 原生合同：路径结论与后续方案（#56）

本页是 [#56](https://github.com/JavaZeroo/termforge/issues/56) 的收口记录：把已完成的证据整理成**可执行的路径结论**，并明确“仍未做、需谁决定”。它不替代 [#34](https://github.com/JavaZeroo/termforge/issues/34) 的架构决定，也不声称 [#41](https://github.com/JavaZeroo/termforge/issues/41) 的原合同已验收。

## 1. 合同已经固定（不再重写预期）

- 逐字段判据、四阶段固定 oracle 与 175B 源：[conpty-native-margin-path.md](conpty-native-margin-path.md)、[margin_contract.py](../../experiments/conpty/margin_contract.py)、[白名单记录](../../experiments/conpty/results/2026-10-26-native-margin-path/validation.json)。
- 分层检查点始终分开：**应用源 VT / 源控制台 CHAR_INFO / transport / 末端候选**；末端正确不能替代原生状态，原生正确也不能补回 transport 已丢失的控制序列。
- 原失败（系统后端 native final 红、官方 80 列原 5 秒生命周期失败）保留在原记录中，未重跑覆盖。

## 2. 根因已收敛到固定上游源码

固定包 **Microsoft.Windows.Console.ConPTY 1.25.260930003**（源码提交 dc8ae365847d41f62d1ebca93815bb00959bc12f，MIT）：

- SCREEN_INFORMATION::_CreateAltBuffer 删除新备用屏自己的 state machine，共享主屏 _stateMachine；
- AdaptDispatch 有两个保存光标状态但只有一个 _scrollMargins，_SetAlternateScreenBufferMode 不保存/重置/恢复它，DECSTBM 却写它；
- 原生 GetSet 负责实际 buffer 切换，原生与末端因此可能处于不同状态实现。

这解释了“备用屏设置 4;5r 后，恢复的主屏 final 仍被上一区域影响”。它是固定源码 + 运行现象的共同推断，**尚无上游确认**，也未被上游版本修复。

## 3. 受支持路径比较（含真实构建与运行，不再只有“可应用”）

| 路径 | 现有证据 | 结论 |
|---|---|---|
| 上游修复后固定正式包 | 已固定根因；官方包 native 仍红 | 长期首选；需把 oracle/最小补丁送上游，接纳与发布未知 |
| 小型 MIT fork（固定 1.25 + 单 margin 补丁） | [构建入口（原 PR #96，已关闭）](https://github.com/JavaZeroo/termforge/pull/96)：baseline focused 1 fail、patched focused 1/1、full adapter 412/412 零 skip；[同源原生/末端对照（原 PR #98，已关闭）](https://github.com/JavaZeroo/termforge/pull/98)：8 次 VT-on/off × 两尺寸，patched 四阶段原生全绿、transport 末端正确、自然 37/EOF109/Job0；[控制场景补充（原 PR #100，已关闭）](https://github.com/JavaZeroo/termforge/pull/100)：12 次（main-only / default-alt / repeat-alt × 两尺寸 × 两版），baseline 的错误精确落在预期阶段，patched 全部阶段文字/完整格属性/光标/viewport 正确 | **唯一已有“同输入 red→green”的路径**；仍是候选，未接入产品 |
| 关闭 ENABLE_VIRTUAL_TERMINAL_PROCESSING 当 raw 后端 | 80×24 末端表面正确，但原生格已把 ESC 当字面内容，12×7 丢 BOTTOM_FIXED | 拒绝 |
| 旧 passthrough flag | 第一方已删除；当前头文件 0x08 是字素宽度策略 | 拒绝 |
| 换另一 console server / 桥接层 | 无同输入原生+末端证据，维护面远大于局部状态修正 | 不采纳；如要调查需另立证据项 |

## 4. 结论（供 #34 决策，需用户确认）

**推荐路径：随包固定 ConPTY 采用上述 MIT fork，并把对照矩阵作为升级门禁。** 这是对 [ADR-0007](../adr/0007-bundled-microsoft-conpty.md)“随包固定官方 ConPTY”的一处依赖变更，按 [ADR-0006](../adr/0006-confirmed-project-direction.md) 必须由用户确认后才生效；本页只是提案 + 实施/维护方案。

采纳前必须补齐（都已有明确入口，尚未完成）：

1. fork 的独立发布身份：源清单、补丁 SHA、双文件 SHA、LICENSE/NOTICE、可复建构建记录（Draft PR #96/#98/#100 已具备大部分，需整理成正式来源清单）。
2. 原 175B + 四阶段 + 控制场景在原矩阵上的复验（已做过 20 次运行，仍需在最终 fork 产物上重跑一次冻结批次）。
3. 产品侧交叉：样式/非颜色属性、history、resize、查询/API、GUI 真实工作流；系统/服务器最低版本与更新策略。
4. 维护方案：每次上游升级必须重放 adapter tests + 本页矩阵；补丁依赖的状态字段归属一旦变化即失效，需重新取证。

**不采纳 fork 时的替代方案**：维持随包官方 1.25，并把原生 margin 差异保留为已知缺口，直到上游修复；这会持续阻塞 #41 的原合同，不能以“末端正确”结项。

## 5. 边界

- 本页不改变产品后端配置、ADR-0007 的当前实现、#41 的开放状态或 #34 的决定权。
- 20 次 fork 运行全部来自 Draft 分支（#96 → #98 → #100 的固定源码），未合并；环境为 Windows 11 build 26200，未覆盖 Server/最低版本、真实 GUI 与长期资源。
- 未测：显式 SGR 变更与保存恢复、非零光标起点、历史/淘汰、resize 交叉、查询/API 交叉、其他应用矩阵、性能。
