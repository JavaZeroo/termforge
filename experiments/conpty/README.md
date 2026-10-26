# 无窗口 ConPTY 生命周期实验

本探针启动项目自己的受控 console child，通过真实 Windows ConPTY 独立验证双向输入、尺寸变化、输出与资源回收。无需用户打开窗口、输入确认，也不连接个人 shell、会话或配置。探针直接核验字节和生命周期；使用同一传输实现的解析器、输入队列和窗口集成见[本地 shell 工作区](../shell/README.md)。

## 构建与自动执行

ConPTY API 最低版本为 Windows 10 1809；这不是所有 Windows 版本均已通过验收的声明。构建需要 CMake 3.20+、支持 C++17 的 Windows 编译器及 SDK、Python 3，已验证环境以运行记录和 CI 为准。仓库根构建也包含 `termforge_conpty_probe`；独立构建如下：

```powershell
cmake -S experiments/conpty -B build/conpty
cmake --build build/conpty --config Release
python experiments/conpty/run.py --binary build/conpty/Release/termforge_conpty_probe.exe --out local/experiments/conpty-run --repeats 5
python -m unittest experiments.conpty.test_conpty -v
```

`--out` 必须是新目录，原始成功/失败 JSON 和 manifest 都保留。测试通过 `TERMFORGE_BIN_DIR` 接收二进制目录；未设置时使用独立构建路径。CI 设置 `TERMFORGE_TEST_ARTIFACTS` 后，每次测试的合成结果会写入其 `conpty/` 子目录。原生 `--host` 入口等待监督器的 `GO`，请使用 Python 入口以保留超时回收保障。

## 验收场景

| 场景 | 自动检查 |
|---|---|
| `normal` | child 就绪；ASCII/中文 UTF-8 经真实输入管道回显；child 报告初始 80×24，resize 后报告 120×40；主动退出码 37 |
| `large` | 16,384 个唯一编号、每条 66 字节的有效载荷，逐字节检查完整内容、顺序与数量；随后正常退出 |
| `cancel` | child 创建自己的孙进程，并持续输出；显式终止内层 Job，退出码 91，整棵受控子树清空 |
| `close` | 同样在持续输出且存在孙进程时关闭 ConPTY，child 退出且受控子树清空；保留实际退出码 |
| `watchdog` | child/孙进程存活时故意停住宿主，8 秒监督器期限后终止外层 Job；确认已到达故障注入点且活跃进程归零 |

ConPTY 输出包含系统生成的 VT 序列，不能视为 child 的原样 stdout。大输出验收直接检查收到的原始字节流中的全部固定有效载荷，不调用 TermForge 解析器，也不声称所有 VT 语义正确。终止场景有意打断输出，不要求中断后的尾部完整。

受控 child 使用 `ReadConsoleW` 累积完整 UTF-16 命令，再严格转换成 UTF-8；这样不会在一次读取的 surrogate 边界转换半个字符。宿主写入/读取的 ConPTY 管道仍使用 UTF-8，中文有效载荷要求不变。这是对受控 console API 路径的验收，不证明任意应用的键盘模式兼容。[Unicode console API 文档](https://learn.microsoft.com/en-us/windows/console/readconsole)

## 回收和失败边界

监督器先启动等待 `GO` 的宿主，将其放入有 `KILL_ON_JOB_CLOSE` 的匿名 Job，才允许创建 ConPTY。宿主、系统 console host、child 和孙进程都在这个任务拥有的树内；超时只终止此 Job。child 在 suspended 状态加入内层 Job 后才恢复执行。所有过程不枚举或关闭用户的无关进程。

输出线程持续排空管道直至关闭，诊断捕获上限 16 MiB，超过后仍排空并报告失败；避免关闭伪终端时无人读取输出造成死锁。资源检查包括 child 信号/退出码、内层 Job 活跃数、输出线程结束、宿主句柄/线程数，以及宿主退出后的外层 Job 活跃数。探针只发送不超过 1 KiB 的控制命令；工作区另有异步输入和有界输出队列，其调度与关闭行为须由 shell 集成测试验证。

每个宿主先记录一次 `startup` 会话，再执行重复场景。本机 Windows 11 首次会话观察到**宿主句柄总数增加 6**；具体归属尚未定位，不能称为零初始化开销。后续各次会话要求句柄数和线程数回到已建立的基线；宿主最终退出后，外层 Job 必须为零活跃进程。这个证据只支持短批次的稳态回收，不能代替长时泄漏检测或其他 Windows 版本验证。

跨系统排查会记录关闭后的即时计数，并在约 1 秒有界期限内等待回到原基线，之后仍不相等则失败。失败保留原始输出首/尾各最多 4096 字节的十六进制，并用 child 的 `TFINPUTHEX` 区分输入损坏与输出编码/VT 序列差异；首次功能检查失败后立即停止同宿主后续重复。这些都是诊断数据，不参与性能结论；历史失败及限制见[跨环境记录](../../docs/research/conpty-platform-check-2026-10-22.md)。

child 显式设置 `STARTF_USESTDHANDLES` 并将三个标准句柄保持 null，由伪终端建立 console 端点，避免继承宿主被采集的标准句柄。[Windows Terminal 维护者对该行为的说明](https://github.com/microsoft/terminal/discussions/15814)

## 来源与结果

代码为独立实现；API 使用依据为 Microsoft 的 [ConPTY 会话文档](https://learn.microsoft.com/en-us/windows/console/creating-a-pseudoconsole-session)、[CreatePseudoConsole](https://learn.microsoft.com/en-us/windows/console/createpseudoconsole) 和 [关闭语义](https://learn.microsoft.com/en-us/windows/console/closepseudoconsole)。关闭期间保持独立输出线程，适用于旧 Windows 上可能阻塞的关闭路径；本机测试系统是 Windows 11 build 26200。

审阅后的纯合成历史记录位于 [results/2026-10-22](results/2026-10-22/manifest.json)，保存源码/二进制身份、系统版本、退出状态与资源计数；后续修复和远端系统验证不回写这些结果。`elapsed_ms` 是含启动/同步/关闭的诊断耗时，不是吞吐、输入延迟或 WindTerm 对照结果。

当前已验收环境为本机 Windows 11 build 26200 和 `windows-2025` runner；不承诺整个 Windows 10/Server 系列。Server 2022 的中文回显已修复，但稳态句柄仍增加 1，未满足资源门槛，定位与修复由 [#21](https://github.com/JavaZeroo/termforge/issues/21)承接。保留[严格兼容性探针](../../.github/workflows/conpty-compat.yml)，可由代理执行 `gh workflow run conpty-compat.yml` 自动重跑；失败仍使该 workflow 失败，不用用户操作测试会话。

## 持续 cmd 输出与 Ctrl+C 独立回归

[#36](https://github.com/JavaZeroo/termforge/issues/36) 的原始 ConPTY 入口由 `conpty_ctrl_c_probe` 和 `experiments.conpty.ctrl_c_run` 提供，不经过窗口、TermForge 解析器或输入队列。先按根贡献指南构建；使用新输出目录：

```powershell
$env:TERMFORGE_BIN_DIR = (Resolve-Path build/bin/Release).Path
python -m experiments.conpty.ctrl_c_run --out local/ctrl-c-new --repeats 3
python -m unittest experiments.conpty.test_ctrl_c -v
```

也可用 `--exe` 指定本次构建的 `conpty_ctrl_c_probe.exe`。Windows 根 CTest 注册为 `conpty_ctrl_c`。原生入口等待外层 Job 的 GO，正常使用 Python 监督入口；测试不使用真实输入桌面或系统剪贴板。

合同保留一百万次 `cmd for /L`，实际输出至少持续一秒后发送单个 ETX，立即发送后续命令，五秒内检查独立完整输出行；命令回显或循环自然结束不算中断。监督器检查退出与自有 Job 回收，分别记录输入/输出身份、探针/源码身份和本次自有 cmd/conhost 映像的系统文件身份。原始诊断只留在输出目录的 `_private` 子目录；公开文件不包含路径、PID 或原始异常文本。

此入口验收的是固定 cmd 场景，不能单独证明所有原生控制处理函数、真实物理按键或所有 Windows 版本通过。当前条件未复现旧失败，也不能据重复通过关闭 #36；环境对照与未决点见[输入机制研究](../../docs/research/conpty-ctrl-c-input.md)。

## 每屏 margin 与可替换后端诊断

`conpty_margin_probe` / `experiments.conpty.margin_run` 对同一固定175字节源检查四阶段真实 console capture、ConPTY EOF和候选末端。标准系统后端与固定官方 side-by-side ConPTY 的差异、关闭VT处理的窄网格反例、包身份及复现命令见[后端调查](../../docs/research/conpty-margin-backends.md)。这里只增加有界诊断，产品默认后端未改变；原生源控制台与末端结果分列，不能据末端正确关闭 #41 或宣布 R2 通过。

`python -m experiments.conpty.margin_contract <capture-directory> --columns 80 --rows 24` 离线检查原175B的四阶段文字、逐格属性和光标；
已知 native final 损坏会返回1。逐字段合同、保留的生命周期失败、固定上游原因和**未编译/未验证**的小补丁候选见[原生处理路径](../../docs/research/conpty-native-margin-path.md)。
