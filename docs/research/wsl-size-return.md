# 四 shell 验证中的 WSL 原生子进程返回边界

## 问题与证据边界

[#80](https://github.com/JavaZeroo/termforge/issues/80) 来自 [PR #79 的默认/system CI](https://github.com/JavaZeroo/termforge/actions/runs/40363003416/job/121713140988)：原四 shell 方法第一轮停在 stage6，30 秒内部期限耗尽，外层 40 秒未超时。四者目录与真实尺寸标记已经核对；失败清理后 Job 为零。旧代码在 stage5 看到尺寸输出便向 WSL 发送 `exit`，并提前写入 `normal_exit=true`。

同一原源码在本机 Windows 11 的有界六次运行均通过，不能解释该 CI。随后只改变合成原生尺寸程序的返回时机：程序输出真实 `TF_SIZE` 后，用 `PeekConsoleInputW` 观察到 `exit\r` 才返回，始终不消费输入。这一次复现 stage6 超时，独立记录实际字节 `657869740d`；返回 WSL 后有新 `TF>`，没有正常退出。源码补丁、二进制和原件均保留。

这证明该测试存在可触发的提前发送窗口。原 hosted 样本没有 native helper 输入边界记录，因此**尚未证明原 CI 必定同因**。该修正也不承诺终端能保存 WSL/native 交接期间用户任意提前输入的内容。

## 限定修正与真实回归

尺寸输出只证明原生程序查到了控制台尺寸。`ui-shells` 仍检查四个会话的实际尺寸与 resize settled；另外要求 WSL 的同次尺寸输出之后出现完整新提示符，才开始原来的 WSL → pwsh → PowerShell → cmd 顺次退出。提示符使用已有独立运输 oracle，保持 8192 B 观察上限，未放宽其输出、转义或编辑判据。四个会话实际回收且无错误后，才写正常退出文件。

新回归仍走实际 `ui-shells`、WM_CHAR、输入队列、ConPTY 和真实 WSL native helper。仅显式测试开关启用一个自有命名事件：helper 输出真实尺寸后等待 release；四个尺寸标记齐全时，宿主先断言实际退出条件仍阻止下一命令，随后释放 helper，再完成原四次正常退出。它没有固定延时、伪造尺寸或改变接收字节。helper 的 15 秒是失联失败期限，场景内部 30 秒及外部 40 秒保持原值。

保留旧退出条件时，同一事件夹具在 4.795 秒明确失败：`size output was mistaken for shell input readiness`。最终修正后，新握手与原两轮共三个进程场景全部通过，四会话依次 stage6/7/8/9、exit0、Job0、desktopgone；两个 unittest 方法耗时 10.872 秒，仅作回归运行记录。

中间曾对全部 shell 使用同一提示符 oracle：握手及原第一轮通过，第二轮 cmd 的 CUP/CR/OSC 重绘使其停在 stage5。该失败原件继续保留。最终只处理已经复现的 WSL 交接；没有放宽共用 oracle，也不声称 cmd/PowerShell 重绘遵循 WSL 运输格式。

## 来源与复验

实现固定为 `2b85fef48149f022b50d9d8878f62539e02c92ba`，基点 `c8206911fb511bc2996579f2bb108ef54ae7acbd`。当前证据配置为 Windows 11 26200、默认自研核心/system 后端；[审核后记录](../../experiments/shell/results/2026-10-27-wsl-size-return/validation.json)分别保存原 CI、原六次未复现、受控 stage6、旧条件红例、中间 stage5 和最终结果的身份与限制。私有目录、实际桌面名及完整运输原件留在本地。

在完整 Release 构建后，设 `TERMFORGE_BIN_DIR` 指向该构建的 `bin/Release`，运行：

```powershell
python -m unittest experiments.shell.test_shell.ShellTests.test_local_shell_exit_waits_for_native_helper_return experiments.shell.test_shell.ShellTests.test_local_shell_launch_working_directory_and_cleanup -v
ctest --test-dir build -C Release --output-on-failure
```

同源码默认/system 完整 CTest 为 46/46，283.89 秒；shell 为 39 方法、81.664 秒，其中 3 项真实系统剪贴板保护跳过。全套共 12 个内部跳过，完整清单和原日志哈希列于记录，包括可选候选/真实应用依赖、hosted CF 保护、opt-in 原版故障注入和私有窗口站 OpenClipboard 拒绝；这些项目没有因 CTest 聚合通过而验收。新握手、原两轮以及 #72 保存/恢复方法在本次完整 shell 回归中均通过。

candidate/system、实验官方后端及 hosted 合并配置需要各自验证。没有实际呈现、WindTerm 行为/性能等价或 R2 完成声明。#38/#35 的其余真实阻塞继续开放。
