# B/CD 恢复：主机重新可达后的身份核验与唯一几何校准（blocked）

## 结论

- 主机 `192.168.3.210:22` 严格 host-key 登录成功；专用 VM `termforge-r2-win11-20261028-7d7e678b` 正在运行，guest `TF-R2-W11`（Windows 11 10.0.26300，用户 `TFTest`，console session 1）可用；固定 WindTerm 2.7.0 EXE SHA-256 与既有计划身份一致 `7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0`。
- 旧失败 `bcd-prewrite-initial-grid-80x25-v1` 的 plan/marker 保留且哈希未变：plan `7718a4b8…`，marker `20fd56d7…`；未重放旧计划。
- 已执行**一次**新的几何校准计划 `windterm-bcd-calibration-v1`（plan SHA-256 `954636ee…`，`.attempted` `20fd56d7…`；case `calibrate`，无 B/C/D/CUP 正文）。初始目标 `1228×839 → 80×24` **成功**（实际 window rect `[160,145,1387,983]`，inclusive size `1228×839`，frozen grid `80×24`，DSR `[1,1]`）。随后固定目标 `988×509 → 60×12` 失败：peer 报 `Resize changed row count`；仅 phase0 完成，phase1 未完成，`audit_mechanics` 因 peer 未 clean 停止。
- 因此校准**未成功完成**，未进入新的 v2 写前/写后正式重试；`executedOriginalRetries=0` 不变。没有重试、没有改期望、没有用估算窗口发送正文。
- 为执行校准，交互会话把专用 VM 显示从 `1024×768` 临时设为 `1600×1200`，校准后已恢复 `1024×768`（`restore_result=0`，bounds `1024×768`）。校准输出在 staging 源副本，旧 plan/attempt/raw 未覆盖。

## 身份与动作

- host：`8845hs`，Ubuntu 24.04.3 LTS，SSH strict host key。
- VM：`termforge-r2-win11-20261028-7d7e678b`，running；guest agent `TF-R2-W11`；WindTerm EXE SHA `7b10531d…`；REA/Node 沿用旧计划身份。
- display：before `1024×768`；校准 `1600×1200`；after restore `1024×768`。
- calibration source：staging copy `C:\TermForgeHarness\Tools\r29-bcd-calibration-src-0`，calibration source hashes 与 `r29-bcd-calibration-source.json` 一致；未修改 `r29-bcd-source-0` 的旧 plan/attempt/raw。
- 校准动作只包含窗口/NAWS/DSR 与只读行数；没有发送 B/C/D/CUP 正文。

## 精确阻塞

peer 在从 `80×24` 调整到目标 `60×12` 时记录 `error="Resize changed row count"`，`naws_history` 虽出现 `[[80,24],[60,12]]`，但 phase1 未完成，无法证明 60×12 目标可用于后续 v2。该错误可能来自 WindTerm terminal API/REA 的行数变化保护或校准控制器预期，现有一次失败不足以归因；不能未经新授权重跑或改预期。

解除条件：用户授权一份新的、仅针对 `60×12` resize 的校准/诊断计划（保留此失败指纹、不重放本计划），先解释 `Resize changed row count` 是 WindTerm 行为、REA/controller guard 还是窗口/DSR 采样问题；只有 60×12 校准成功后，才能冻结并执行原尚未使用的唯一 v2 正式重试。不得把本次 phase0 的 80×24 成功拼成完整校准或 B/C/D 结论。

## 产物

- `bcd-calibration-result.json`、`peer.json`、`cleanup.json`、`profile-check.json`、`run.json`、`bcd-calibration-plan.json` 见本目录。
- 原始私有 raw 留在专用 VM；公开材料仅身份与脱敏结果。
- 旧失败记录：`research/ui/results/2026-11-06-resize-bcd-address-pre-write-failed/` 不变。
- #27/#29/R2 不关闭；未选择 A/B/C，未改产品行为。
