# 正式 Qt：有界系统复制入口

本切片把已冻结的 R1 文字复制合同接入正式 Qt 入口：终端焦点下 `Ctrl+Shift+C` 与实际终端 Copy 上下文菜单，复用 `#322` 的有界 `copy_selection` / `TextSelection` 源映射，将软续行合并、硬行 CRLF、中文与有界文本转换后写入系统 `CF_UNICODETEXT`。`Ctrl+C` 保持终端中断语义，不新增剪切/粘贴/Find/其他快捷键。

## 实现

- `TerminalWidget`：新增 Copy 上下文菜单 action 与 Ctrl+Shift+C；`copy_to_clipboard()` 区分空选区、失效选区、UTF-8/UTF-16 容量超限和系统失败；失败保留选区并显示诊断，不伪装成功。
- `SystemClipboardWriter`：Windows `OpenClipboard` 短重试、UTF-16 上限 1 Mi 单元、内嵌 NUL 拒绝、`CF_UNICODETEXT` 写入；非 Windows 仅作 Qt clipboard fallback。
- 本地隔离测试：fake writer 验证键盘/实际菜单入口、空选区不调用 writer、淘汰失效、失败不清选区；不接触个人 CF。
- hosted-only 测试：独立 Win32 CF 读回，计划覆盖精确 Unicode/NFD/尾空格/软硬行、空选区 sentinel、自持有 owner 占用失败与第二次动作成功、淘汰 sentinel、child 清理；仅当 `GITHUB_ACTIONS=true` 且 `RUNNER_ENVIRONMENT=github-hosted` 运行。

## 当前结果（系统 CF 已在 hosted CI 验证）

- 本机 `qt_selection_migration` 通过（fake 入口 1/1），Qt 正式路径 9/9；本地未调用系统 CF。
- hosted Windows CI 的 `qt_clipboard_system` 通过：`keyboard_cf=true`、`menu_cf=true`、`empty_sentinel=true`、`occupied_failure=true`、`release_success=true`、`eviction_invalid=true`、`cf_verified=true`。验证使用独立 Win32 CF 读回，入口为真实 Ctrl+Shift+C 与实际 Copy 菜单 action；自有 holder 进程真实占用后失败、释放后第二次明确动作成功。run `42143570635` / job `127649515327`。
- 首次 hosted 尝试因 Qt offscreen `QWidget::winId()` 不是 Win32 HWND 而返回 `OpenClipboard:1400`；改为测试内真实隐藏 owner 窗口传给同一 `SystemClipboardWriter` 后通过。这是测试环境绑定修正，不是产品复制失败。
- 当前 head CI 结果与身份见交付报告；本地无 CF 写入。

## 身份

源码/构建/产物身份在 [validation.json](validation.json)；本地测试 fixture 使用同一真实 child / 固定 ConPTY / offscreen Qt 构建。hosted 测试未产生 CF 证据，不能把 offscreen 缓存当 CF。

## 边界

- 系统 CF 只在一次性 hosted Windows CI 的受保护入口验证；当前该环境预检失败，系统 CF 未验收。
- 不覆盖宽字/结合字复制列、系统剪贴板占用后的完整产品重试、粘贴、Find、选词选行、自动滚动、物理呈现/DPI/性能。
- 旧 B/CD 寻址、192.168.3.210 校准/v2、A/B/C、旧六阶段失败、#27/#29/#30/#7 状态不变。
