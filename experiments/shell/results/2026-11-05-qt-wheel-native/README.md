# 正式 Qt：恢复 Windows 原生滚轮按页设置（#29 收尾）

#318 证明 Qt 6.10 Windows 主题把原生 `WHEEL_PAGESCROLL` 扁平化为 3 行，因此仅靠 `QStyleHints` 无法恢复用户设置。本切片在**正式 Windows Qt 滚轮入口**直接复用 R1 的 `SystemParametersInfoW(SPI_GETWHEELSCROLLLINES)` 查询与规则，恢复 0 禁用、普通正整数行数与 `WHEEL_PAGESCROLL` 页步长，并让后续系统设置变化在下一滚轮事件被观察到。其余平台边界保持独立；历史阅读锚点、半 detent 和会话隔离回归不变。

## Windows 滚轮合同

- 每个真实 `QWheelEvent` 的整 detent 到来时重新查询 `SPI_GETWHEELSCROLLLINES`，不缓存，因此用户后续修改系统滚轮设置会被下一次滚轮事件观察到。
- 查询成功：
  - `0`：禁用滚轮历史滚动；
  - 普通正整数：按该行数滚动；
  - `WHEEL_PAGESCROLL`（`DWORD(-1)`）：页步长 = **当前可见高度 − 1，至少 1 行**（沿用 R1 合同）。
- 查询失败：回退 **3 行**。依据是 R1 在查询前把 `lines` 初始化为 3，失败时保持该值；不会把失败猜成页模式。
- 其他平台：保持 YAGNI/边界清晰，只使用 `QStyleHints` 的正整数行数；0 禁用；不在未证实平台实现页标记。

## 测试与证据

`qt_history_navigation` 的历史/锚点回归继续使用真实 Qt 事件 + 受控 child；新增滚轮合同用例通过可恢复的应用内注入 `WheelSettingForTest` 进入**真实滚轮入口**，不修改宿主全局设置：

- 原生路径：用只读 `SPI_GETWHEELSCROLLLINES` 计算当前设置并核对实际滚轮步长（0/普通行数/按页都按真实值处理）；
- 注入 0：不滚动；
- 注入普通 3 + 半 detent：`60+60` 才滚一次且正好 3 行；
- 注入 `WHEEL_PAGESCROLL`：步长 = 高度−1，至少 1；缩到 20 行后下一事件使用 19；
- 注入查询失败：回退 3 行，不是页模式；
- 设置更新：先 2 行、再 5 行，下一次滚轮使用新值；
- 原有滚轮浏览、resize/输出锚点、淘汰/ED3/RIS、普通输入回底、两会话隔离、child 退出与 Job 回收继续通过。

同场景控件外观不变（浏览页仍由同一 `HistoryViewport` 投影），沿用[历史浏览结果的底部/滚动/缩窄/新输出截图](../2026-11-05-qt-history-anchor/screens/history-bottom.png)及其[证据说明](../2026-11-05-qt-history-anchor/README.md)；本切片只改变滚轮步长来源，没有新的视觉布局。

## 验证

- `ctest --test-dir build/qt-cpp -C Release -R '^qt_history_navigation$'`：1/1 通过（约 2.0 s）。
- Qt 正式路径回归 8/8 通过（14.33 s）。
- 源码/二进制身份与构建环境见 [validation.json](validation.json)。

复现：

```powershell
cmake --build build/qt-cpp --config Release --target qt_history_navigation_test --parallel 3
ctest --test-dir build/qt-cpp -C Release -R '^qt_history_navigation$' --output-on-failure
```

## 边界

- 仅 Windows 11 x64 offscreen Qt 6.10.3 / 固定 ConPTY；不修改宿主滚轮设置，测试用可恢复注入。
- 页步长是“可见高度−1/至少1”的 R1 产品合同；不证明 WindTerm 原版滚轮/翻页数值等价。
- 不覆盖滚动条拖动、PageUp/PageDown 历史、粘贴回底、阅读中选择/复制/Find、物理呈现/DPI/Unicode 历史/性能。
- #317 的 native-grid 与保留源寻址取舍仍是单独行为方案待办，需具体方案与用户确认；旧六阶段 `--resize` exit 1 保留。
- #27/#29/#30/#7 仍 OPEN；不自行合并、不触发 Codex review。
