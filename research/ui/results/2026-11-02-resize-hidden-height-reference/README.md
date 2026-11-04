# 隐藏主屏高度：原版严格审计失败保留，离线补充审计通过

[隐藏主屏高度探针](../../resize_hidden_height_reference.md)于 2026-11-02 在受控取证环境完成一次七阶段原版运行（`primary 91×20 → 61×20 → 1049h → 61×14 → 61×20 → 1049l → 91×20`）。严格导出被固定文本白名单拒绝（`Only fixed hidden-height-source synthetic document lines are allowed`），该失败原样保留。

本次从仍可用的取证环境恢复出完整录制（含 `app`、`profile` 与运行结果），以**只读原始录制、不重跑原版、不改旧失败记录**的方式完成独立离线补充审计：

- 补充结果：[hidden-height-tail-supplement.json](hidden-height-tail-supplement.json)，SHA256 `4c0f28f25adf2acc6c5124889ae0dd430425f871a7de0d1fdb68ffacb78564ed`。
- `schema=termforge-original-resize-hidden-height-tail-supplement-v1`，`status=original-audit-failed-offline-supplement-passed`，`failure_preserved=true`，`additional_original_runs=0`。
- 原失败复现为同一 `ValueError` 且 `mechanics-check.json` 仍不存在；新增派生文本仅为固定源 `S[:61]+S[91:]`（`S = 89 spaces + uvw`），仅适用于返回阶段 5/6 的文档行 11。
- 授权计划 SHA256 `938992c4c68eb0c0587d3c789e4d1711a70264df5f7631c904707e29c7280fb`；执行源码基 `88ace8dec723f0fe2fb41d7077e83b6cda6a4d61`；执行源归档清单 `51e80b93…`；原版 `WindTerm.exe` `7b1053…` 与 4047 文件清单 `e459adf2…` 重新核验通过。
- 七阶段、源 91 B、DSR 请求 28 B；公开件只含来源可派生文本，不含私有路径、原包或 profile。

## 审计后的阶段观察

| 阶段 | 网格 | DSR | 文档行数 | 文档行 11（零基） |
|---|---|---|---|---|
| 0 primary-initial-body | 91×20 | 19;7 | 19 | 89 空格 + `uvw`（92） |
| 1 primary-width-shrunk | 61×20 | 19;7 | 19 | 89 空格 + `uvw`（92） |
| 2 alternate-entered | 61×20 | 1;1 | 20 | 89 空格 + `uvw`（92） |
| 3 alternate-height-shrunk | 61×14 | 1;1 | 20 | 89 空格 + `uvw`（92） |
| 4 alternate-height-restored | 61×20 | 1;1 | 20 | 89 空格 + `uvw`（92） |
| 5 primary-restored | 61×20 | 14;7 | 19 | **61 空格 + `w`（62）** |
| 6 primary-width-restored | 91×20 | 14;7 | 18 | 61 空格 + `w`（62） |

即：备用屏活动期间 API 仍返回主文档（不能当作活动备用屏网格）；返回主屏后，宽行被恢复到**当前协议宽度**：`u`、`v` 与 61..90 列的空格消失，仅软续接的 `w` 保留，DSR 由 `19;7` 变为 `14;7`。文档行 18（末尾空行）在阶段 6 的公开数组中不再出现。

## 与当前核心的差异

当前核心在 #311 后的工程合同是“隐藏帧只在宽度实际变化时适配足迹，纯高度变化保留源 Cell”（`resize_hidden_height_test`，已合入）。实测该路径返回主屏后保留 `89 空格 + uvw`（92 字符）；上表原版返回后为 `61 空格 + w`（62 字符）。因此：

- 原版行为更接近“返回时按当前宽度裁剪隐藏主屏保留行”，与 #311 的保留合同**不一致**；
- 当前产品按 ADR-0010 已确认的正文保留方向实现，属于对原版观察的**有意差异**；该差异当时尚未经用户确认（状态已于 2026-11-04 由用户决定解除，见下方更新），也未据本记录修改实现或 #311 报告；
- 该差异涉及隐藏帧/1049 返回的具体行为选择，按项目规则应提交用户确认后再决定是否调整合同或实现。

本记录只完成取证与对比，不修改核心代码，不关闭 #27/#29，`semantic_acceptance=false`、`hidden_height_policy_classification=null`。

## 复验

离线审计与测试（不启动原版；证据环境内的录制路径不发布）：

```powershell
python -B -X utf8 -m unittest research.ui.test_resize_hidden_height_reference research.ui.test_export_resize_hidden_height_reference research.ui.test_audit_resize_hidden_height_tail -v
python -B -O -X utf8 -m unittest research.ui.test_resize_hidden_height_reference research.ui.test_export_resize_hidden_height_reference research.ui.test_audit_resize_hidden_height_tail -v
```

补充审计命令（在保留冻结源/录制/计划的环境执行，输出必须为新文件）：

```text
python -m research.ui.audit_resize_hidden_height_tail --directory <original recording> --plan <original plan>
  --destination <new supplement.json> --expected-plan-sha256 938992c4c68eb0c0587d3c789e4d1711a70264df5f7631c904707e29c7280fb
```

## 边界

- 单次直接 Telnet 记录；备用屏阶段的 API 文本不等于活动备用屏网格，矩形/像素与完整 grid、样式未验证。
- 未重跑，未修改旧失败或旧计划；补充件不把新环境倒填为旧运行 provenance。
- 未覆盖 Unicode、其他尺寸/连续输出、保存光标交叉、历史淘汰、Qt/固定 ConPTY 或性能。
- 环境盘点与恢复条件见[取证环境记录](../../../../docs/research/original-evidence-runtime-2026-11-04.md)。

## 2026-11-04 用户确认（后续）

上述差异已由用户在 2026-11-04 明确确认：**保留完整正文，接受该样本与 WindTerm 2.7.0 的可观察差异**；维持 #311 的保留方向，不为匹配原版返回裁剪新增裁剪（[决定评论](https://github.com/JavaZeroo/termforge/issues/29#issuecomment-6627911863)、[PR #312](https://github.com/JavaZeroo/termforge/pull/312)、[ADR-0010](../../../../docs/adr/0010-bounded-resize-retention.md)）。

本记录仍只作为**原版观察**：严格审计失败、离线补充审计结果、`semantic_acceptance=false` 与 `hidden_height_policy_classification=null` 均不改写；产品保留行为属于已确认有意差异，工程回归见 [hidden-height retention 工程合同](../../../../experiments/core/results/2026-11-04-hidden-height-retention/README.md)。该确认不外推到其他隐藏帧/尺寸/Unicode 行为，也不表示正式 Qt 路径已验收或 #27/#29/R2 完成。
