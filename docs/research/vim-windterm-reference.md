# WindTerm 2.7.0 的 Vim、原生尺寸与 CPR 有界对照

固定原版在一次观察中完成了原 Vim 的插入、60×12 resize、原 93 B Ctrl-O 命令、写盘和返回主提示符。后续原生尺寸与 CPR 检查暴露了入口差异：另一份原版样本的缓冲区为 **60×3000**、可见窗口为 **60×12**；未改动的 CPR 入口没有观察到回复，而显式启用 VT 输出的独立变体得到回复并正常退出 shell。

四个样本各自有边界，不能合并为一次完整 TUI 通过。没有修改 TermForge 产品/测试源码，没有采用修补 MSYS DLL 或选择 WinPTY 为产品后端。#52 的原固定运行库兼容方案仍未完成。

## 身份、隔离与停止条件

- 原版：官方 WindTerm 2.7.0 Windows x64，EXE SHA256 `7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0`；每次复制的 4,047 文件清单 SHA256 `e459adf2f12adb6a1b7f55e66a9da1c22794185e66e848057d21e0c60d669203`，沿用[既有准备守卫](../../research/ui/launch.py)。原版 cmd 横幅记录 Windows `10.0.26200.9457`。
- Vim：与[隔离运行库实验](vim-msys-runtime-prototype.md)相同的固定 Vim 9.1 patches 1–532，EXE SHA256 `2e0fda63a89171adfc46c9417d5105cc39ed778cc69a890b83a4eabc34e7646a`；使用未修补 MSYS DLL，SHA256 `3096d2ba9be20e90be1399edd391bac65d84bc387f5a419b502c9b326081930f`。配置 PATH 指向冻结的原始五文件副本；本批没有观察实际加载模块路径。
- 公开自有夹具按源码基点 `b60cd65b9fa6af724279780faa8d78847eb5cef0` 核对。`entry` 只固定文件哈希，不补造当时未记录的 HEAD；其余三份计划明确记录该提交。原 `candidate-vim.vim` 334 B，SHA256 `386d475e5297169cf7fcc425dcc0f6b0174c237ff86be03564b2d505c5d79797`；原尺寸/CPR helper EXE 分别为 `a7a45db0976dd1e157871b6a9b50b1362ceb8cb8ea604470e58791920805d408` / `691b440e9c500c76c1c09c448aafbe2316793bf341ffe69bbc2bf70e41be64cc`。
- 每次只创建一个合成本地 Shell 配置、独立 HOME/cache、非输入 window station 和自有 Job；不操作个人剪贴板、显示设置或 WSL。REA CLI 固定 6.7.0，使用既有 `instrument-with-frida attach` 入口。
- 每个问题在启动前各声明最多一次运行，合计四次，均已停止。CLI 采集 30 s、外层监督 45 s；第一份入口观察的脚本期限为 28 s，其余为 25 s，另有晚 0.5 s 的脚本兜底。后续样本针对新的尺寸/模式问题，不重跑前一场景取绿。

## 四个独立样本

| 样本 | 实际结果 | 未覆盖或失败 |
|---|---|---|
| `entry`：入口可行性 | 实际 `winpty_open`、`winpty_spawn`、91×23 `winpty_set_size` 调用；原 Vim 写出 READY、进入插入并逐字接收标记 | Vim 由 `cmd /k` 直接启动；不含 resize、写盘、CPR、正常退出，结束由自有回收完成 |
| `vim`：原输入与真实窗口 resize | 从 cmd 提示符键入原启动命令；READY 和 `[New]` 后插入；原 93 B 命令写出 `60x12`；原 `Ctrl-O:wq` 保存并恢复 `TF>` | 原尺寸门槛未通过，脚本在 stage56 超时；CPR 和 shell exit 尚未运行。shell-handle 观察器存在一次参数类型错误，正常退出没有证据 |
| `native`：独立尺寸/CPR | 原尺寸 helper 输出 `TF_SIZE_60x3000`；独立 `GetConsoleScreenBufferInfo` 同时记录 buffer 60×3000、window `[0,0,59,11]`、viewport 60×12 | 原 `dwSize=60×12` 预期为 false。CPR 文件为空，脚本在 stage52 超时，query 进程退出码未观察；未发送 `exit 37` |
| `mode`：独立 VT 输出变体 | 读到输出模式 3；仅开启 VT 输出后模式为 7，未改动的 CPR helper 返回 0，文件为 `ESC[7;1R`；模式恢复，shell 自然退出 37 | 新增模式包装器属于不同配置，不代表前一原入口已通过；没有运行 Vim，CPR 的实际生产方未跟踪 |

`vim` 的窗口准备先在 shell 中校准像素尺寸：1360×860/91×23 → 895×563/52×12 → 990×563/60×12，再恢复 1360×860 后启动 Vim。插入标记之后，只请求一次 990×563，观测到实际 WinPTY 60×12 调用成功及 UI capacity 12。该几何和 Qt 事件调度不同于 TermForge，不能用它比较竞态概率、输入延迟或性能。

## Vim 文件的独立核对

`vim` 样本的输入日志 **115 B** 与以下序列逐字节相同：`i`、`TF_CANDIDATE_TUI`、原 93 B resize 命令、`Ctrl-O:wq\r`。插入事件文件 **58 B** 精确符合 `INSERT`、逐字符、`LEAVE / INSERT / LEAVE`；保存文件 **17 B** 为标记加 LF，resize 文件 **5 B** 为 `60x12`。恢复主提示符来自该样本的 UI 模型观察；实际呈现端点保持 `null`。

该样本的输入命名管道成功匹配，但 WriteFile 探针没有记录调用；这不能解释为没有输入。完整 Vim 日志直接证明上述应用接收序列。`winpty_spawn` 的 shell-handle 观察器向 Frida 传入了 Boolean，出现 `expected an integer`，后续独立样本改为整数参数；保留该错误，不补造该样本的进程退出证据。

这里只观察到一次完成的 Vim 编辑过程，不能证明原版不会遇到同类竞态，也不追溯归因此前未记录输入的失败。第一份 stage56 样本没有保存实际尺寸 helper 文本；后一份独立的 60×3000 结果支持尺寸区别，不能冒充前一份缺失记录。

## 原生尺寸与 VT 模式的解释

`shell_size_client` 查询的是 `dwSize`。Windows 将它定义为整个屏幕缓冲区尺寸，`srWindow` 则描述显示窗口，两者不是同一字段。[Microsoft 字段说明](https://learn.microsoft.com/en-us/windows/console/console-screen-buffer-info-str)。本批原版的 60×3000 与 60×12 是运行观察；现有 TermForge 原生缓冲区判据仍未改变，是否调整跨后端合同需明确提案及用户确认。

现有 [CPR helper](../../experiments/shell/query_client.cpp)设置输入的 `ENABLE_VIRTUAL_TERMINAL_INPUT`，没有启用输出的 `ENABLE_VIRTUAL_TERMINAL_PROCESSING`。Windows 文档将后者定义为输出 VT 控制序列的解析开关，并说明输入 VT 标志通常与它配合使用。[Microsoft 模式说明](https://learn.microsoft.com/en-us/windows/console/setconsolemode)。

`mode` 样本实际输出模式为 **3 → 7 → 3**；输入模式为 **503 → 512 → 503**。包装器只在自有控制台设置输出标志，运行相同 helper 后恢复两种模式。helper 原 5 s `WaitForSingleObject` 保留，父进程额外提供 7 s 监督以约束可能阻塞的 `ReadConsoleW`；本次没有触发监督超时。shell 退出 37 来自复制的真实进程句柄，不是回收产生的退出码。

这些事实支持继续核对夹具的模式前提；**前一失败样本未记录模式，不能将本次读到的 3 回填为它的实测值，也不能据此宣称已因果修复该失败**。CPR 可能由原生控制台处理，本批没有证明回复来自 WindTerm 的末端解析器。模式变体成功与原入口 stage52 未完成分别保留。

## 回收、公开材料与结论边界

四次配置检查均只含一个合成会话；Job 最终 active 均为 0，总进程数分别为 13、14、15、10，原始 terminated 计数均为 0。外层仍明确请求了终止与回收，该计数不等于所有成员自然退出；只有 `mode` 的 shell 单独观察到自然退出 37。

[白名单摘要](../../research/ui/results/2026-10-27-vim-native-reference/validation.json)记录每份样本的输入/工具/原件哈希、实际调用、错误、文件检查和限制。导出重新核对四份私有记录及合成文件，未重新启动 GUI。REA provider 的 subject/environment 仍为 `null`、捕获时 state 为 `running`；包身份由本地准备守卫单独补充，不能改写成 provider 自己完成了身份认证。采集结束后的自有回收另列。

原件和临时原创驱动保留在 ignored `local/r52-windterm-reference`、`local/r52-windterm-vim-contract`、`local/r52-windterm-native-size`、`local/r52-windterm-cpr-mode`，每处计划/驱动/参数/观察/回收可本地复核。公开摘要不含私有驱动全文或原始插桩输出，因此不是一套公开的一键复现包；不得据此扩大重复性结论。发行物、DLL、资源、绝对路径、PID/句柄及用户配置均不进入公开材料。

本批补充原版同 Vim 的有限行为证据，以及原生 buffer/viewport、控制台模式的真实差别。#52/#28/#56 可据此继续调查兼容方案；原固定运行库的官方完整 CTest **56/57、402.66 s、8 项内部 skip**没有重跑，也没有被这些样本替代。#47、Draft #50、完整 R2 和产品架构选择保持各自原出口，不据此采用 WinPTY、替换用户应用运行库或更改验收门槛。
