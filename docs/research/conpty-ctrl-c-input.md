# ConPTY 的 Ctrl+C 输入编码与持续输出限制

## 范围与来源固定

本文使用 Microsoft 第一方源码、Microsoft Learn、`microsoft/terminal` 官方 issue、固定 WindTerm 2.7.0 发行物的定向静态分析，以及本机自有 ConPTY 探针的无窗口 CDB/ETW 追踪，供 issue #36 判断输入编码与可实现边界。未启动参考应用或进行桌面/截图实验；调试器对时序的扰动单独保留，不作为性能或无插桩行为证据。

Windows Terminal 安装版本 `1.24.12741.0` 对应 annotated tag [`v1.24.12741.0`](https://github.com/microsoft/terminal/releases/tag/v1.24.12741.0)。tag 对象 [`1f5cce251bcbe2365c54040a7772910b2b8623d8`](https://api.github.com/repos/microsoft/terminal/git/tags/1f5cce251bcbe2365c54040a7772910b2b8623d8) 指向提交 **`7ff11ee107c984e236960ec948b450ccbc10187b`**；下文源码链接全部固定到该提交。Microsoft Learn 页面与 issue 状态查阅于 2026-10-24，Learn 内容不是不可变版本，故同时给出具体段落或原文短语。

**结论先行：** ETX（`0x03`）是 Windows Terminal 普通 VT 输入路径及 ConPTY conhost 输入解析器明确采用的 Ctrl+C 编码，应继续作为正常 Ctrl+C 输入。它只在客户端控制台输入模式允许时进入控制事件处理，并不保证事件立即分派。固定源码还揭示了 conhost 在全局控制台锁最外层解锁时才分派控制事件的设计，持续输出下存在有依据的延迟机制；但仅凭静态源码不能把当前 build 26200 探针的唯一根因定死，也没有证据支持改用另一编码、win32-input-mode 或外部 `GenerateConsoleCtrlEvent` 就能修复。

## 事实：ETX 是正确输入，但“编码正确”不等于“信号已交付”

### 1. Windows Terminal 的普通路径将 Ctrl+C 编成控制字符

- Microsoft Learn 的 [Console Virtual Terminal Sequences](https://learn.microsoft.com/en-us/windows/console/console-virtual-terminal-sequences#modifiers) 在 **Input Sequences → Modifiers** 段明确写道：Ctrl 通常原样传递系统给出的、移入 `0x00`–`0x1f` 控制字符区的单字符；例如 Ctrl+`[` 成为 ESC。该表只单列少数例外，Ctrl+C 不在例外中。
- 固定版本的 [`TerminalInput::_makeCtrlChar`](https://github.com/microsoft/terminal/blob/7ff11ee107c984e236960ec948b450ccbc10187b/src/terminal/input/terminalInput.cpp#L588-L612) 对 `'@'`–`'~'` 执行 `ch & 0b11111`；因此 `C` 映射为 `0x03`。调用方在 [`TerminalInput::HandleKey`](https://github.com/microsoft/terminal/blob/7ff11ee107c984e236960ec948b450ccbc10187b/src/terminal/input/terminalInput.cpp#L233-L314) 中使用此转换。
- 按键没有在 [`TermControl::_KeyHandler`](https://github.com/microsoft/terminal/blob/7ff11ee107c984e236960ec948b450ccbc10187b/src/cascadia/TerminalControl/TermControl.cpp#L1843-L1877) 中被快捷键或活动 IME 组合截获时，进入通用发送路径；[`ControlCore::TrySendKeyEvent`](https://github.com/microsoft/terminal/blob/7ff11ee107c984e236960ec948b450ccbc10187b/src/cascadia/TerminalControl/ControlCore.cpp#L660-L714) 调用终端输入转换，再由 [`ControlCore::SendInput`](https://github.com/microsoft/terminal/blob/7ff11ee107c984e236960ec948b450ccbc10187b/src/cascadia/TerminalControl/ControlCore.cpp#L483-L515) 写连接。最终 [`ConptyConnection::WriteInput`](https://github.com/microsoft/terminal/blob/7ff11ee107c984e236960ec948b450ccbc10187b/src/cascadia/TerminalConnection/ConptyConnection.cpp#L552-L599) 做 UTF-16→UTF-8 转换并 `WriteFile` 到 ConPTY 管道。
- 对通常可映射为字符的 Ctrl+C，固定源码还有字符事件路径：[`Terminal::SendKeyEvent`](https://github.com/microsoft/terminal/blob/7ff11ee107c984e236960ec948b450ccbc10187b/src/cascadia/TerminalCore/Terminal.cpp#L668-L692) 可委托字符处理，[`Terminal::SendCharEvent`](https://github.com/microsoft/terminal/blob/7ff11ee107c984e236960ec948b450ccbc10187b/src/cascadia/TerminalCore/Terminal.cpp#L719-L785) 仍进入同一个 `TerminalInput::HandleKey`。因此不能把某一条 UI 事件分支孤立当作所有键盘布局的唯一路径；共同终点仍是上述控制字符编码。

### 2. ConPTY 服务端明确把 ETX 当作特殊 Ctrl+C

[`InputStateMachineEngine::_DoControlCharacter`](https://github.com/microsoft/terminal/blob/7ff11ee107c984e236960ec948b450ccbc10187b/src/terminal/parser/InputStateMachineEngine.cpp#L141-L157) 对不带 Alt 的 `UNICODE_ETX` 合成 Ctrl+C 的 key-down/key-up，并两次调用 `WriteCtrlKey`；注释原文是 “This is Ctrl+C, which is handled specially by the host.” 这直接确认单个 `0x03` 是受支持的 ConPTY 输入，而非普通文本字节或错误用法。

[`InteractDispatch::WriteCtrlKey`](https://github.com/microsoft/terminal/blob/7ff11ee107c984e236960ec948b450ccbc10187b/src/terminal/adapter/InteractDispatch.cpp#L44-L54) 随后调用 `HandleGenericKeyEvent`，让宿主决定是否发送中断。与之对照，同文件 [第 30–42 行](https://github.com/microsoft/terminal/blob/7ff11ee107c984e236960ec948b450ccbc10187b/src/terminal/adapter/InteractDispatch.cpp#L30-L42) 明确说普通 `WriteInput` 的 Ctrl+C 只进入输入缓冲区、不触发中断。ETX 所走的恰是前者，而非后者。

### 3. 控制事件是否产生受控制台模式与进程属性约束

- [`HandleGenericKeyEvent`](https://github.com/microsoft/terminal/blob/7ff11ee107c984e236960ec948b450ccbc10187b/src/host/input.cpp#L98-L147) 只有在 Ctrl 按下、key-down、虚拟键为 `C` 且 `IsInProcessedInputMode()` 为真时才调用 `HandleCtrlEvent(CTRL_C_EVENT)` 并终止等待中的读取。
- Microsoft Learn 的 [SetConsoleMode](https://learn.microsoft.com/en-us/windows/console/setconsolemode#parameters) 在 **ENABLE_PROCESSED_INPUT** 条目说明：启用时 Ctrl+C 由系统处理而不放入输入缓冲区；在 **Remarks** 又说明它决定 Ctrl+C 是由 `ReadConsoleInput` 读到还是交给应用处理函数。关闭该模式时，Ctrl+C 是键盘输入，不是信号。
- [Ctrl+C and Ctrl+Break Signals](https://learn.microsoft.com/en-us/windows/console/ctrl-c-and-ctrl-break-signals) 首段及项目列表说明：默认它们是传递给同一控制台所有附加进程的信号；但禁用 `ENABLE_PROCESSED_INPUT`，或以 `SetConsoleCtrlHandler(NULL, TRUE)` 设置可继承的忽略属性，都会阻止 Ctrl+C 处理函数被调用。Ctrl+Break 则始终按信号处理。[SetConsoleCtrlHandler](https://learn.microsoft.com/en-us/windows/console/setconsolectrlhandler#parameters) 的 **HandlerRoutine / Add** 段也明确说明该忽略属性会被子进程继承，且 `AttachConsole`、`AllocConsole`、`FreeConsole` 会重置处理函数表。

所以，空闲提示符下 ETX 后还能执行命令，只能证明该次输入经过了解析，不能证明持续运行时已向每个目标进程交付 `CTRL_C_EVENT`。

## 事实：Windows Terminal 有特殊路径，但没有绕过 ConPTY 的独立 Ctrl+C 信号通道

1. **快捷键优先。** 固定版本 [`TermControl::_KeyHandler` 第 1843–1857 行](https://github.com/microsoft/terminal/blob/7ff11ee107c984e236960ec948b450ccbc10187b/src/cascadia/TerminalControl/TermControl.cpp#L1843-L1857) 在 key-down 时先处理 key binding，命中便不再向 PTY 发送。当前 [`defaults.json`](https://github.com/microsoft/terminal/blob/7ff11ee107c984e236960ec948b450ccbc10187b/src/cascadia/TerminalSettingsModel/defaults.json#L732-L734) 将复制绑定到 Ctrl+Shift+C、Ctrl+Insert 和 Enter；仓库的 [`userDefaults.json`](https://github.com/microsoft/terminal/blob/7ff11ee107c984e236960ec948b450ccbc10187b/src/cascadia/TerminalSettingsModel/userDefaults.json#L26) 另有 Ctrl+C 复制映射。实际用户设置可以覆盖默认行为，因此调查 Windows Terminal 时必须检查绑定；这与直接写原始 ConPTY 管道的探针是不同条件。
2. **win32-input-mode 是另一种序列化，不是另一条信号通道。** [`TerminalInput::HandleKey` 第 115–121 行](https://github.com/microsoft/terminal/blob/7ff11ee107c984e236960ec948b450ccbc10187b/src/terminal/input/terminalInput.cpp#L115-L121) 可在该模式启用时直接编码 Win32 键事件。但 ConPTY 服务端 [`InputStateMachineEngine::ActionCsiDispatch` 第 501–508 行](https://github.com/microsoft/terminal/blob/7ff11ee107c984e236960ec948b450ccbc10187b/src/terminal/parser/InputStateMachineEngine.cpp#L501-L508) 仍调用同一个 `WriteCtrlKey`，注释还说明这是为了确保 Ctrl+C/Ctrl+Break 得到特殊处理。因此没有第一方依据把切换 win32-input-mode 当成洪流修复。
3. **普通连接写入没有带外控制 API。** 上述 `ConptyConnection::WriteInput` 只转换并写输入管道；本文所列普通 Ctrl+C 路径未调用 `GenerateConsoleCtrlEvent`。Microsoft Learn 的 [Creating a Pseudoconsole Session](https://learn.microsoft.com/en-us/windows/console/creating-a-pseudoconsole-session) 在 “Once the process is created successfully…” 段也只规定宿主把用户交互序列化到输入管道、从输出管道读取呈现信息。

这回答的是 **Windows Terminal** 源码行为，不能据此推断 WindTerm 的内部实现；WindTerm 的既有发布说明只能作为其自身限制陈述，不能替代 Microsoft 的机制证据。

## WindTerm 2.7.0 的定向静态补充（2026-10-24）

本次按用户要求直接分析固定官方发行物 `WindTerm.exe`，SHA-256 为 `7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0`。复用 Ghidra 12.1.4 已完成的 `local/analysis/windterm-20261022-01` 数据库，以 `-noanalysis` 定向导出，未执行 WindTerm、未打开参考窗口或截图。原始反编译仅留在 ignored 的 `local/analysis/windterm-r2-ctrl-c/`；公开记录只保留地址和结论。

**静态观察：**

- ConPty 虚表的写入口位于 `0x1402e5c30`（虚表槽 `0x140a2fa00`）：读取传入 `QByteArray` 的数据和长度，对对象 `+0x60` 的句柄调用普通同步 `WriteFile`。`0x1402e5380` 的创建路径将该句柄与 ConPTY 输入管道关联。此函数没有单独的 Ctrl+C 分支。
- 输出由 `PipeThread::run` 对应的 `0x1402e4cc0` 独立线程持续 `ReadFile`，与写入不是同一线程。该结构支持保留独立输出排空，但不能证明所有上层调用者都避免阻塞。
- 当前二进制的 Ghidra 导入/符号清单未出现 `GenerateConsoleCtrlEvent` 或 `SetConsoleCtrlHandler`；`WriteFile` 的完整导入引用只有六处，其中 ConPty 上述写入口与协议/进程基础设施分开。未发现不等于证明任何间接解析或外部模块均不存在这些能力。
- 写入口没有检查 `WriteFile` 的布尔返回值，而直接返回输出字节数变量。这是固定二进制的静态事实；它没有提供“洪流写失败”的运行证据，不能直接归因为 #36，也不应复制这个错误处理缺口。
- 官方固定提交的公开 [`ConPty.cpp`](https://github.com/kingToolbox/WindTerm/blob/a8336c1d1e3dd1a981500dae8ef9eb42066de95f/src/Pty/ConPty.cpp) 仅作命名和结构旁证；发行物地址及实际指令是这里的直接依据，不把公开片段当作完整应用源码。

**仍然未知：** 本轮常量搜索与候选函数反编译尚未定位普通键盘 Ctrl+C 到 ConPty 写入口的完整上层链，不能声称已从 WindTerm 二进制证明其发送 ETX。静态最后一段也没有回答当前 build 26200 的控制台锁、输入模式或处理函数为何未执行。因此它排除了“TermForge 必须模仿一个已观察到的 WindTerm 带外 Ctrl+C API”这一无依据方向，但不构成 #36 修复或运行时行为比较。

## 事实与推断：持续输出下确有已知延迟机制，但当前探针根因仍未完全定位

### 固定源码揭示的机制（事实）

- ConPTY 的 [`VtInputThread::_InputThread`](https://github.com/microsoft/terminal/blob/7ff11ee107c984e236960ec948b450ccbc10187b/src/host/VtInputThread.cpp#L78-L117) 在 `ProcessString` 前执行全局 `LockConsole()`，退出作用域时 `UnlockConsole()`。第 106–110 行注释是直接的机制证据：只有全局 unlock 会尝试分派 ctrl events；若误用 gci unlock，按 Ctrl+C 后“won't be dispatched until the next console API call”，并以 `powershell sleep 60` 为例。
- 全局 [`UnlockConsole`](https://github.com/microsoft/terminal/blob/7ff11ee107c984e236960ec948b450ccbc10187b/src/host/handle.cpp#L13-L29) 只在递归计数为 1 时调用 `ProcessCtrlEvents()`；否则只释放一层锁。[`HandleCtrlEvent`](https://github.com/microsoft/terminal/blob/7ff11ee107c984e236960ec948b450ccbc10187b/src/host/input.cpp#L228-L245) 先仅设置 `CtrlFlags`，真正处理在 [`ProcessCtrlEvents`](https://github.com/microsoft/terminal/blob/7ff11ee107c984e236960ec948b450ccbc10187b/src/host/input.cpp#L265-L336) 中进行。
- 输出也使用同一全局锁：[`WriteConsoleAImpl`](https://github.com/microsoft/terminal/blob/7ff11ee107c984e236960ec948b450ccbc10187b/src/host/_stream.cpp#L440-L456) 和 [`WriteConsoleWImpl`](https://github.com/microsoft/terminal/blob/7ff11ee107c984e236960ec948b450ccbc10187b/src/host/_stream.cpp#L624-L643) 均在写入时 `LockConsole()` / `UnlockConsole()`。
- 官方任务 [`#2141: Lock and Unlock in conhost should decouple Ctrl+C dispatch and use smarter handling`](https://github.com/microsoft/terminal/issues/2141) 仍开放，正文明确批评依赖递归锁计数、不同层 unlock 是否分派不一致，并把解耦 Ctrl+C 分派列为待做工作。这给上述设计是已知待改问题提供了第一方确认。

### 官方已知洪流限制（事实）

- 官方 bug [`#6258: Cannot send CTRL+C signal to remote machine while terminal is rendering text`](https://github.com/microsoft/terminal/issues/6258) 仍开放，标签含 `Area-Performance`、`Issue-Bug`、`Product-Terminal`。报告描述大量输出时 Ctrl+C/Ctrl+Z 等输入直到呈现追上才生效。维护者在 [2020-06-25 评论](https://github.com/microsoft/terminal/issues/6258#issuecomment-649762617) 给出本地、WSL SSH、Win32 OpenSSH 以及 conhost 对照测量，确认 UI 队列工作只略有帮助，仍“have a long way to go”；另一维护者在 [2021-08-09 评论](https://github.com/microsoft/terminal/issues/6258#issuecomment-895083277) 将潜在根因指向 #2141，但措辞是 “might be”。
- [`#15055: Input freeze on heavy output`](https://github.com/microsoft/terminal/issues/15055) 报告重输出时 Ctrl+C/Ctrl+D 无法中止；维护者在 [关闭评论](https://github.com/microsoft/terminal/issues/15055#issuecomment-1524088635) 将其归为 #6258 的重复项。这说明 1.16 时代仍有同类报告，但不是当前 1.24 或 build 26200 的回归验证。
- 功能请求 [`#19417: Allow Ctrl+C to drop buffered output as an option`](https://github.com/microsoft/terminal/issues/19417) 以 `not_planned` 关闭。维护者在 [2025-10-07 评论](https://github.com/microsoft/terminal/issues/19417#issuecomment-3377745862) 称缓慢排空的缓冲区很可能不受 Terminal 控制，本地应用的 Ctrl+C 已经发送信号，一旦应用调用 `WriteConsole`，Terminal 不再缓冲。此评论说明不能把“屏幕仍显示旧输出”当作“信号未发送”的充分证据；它也不能解释或否定本次原生子进程处理函数完全无标记的观察。

### 对当前观察的受限推断

持续 `WriteConsole` 与 ConPTY 输入处理竞争同一全局锁，加上控制事件仅在最外层 unlock 分派，是**有源码依据且与持续输出下不中断相容的候选机制**。官方 #6258 与 #2141 使“重输出下输入/中断可能严重延迟”成为已知限制，而非纯猜测。

但这不是当前现象的完整因果证明。实际 `KEY_EVENT_RECORD` 与外部 `GenerateConsoleCtrlEvent` 同样未触发处理函数，说明不能未经追踪就把全部失败归结为 VT 输入线程锁竞争。

## 本机 conhost 的无窗口 CDB 对照（2026-10-24）

使用已安装 CDB 10.0.26100.1742，仅调试自有 `local/r2-ctrl-c-probe` 及其创建的 conhost/cmd 子进程；没有窗口、截图或参考应用。Microsoft 符号服务器取得的 PDB 只写入 ignored 的 `local/symbols/`。探针直接复用当前 `Session`，保持 100 万次 `cmd for /L` 原负载；普通重建运行再次失败，洪流 ETX 与后续命令共 21 字节均由 `WriteFile` 报告写完，5 秒内没有 `TF_AFTER`。

**实际加载身份：** 探针使用 `C:\WINDOWS\system32\conhost.exe`，文件/产品版本 **10.0.26100.9444**，PDB 身份 `716FC77AB82C93512B79D2D4ACBC46F71`。这填补了“系统还是 Terminal 附带 conhost”的缺口，但不表示 Microsoft 固定提交与该系统二进制逐行一致。

**观察与扰动必须并列保留：**

- 空闲对照在真实符号断点中依次命中 `InteractDispatch::WriteCtrlKey`、`HandleGenericKeyEvent`、`HandleCtrlEvent` 和 `ProcessCtrlEvents`，调用链经过 `_DoControlCharacter`、`StateMachine::ProcessString` 和 `VtInputThread::DoReadInput`；cmd 随后进入 `KERNELBASE!CtrlRoutine`，并执行 `TF_AFTER_IDLE`。这直接验证本机正常路径。
- 一次较少断点的洪流追踪未命中上述控制键路径，失败点的 “ConPTY Input Handler Thread” 栈为 `NtReadFile` → `KERNELBASE!ReadFile` → `VtInputThread::DoReadInput`，不是 `LockConsole`。这不能支持“ETX 已证明卡在控制台锁”的说法。
- 全量 `ProcessCtrlEvents` 采样产生大量重复断点，只用于识别路径，不是性能测量；不同追踪的输出量和命中情况不得直接比较。原始日志保留在 ignored 的 `local/r2-ctrl-c-probe/`，临时探针与 CDB 脚本不进入产品交付。

**同次 cmd/conhost 对照（trace 10–12）：** 本机 cmd 版本为 **10.0.26200.9457**。实际符号与反汇编表明，`cmd!Handler` 对事件 0/1 调用 `cmd!SetCtrlC` 设置 `CtrlCSeen`；`cmd!eFor+0x1d2` 在循环中检查该标记，非零时经 `cmd!eFor+0x286` 清理并 `longjmp`。因此必须以 `Handler`/`SetCtrlC` 和该退出分支为观测点，不能把仅仅进入 `KERNELBASE!CtrlRoutine` 当成 cmd 已处理 Ctrl+C。

在 trace 12 的同一次原始负载中，先观察到宿主依次写入 47 字节循环命令、1 字节 `03` 和 20 字节后续命令；随后出现的 `WriteCtrlKey`、`HandleCtrlEvent`、`ProcessCtrlEvents` 和 `CtrlRoutine` 属于此前已在执行的空闲 ETX 对照。洪流后的 5 秒失败点，conhost `ServiceLocator::s_globals+0x6d4` 的 `CtrlFlags=0`，cmd `CtrlCSeen=0`；没有洪流对应的 `HandleCtrlEvent`、`ProcessCtrlEvents`、`cmd!Handler`、`SetCtrlC` 或 `eFor` 退出分支。conhost 输入线程仍等待下一次 `ReadFile`，cmd 主线程仍在 `eFor` 内。清理阶段的 `cmd!Handler(ecx=2)` 是 `CTRL_CLOSE_EVENT`，不属于本次 Ctrl+C。trace 10/11 独立重复了洪流写入成功而 cmd 标记为零的结果。

这修正了此前按日志顺序把空闲控制路径误归到洪流的中间判断：**现有同次证据是洪流 ETX 尚未进入 conhost 控制分派，而不是已经分派但 cmd 忽略它。** 仍未知的是已写入匿名管道的字节为何在该次快照前没有被输入线程读取/解析；`ReadFile` 等待栈本身也不能证明所有普通运行的唯一内核等待原因。CDB 会改变调度与输出时序，上述结论只描述该次插桩样本，不能代替普通运行的因果证明。

**无断点 ETW 对照：** 使用 Microsoft 固定源码中的 `Microsoft.Windows.Console.Host` provider（GUID `fe1ff234-1f09-50a8-d38d-c44fab43e818`）采集同一次普通失败运行；6 秒 trace 报告 `Events Lost: 0`、`Buffers lost: 0`。cmd 的输入模式在循环前后保持 `0x7`，其中 `ENABLE_PROCESSED_INPUT` 仍启用。`CookedRead` 记录了 `@echo off`、`echo TF_AFTER_IDLE` 和原始 `for /L`，没有 `TF_AFTER_FLOOD`，独立确认后续命令没有进入 cooked-read 执行路径。该 conhost 在此次采集中没有 `ConPTY ReadFile`、`HandleCtrlEvent` 或 `ProcessCtrlEvents` 事件，故 ETW 不能补齐控制分派链，未出现事件不等于对应函数未执行。会话已停止；原始 ETL/解码文本包含其他控制台内容，提取上述事实后已删除，不进入公开证据。

因此，当前实际卡点是**洪流 ETX 写入与 conhost 控制分派之间的时序敏感缺口**，不是已证明的 cmd 停止条件错误。下一步需用更低扰动的同次 I/O 追踪关联宿主写、管道读完成与 `CtrlFlags`，不能把不同运行的片段拼成单一根因；仍不得修改负载或输入编码来制造通过。

## 对 #36 的具体建议

### 可以实施，但不能预先宣称解决当前洪流复现

1. **保持单个 ETX 作为默认 Ctrl+C ConPTY 输入。** 它有固定 Windows Terminal 编码和 ConPTY 服务端特殊处理的直接依据；不要改成字面 `^C`、普通 `INPUT_RECORD` 字节 dump 或任意 GenerateConsoleCtrlEvent 调用。
2. **落实 ConPTY 官方 I/O 隔离要求。** [Creating a Pseudoconsole Session](https://learn.microsoft.com/en-us/windows/console/creating-a-pseudoconsole-session) 的首个 Warning 要求输入、输出通道分别由独立线程服务，并警告同一线程在通道缓冲区已满时发阻塞请求可能死锁；关闭段也要求输出持续排空。TermForge 应保证持续排空输出、输入写不被呈现/解析线程卡住，并检查写完成与错误。固定 Windows Terminal 自身也在 [`ControlCore::SendInput` 第 504–506 行](https://github.com/microsoft/terminal/blob/7ff11ee107c984e236960ec948b450ccbc10187b/src/cascadia/TerminalControl/ControlCore.cpp#L497-L515) 避免持终端锁调用可能无限阻塞的 `WriteFile`，并在 [`ConptyConnection::WriteInput`](https://github.com/microsoft/terminal/blob/7ff11ee107c984e236960ec948b450ccbc10187b/src/cascadia/TerminalConnection/ConptyConnection.cpp#L552-L599) 串行化 overlapped 写。它们是实现参考，不是当前环境会修复的证明。
3. **保留原始完整负载作为验收条件。** 若做宿主 I/O 修正，必须用题述同一持续 cmd 循环验证：单个 ETX 后循环被中断、后续 `echo TF_AFTER` 可执行，并单独验证原生子进程处理函数收到事件。不能以输出变少、仅恢复提示符、仅写入成功或最终循环自然结束作为通过。

### 目前没有依据作为修复的方案

- **切换 win32-input-mode：** 服务端最终汇入同一个 `WriteCtrlKey` 和锁/分派机制，不能据此承诺绕过洪流。
- **把 `GenerateConsoleCtrlEvent` 当通用带外修复：** [GenerateConsoleCtrlEvent](https://learn.microsoft.com/en-us/windows/console/generateconsolectrlevent#parameters) 要求目标与调用方共享调用方关联的控制台；`CTRL_C_EVENT` 还不能限定到特定进程组，非零组 ID 时函数可成功但组内收不到信号。它不是可定向替代单个 ConPTY 客户端 Ctrl+C 的通用 API，且本次外部调用已失败而无标记。
- **以 Ctrl+Break 等价代替 Ctrl+C：** 两者模式约束、读取终止和默认行为不同；本次 Ctrl+Break 能终止 cmd 只提供诊断对照，不是 Ctrl+C 修复。
- **宣称只能由 TermForge 改字节编码解决：** 原始探针已经使用受支持的 ETX；现有第一方证据没有给出另一种可绕过 conhost 分派机制的输入编码。

因此，#36 当前可以记录并实施“正确的 ETX + 独立无阻塞 I/O 架构”，但在完整原负载通过前，仍应保留为未修复/受限，而不是关闭为编码错误。若必须提供确定性的强制终止，应作为语义明确、单独命名的终止能力，不能冒充 Ctrl+C 信号交付。

## 必须保留的证据缺口

1. **二进制对应关系：** 已确认探针加载系统 conhost 10.0.26100.9444，而不是随 Terminal 分发的副本；固定 Microsoft 提交仍不证明与该二进制逐行一致。
2. **运行时卡点：** trace 12 已在同次插桩失败样本中关联宿主写入、`CtrlFlags=0`、`CtrlCSeen=0` 与 cmd 留在 `eFor`，但断点会改变时序，且等待中的 `ReadFile` 不是字节未被读取的充分证明。尚缺同次低扰动管道 I/O 追踪，确认写入对应的读完成与解析位置；不能把不同运行的片段拼成单一根因。
3. **模式与继承状态：** ETW 已确认本次 cmd 在循环期间输入模式为 `0x7`，未关闭 `ENABLE_PROCESSED_INPUT`；但原生子进程的忽略属性、控制台附加关系与进程组仍需记录。尤其 `SetConsoleCtrlHandler(NULL, TRUE)` 的继承属性和 `AttachConsole` 重置处理函数表都可能影响外部对照实验。
4. **多路径失败的关系：** ETX、真实 `KEY_EVENT_RECORD`、外部 `GenerateConsoleCtrlEvent` 三条路径均未出现处理函数标记，尚不能证明它们共享一个根因，也不能只用 #6258 解释全部。
5. **信号与呈现不可混同：** `TF_AFTER` 未出现、旧输出仍滚动、循环未停止、处理函数无标记是不同观测。应分别保留时间线和判定，不以其中一个推断另一个。
6. **跨产品边界：** 没有本次 Windows Terminal 或 WindTerm 运行时复现；WindTerm 只有固定公开组件源码与不完整应用二进制的静态证据，完整键盘输入链仍未知。不得把 Microsoft 的锁机制或 #6258 直接归因为 WindTerm 行为，也不得用 WindTerm 的普通写入口解释 build 26200 的实际卡点。

## 实际访问的关键第一方来源

- 固定提交总入口：<https://github.com/microsoft/terminal/tree/7ff11ee107c984e236960ec948b450ccbc10187b>
- 输入编码：<https://github.com/microsoft/terminal/blob/7ff11ee107c984e236960ec948b450ccbc10187b/src/terminal/input/terminalInput.cpp#L588-L612>
- ETX 特殊处理：<https://github.com/microsoft/terminal/blob/7ff11ee107c984e236960ec948b450ccbc10187b/src/terminal/parser/InputStateMachineEngine.cpp#L141-L157>
- 控制键与模式：<https://github.com/microsoft/terminal/blob/7ff11ee107c984e236960ec948b450ccbc10187b/src/host/input.cpp#L98-L147>
- 锁与延迟分派：<https://github.com/microsoft/terminal/blob/7ff11ee107c984e236960ec948b450ccbc10187b/src/host/VtInputThread.cpp#L78-L117>、<https://github.com/microsoft/terminal/blob/7ff11ee107c984e236960ec948b450ccbc10187b/src/host/handle.cpp#L13-L29>
- ConPTY 通道模型：<https://learn.microsoft.com/en-us/windows/console/creating-a-pseudoconsole-session>
- 控制台信号与模式：<https://learn.microsoft.com/en-us/windows/console/setconsolemode>、<https://learn.microsoft.com/en-us/windows/console/ctrl-c-and-ctrl-break-signals>、<https://learn.microsoft.com/en-us/windows/console/setconsolectrlhandler>
- 官方已知限制：<https://github.com/microsoft/terminal/issues/6258>、<https://github.com/microsoft/terminal/issues/2141>、<https://github.com/microsoft/terminal/issues/15055>、<https://github.com/microsoft/terminal/issues/19417>

## 2026-10-26 接手复核与固定回归入口

在固定 `41cbbe3` 的独立工作树上，不修改产品代码，新鲜默认构建的 `normal` 三轮先通过，随后原 `normal` / `resize` 各十个 fresh 场景均通过。新 GO/Job 包装的原始 ConPTY 探针五轮通过；此前失败的旧探针原二进制在受监督的挂起创建包装器下五轮也通过，其文件字节保持不变。没有延长原五秒期限、修改一百万次循环、重复 ETX 或替换输入编码。

本次 PowerShell 的版本资源显示字符串均为 `10.0.26100.8875`，但 `VS_FIXEDFILEINFO` 的数值版本为 cmd `10.0.26100.9278`、conhost `10.0.26100.9444`；conhost 数值版本与旧报告一致。显示字符串和数值版本并不等价，文件 SHA-256 在此次核对前后不变，不能据此宣称系统曾更换版本。回归入口同时记录固定数值、资源字符串和文件哈希。当前仍未定位旧失败与现在成功的因果差异，旧失败不能改判成功。

已将一次性原始反馈整理为[受监督 cmd 回归入口](../../experiments/conpty/README.md#持续-cmd-输出与-ctrlc-独立回归)。输入/输出、实际系统文件和当前构建身份与回收结果各自落盘，遇到旧失败会保留失败而非跳过。它完成 #36 的可重复探针工作，**没有产品 Ctrl+C 修复，也没有验收原生处理函数的独立信号场景**。旧 9444 失败与根因缺口继续保留，#36 保持 OPEN。
