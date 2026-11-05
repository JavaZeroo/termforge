# WindTerm 2.7.0：同时缩宽缩高并写入后的视口/行/DSR

[双轴探针](../../resize_dual_axis_reference.md)在受控取证环境完成一次固定三阶段原版运行（`91×20 → 61×14` 同一窗口动作 → `91×20`）。本记录补充 #27/#29 此前缺失的“同时改变宽高”原版端点；**阶段 1 是缩放之后又执行固定 `CUP(7;2)+DQ` 的观测**，没有“缩放后、写入前”的原版正文端点。旧只缩宽、顺序宽高组合与隐藏帧记录均不改写。

公开件：[strict-export.json](strict-export.json)，SHA256 `95ad9c5bd8d73e278e794fa327e2fd902739c3da9bd4be6add4d560a00a7d18f`；单次计划 SHA256 `3beabf9a44537d43a400209786f4aeccc29a255307a74f4a4c13cf0ac812d55d`，`.attempted` 单次标记见 [plan.attempted.txt](plan.attempted.txt)（SHA256 `ed761217af8e88440bea5afe49a649b79889f25dabd35db03f616895e8f83a34`）。参考 EXE `7b10531d…`、4047 文件清单 `e459adf2…`、源码归档清单 `96d3783a…` 由严格出口核验。

首次严格导出因冻结白名单未接受本轮实际出现的“当前列裁剪 + 固定写入”文档行而拒绝（`run_plan` 在 `audit_mechanics` 停止，`mechanics-check.json` 未写出）；失败保留，未重跑原版。修正仅限严格的离线行白名单/导出器，随后在同一原始录制、同一计划与同一 `.attempted` 标记上重新生成公开件。

## 身份分层

| 身份 | 内容 |
|---|---|
| 冻结原版运行源 | 提交 `dee3c78cfda60a99211dcf8f217be6e361172d5f`；10 个执行源哈希见 [validation.json](validation.json) 的 `frozen_original_run`，与严格出口 `execution_source_files_sha256` 一致；计划/attempted 见上 |
| 离线严格出口 | 提交 `2fc3f25cb1887b569c583068a49e874fa4bd9156` 的导出器/白名单；严格出口的 `offline_export_source_files_sha256` 已核对，未把这些后加文件倒填为原运行源 |
| 核心回放/本地测试源 | 同一提交 `2fc3f25` 的 `resize_dual_axis_test.cpp` 与 CMake 注册；不是原版运行源 |
| 文档 | 早期报告提交 `1e41fc7…` 与本次修正提交（见 validation 的 `documentation`） |

## 三阶段实测

| 阶段 | 实际窗口 | NAWS | 事件数 | DSR | scroll | 文档行数 | 文档行 11（零基） |
|---|---|---|---|---|---|---|---|
| 0 initial-body | 1360×749 | 91×20 | 1 | `19;7` | 0 | 19 | `89 空格 + uvw`（92） |
| 1 dual-shrunk-with-write | 1000×569 | 61×14 | 2 | **`7;4`** | **5** | 19 | **` DQ` + 58 空格 + `w`（62）** |
| 2 dual-restored-unmodified | 1360×749 | 91×20 | 3 | **`7;4`** | **5** | 18 | 同上（62） |

- 阶段 0→1 是**一次窗口动作同时缩宽缩高**；本次只新增一个 NAWS 事件（1→2），不能据此宣称所有情况都原子合并，事件数/顺序按实测保存。
- 阶段 1 在缩放稳定后又执行了固定 `CUP(7;2)+DQ`：`scroll=5`、DSR `7;4`，文档行 11 为 62 字符（` DQ`+58 空格+`w`）。**无法从本录制判断裁剪/物化发生在 resize、CUP 还是写入的哪一时点**；也没有缩放后、写入前的原版正文观测。
- 恢复 `91×20` 后，已写入行仍为 62 字符、`scroll`/DSR 仍为 `5`/`7;4`，没有回拉；这是写入后的恢复端点，不单独证明 resize 本身的行为。行 11 的 `w` 矩形从阶段 1 的 `(147,126)` 变为阶段 2 的 `(635,108)`。

以上是固定单样本的直接 Telnet UI 观察。`presented_done=null`、`semantic_acceptance=false`，不推断内部 history 条数、完整活动网格、样式或所有尺寸/顺序行为，也不推断裁剪发生的时点。

## 核心有界工程回放

[`resize_dual_axis_test`](../../../../experiments/core/resize_dual_axis_test.cpp)（CTest `resize_dual_axis_core`）按固定源与协议输入回放：初始 91×20、一次 `resize_preserving(14,61)`、`CUP(7;2)+DQ+DSR`、一次 `resize_preserving(20,91)` 与最终 DSR。11 项检查通过：

- 初始文档 18 个逻辑行，第 11 行为 92 字符；双轴事务后、固定写入前保留行仍为完整 92 字符。**这是 TermForge 的工程保留不变量，不是原版 pre-write 观测**。
- 固定写后该编辑行按当前 61 列足迹物化为 ` DQ` + 58 空格 + `w`（62 字符），与记录的原版**写后**行逐字节一致；DSR 均为 `7;4`（零基 row6/column3）。
- 恢复 91×20 后书写行仍为 62 字符、DSR 仍为 `7;4`，与记录的写后端点相同。

这是该固定样本写后端点的工程回放，不是原版 scroll/history 布局、pre-write 裁剪时点或一般 reflow oracle；没有修改生产实现。

## 与既有记录的关系

- [顺序宽高组合](../2026-11-02-resize-combined-reference/README.md)没有同样的后续写入干预，而本样本正文端点是“缩放+固定写入后”；两者不能作为裁剪归因的对照，不能据此裁决双轴 resize 与顺序 resize 的裁剪差异，也不证明宽高交换律。
- [原版隐藏帧返回裁剪](../2026-11-02-resize-hidden-height-reference/README.md)记录的是备用屏返回主屏时裁剪隐藏行；本记录是主屏双轴变化并写入后的视口/写入端点，样本与路径不同。
- TermForge 的确认方向是保留完整正文（ADR-0010）；本记录只作为**原版写后端点观察**，不把产品保留行为写成与原版一致，也不把裁剪观察回填为产品合同。
- 旧六阶段 B/CD native-sync 失败仍不能由本记录解释：这里是 WindTerm Qt UI 的协议/文档/视口观察，不是 ConPTY 原生网格；同时缩宽+缩高后原生网格如何重排、B/C/D 行如何对应仍缺独立证据。旧断言不删除、不放宽。

## 复验

```powershell
python -B -X utf8 -m unittest research.ui.test_resize_dual_axis_reference research.ui.test_export_resize_dual_axis_reference -v
node research/ui/test_resize_dual_axis_stages.js research/ui/resize_dual_axis_reference.js
```

离线命令只校验冻结源、peer/controller 与严格出口守卫，不启动原版；本记录不重跑原版。

## 边界

单次直接 Telnet、固定 75 B ASCII、主屏、无备用屏/Unicode/连续输出/其他尺寸；不覆盖真实选择/Find、完整网格/样式、ConPTY 传输、物理呈现或性能。**缩放、CUP 与写入对 62 字符行的责任划分未知，原版 pre-write 正文保持未知。** #27/#29/#30/#7 仍 OPEN。
