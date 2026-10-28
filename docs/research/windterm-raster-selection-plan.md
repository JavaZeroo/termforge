# #129：端点守卫与同图颜色正控的独立计划

状态：完成最终复核及明确单次授权后，已按固定生产源执行一次，得到[三阶段受控栅格的 partial 结果](windterm-raster-selection-reference-2026-10-29.md)。#126 的执行源 `15c736b`、一次 phase0/0 grab/0 pixel 记录保持不变。下面保留执行前方案；原 `execution_authorized=false` 准备计划未修改，新授权计划另建并绑定准备 HEAD `1aa8054`。

固定生产源为 `e302fa3acb6a2ed2982aad5550e027d37c772da6`。[脱敏准备计划](../../research/ui/results/2026-10-28-raster-selection-proposal/plan.json)列出每阶段 hex/SHA、源哈希与固定 Qt 身份；五阶段长度为 `43/38/20/53/54` 字节，另各追加 DSR。它是不可执行的离线提案，不是已获运行授权的计划；最终明确指令后应另生成新的授权计划及唯一输出，不修改旧批身份。

## 选区守卫与分开回放

生产读取已验证的 `selectionCaretP`、`selectionAnchorP`，只接受两个已知、非负、有限的安全整数且相等。先把 `selectionText` 投影为 type/length，再读取端点；非空文本不参与判定，字符串本身不进入新捕获记录。端点不同或未知均停止，不能只删除守卫。

[离线回放](../../research/ui/results/2026-10-28-raster-selection-proposal/replay.json)明确分开三个记录：两条历史原版反例的端点相等，选区门槛允许；旧 #126 实际 phase0 没有采集 P 端点，保持 null、门槛拒绝。历史端点没有移植给 #126 的 marker/ROI，门槛允许也不代表任何栅格或整条工作流通过。每个记录独立绑定原 Evidence ID 和原始文件 SHA。

保留实际 caret 对 H/space ROI 的重叠拒绝。对于 API 返回的零宽插入点，按至少 1 个逻辑像素的占用保守检查；H 与其前置 space 还必须同高、同行且精确相邻。没有缩小、平移 ROI 或假定光标 blink 隐藏。坐标与 DPR 均从新批实际返回值校准，不复用旧批尺寸。

## 五阶段新源

S1=`SGR31;44`，S2=`SGR32;45`，D0=`SGR0`。比较对象始终为字形 H 及其前一个显式空格，每阶段仍只抓一次 viewport。

| 阶段 | 原样目标及正控 | 最后当前 SGR |
|---|---|---|
| 0 | 先 `0m`、清屏并定位 `(2,3)` 写 `D0 H H`；再定位 `(4,3)`、S1 写 `M0 H H`。一张图内各有双 H 校准 | S1 |
| 1 | `1049h` 后不补 SGR 写 `A0 H`；同一行显式 S1 写 ` A0S1 H`，再 D0 写 ` A0D0 H` | D0 |
| 2 | 定位 `(4,5)`，显式 S2 写 `A1 H H`，双 H 校准 | S2 |
| 3 | 重复 `1049h` 后不补 SGR 写 `A2 H`；同一行依次 D0/S1/S2 写 ` A2D0 H`、` A2S1 H`、` A2S2 H` | **S2**，用于明确退出前状态 |
| 4 | `1049l` 后不补 SGR 写 ` M1 H`；同一行依次 S1/S2/D0 写 ` M1S1 H`、` M1S2 H`、` M1D0 H` | D0 |

表格中的每个前导空格均是源字节。阶段 4 的第一个空格用于把保留的 M0 标签与新 M1 标签分成完整 token，不改变 SGR。完整源由 [Python 生产器](../../research/ui/raster_selection_reference.py)的 `phases()` 固定，hex、长度和 SHA 写入准备计划；所有行末都额外发送 `CSI 6n`，随后等待独立阶段确认。

所有标签按**完整 token**查找，不把 `M1S1 H` 当成 `M1 H`，也不匹配标签前后的其他字符。每个 marker 的 label、组别、字形偏移和编号独立核对。同帧正控必须与原样目标处在同一文档行及同一逻辑绘制行。五阶段最长行小于 40 列，source 冻结后逐条完整 NAWS 守卫不变；不重做模式矩阵。

## 比较规则

阶段 0 的 D0/M0、阶段 2 的 S2 都先校验同图双 H 的背景与主要墨迹颜色一致。阶段 1、3、4 优先列出目标与**同帧同一行**显式正控的 RGBA 匹配列表。一个匹配可记录其正控名称；多个匹配、没有匹配或颜色不可区分时，唯一分类均为 null。空列表不能推出 reset、默认色或原版缺陷。

跨 frame 的 D0/S1/S2 基准匹配另列，不能替代同帧正控，也不消除 UI 高亮/基线差异。字段始终命名 `dominant_ink_rgba` / `background_rgba`，描述受控栅格输出，不冒称内部 pen 或完整 current_style。

## 原生与执行边界

[GUI 脚本](../../research/ui/raster_selection_reference.js)沿用已只读核实的固定 Qt5Widgets/Qt5Gui 完整 SHA、模块路径和导出 RVA，GUI 线程执行 QWidget::grab→QPixmap::toImage→QImage::pixel。QPixmap/QImage 各 32B、不预构造；正常返回后按 image→pixmap 普通析构。QRect 为 inclusive 边界，不读取或猜测 format/stride。既有 Qt 栅格计划提供静态 ABI 依据；旧 #126 没有实际调用这些函数，不能转记为原生栅格已验证。

仍为独立 station/desktop、OwnedJob、合成 loopback/profile、一个新唯一目录。新批最多启动原版一次、每阶段至多一次 grab，共最多五次；capture45、CLI wait60，整程上限另列未知。执行前的不可运行提案与[新授权计划的脱敏投影](../../research/ui/results/2026-10-29-raster-selection-reference/plan.json)分别保留；新批实际只尝试 phase0/1/2 的三次 grab。

空图、无墨迹、DPR/坐标/端点/caret/标签不成立即保存 partial/unknown 并停止，不移动 ROI、追加抓取或重跑。只公开合成输入、颜色/尺寸标量和 ROI 哈希；原始像素、截图、路径、PID 和资源均留 local。受控栅格不代表 presented 或性能结果。

## 复核材料

- [显式离线校验与分析](../../research/ui/raster_selection_analysis.py)、[发布门槛](../../research/ui/export_raster_selection_reference.py)：不使用可被 `-O` 删除的 assert；未知与不唯一比较不提升为结论。
- [回放投影器](../../research/ui/selection_guard_replay.py)：核对 #126 原文件 SHA 后确认端点确实未捕获；不由 `caretPosition` 补造 P 端点。
- [公共回归](../../research/ui/test_raster_selection_reference.py)和[原生调用合成边界](../../research/ui/test_raster_selection_native.js)：覆盖历史/缺端点分开回放、非空文本但 collapsed 可继续、noncollapsed/未知仍拒绝、零宽 caret 重叠、完整 token、同帧默认色/主色匹配及歧义、32B sret/析构和未获执行授权不得启动。

本方案不改旧 #118/#123/#126 树或记录，不把不同运行的成功拼成联合验收。新批 phase2 因主要墨迹频数并列而停止，未追加抓取或重跑；完整样式、重复进入和退出恢复仍未知，局部输出颜色事实见独立结果。
