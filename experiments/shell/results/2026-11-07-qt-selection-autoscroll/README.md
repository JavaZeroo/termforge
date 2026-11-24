# 正式 Qt：跨历史拖选的有界自动滚动

本切片把既有 Win32 R1-03“稳定行列选区、拖出可见区有界自动滚动”合同迁移到正式 Qt 正常主屏，复用现有 Screen owner/行身份/rank/epoch、`HistoryViewport`、`ViewMapping` 与 `TextSelection`；不新增历史存储或选择模型。

## 策略（Win32 → Qt 映射）

- Win32 `selection_autoscroll`：仅在 selecting、未关闭、仍持有 capture 时，按指针是否在可见 extent 上/下决定方向；每个 timer tick `viewport.scroll_by(screen, ±1)`，滚动成功后再 `selection_move`，因此每 tick 最多一行。
- Qt：公开鼠标左键按下开始选区；拖出可见网格上/下边缘启动 Qt 定时器（40ms，项目策略；无性能验收含义），每 tick `HistoryViewport::scroll_by(±1)` 并把选区端点重扩展到边缘源位置；拖回/松开/失焦/隐藏/UngrabMouse/无关 resize/关闭会话停止。
- 滚动条/阅读锚点、painted 高亮、Copy 全部使用同一 `TextSelection` 源范围；无第二历史或 resize 模型。默认滚动条启用。

## 验证

`qt_autoscroll`（真实固定 ConPTY child、两会话、默认滚动条、注入 writer）：

- 默认 40ms 从底部按住拖到上方：每 tick 一行；以独立字面 oracle 比较完整 Copy 文本（非 contains）；高亮 span 源行/列逐项一致；反向跨页选择。
- 正向拖到下方：`top` 增加量 == 新增 tick 数，同字面 oracle 比较 Copy/高亮。
- 拖回、活跃边缘直接松开：定时器停止；观察窗内 top/选区不再变化。
- 活跃边缘失焦、关闭目标会话：定时器停止、top 稳定、无后台选区变化。
- 两 child 自然退出 37、自有 Job 清理通过；复制使用注入 writer，非真实 CF。

## 未执行 / 阻塞

- soft/hard 混合及宽字/结合标的跨页自动滚动源列；协调缩窄/恢复；持续追加/淘汰/ED3/RIS 与活跃 autoscroll 的联合行为；活跃期间 Find/滚动条交叉。当前 12/12 不替代这些。

## 证据边界

- 离屏 Qt；物理呈现/DPI 未验证；不包含选词/选行、矩形选择、备用屏/TUI、配置持久化或新性能阈值。
- 本切片不关闭 #29/#30/R2；#324 仍等待用户决定；BCD/v2、旧失败与未跟踪文件不变。
