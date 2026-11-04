# WindTerm 2.7.0：主屏隐藏期间的高度变化

本工具在主屏先写入正文并缩宽，再进入备用屏完成高度缩小和恢复，最后返回主屏并恢复宽度。调查目标是**隐藏主屏的保留右尾、saved cursor 与高度变化的交叉**。这是独立 namespace、源码归档和新计划；已有 active-frame 观察不能代填 hidden-primary 策略。

## 已知依据与本次合同

[组合尺寸观察](results/2026-11-02-resize-combined-reference/README.md)已经实际验证本次使用的五组窗口/NAWS 几何，并在始终活动的主屏中保留 75 B 正文。其执行源为 `2c7c5929e11d911ad69a7e2b7c420120591862ed`，严格公开件 SHA256 为 `6a1561f3e6f6bc480c6a2f086adf7ed4798d5007b04a7bc71c7ae9deba90cfe6`。本工具的 `geometry_basis.hidden_primary_height_previously_observed=false` 明确隐藏主屏语义仍未知；不能由活动主屏的 DSR 或文档恢复结果推出它。

唯一 case `hidden-primary-height`，独立新进程最多运行一次：

| 阶段 | 窗口，px | NAWS，列×行 | 本阶段源，再 DSR |
| --- | --- | --- | --- |
| primary-initial-body | 1360×749 | 91×20 | 固定 75 B 主屏正文 |
| primary-width-shrunk | 1000×749 | 61×20 | 无 |
| alternate-entered | 1000×749 | 61×20 | `ESC[?1049h`，8 B |
| alternate-height-shrunk | 1000×569 | 61×14 | 无 |
| alternate-height-restored | 1000×749 | 61×20 | 无 |
| primary-restored | 1000×749 | 61×20 | `ESC[?1049l`，8 B |
| primary-width-restored | 1360×749 | 91×20 | 无 |

只有阶段 `1,3,4,6` 改变窗口；阶段 2、5 不改变窗口，却必须各自完成输入、DSR、稳定正文观察和 ack，不能跳过。各阶段都有独立冻结的 grid；非 resize 阶段复用当前 NAWS 事件数，不能伪造一次尺寸事件。

初始源与组合尺寸样本相同：

```text
1b5b3f366c1b5b3f37681b5b721b5b306d1b5b324a1b5b323b314848544f501b5b363b3148484152445f410d0a484152445f421b5b31323b3930487576771b5b31393b31484c4f57454e44
```

75 B SHA256 为 `ff48cd41220a72e5da2350dc97c3c8c8c884d48cc4f7fa2328609bf8bc0cb09e`。两条 1049 源分别为 `1b5b3f3130343968`、`1b5b3f313034396c`，合计源 91 B；七次 DSR 共 28 B。除这两条明确的模式切换外，初始之后不发送可打印正文、CUP、CR/LF、清屏或重绘。

## 观察与停止边界

- 初始稳定阶段要求 `HTOP`、`HARD_A`、`HARD_B`、`u`、`v`、`w`、`LOWEND` 各一次和 DSR `19;7`，排除无效预置。后续正文、marker、saved cursor 和返回主屏结果均为待观察，不预设恢复结果。
- 备用屏的空文档或零 marker 可以记录。它们不能当作整个语义通过，也不允许用假造的主屏内容填充空观察；返回主屏仍采完整有限文档及 marker 列表，包括可能缺失的右尾。
- 每阶段分别记录 API 文档行、完整合成字符串、source/API marker 位置、像素矩形、capacity/scroll/top 与 DSR。DSR 独立安全界限是行 1..200、列 1..500，不按 14 行 viewport 夹断。ASCII 文本 offset 重合不推广为一般 API 位置单位。
- resize 仅接受 armed 单轴区间，准确末端 NAWS/capacity 稳定 600 ms 后才释放该阶段输入；正文/窗口稳定 400 ms 后才确认。非 resize 的 1049 阶段也完整等待 400 ms 观察和 ack，期间不授权 NAWS 变化。
- 先保存实际窗口、NAWS、capacity、事件数再检查固定几何。失败快照保留独立可取得的窗口与 peer 字段，terminal 绑定失败不能删除它们；不可取得的字段保留 null，不自动校准。
- 完整正文只允许固定 ASCII 源及 61/91 列切分、空白 padding 的有限目录。重复 marker、私有文本、目录外内容、越界、陈旧事件、缺少 ack、来源变化或期限到达均拒绝并保留失败，不修改旧源码或目录取得通过。

严格成功出口仍固定为 `status=observation`、`semantic_acceptance=false`。空备用屏、保留或丢失右尾、DSR 变化都是调查结果；如何实现只能依据实际结果另作字面回放。

## 独立身份与离线验证

运行源码固定 10 个文件，出口身份固定 3 个文件；全部使用新文件名和 schema，旧 height、geometry、measured、combined 源不修改。复用既有 protected launcher、合成 profile、原包/模块、manifest、station/Job、单次 `.attempted` 守卫；保留 41/42/43/45/60 s 各层期限，全生命周期界限仍为 null，不自动重试或延时。

```powershell
python -B -X utf8 -m unittest research.ui.test_resize_hidden_height_reference research.ui.test_export_resize_hidden_height_reference -v
python -B -O -X utf8 -m unittest research.ui.test_resize_hidden_height_reference research.ui.test_export_resize_hidden_height_reference -v
node research/ui/test_resize_hidden_height_stages.js research/ui/resize_hidden_height_reference.js
```

Python 使用内存连接驱动真实 peer，Node 使用实际 controller；离线检查不启动原版、socket、GUI 或 native 构建。源码提交和归档由执行者统一冻结，之后建立一次新计划。未运行原版前，不能把离线测试或活动主屏结果写成隐藏主屏已验收；本工具也不覆盖 Unicode、其他 margin/origin、后续编辑、任意尺寸、历史淘汰、ConPTY、性能或完整 R2。
