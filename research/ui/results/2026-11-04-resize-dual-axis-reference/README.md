# WindTerm 2.7.0：同时缩宽缩高后视口/滚动与新 CUP 写入

[双轴探针](../../resize_dual_axis_reference.md)在受控取证环境完成一次固定三阶段原版运行（`91×20 → 61×14` 同一窗口动作 → `91×20`）。本记录补充 #27/#29 此前缺失的“同时改变宽高”原版证据；旧只缩宽、顺序宽高组合与隐藏帧记录均不改写。

公开件：[strict-export.json](strict-export.json)，SHA256 `95ad9c5bd8d73e278e794fa327e2fd902739c3da9bd4be6add4d560a00a7d18f`；单次计划 SHA256 `3beabf9a44537d43a400209786f4aeccc29a255307a74f4a4c13cf0ac812d55d`，`.attempted` 单次标记见 [plan.attempted.txt](plan.attempted.txt)（SHA256 `ed761217af8e88440bea5afe49a649b79889f25dabd35db03f616895e8f83a34`）。执行源基点 `dee3c78cfda60a99211dcf8f217be6e361172d5f`；参考 EXE `7b10531d…`、4047 文件清单 `e459adf2…`、源码归档清单 `96d3783a…` 由严格出口核验。

## 三阶段实测

| 阶段 | 实际窗口 | NAWS | 事件数 | DSR | scroll | 文档行数 | 文档行 11（零基） |
|---|---|---|---|---|---|---|---|
| 0 initial-body | 1360×749 | 91×20 | 1 | `19;7` | 0 | 19 | `89 空格 + uvw`（92） |
| 1 dual-shrunk-with-write | 1000×569 | 61×14 | 2 | **`7;4`** | **5** | 19 | **` DQ` + 58 空格 + `w`（62）** |
| 2 dual-restored-unmodified | 1360×749 | 91×20 | 3 | **`7;4`** | **5** | 18 | 同上（62） |

- 阶段 0→1 是**一次窗口动作同时缩宽缩高**；本次只新增一个 NAWS 事件（1→2），不能据此宣称所有情况都原子合并，事件数/顺序按实测保存。
- 缩小后 `scroll=5`，协议 `CUP 7;2` 落在文档行 11（`5 + 7 - 1`），写入 `DQ` 后 DSR 为 `7;4`。`u`、`v` 与右侧源空格被裁到当前 61 列，仅保留软续接 `w`；第 11 行从 92 字符变为 62 字符。
- 恢复 `91×20` 后文档仍是 62 字符裁剪行，`scroll`/DSR 仍为 `5`/`7;4`，没有回拉被裁文本；末尾空行不再出现在 API 文档数组（18 行）。行 11 的 `w` 矩形从阶段 1 的 `(147,126)` 变为阶段 2 的 `(635,108)`。

以上是固定单样本的直接 Telnet UI 观察。`presented_done=null`、`semantic_acceptance=false`，不推断内部 history 条数、完整活动网格、样式或所有尺寸/顺序行为。

## 核心有界字面回放

[`resize_dual_axis_test`](../../../../experiments/core/resize_dual_axis_test.cpp)（CTest `resize_dual_axis_core`）按本记录固定源与协议输入回放：初始 91×20、一次 `resize_preserving(14,61)`、`CUP(7;2)+DQ+DSR`、一次 `resize_preserving(20,91)` 与最终 DSR。11 项检查通过：

- 初始文档 18 个逻辑行，第 11 行为 92 字符；双轴事务后保留行仍为完整 92 字符（产品保留方向，原版此处已裁剪/滚动）。
- 固定写后该编辑行按当前 61 列足迹物化为 ` DQ` + 58 空格 + `w`（62 字符），与记录的原版行逐字节一致；DSR 均为 `7;4`（零基 row6/column3）。
- 恢复 91×20 后书写行仍为 62 字符、DSR 仍为 `7;4`，与记录相同。

这是该固定样本的字面回放，不是原版 scroll/history 布局或一般 reflow oracle；也没有修改生产实现。

## 与既有记录的关系

- [顺序宽高组合](../2026-11-02-resize-combined-reference/README.md)中先单独缩宽（91×20→61×20）保留了 92 字符行，再缩高也未丢右尾；本样本在**同一动作**同时缩宽缩高却得到 61 列裁剪行并滚动 5。两者端点相似但操作不同，不能互相替代，也不证明宽高交换律。
- [原版隐藏帧返回裁剪](../2026-11-02-resize-hidden-height-reference/README.md)记录的是备用屏返回主屏时裁剪隐藏行；本记录是主屏活动时双轴变化后的视口/写入映射，样本与路径不同。
- TermForge 的确认方向是保留完整正文（ADR-0010）；本记录只作为**原版观察**，不把产品保留行为写成与原版一致，也不把裁剪观察回填为产品合同。
- 旧六阶段 B/CD native-sync 失败仍不能由本记录解释：这里是 WindTerm Qt UI 的协议/文档/视口观察，不是 ConPTY 原生网格；同时缩宽+缩高后原生网格如何重排、B/C/D 行如何对应仍缺独立证据。旧断言不删除、不放宽。

## 复验

```powershell
python -B -X utf8 -m unittest research.ui.test_resize_dual_axis_reference research.ui.test_export_resize_dual_axis_reference -v
node research/ui/test_resize_dual_axis_stages.js research/ui/resize_dual_axis_reference.js
```

离线命令只校验冻结源、peer/controller 与严格出口守卫，不启动原版；本记录不重跑原版。

## 边界

单次直接 Telnet、固定 75 B ASCII、主屏、无备用屏/Unicode/连续输出/其他尺寸；不覆盖真实选择/Find、完整网格/样式、ConPTY 传输、物理呈现或性能。#27/#29/#30/#7 仍 OPEN。
