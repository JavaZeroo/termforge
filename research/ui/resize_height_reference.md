# 高度与顺序组合尺寸原版观察协议

本探针为 #27/#29 已确认调查范围内的独立观察工具。复用受保护的 WindTerm 2.7.0 启动与公开 Qt 文本接口，分别启动两个全新进程；不修改既有冻结探针，不经过 ConPTY。准备源码和离线测试不代表原版已经运行或高度语义已经验收。

## 固定样本

| case | 公开 QWidget 窗口尺寸，像素 | 必须观测的 NAWS，列×行 | 阶段数 |
| --- | --- | --- | --- |
| `height-only` | 1360×749 → 1360×569 → 1360×749 | 91×20 → 91×10 → 91×20 | 3 |
| `width-then-height` | 1360×749 → 1000×749 → 1000×569 → 1000×749 → 1360×749 | 91×20 → 61×20 → 61×10 → 61×20 → 91×20 | 5 |

每一步只有一个轴改变；组合样本先缩宽，再缩高、恢复高度，最后恢复宽度。`749−10×18=569` 是从既有几何得到的**未验证预测**。必须独立满足实际 `QWidget::geometry()` 与 NAWS，禁止自动校准、更改断言、重试同一计划。`geometry()` 返回 inclusive LTRB，尺寸计算为 `right−left+1`、`bottom−top+1`；坐标原点可以非零。

两例共享同一 ASCII 输入，首阶段之后仅发送 `ESC[6n`，不发 CUP、CR/LF、重绘或补写：

```text
ESC[?6l ESC[?7h ESC[r ESC[0m ESC[2J
ESC[2;1H HTOP
ESC[6;1H HARD_A CR LF HARD_B
ESC[12;90H uvw
ESC[19;1H LOWEND
```

上面换行与分隔空格仅用于说明。实际输入 **75 字节**，SHA-256 `ff48cd41220a72e5da2350dc97c3c8c8c884d48cc4f7fa2328609bf8bc0cb09e`，准确十六进制：

```text
1b5b3f366c1b5b3f37681b5b721b5b306d1b5b324a1b5b323b314848544f501b5b363b3148484152445f410d0a484152445f421b5b31323b3930487576771b5b31393b31484c4f57454e44
```

`HARD_A` 与 `HARD_B` 由明确 CR/LF 分开；`uvw` 从 91 列窗口的第 90 列开始，跨越原始右边界；`LOWEND` 使协议光标停于第 19 行第 7 列。阶段 0 在稳定取样后必须包含 `HTOP/HARD_A/HARD_B/u/v/w/LOWEND` 各一次，并返回 DSR `19;7`，否则禁止第一次 resize。后续仍记录每个 marker 的有无、完整合成正文及 DSR，不预设其保留、丢失、移动或恢复。

## 同步与停止条件

- 固定启动器、原版 exe/包 manifest、Qt 模块路径、合成会话、独占 station/Job 与清理检查保留；每个新计划只能尝试一次，失败也保留 `.attempted`。
- 初始准确 NAWS 与窗口几何稳定后才释放输入。只有前阶段完整观察写入 ack，peer 才 arm 下一阶段；只有 armed 单轴允许 NAWS 在两端点闭区间内变化，另一个轴必须完全相等。未 arm 的尺寸变化、越界、双轴、过期 event count 或不精确的末端均拒绝。
- NAWS/容量一致且末端尺寸稳定 600 ms 后写 `resize-ready`；每阶段终端状态与窗口几何稳定 400 ms 后写 ack。中间变化会重新计时，不能累计稳定时间。全部阶段观察完成后才能正常结束采样。
- 固定 JS deadline 41 s、兜底 timer 42 s、启动器场景参数 43 s、REA 45 s、CLI 等待 60 s。它们不是全生命周期时限；`whole_lifecycle_bound_seconds=null`。不扩大时限解决失败。
- DSR 使用独立安全界限：行 1..200、列 1..500，私有前缀是否存在单独记录；不以当前 10 行 viewport 截断或拒绝可能真实的 `19;7`。额外/重复回复、非预期字节、错误 source/phase/case、清理失败均阻断审计。

## 文本、位置与公开出口

`visibleLineCount`/`lineText(i)` 的文档行号、`lineStart(i)` 返回值、JS UTF-16 offset、传给 `visibleRectFromPosition` 的 API position、像素矩形与协议 DSR 分列。仅本有限 ASCII 源中，UTF-8 字节数、Unicode scalar 数、UTF-16 code unit 数一致；不推断一般原版位置单位或 C++ Cell 映射。

新 `HeightPeer` 覆盖旧 resize peer 的固定行数约束，旧文件不变。公开 exporter 验证完整源、计划、原包、provider 消息、窗口、逐阶段 ack 和清理身份；全文必须属于按 phase 冻结的有限 ASCII 源目录，包括已声明的 padding 和 61/91 列切片。公开每行目录索引、UTF-8 hex、单位计数及完整数组 SHA，能够独立重建正文。未知文本、重复 marker、任意私有文本、非有限数、重复 JSON 字段均拒绝；未列入白名单的附加元数据不转发。

初始 marker 必须完整；后续缺失可以如实记录，但报告固定 `status="observation"`、`semantic_acceptance=false`、`height_policy_classification=null`。机械完整不等于语义匹配，更不代表原版高度策略、备用帧、Unicode、后续写入、固定 ConPTY 或性能验收。若实际正文超出有限目录，保留失败，另行独立审计；不能临时放宽冻结运行源码。

## 可移植源码与离线验证

运行源码集合由 `resize_height_reference.SOURCE_FILES` 固定为 10 个文件，含实际复用的 protected peer/launcher/Qt source；离线 exporter 单独标识 3 个文件。归档使用根 `reference-source.json` 的严格 `termforge-mode-reference-source-v1`：恰好 `schema/source_base_commit/source_files_sha256`，文件集合必须完全一致。无 manifest 时才读取真实 Git；不伪造 Git。prepare/run 前后验证源码及 manifest 字节，输出/计划目录必须新建且位于 bundle 根 `local/` 内。

```powershell
python -B -X utf8 -m unittest research.ui.test_resize_height_reference research.ui.test_export_resize_height_reference -v
python -B -O -X utf8 -m unittest research.ui.test_resize_height_reference research.ui.test_export_resize_height_reference -v
node research/ui/test_resize_height_stages.js research/ui/resize_height_reference.js
```

Python 测试使用真实受保护 peer 状态机和内存连接替身，覆盖两固定流程、分段 DSR、单轴边界、过期/错误握手、计划归档、完整 exporter 身份与失败脱敏。Node 在纯 V8 中执行实际 controller 和文本采样代码，覆盖 600/400 ms 稳定、初始夹具、固定时限、阶段 ack、几何单位与末阶段缺失。它们不打开 socket、不启动原版或 GUI；exporter 的 CLI 拒绝测试仅启动 Python 子进程。

实际 prepare/run 必须由持有原版环境的执行者基于冻结源码分别建立 fresh plan；本说明不预填本地/虚拟机路径，不授权复用失败计划，也不将离线测试计入原版次数。
