# 正式 Qt：有界字面量 Find（正常主屏 + 历史显示页）

本切片把既有 `TextSearch` 的有界字面量搜索接入正式 Qt：`Ctrl+Shift+F` 打开当前会话 Find 面板，复用 4096 UTF-8 字节查询上限、1024 结果上限、字面量/无规范化/ASCII case folding、循环/截断/持续扫描策略；命中保存源 `TextPosition`，通过 `HistoryViewport` 与当前 `ViewMapping` 导航和高亮，不保存显示行号。

## 实现

- `TerminalWidget`：Find 面板含查询框、Previous/Next/Close、Match case 和状态行；`Ctrl+Shift+F` 打开并聚焦，Escape 关闭并恢复终端焦点。
- 搜索按 `TextSearch::step` 的 4096 单位预算分片，计时器继续；空/超限/无命中/截断/失效状态来自既有合同。
- 输出变化调用 `TextSearch::notify_change`，重扫/失效；当前命中在 `TextDocument` 上以源位置 live-validate，再经当前显示页 `ViewMapping` 投影；`reveal` 定位源行，历史/当前页均可用。
- 查询只在 Find 面板内处理，不作为终端输入；复制仍走已合入的源选择/系统 Copy。

## 验证

- `qt_find` 1/1，Qt 正式路径 10/10；确定性 selection child：`Ctrl+Shift+F` 实际按键、查询焦点、`SEL_SOFT_` 源命中、历史 reveal、高亮投影、Previous/Next、无命中、Escape 焦点恢复、4096 查询上限；未触个人剪贴板/桌面。
- 截图：[find-panel.png](find-panel.png)（同一真实 Qt 测试窗口，历史命中态；离屏，不代表物理 DPI/呈现）。
- 当前 head CI 与源码/构建身份见 [validation.json](validation.json)；本切片不代表 #29/#30/R2 完成。

## 边界

- 固定样例/ASCII 字面量与离屏 Qt；中文/NFD 只声明字面标量匹配，不宣称字素/glyph/宽度等价。
- 不覆盖选词/选行、滚动条/翻页键历史、备用屏/TUI Find、物理呈现/DPI、完整 Unicode 字素或性能阈值；不新增数值性能验收。
- 旧 B/CD、192.168.3.210 校准/v2、A/B/C、旧六阶段失败、#27/#29/#30/#35/#7 状态不变。
