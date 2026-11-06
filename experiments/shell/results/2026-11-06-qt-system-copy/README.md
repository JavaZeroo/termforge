# 正式 Qt：有界系统复制入口（分层 CF 证据）

本切片把已冻结的 R1 文字复制合同接入正式 Qt 入口：终端焦点下 `Ctrl+Shift+C` 与实际终端 Copy 上下文菜单，复用 `#322` 的有界 `copy_selection` / `TextSelection` 源映射，将软续行合并、硬行 CRLF、中文与有界文本转换后写入系统 `CF_UNICODETEXT`；`Ctrl+C` 保持终端中断语义，不新增剪切/粘贴/Find/其他快捷键。

## 实现

- `TerminalWidget`：新增 Copy 上下文菜单 action 与 Ctrl+Shift+C；`copy_to_clipboard()` 区分空选区、失效选区、UTF-8/UTF-16 容量超限和系统失败；失败保留选区并显示诊断，不伪装成功。
- `SystemClipboardWriter`：Windows `OpenClipboard` 短重试、UTF-16 上限 1 Mi 单元、内嵌 NUL 拒绝、`CF_UNICODETEXT` 写入；非 Windows 仅作 Qt clipboard fallback。
- 本地隔离测试：fake writer 验证键盘/实际菜单入口、空选区不调用 writer、淘汰失效、失败不清选区；不接触个人 CF。
- hosted-only 测试：独立 Win32 CF 读回，覆盖精确 Unicode/NFD/尾空格/软硬行、空选区 sentinel、自持有 owner 占用失败与第二次动作成功、淘汰 sentinel、child 清理；仅当 `GITHUB_ACTIONS=true` 且 `RUNNER_ENVIRONMENT=github-hosted` 运行。

## 三层结果（互不替代）

| 层 | QPA / owner | 结果 | 能证明什么 |
|---|---|---|---|
| 本地 fake writer | offscreen / fake | `qt_selection_migration` 1/1、Qt 正式路径 9/9 | 产品键盘/实际菜单/空/失效/失败状态路径；**不触个人 CF** |
| hosted offscreen CF | offscreen / 注入隐藏 Win32 owner | run `42160062253` / job `127703347210`：`cf_verified=true`，`qpa=offscreen`，`offscreen_only=true`，`clipboard_owner=injected-hidden-win32-window`，`product_default_owner_verified=false` | 独立 CF 读回的真实成功；**不验证正式控件默认 native owner** |
| hosted Windows QPA 正式路径 | Windows QPA / 默认产品 writer / 控件真实 native HWND | run `42160062253` / job `127703347210`：`cf_verified=true`，`qpa=windows`，`offscreen_only=false`，`clipboard_owner=product-widget-native-hwnd`，`product_default_owner_verified=true` | 正式 Qt Windows 控件、默认产品 writer、公开 Ctrl+Shift+C 与实际 Copy 菜单 → 系统 CF 的独立字节读回 |

两个 hosted 层的 CF 检查均为：`keyboard_cf=true`、`menu_cf=true`、`empty_sentinel=true`、`occupied_failure=true`、`release_success=true`、`eviction_invalid=true`、Job `active=0`。目标文本为固定 `COPY_UNICODE:中文 é  \r\n`（中文、NFD、尾空格、硬 CRLF）。

**首次失败保留为历史**：hosted offscreen 首次尝试返回 `OpenClipboard:1400`，因为 offscreen `QWidget::winId()` 不是 Win32 HWND；测试注入隐藏 Win32 owner 后通过。这是测试绑定修正，不是产品复制失败；Windows QPA 层随后证明真实控件 HWND/默认 writer 也成功。

## 身份

- 代码证据提交：`2f6dac2`（源文件逐文件哈希见 [validation.json](validation.json)）；本地 Qt 9/9，hosted run/job 见上表。
- validation 同时记录 QPA/owner/run/job、源码/构建身份；offscreen 与 Windows QPA 两层不合并成“所有 Qt 配置”。文档跟进提交单独记录，不回写旧 run。
- 系统 CF 只在一次性 hosted Windows CI 的受保护入口验证；本地 fake 与 offscreen 缓存都不当 CF 证据。

## 边界

- Windows QPA 层验证的是**系统 CF 字节与两个公开入口**，不是物理呈现、DPI、字形/宽字/结合字复制列或完整复制 UI。
- 不覆盖系统剪贴板占用后的完整产品 UX/重试策略、粘贴、Find、选词选行、自动滚动、其他尺寸/性能；不新增剪切/粘贴/其他快捷键。
- 旧 B/CD 寻址、192.168.3.210 校准/v2、A/B/C、旧六阶段失败、#27/#29/#30/#7 状态不变。
