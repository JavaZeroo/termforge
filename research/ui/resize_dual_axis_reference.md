# WindTerm 2.7.0：一次窗口动作同时缩宽缩高后的正文、视口与新写入

本工具补齐 #27/#29 的一个独立原版缺口：既有[只缩宽 CUP/DSR](results/2026-11-03-reflow-addressing-reference/README.md)、[顺序宽高组合](results/2026-11-02-resize-combined-reference/README.md)与[隐藏帧七阶段](../../experiments/shell/results/2026-11-04-qt-hidden-retention/README.md)都不足以决定“同一窗口动作同时改变宽高”后正文/视口/新 CUP 写入位置。本工具用新的 namespace、源码归档与单次计划做一次固定三阶段直接 Telnet 观察，不修改旧探针或旧结果。

## 固定三阶段合同

唯一 case：`dual-axis-retention`，一个独立新进程，最多一次原版运行。

| 阶段 | 请求/实际窗口，px | 待核 NAWS | 终端输入 |
| --- | --- | --- | --- |
| initial-body | 1360×749 | 91×20 | 固定 75 B 正文，再 DSR |
| dual-shrunk-with-write | 1000×569 | 61×14 | 先一次窗口动作同时缩宽缩高；稳定后 `CUP(7;2)` + `DQ` + DSR |
| dual-restored-unmodified | 1360×749 | 91×20 | 一次窗口动作同时恢复；仅 DSR |

初始源与既有组合样本相同（75 B，SHA256 `ff48cd41220a72e5da2350dc97c3c8c8c884d48cc4f7fa2328609bf8bc0cb09e`）：`HTOP`、CR/LF 分开的 `HARD_A`/`HARD_B`、跨第 91 列边界的 `uvw`、把协议光标放到 `19;7` 的 `LOWEND`。初始阶段稳定后必须有七个 marker 各一次和 DSR `19;7`，否则停止。

缩小阶段只在稳定后发送一次固定的 `ESC[7;2HDQ` 与 DSR；恢复阶段只发 DSR。没有其他 CUP、CR/LF、清屏、重绘或文本。`CUP(7;2)` 是冻结前选定的区分位置：它可能落在与保留源行不同的视口行；实际落点只记录、不预判。

## 观测与停止

- public Qt 窗口 QRect 使用 inclusive LTRB；协议 DSR、NAWS 事件、capacity、API 文档行/位置、ASCII text offset、marker 像素矩形分别记录。请求窗口与实际窗口分开；一次窗口请求不假定只产生一个 NAWS 事件，事件数量与顺序全部保存。
- 每个稳定端点先保存实际窗口、NAWS、capacity 与事件数，再断言固定预测。窗口或 NAWS 与请求不符、未 armed 的 NAWS、越界事件、陈旧快照、阶段/ack 缺失、初始 fixture 不符或固定期限到达都停止；不校准、不重试、不延长期限。
- 文档只允许固定源派生的 ASCII 文本：初始 19 行，之后除一行被固定 `DQ` 写入替换外仍为源派生行；写后唯一行、文本 offset、marker 保留/丢失均作为观察，不当作语义通过。
- 严格结果固定 `status=observation`、`semantic_acceptance=false`；不预判原版内部是否保留完整正文、如何滚动/重排或 CUP 应落到哪一行。

## 身份与离线验证

新执行源码固定为 10 个文件；新出口固定 3 个源文件。manifest、源码归档、计划、原版包/模块、合成 profile、station/Job 与单次 `.attempted` 守卫沿用既有机制。新的公开结果只包含固定合成输入/文本、DSR、NAWS/几何标量、写后行与哈希。

```powershell
python -B -X utf8 -m unittest research.ui.test_resize_dual_axis_reference research.ui.test_export_resize_dual_axis_reference -v
python -B -O -X utf8 -m unittest research.ui.test_resize_dual_axis_reference research.ui.test_export_resize_dual_axis_reference -v
node research/ui/test_resize_dual_axis_stages.js research/ui/resize_dual_axis_reference.js
```

离线测试不启动原版、socket、GUI 或 native 构建；离线通过不算原版运行或核心实现验收。本样本不覆盖备用帧、Unicode、其他尺寸/顺序、连续 resize、历史淘汰、ConPTY、呈现或性能。
