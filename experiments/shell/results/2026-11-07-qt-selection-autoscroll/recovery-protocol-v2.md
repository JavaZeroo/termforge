# #327 恢复协议 v2（离线提案，未执行，待用户授权）

基线：PR327 head `ac5dd4fb5de5e6b22302c1ed32bfd6483cc4fe77`；main `8506c6c` 仅参考。旧账本/失败产物不变，现有 5 次失败、4 次重跑与一次恢复额度已耗尽。本文件不运行、不构建、不改产品。

## 1. 固定输入与起点

- fixture：`history_fixture.hpp` 的确定性 child。输出为硬 CRLF 物理行 `TF_H_001` … `TF_H_160`，随后 `TF_END`、prompt；启动 1500ms 后自发追加 `TF_H_161` … `TF_H_180`、`TF_END2/3`。每行是独立硬行，无软续行，文本长度 8，不含宽字/结合标。
- settled 条件：等待画面出现 `TF_H_180`、`TF_END3` 与 prompt，且之后至少一个完整观察窗（例如 500ms）无新输出/无保留行变化；记录当时的 retained_size、top、anchor、滚动条 range/value。此后拖选期间不得有追加。
- 通过公开键盘 `Ctrl+Shift+Home` 到历史最早保留行，再通过**公开滚动条单步/页操作**向下移动，直到 painted page 的 row0 文本恰为 `TF_H_100`（每次移动都记录滚动条 value/row0 字面）。若越过或找不到该行，协议失败，不选其他行替代。
- 此时屏幕 page top source = `TF_H_100`，row1 = `TF_H_101`；源行均为硬行。只读记录来自 painted page 文本；independent expected 不来自 actual selection range。

## 2. 非滚动 sanity（公开鼠标）

- 鼠标左键在 row0、`TF_H_100` 末尾按下：`x = margin + 8*cell_width`，`y = margin + 1`（选择命中 nearest 规则得到 column=8，即行尾 half-open 端点）；按下事件 `MouseButtonPress`, LeftButton, buttons=Left。
- 在同一 row0 的 column0 释放：`x=margin, y=margin+1`，`MouseButtonRelease`, LeftButton, buttons=NoButton。无移动/无 timer。
- 预期：range 为 `TF_H_100` 的 col0..8；Copy 注入 writer 的完整文本恰为 UTF-8 `TF_H_100`（8 字节，无 CRLF，因为 begin/end 在同一行且终列=行宽，offset+1==saved size）。hex `54465f485f313030`。若有 CRLF 或额外行即失败。

## 3. 指定自动滚动路径（默认 40ms，公开事件）

- 重新定位到 page top=`TF_H_100`（同上），记录滚动条 value `v0`、top、painted row0 字面。
- `MouseButtonPress` 在 row0、column8（`TF_H_100` 末尾）：x=`margin+8*cw`, y=`margin+1`，LeftButton，buttons=Left。
- 立即 `MouseMove` 到可见网格上方外部：x=`margin`（column0），y=`-20`，buttons=Left，button=NoButton。该移动同时把 selection endpoint 设为 edge column0；timer 在 40ms 路径启动。
- 观察并记录滚动条 `valueChanged` 序列与时间：从 `v0` 到 `v0-1`（即 top 从 `TF_H_100` 到 `TF_H_099`）是**第一次有效 tick**。在该信号槽内立即发送 `MouseButtonRelease` 到 `(margin,-20)`，LeftButton，buttons=NoButton。这样下一次 40ms tick 尚未发生。
- 记录：首次移动时间、第一次 valueChanged 时间、release 时间、信号序列、release 后 200ms 内是否还有 valueChanged/top/selection 变化。若出现第二个 valueChanged 或 top 100→99→98，协议失败（不是恰好一次有效 tick）。
- 通过出口（全部满足）：
  - valueChanged trace 只有一次有效变化 `v0 -> v0-1`；release 后观察窗无进一步滚动。
  - actual source range：begin = `TF_H_099` col0，end = `TF_H_100` col8；不得用 range 生成 expected，expected 来自固定字面。
  - painted highlight：page row0 是 begin 行 `TF_H_099` 的 0..8 列，row1 是 end 行 `TF_H_100` 的 0..8 列；只读 span 映射到固定字面行，不混 `visible_lines` 与 painted 坐标。
  - Copy 完整文本：按 `TextSelection::copy` 的 half-open/包含性，begin `TF_H_099` col0、end `TF_H_100` col8，且两行均硬行：复制 line099 全文 + CRLF + line100 全文，**无末尾 CRLF**。
    - 字面：`TF_H_099\r\nTF_H_100`
    - UTF-8 hex：`54465f485f3039390d0a54465f485f313030`
    - 长度 18 字节。
- 失败出口：任一字面、range、highlight、tick trace、settled 条件不符即停，保存原始事件/信号/截图/文本；不再重跑，不修改期望，不用内部 range 反推。

## 4. 单次授权边界

一次授权只覆盖：一次 settled 定位 + 一次非滚动 sanity + 一次指定 1-tick 自动滚动路径（可含必需的失败诊断记录，但同一路径不重跑）。首失败即停；成功也不代表原交叉矩阵（软/硬、宽/结合标、缩窄/恢复、追加/淘汰/ED3/RIS、Find/滚动条、正确 child 输入）完成，也不关闭 #29/#30/R2。后续集成/回归与新故障计数另行授权，不因本次通过扩大额度。

## 5. 已知不确定/需用户决定

- 公开滚动条单步是否能稳定走到 row0=`TF_H_100`（可能需要页操作；每次移动需记录）。若不能精确定位，按失败出口停止，不换行替代。
- 滚动条 `valueChanged` 在 40ms timer 下是否能作为唯一 tick 观察信号；若平台合并/延迟信号，按失败出口记录“无法判定恰好一次”，不调慢 timer、不直接调用滚动函数。
- 实际 child 行身份/PID、构建产物身份、窗口尺寸、cell 尺寸需在运行时记录；本提案不预填运行观察。
