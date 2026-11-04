# 原版取证运行时与归档可恢复性盘点（2026-11-04）

本文记录 #27/#29 原版取证环境在恢复暂停工作后的一次实际盘点与一次证据缺口解除。只记录可核验的路径类别、版本/哈希与恢复条件，不发布私有路径、凭据、原包或 profile。

## 结论

- 取证环境仍在：本机 QEMU 域 `termforge-r2-win11-20261028-7d7e678b` 运行中，可通过宿主 `virsh` + QGA 只读访问。
- 环境内保留完整工具、发放的发行物与历史录制：`C:\TermForgeHarness`（`Tools`/`Results`/`State`）。
- 此前在宿主侧“blocked”的隐藏主屏高度审计不是不可恢复：宿主归档只含 27 个结果文件，缺 `app`、`profile`、`reference-source.json` 等取证输入；完整输入仍在环境内。
- 已用其余正确归档完成一次**离线补充审计**（不重跑原版、不改旧失败）：见[隐藏主屏高度结果](../ui/results/2026-11-02-resize-hidden-height-reference/README.md)。

## 已核验身份

| 对象 | 实际身份 |
|---|---|
| 参考发行物 EXE | `WindTerm.exe` SHA256 `7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0` |
| 参考发行物清单 | 4047 文件，清单 SHA256 `e459adf2f12adb6a1b7f55e66a9da1c22794185e66e848057d21e0c60d669203` |
| 便携运行时 | `r27-reference-runtime-0`（portable Python 104,952 B、Node 93,922,624 B；运行产物另计） |
| 隐藏高度计划 | SHA256 `938992c4c68eb0c0587d3c789e4d1711a70264df5f7631c904707e29c7280fb`；`.attempted` 单次标记存在 |
| 执行源码快照 | 含 `reference-source.json`，清单 SHA256 `51e80b939c4c026a88fd098eeffe4473921815601fbb1435cb7eabd7adedb5cf`；执行基 `88ace8dec723f0fe2fb41d7077e83b6cda6a4d61` |
| 隐藏高度录制 | 七阶段；`app` 4048 文件、`profile`（含 `user.sessions`）在；`mechanics-check.json` 按原审计失败保持缺失 |
| 旧严格导出 | 原失败保留（文本白名单 `Only fixed hidden-height-source synthetic document lines are allowed`），未改写 |
| 补充审计 | `termforge-original-resize-hidden-height-tail-supplement-v1`；`additional_original_runs=0`；公开件 SHA256 `4c0f28f25adf2acc6c5124889ae0dd430425f871a7de0d1fdb68ffacb78564ed` |

## 为什么宿主侧曾显示 blocked

宿主归档（`hidden-height-0-raw.zip`，27 个文件）只是一份结果子集：缺 `app/`（4047 文件发行物副本）、`profile/`（合成 profile 与 `user.sessions`）和 `reference-source.json`（源码归档清单）。严格导出会在 `provenance_facts` 前失败，无法在宿主完成来源复核。归档不是“不可恢复”，而是**未包含全部输入**。

## 本次解除方式

1. 只读列出环境内的工具、运行时、录制与结果；核对 EXE、清单、计划、源码归档清单哈希。
2. 把执行源码快照复制为**新目录**（`r29-hidden-height-audit-tail-0`），只加入离线审计脚本；原始快照、计划、录制和结果不改。
3. 以原始录制、原始计划、原始参考发行物在新目录运行 `research.ui.audit_resize_hidden_height_tail`，输出到**新结果文件**；旧失败保留。
4. 把公开结果复制回仓库并新增结果记录与测试/CTest 注册；不把新环境倒填为旧 provenance，不重跑原版。

## 可继续复用的条件与边界

- 环境仍可用时，`Tools` 下的其他 `r29-*-source-0` 快照与 `Results` 下的对应 `-raw`/`-public` 件可用于同类离线补充审计；每例必须使用自己的计划哈希与新输出文件。
- 一旦环境或录制丢失，这些旧失败只能保持 blocked；解除条件为：原始计划 `.attempted`、完整原始录制（`app`/`profile`/结果）、冻结执行源码与参考发行物同时存在，且允许只读复核。
- 本盘点不证明其他 archived 原件完整，也不把某一次解除外推到未核对的记录。
- 环境内的私有路径、profile、原包与凭据只留本地，不提交；仓库只发布可派生文本、哈希与证明字段。
