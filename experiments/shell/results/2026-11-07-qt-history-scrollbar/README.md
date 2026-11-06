# 正式 Qt：历史滚动条与源阅读锚点

本切片把既有 Win32 滚动条历史浏览策略迁移到正式 Qt 正常主屏：真实 `QScrollBar` 的拖动/页区/按键最终通过 `HistoryViewport` 的源 rank/行身份锚点定位；滚动条值只是当前显示页 top，不是稳定源 ID。已有滚轮、`Ctrl+Shift+Home/End`、Find reveal、拖选/Copy 与终端输入合同保持不变。

## 实现

- 主屏右侧垂直滚动条；出现时其宽度进入现有 Qt→core→ConPTY 网格宽度路径，未出现时不占宽；无第二 resize/存储模型。
- 滚动条 `valueChanged` 映射到 `HistoryViewport::scroll_to/bottom`；刷新时从 `reconcile` 的 top/max 同步回滚动条（阻塞信号，防止值反向当身份）。
- 新输出保持暂停阅读的源锚点与页面；回到底部 resume following；淘汰/ED3/RIS 按已有锚点钳制规则；缩窄/恢复后同源锚点；Find reveal 后滚动条同步。
- 固定布局的旧测试进程显式关闭滚动条占宽，保留其既有 80×24 夹具；滚动条启用路径由 `qt_history_navigation` 覆盖。

## 验证

`qt_history_navigation`（真实 child 确定性 160 行日志 + 自发追加）新增：

- 滚动条可见/range 与 `maximum_top` 一致；出现时网格列数按滚动条 extent 计算。
- 实际鼠标拖动、页区点击、滚动条 Home/End；值==HistoryViewport top，源锚点==显示页首源行 id，不把值当源 ID。
- 新输出时锚点/页面不跳、滚动条值跟随；回底 following；缩窄 60 列/恢复后同源锚点；Find reveal 同步；双会话滚动/焦点隔离。
- 本地 Qt 正式路径 10/10；截图 `screens/history-scrollbar.png`。

## 边界

- 离屏 Qt；不宣称物理呈现/DPI、备用屏/TUI、自动滚动/选词选行、配置持久化或性能阈值。
- 未覆盖 child 实际窗口尺寸对滚动条宽度的独立 ACK（现有 resize smoke 覆盖产品网格/transport；滚动条+child 尺寸组合仍为边界）。
- #324 仍等待用户追加验证决定；BCD/VM/v2、旧失败与未跟踪脚本不变。
