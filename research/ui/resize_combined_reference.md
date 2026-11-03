# WindTerm 2.7.0：先缩宽、再缩高、分步恢复的独立观察

本工具调查宽度保留的正文在高度变化后是否仍存在，以及恢复高度、恢复宽度分别怎样影响正文、显示与协议光标。它使用新的 namespace、源码归档和单次计划，不修改已运行的 `resize_height_reference`、`resize_geometry_reference` 或 `resize_height_measured_reference`。

## 已知来源与本次未知

- [独立窗口几何](results/2026-11-02-resize-geometry-reference/README.md)：执行源 `3d4c8c2d6222caf0fc024729d9e0b34c020a69cf`，严格公开件 SHA256 `f18a2320c073ed477f773b42076c9968c0c8af8530af19548d5695e66e8cf9b5`，只观察 `1360×749 → 1360×569` 的 `91×20 → 91×14`。
- [纯高度正文观察](results/2026-11-02-resize-height-measured-reference/README.md)：执行源 `50a560739e45d00ee31177d84b7c54a144f5273e`，严格公开件 SHA256 `32d3f6cf2d1c6746ff517b684ecd8c631c86ecc87874209ba5f831ea244730e0`，在 `91×20 → 91×14 → 91×20` 保留正文，DSR `19;7 → 14;7 → 14;7`，恢复高度后查询矩形保持缩高位置。

上述事实不证明 **1000×569 对应 61×14**，也不证明先缩宽后的高度行为。本工具将这组尺寸作为固定待检验预测，`geometry_basis.combined_grid_previously_observed=false`；运行时保存实际测量后再检查，不自动调整窗口、行数、splitter 或期限。

旧 10 行预测失败仍保留原源码和计划；旧 `width-then-height` 没有运行。本工具不是把旧失败预期改成 14 行后重跑。

## 固定五阶段合同

唯一 case：`width-height-retention`，一个独立新进程，最多一次原版运行。

| 阶段 | 请求/实际窗口必须为，px | 待核 NAWS，列×行 | 终端输入 |
| --- | --- | --- | --- |
| initial-body | 1360×749 | 91×20 | 固定 75 B 正文，再 DSR |
| width-shrunk-unmodified | 1000×749 | 61×20 | 仅 DSR |
| height-shrunk-unmodified | 1000×569 | 61×14 | 仅 DSR |
| height-restored-unmodified | 1000×749 | 61×20 | 仅 DSR |
| width-restored-unmodified | 1360×749 | 91×20 | 仅 DSR |

各步只改变一个窗口轴。初始源与纯高度观察相同：

```text
1b5b3f366c1b5b3f37681b5b721b5b306d1b5b324a1b5b323b314848544f501b5b363b3148484152445f410d0a484152445f421b5b31323b3930487576771b5b31393b31484c4f57454e44
```

75 B 的 SHA256 为 `ff48cd41220a72e5da2350dc97c3c8c8c884d48cc4f7fa2328609bf8bc0cb09e`。预置 `HTOP`、CR/LF 分开的 `HARD_A`/`HARD_B`、从第 90 列开始跨过原 91 列边界的 `uvw`，以及把协议光标放在 `19;7` 的 `LOWEND`。初始阶段稳定后必须有七个 marker 各一次和 DSR `19;7`，否则停止。

后四阶段没有 CUP、CR/LF、补写、清屏或重绘。DSR、完整文档、marker 保留与恢复行为均只观察，不从纯高度样本或标准代填预期。DSR 的独立安全界限仍是行 1..200、列 1..500，不由当前 14 行 viewport 夹断。

## 保存、同步与停止

- public Qt 窗口 QRect 使用 inclusive LTRB；协议 DSR、NAWS、capacity、API 文档行/位置、ASCII 文本 offset、marker 像素矩形分别记录。每个稳定端点先保存实际窗口、NAWS、capacity 与事件数，再断言是否符合固定预测。
- 失败结果额外保留 `failure_geometry`：请求和实际窗口、可取得的 NAWS/capacity/事件数。窗口、peer、terminal 独立读取；终端文本绑定失败不会删除已取得的窗口和 NAWS，未取得的字段为 null。该失败快照不进入成功严格出口，也不构成语义通过。
- peer 只在已 armed 的单轴区间接受尺寸变化；每步末端 NAWS 与 capacity 稳定 600 ms 后释放该阶段 DSR，正文和窗口稳定 400 ms 后才确认观察。前一阶段 ack 是下一步的前置，不跳过阶段。
- 维度预测不符、改变未授权轴、越界 NAWS、陈旧事件、缺少阶段/ack、初始 fixture 无效、来源改变或固定期限到达都停止；不在 run 内校准、重试或延长。保留 41/42/43/45/60 s 各层界限，全生命周期界限仍为 null。
- 后续完整文档仅允许固定 ASCII 源生成的有限目录，包含 61/91 列的确定切分与空白 padding；公开件逐行记录 UTF-8 hex、目录索引与完整数组 SHA。目录外文本拒绝，未知内容保留为失败，不扩大旧目录取得通过。
- 后续 marker 丢失可保留为观察，但不作为语义成功。严格结果固定 `status=observation`、`semantic_acceptance=false`；不预判高度变化是否应截尾、重排、移入 history 或回拉。

## 身份与离线验证

新执行源码固定为 10 个文件；新出口固定记录 3 个源文件。manifest、源码归档、计划、原版包/模块、合成 profile、station/Job 与单次 `.attempted` 守卫沿用已验证机制，旧源码字节不变。源码提交和打包由执行者统一完成，之后才建立一次新计划。

```powershell
python -B -X utf8 -m unittest research.ui.test_resize_combined_reference research.ui.test_export_resize_combined_reference -v
python -B -O -X utf8 -m unittest research.ui.test_resize_combined_reference research.ui.test_export_resize_combined_reference -v
node research/ui/test_resize_combined_stages.js research/ui/resize_combined_reference.js
```

离线 Python 执行内存连接中的真实 peer，Node 执行实际 controller；不会启动原版、socket、GUI 或 native 构建。离线通过只说明守卫与采样流程可测，不算原版运行或核心实现验收。本样本不包含备用帧、编辑、Unicode、历史淘汰、ConPTY、实际 glyph 呈现、性能或完整 R2。
