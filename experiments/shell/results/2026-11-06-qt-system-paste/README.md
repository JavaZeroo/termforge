# 正式 Qt：有界系统粘贴入口

本切片把已冻结的 R1 粘贴合同接入正式 Qt 正常主屏：`Ctrl+Shift+V`、`Shift+Insert` 与实际终端 Paste 上下文菜单；默认 Windows 读取器读取 `CF_UNICODETEXT`，通过现有有界 ConPTY 输入队列发送给发起会话。复用现有 UTF-16/UTF-8 容量、CR 归一化、多行确认、目标绑定和错误反馈策略；不新增 bracketed paste、备用屏/TUI 粘贴、自动逐行、Find 或字素规则。

## 实现

- `ClipboardReader`/`SystemClipboardReader`：Windows `OpenClipboard` 短重试、1 Mi UTF-16 上限、缺失终止符/空/失败分开；读取在线程外，结果回投 Qt。
- `TerminalWidget`：三个公开入口；异步读取→`prepare_paste`（既有 CR 归一化/容量/多行确认）→分块 1024 B 发送；输入队列拒绝时保留同一 chunk 重试，不确认、不重复；绑定发起会话 child PID，目标关闭/变化即失败；第二入口在读取/传输中报 busy；空/失败/取消/超限/无有效目标有不同 notice。
- 多行使用真实 `QMessageBox` Paste/Cancel；确认后恢复窗口/终端焦点。`Ctrl+C`、普通 `Insert`、复制与 IME 行为不变；既有 `qt_unicode_input` 的“未实现粘贴快捷键”断言更新为“已实现入口且不落成 Insert 字节”，并禁用 reader 避免本机 CF。

## 入口与结果矩阵

| 层 | 入口 | 结果 |
|---|---|---|
| 本地 fake reader + 真实 child 输入记录 | Ctrl+Shift+V 单行、Shift+Insert 多行取消、真实对话框确认（role 选择点击 Paste，无 auto-confirm 捷径）、实际 Paste 菜单中文/NFD、busy、目标关闭、双会话切换、读取中关闭、新会话隔离 | `qt_paste` 1/1；Qt 正式路径 14/14；fake reader，未触个人 CF |
| hosted Windows QPA 默认 reader + 真实控件 HWND | Ctrl+Shift+V、实际 Paste 菜单、Shift+Insert；真实 CF；自有 holder 占用失败/释放后第二次动作 | run `42183487961` / job `127778260596`：`keyboard_cf/menu_cf/shift_insert_cf/occupied_failure/release_success=true`，child 字节核对；`qpa=windows`、默认 owner |
| 同上 | 多行取消/确认 | **hosted 历史失败，未闭合**；恢复尝试 3 已按最小修复方案授权（role 选择 + 双向插桩 + opt-in 单次 workflow，见[尝试账本](attempt-ledger.md)），结果运行后回填；本地 fake reader 覆盖取消零增量/确认一次（真实对话框，无 auto-confirm 捷径），不能替代 hosted |

本地 `qt_paste` 的 child 把收到的每个字节写入文件，不执行粘贴文本；每步以动作前文件内容为基线，按精确字节增量核对单行一次、多行取消零增量、确认一次。hosted 场景的独立 child 同样只记录固定合成输入，不执行粘贴内容。

## RED/GREEN 与限制

- 开发中先发现既有 `qt_unicode_input` 仍断言粘贴快捷键“未实现”，更新该测试并禁用其 reader，使其不会触碰个人 CF；这是测试合同随入口实现更新，不是产品回归。
- 多行 hosted 失败的根因已离线证明并修复：产品把 `QMessageBox::exec()`（返回被点击标准按钮值，实测 accept=2/escape=3）错当 `QDialog::DialogCode` 比较，确认恒被判为拒绝；修复按 `clickedButton()` 的 role 判定。本地 `qt_paste` 以真实对话框（role 选择点击、无 auto-confirm）验证取消零字节/确认 CR 归一化一次，Qt 正式路径 14/14。hosted 真实 CF 组合待唯一一次授权 run 闭合（未回填前不算通过）。
- 本地 Qt 正式路径 14/14；hosted 真实 CF 三入口与占用/释放通过。第二会话切换/关闭复用只在本地 fake reader 场景覆盖关闭目标，未在 hosted 组合验证。
- 多行 hosted 历史失败保留不删除、不称 flaky；恢复尝试 3 按最小修复方案执行中，结果见[尝试账本](attempt-ledger.md)。
- 不覆盖 bracketed paste、备用屏/TUI 粘贴、自动逐行、Find、完整文字/选择、物理呈现/DPI/性能。系统 CF 只在该一次性 hosted Windows QPA 运行内验证；本地/offscreen 缓存不是 CF 证据。

## 身份

源码/构建/二进制/运行身份见 [validation.json](validation.json)；hosted run/job 为 `42183487961`/`127778260596`。旧 B/CD、192.168.3.210 校准/v2、A/B/C、旧六阶段失败、#27/#29/#30/#35/#7 状态不变。
