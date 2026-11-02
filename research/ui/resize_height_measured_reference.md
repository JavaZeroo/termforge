# 基于独立几何实测的纯高度正文观察

这是新 namespace、新执行源码集合、新计划的单 case 调查，不修改或重放旧 `resize_height_reference`。旧 `cf58f30` 的 10 行预测失败与原计划原样保留，旧 `width-then-height` 运行次数仍为 0。

## 已知几何与待观察语义

[独立几何结果](results/2026-11-02-resize-geometry-reference/README.md)实际测得窗口 `1360×749 → 1360×569` 对应 `91×20 → 91×14`；终端 viewport 高度只减少 120 px，另一 splitter 子区域减少 60 px。严格公开件 SHA256 为 `f18a2320c073ed477f773b42076c9968c0c8af8530af19548d5695e66e8cf9b5`。该两状态结果只提供本样本的缩高几何依据，未观察正文、DSR 或恢复。

新 case **`height-retention-measured`** 固定为：

| 阶段 | 窗口实际尺寸必须为，px | NAWS/capacity 必须为 | 终端输入 |
| --- | --- | --- | --- |
| initial-body | 1360×749 | 91×20 | 固定 75 B 正文，再 DSR |
| height-shrunk-unmodified | 1360×569 | 91×14 | 仅 DSR |
| height-restored-unmodified | 1360×749 | 91×20 | 仅 DSR |

**恢复端 20 行是本次要独立检验的几何条件**，不写成之前已经观察通过。运行中不调整高度、不改目标行数、不改 splitter、不重试失败计划。高度缩小和恢复期间没有 CUP、CR/LF、补写、清屏或重绘。

## 精确初始正文

两次高度变化之前只发送同一原先已验证的 75 B：

```text
1b5b3f366c1b5b3f37681b5b721b5b306d1b5b324a1b5b323b314848544f501b5b363b3148484152445f410d0a484152445f421b5b31323b3930487576771b5b31393b31484c4f57454e44
```

SHA256：`ff48cd41220a72e5da2350dc97c3c8c8c884d48cc4f7fa2328609bf8bc0cb09e`。`HTOP` 是上部标记，`HARD_A` 与 `HARD_B` 由 CR/LF 分开，`uvw` 从原第 90 列开始跨过 91 列右边界，`LOWEND` 将协议光标停在第 19 行第 7 列。

初始稳定阶段必须有七个 marker 各一次和 DSR `19;7`，否则停止。之后 marker 是否保留、文档位置、像素位置、DSR 是否夹到当前高度、增高后是否恢复均是未知观察，不能由标准或当前 C++ 策略代填预期。DSR 的独立安全边界始终为行 1..200、列 1..500，不因 viewport 只有 14 行就拒绝可能真实的第 19 行。

## 机制、证据与局限

- 复用已经离线验证的 protected launcher、Telnet peer、初始/逐阶段 ack、NAWS 单轴边界及 public Qt 文本采样机制，但执行源和模板使用本工具的独立文件名、schema 与消息 namespace。旧源码字节保持不变。
- 初始和每次 resize 的准确 NAWS/capacity 稳定 600 ms 后才释放该阶段输入；正文/窗口状态稳定 400 ms 后才确认观察。每阶段先观察再进入下一步，缺失任何阶段不能形成完整结果。
- 沿用单次 `.attempted`、原版包/模块、合成 profile、station/Job、固定界限以及 41/42/43/45/60 s 各层期限。不增加时限或自动校准；全生命周期界限仍为 null。
- 计划包含 `geometry_basis` 的公开件 SHA、已观测两组窗口/NAWS 与 `restore_geometry_previously_observed=false`。执行源码清单固定为 10 个文件；归档与本地 Git 身份分开，prepare/run 前后严格核对源与 manifest。
- 全文只允许固定 ASCII 源导出的有限目录，并公开可重建的 UTF-8 hex、目录索引及完整数组 SHA。后续 marker 缺失可记录为观察，任意私有文本、非预期字节或目录外文本仍拒绝。超出冻结目录时保留失败，不改旧源取得通过。
- 原 API 文档行/位置、ASCII 字符 offset、DSR 与 `visibleRectFromPosition` 像素矩形分别记录。严格结果固定 `status=observation`、`semantic_acceptance=false`；不声称已验高度保留、滚动、历史、备用帧、Unicode、ConPTY 或性能。

## 离线检查与原版执行

```powershell
python -B -X utf8 -m unittest research.ui.test_resize_height_measured_reference research.ui.test_export_resize_height_measured_reference -v
python -B -O -X utf8 -m unittest research.ui.test_resize_height_measured_reference research.ui.test_export_resize_height_measured_reference -v
node research/ui/test_resize_height_measured_stages.js research/ui/resize_height_measured_reference.js
```

Python 用内存连接执行真实 peer 状态机，Node 执行实际 JS controller；它们不启动原版、socket、GUI 或 native 构建。原版由执行者在源码提交冻结后统一建立新计划运行一次。本工具准备及离线通过均为原版 0 次，不与旧失败或独立几何运行合并计数。
