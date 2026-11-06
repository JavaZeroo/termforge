# 正式 Qt：有界字面量 Find（正常主屏 + 历史显示页）

本切片把既有 `TextSearch` 的有界字面量搜索接入正式 Qt：`Ctrl+Shift+F` 打开当前会话 Find 面板，复用 4096 UTF-8 字节查询上限、1024 结果上限、字面量/无规范化/ASCII case folding、循环/截断/持续扫描策略；命中保存源 `TextPosition`，通过 `HistoryViewport` 与当前 `ViewMapping` 导航和高亮，不保存显示行号。

## 实现

- `TerminalWidget`：Find 面板含查询框、Previous/Next/Close、Match case 和状态行；`Ctrl+Shift+F` 打开并聚焦，Escape 关闭并恢复终端焦点。
- 搜索按 `TextSearch::step` 的 4096 单位预算分片，计时器继续；空/超限/无命中/截断/失效状态来自既有合同。
- 输出变化调用 `TextSearch::notify_change`，重扫/失效；当前命中在 `TextDocument` 上以源位置 live-validate，再经当前显示页 `ViewMapping` 投影；`reveal` 定位源行，历史/当前页均可用。
- 查询只在 Find 面板内处理，不作为终端输入；复制仍走已合入的源选择/系统 Copy。

## 验证

- `qt_find` 1/1，Qt 正式路径 10/10；two real selection children with independent input recording：multiple `SEL_N_` matches 的顺序/index、双向 wrap、精确源文本/列与显示宽度、历史 reveal、缩窄/扩大同源迁移、淘汰不复活、RIS 旧 epoch 失效；两个会话查询隔离，查询零泄漏到 child，Escape 后 `Z` 恰好一次只到正确 child；空查询、实际 Match case、4097-byte/中文/NFD 超限拒绝、Return/F3/Shift+F3。未触个人剪贴板/桌面。
- **1024 截断/有限循环已通过真实入口**：第 3 个真实 child 输出 52 行 `X...ZZ`（52 命中，>4096 字节扫描）+ 26 行 × 40 个 `AB`（1040 命中）；正式查询框输入 `ZZ` 在 25ms 测试节拍下保留 Searching/部分命中，公开 Next 不越出已扫描前缀；输入 `AB` 达到 1024 capped、提示截断，首尾 Previous/Next wrap 有限（1023↔0）。未改产品上限/预算/注入文档。
- 截图：[find-panel.png](find-panel.png)（同一真实 Qt 测试窗口，历史命中态；离屏，不代表物理 DPI/呈现）。
- 当前 head CI 与源码/构建身份见 [validation.json](validation.json)；本切片不代表 #29/#30/R2 完成。

## 边界

- 固定样例/ASCII 字面量与离屏 Qt；中文/NFD 只声明字面标量匹配，不宣称字素/glyph/宽度等价。
- 不覆盖选词/选行、滚动条/翻页键历史、备用屏/TUI Find、物理呈现/DPI、完整 Unicode 字素或性能阈值；不新增数值性能验收。
- 1024 截断现在由真实 child/查询入口覆盖；固定 child 仍受 100 行历史 + 24 live 行容量约束，本次负载 78 行（52 ZZ + 26 AB）在容量内。
- 旧 B/CD、192.168.3.210 校准/v2、A/B/C、旧六阶段失败、#27/#29/#30/#35/#7 状态不变。


## 原派单缺口 → 证据

| 原要求 | 本次证据 |
|---|---|
| 多命中准确顺序/循环/当前页与历史页 | 多个 `SEL_N_` 顺序/index、双向 wrap、历史 reveal、精确源文本/列和显示宽度 |
| 缩窄扩大同源迁移、写入/淘汰失效、旧 epoch | resize 前后源 range 不变；淘汰后无复活；RIS 后旧位置拒绝、新 epoch 可查 |
| 双会话隔离、Escape 后输入只到正确 child | 两个真实 child + 独立输入文件；查询零泄漏，Escape 后 `Z` 恰好一次到目标 |
| 空查询、大小写、1024 截断/有限循环、4096 字节边界 | 空查询/Match case/Return/F3/Shift+F3；真实键盘 4096 合法/4097 拒绝，中文/NFD 经 Qt IME commit 合法与超限；真实 child 1040 命中达到 1024 capped，首尾有限 wrap；>4096 扫描保留 Searching/部分命中且不越界 |
| 公开入口而非测试 setter 替代 | 首次查询、空查询、大小写、ASCII 键盘超限、中文/NFD IME commit、Return/F3 均经真实查询框；setter 仅用于失效/前置观测 |

## 身份

源码/测试/构建身份见 [validation.json](validation.json)；截图为本机同一 Qt 测试窗口的离屏历史命中态。
