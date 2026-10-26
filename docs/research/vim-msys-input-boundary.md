# Vim resize 后 Ctrl-O 的输入边界

[#52](https://github.com/JavaZeroo/termforge/issues/52) 的 GUI stage59 仍未修复。新的有界运行时记录将捕获到的失败定位在 MSYS 控制台事件到 `read(0)` 的转换：TermForge 成功写出完整 93 B，原生输入也包含 Ctrl-O，但 Vim 收到的读结果仅剩其后 92 B。本文记录诊断，不改变固定应用、输入、期限或验收合同。

## 固定身份与观测范围

基点 `44ea45b` 的产品代码与 `c662011` 相同；候选 `termforge.5`、官方 ConPTY `1.25.260930003`、flags24。固定 MSYS Vim 9.1 patches 1–532 的 SHA-256 为 `2e0fda63a89171adfc46c9417d5105cc39ed778cc69a890b83a4eabc34e7646a`，MSYS DLL 文件为 `3096d2ba9be20e90be1399edd391bac65d84bc387f5a419b502c9b326081930f`，对应公开源码 `2e2ef940151485490aff0c7b00c00965ef7a71a4`。

临时诊断程序 SHA-256 为 `73472864e5aa291704917812ff1848f7a59be6e5f30c41f6a613e3fc18c740d4`。仅在原 VimEnter READY 和 `[New]` 就绪后，从会话自己的 Job 查询唯一固定路径 Vim，附加 REA/Frida；附加完成后继续原 `i` 输入。该握手包含在原25秒场景期限内，外层仍40秒，Vim脚本、尺寸、93 B命令和原精确断言均保持。探针记录实际 API 参数与返回，不改输入字节、应用数据缓冲区或返回值；Frida安装代码钩子，初始调度及观察开销仍会影响运行。

首批最多4轮，出现原 stage59 与诊断成功后停止，实际为两红一绿。随后为核对具体调用点，另设最多2轮、首次原失败或其他失败即停止的批次，实际1轮原 stage59。第二批核验加载模块路径、PE时间戳、COFF函数位置及4个入口的实际32 B前缀，然后观察两个函数的进入/返回；它不是新的修复方案。

每轮上限512条事件，每次原生调用最多256个记录，每次 `read` 最多512 B；四轮均未截断。全部原件、工具完整 Evidence、源代码补丁与二进制留在本地，公开[白名单时间线及哈希](../../experiments/shell/results/2026-10-27-vim-msys-input/validation.json)。所有运行均原期限内结束、外层 Job active0/terminated0、私有 desktop 已回收。

## 直接观察

三次红例中，writer 都一次成功写出93 B，包括开头 `0f`。原生 `ReadConsoleInputW` 可看到该字符；之后的 MSYS `read(0)` 只返回 `:call writefile(... )` 对应的92 B。原 InsertCharPre 文件记录冒号及命令进入插入内容，resize事件文件为空，原场景在stage59失败。

核对调用点的红例具有以下顺序。线程名称仅为该次运行的别名，序号对应公开JSON；没有将不同进程时钟拼成耗时测量。

| 序号 | 实际事件 |
|---|---|
| 15–17 | T1进入 `process_input_message`，Peek得到两个resize事件，进入 `send_winch_maybe` |
| 18 | T3从 `cons_master_thread` 调用 Read，取走两个resize事件 |
| 19 | T1的 `send_winch_maybe` 返回true |
| 20–21 | T1仍在 `process_input_message` 中，Read清理1个记录，实际取走的是新到的Ctrl-O；随后返回 `input_winch=3` |
| 22–27 | 下一次处理从冒号开始，最终 `read(0)` 交给Vim的内容为92 B，没有Ctrl-O |

原生Read的返回地址由DLL内COFF函数边界独立映射；函数进入/返回记录同时覆盖了这段执行。上述三层丢失假设中，“写入之前或原生输入之前丢失”与“Vim已收到但忽略Ctrl-O”不符合这些红例，丢失发生在已观测的MSYS转换边界。

首批绿例中，T1先实际读走第一个resize事件；另一个线程才移除剩下的resize并写回93个键盘记录。随后MSYS完整返回93 B及退出命令，原写盘、resize文件、InsertLeave/Enter、主屏恢复、回复、退出与回收检查均通过。它是带探针的一次原断言成功，不是修复后的两轮验收。

## 源码解释与未完成项

固定源码的 [`send_winch_maybe`](https://github.com/git-for-windows/msys2-runtime/blob/2e2ef940151485490aff0c7b00c00965ef7a71a4/winsup/cygwin/fhandler/console.cc#L935) 在发送SIGWINCH时临时释放、再取得 `input_mutex`；[`process_input_message`](https://github.com/git-for-windows/msys2-runtime/blob/2e2ef940151485490aff0c7b00c00965ef7a71a4/winsup/cygwin/fhandler/console.cc#L1124) 先Peek并处理旧快照，再按旧数量Read清理。同期 [`cons_master_thread`](https://github.com/git-for-windows/msys2-runtime/blob/2e2ef940151485490aff0c7b00c00965ef7a71a4/winsup/cygwin/fhandler/console.cc#L363) 可以移除resize并写回键盘记录。由此推断旧快照在释放锁期间失效，与实际调用顺序吻合；尚没有单变量修补后的因果红绿，不宣称已完成修复。

这个释放锁的代码来自[避免less信号处理longjmp导致死锁的上游修正](https://github.com/git-for-windows/msys2-runtime/commit/76e1edeb27a573a955ebf3ae5a7a7fd1a75b290f)。后续方案必须同时保留这项约束，不能简单删除解锁，也不能补发Ctrl-O、延长等待、换应用或跳过原断言来结项。可继续准备隔离的最小运行库修补与真实输入回归；外部应用或运行库升级的诊断结果不能自动替换原固定验收目标，正式兼容方案另行提出。

REA provider本身没有绑定目标文件身份、平台或版本，这些字段及限制原样保留；文件哈希、脚本报告的平台、第二批加载模块与入口字节核对是另外的证据。进入附加前已在执行的调用不在覆盖范围内；原 `i`、完整标记和失败命令边界均已捕获。没有追溯归因未保存原生输入的历史失败，也没有推出默认/system、其他MSYS版本、WindTerm或性能结论。

临时C++改动已按基点字节恢复，原工程重建成功，工作树没有产品或测试改动。重建EXE为 `b061934e08561a7bc2e0dc2773412ef3d900e2728267ce1d5c54be5ca6ad8515`，与探针及探针前EXE各自分列，未声称哈希相同或重新完成全量回归。#52、#47和Draft #50继续开放；[native目录复用](vim-fixture-isolation.md)已修的结论保持独立。
