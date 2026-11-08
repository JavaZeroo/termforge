# 正式 Qt / 固定 ConPTY：DSR 6n 回复的分层证据（#28 回复合同调查切片）

本切片在**正式 C++ / Qt Widgets / 随包固定 Microsoft ConPTY** 构建中，对一个有界、确定的单会话场景核对“核心回复 → Qt 回写 → 真实 child 收到字节”的完整证据链，并分别保留核心孤立合同、ConPTY 原始传输、Qt 产品回写和 child 实际接收四层观察。

**结论先行（限定场景）**：在 80×24 固定主屏、单会话、无 resize/备用屏/滚动区、child 已启用 VT 输入模式的场景下，正式 `SessionModel` 在 transport 的固定源串起点 23（`AB`+`ESC[6n` 的 `AB` 起点；`ESC[6n` 本身从零基字节 25 开始）解析到 child 的查询，按既有 reply 合同生成 `ESC[1;3R` 并经同一有界输入队列回写；真实 child 的独立接收文件恰好收到 `1b5b313b3352`（`ESC[1;3R`），PID 与 SessionModel 绑定，自然退出与 OwnedJob 回收均通过。固定 ConPTY 在本配置下**不会自行代答** DSR：host 静默对照中 child 只观察到自己的超时（exit 42），host 写入的另一个位置值也能原样到达 child。

不修改产品行为、默认入口或 query 支持；不新增 DA1/DA2/OSC 回复；不进入 #28 整体 TUI 验收，也不宣称 WindTerm 对齐或性能。

## 场景与独立预期

- 产品入口：正式 `SessionModel`（`src/qt/terminal_session.hpp`，`ParseMode::recovery`，80×24，随包固定 ConPTY 1.25.260930003），不是旧 candidate shell，也不是 strict replay。
- 固定输入源（child 只写一次，6 B）：`41421b5b366e` = `AB` + `ESC[6n`。child 位于主屏 home，先写两个可打印 ASCII 单元，再查询光标位置。
- 独立字面预期**来自已确认的现有 reply 合同**（`experiments/core/reply_test.cpp`：`CSI 6n` 回复为 `ESC[row;colR`、行列均为 1 基；同一测试以 “ab”+`ESC[6n` 断言 `ESC[1;3R`），不是从被测产品输出反推：home 写 2 个单元后协议光标为 0 基 (0,2)，故预期 `1b5b313b3352` = `ESC[1;3R`。
- child（本测试可执行文件的 `--query-child` 重入入口）打开控制台标准句柄，设置 UTF-8、`ENABLE_VIRTUAL_TERMINAL_INPUT` 与 VT 输出模式，写入固定源后以有界 `ReadConsoleW` 收集实际输入单元并写原始字节文件 + metadata；不调用 `GetConsoleScreenBufferInfo` 等可能触发 conhost 光标同步的 API。

## 四层证据

| 层 | 入口/源码 | 本层实际观察 | 能支持到哪一步 |
|---|---|---|---|
| 1. 核心孤立合同 | 既有 `experiments/core/reply_test.cpp` + CTest `reply_core`（`CMakeLists.txt` 注册） | 本轮复跑通过（0.01 s）；契约断言 “ab”+`ESC[6n` → `ESC[1;3R`，`5n` 负例不回复，DA1 仅 strict 回复 `ESC[?6c`、recovery 静默 | 只有核心 Parser 能生成该字节；不含 Qt/ConPTY/child |
| 2. ConPTY 原始传输（host 静默） | 本切片 `qt_query_reply_test` 的 raw-silent 阶段：`tf_conpty::Session` + 固定后端，观察到 query 后**不写任何回复** | transport = `1b5b31741b5b631b5b3f31303034681b5b3f393030316841421b5b366e`（`ESC[1t`、`ESC[c`、`ESC[?1004h`、`ESC[?9001h` 启动包 + `AB` + `ESC[6n`）；child 接收为空、exit 42（自身 4 s 超时），`cleanup_complete=true`、`active_after=0` | 固定 ConPTY 转发了 child 查询，但**没有自行产生回复**；此时 child 收不到任何 DSR 字节 |
| 3. ConPTY 原始传输（host 写入诊断值） | 同一 raw 阶段，host 在观察到 query 后写入 `1b5b393b3952`（`ESC[9;9R`，仅诊断用） | child 原始接收文件 = `1b5b393b3952`，exit 0；transport 与层 2 相同 | host→ConPTY→child 的写入路径确实送达真实 child，且未被 conhost 改写为实际位置 |
| 4. Qt 产品回写 + child 实际接收 | 正式 `SessionModel`：`poll()` 中 `parser_.feed()` → `take_replies()` → `send(QByteArray)` → 同一有界 `InputQueue` | transport 含启动包 + `AB` + `ESC[6n` 与关闭 `ESC[?1004l/ESC[?9001l`；`query_offset`=23 是固定源串 `AB`+`ESC[6n` 的起点（`AB` 占 23/24，`ESC[6n` 从零基字节 25 开始）；`da1_startup_observed=true`；core 主屏行 0 = `AB`、协议光标 (0,2)；child 原始接收 = `1b5b313b3352`，metadata 自报接收一致；child PID 与 `model.resize_state().child_pid` 一致；`natural_exit=true`、`cleanup_verified=true`、`output_eof=true`、`active_after=0` | 产品路径把合同回复送达真实 child；启动包中的 DA1 请求未产生回写（child 精确等于 DSR 回复，未收到 `ESC[?6c`） |

两次完整运行（实现提交后的干净工作树）字节结果一致，仅 PID 不同：

| run | product child PID | product 接收 hex | raw-silent 接收/exit | raw-distinct 接收/exit | supervisor job_after |
|---|---|---|---|---|---|
| 1 | 16284 | `1b5b313b3352` | 空 / 42 | `1b5b393b3952` / 0 | `{active:0,total:8,terminated:0}` |
| 2 | 13780 | `1b5b313b3352` | 空 / 42 | `1b5b393b3952` / 0 | `{active:0,total:8,terminated:0}` |

`presented_done=null`；本切片不抓图、不比较像素，也不测量呈现时间。

## 与已有证据的关系（为什么需要新切片）

- 核心层已有 `reply_test` / `reply_core`；本切片未复制核心测试，只引用其契约并在产品路径复用同一字面预期。
- 既有 Vim 轨迹（`docs/research/tui-vim-trajectory-2026-11-02.md`）只证明固定 ConPTY transport 中出现过 4 次 `ESC[6n`、核心有回复能力；该文档同时明确“未核对 Vim 实际消费回复后继续的行为”。其第 119 行的括注“ConPTY 由 conhost 代答”与本轮固定配置的动态对照和 pinned 源码都不一致：pinned `outputStream.cpp` 的 `ConhostInternalGetSet::ReturnResponse` 在 VTIO（ConPTY）模式下直接返回，注释写明 “ConPTY should not respond to requests. That's the job of the terminal.”。因此该括注应视为未经核对的假设；本切片保留原记录，不重写历史结论，只以新证据修正该说法。
- 旧 candidate shell 曾用 `shell_query_client.exe` 核对“产品侧回复 child”，但那是旧候选后端组合，不是正式 Qt / 固定 ConPTY；当前 Qt CTest 没有覆盖该链路。本切片改用正式 `SessionModel` 与同一可执行文件的独立 child 模式，补齐 Qt 回写与真实接收。

pinned 源码旁证（静态解释，不替代动态证据）：child 的 VT 输出由 `WriteCharsVT` 先更新内部 state machine、再把输入文本转发给 host（`src/host/_stream.cpp`），所以 host 能看到 `ESC[6n`；host 写入的 `CSI R` 在 `InputStateMachineEngine` 中仅在 `IsVtInputEnabled()` 时作为未处理序列透传给 child（`ActionCsiDispatch` 的 `CSI_F3` 分支），与本次 child 启用 VT 输入模式的观察一致。

## 环境与身份

- Windows 11 x64 build 26200；MSVC 19.42.34433.0；Qt 6.10.3 win64_msvc2022_64 offscreen；随包固定 ConPTY 1.25.260930003（`conpty.dll` `feeef341…805a`、`OpenConsole.exe` `3d66b23d…0586`）；app-local CRT 14.51.36247.0；80×24。
- 后端身份：`reviewed-side-by-side-1.25.260930003`，creation flags = 24（`PSEUDOCONSOLE_GLYPH_WIDTH_CONSOLE` 0x18）；产品配置身份 `cpp-core / qt-widgets / reviewed-side-by-side-1.25.260930003 / 80x24`。
- 实现提交：`4806812`（测试 + CMake 注册 + 本次结果记录前置的实现提交；验证文件为后续文档提交）。完整机器可读身份、命令和两次运行原始记录见 [validation.json](validation.json)。

## 局限（保持开放）

- 只覆盖**一个** 80×24 固定主屏、单会话、home 起点的确定位置；不含 resize、备用屏、滚动区、宽字符/结合字、历史、选择、Find。
- 只覆盖已实现的 `DSR 6n`；DA1 产品路径仍 strict-only/recovery-silent，DA2、OSC 10/11、括号粘贴、鼠标/焦点模式等未实现或未验证，本切片不改变这些结论。
- child 已启用 `ENABLE_VIRTUAL_TERMINAL_INPUT`。这是 pinned ConPTY 把未处理 CSI 透传给 child 的条件；未启用该模式的 child 在本轮未测试，不能据此推断其接收字节（例如是否被解释为按键事件）。
- 未和 WindTerm 原版对照，未验证真实 TUI（Vim/More 等）消费回复后的继续行为；#28 的整体 TUI 矩阵、#27/#29/#30 联合验收仍开放。
- 单次运行约 4.5 s，不是性能或呈现测量；`presented_done=null`。

## 复验

```powershell
python -m tools.prepare_qt_build   # 或复用已准备且身份匹配的 local/qt-build-deps
cmake -S . -B build/query-reply -G "Visual Studio 17 2022" -A x64 `
  -DTERMFORGE_BUILD_QT_SHELL=ON -DBUILD_TESTING=ON `
  -DTERMFORGE_BUILD_CANDIDATE_ADAPTER=OFF `
  -DQt6_DIR="$PWD/local/qt-build-deps/qt/lib/cmake/Qt6" `
  -DTERMFORGE_CONPTY_BACKEND_DIR="$PWD/local/qt-build-deps/conpty" `
  -DTERMFORGE_MSVC_RUNTIME_DIR="$PWD/local/qt-build-deps/crt"
cmake --build build/query-reply --config Release --target qt_query_reply_test reply_test private_mode_test --parallel 2
ctest --test-dir build/query-reply -C Release -R '^qt_query_reply$' --no-tests=error --output-on-failure
ctest --test-dir build/query-reply -C Release -R '^(reply_core|private_mode_core)$' --output-on-failure
```
