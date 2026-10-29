# #160 owned desktop 有界诊断准备

状态：提案，`execution_authorized=false`，实际编译 EXE=null，实际两 case 执行均为0。本切片只准备独立入口，不修正产品或现有监督器，不注册 CTest、CI 或默认运行项。#160/#38/R2 仍开放；后续执行须由 root 另发租约并冻结实际源码、构建、环境、二进制和新输出目录。

## 固定失败与问题

固定源 `01a979da926c05c8e6c34e3f5e5ed7b8d6916de1` 的 PR157/default job123121553707：原 watchdog 3秒到期，`timed_out=true/exit99/watchdog_ready=true`，Job5/6/0→0/6/0；父 creator handle 关闭成功后首次 OpenDesktop 返回 handle，`desktop_gone=false`。它只证明那个时点可打开；没有后续采样或持有者证据，不能称永久泄漏。

原 archive 4794486B/SHA256 `05392812a79a0b38d48384b47905c85c9f46285cbedcf0b4afa2275550c9efbb`；LastTest 167717B/SHA256 `02830475404963817d9aaa240e1f859f9aff322101b6477a6fc1854110507afd`；shell watchdog sample 548B/SHA256 `a058824d202f7ad8bbc16975028b1fd14d162dd574dd820593210358b082682b`。原 archive、sample、路径、PID、desktop/window/thread identity 都仅私有保存。本项不混入同 archive 中独立 ConPTY watchdog 的结果。

现有 `desktop_gone` 在成功 OpenDesktop 后没有检查 CloseDesktop 返回。该盲点会影响后续观察的自持引用排除，但发生在首次 Open 已成功之后，不能据此解释首次成功。新入口只在诊断本身检查每次关闭，不修改原函数或原 oracle。

## 两 case，均最多一次

1. `retained-second-handle`：复用 OwnedTestDesktop 创建自有 non-input desktop，再 Open 一个明确记账的 second handle。关闭 creator，保留首次存在 probe 的 false；确认 extra handle 尚持有时可打开。检查关闭 extra 后，以其 CloseDesktop 完成时点为 anchor，在0/50/250/1000ms各作一次独立存在观察。无子进程，Job/root 不适用。
2. `original-watchdog`：仍调用原 `execute(exe, "watchdog", out, timeout=3)`，保持真实 launcher/shell 树、GO、原 communicate/Job drain/异常清理期限与输出 oracle。诊断进程内部临时替换该调用使用的 Desktop/Job/Popen 观察 seam，结束即恢复；不编辑原 `run.py` 或 `OwnedJob`。Job assign 使用原 Popen handle 的非继承 duplicate，核对 PID/image/creation、精确 Job 成员及 GO 前1/1/0；所有关闭只操作本次自有 handle。

第二 case 的 creator close 与首次 Open 位于原执行边界，首 probe不插入窗口枚举。其完成后（存在时包含已检查的 probe Close）为 followup anchor，再在0/50/250/1000ms各观察一次。原 execute 的 finally 在返回前继续运行，故实际采样可能晚于目标时点；每个目标/实际 offset、API调用与关闭时间分列，不补采、不重排、不假称精确调度。每个 followup 即使之前已 absent 也独立记录；不是重试到绿。

首 `desktop_gone`、原 sample/status/error 永久保留。后续 gone 不覆盖首 false，不写产品通过或根因。若原 watchdog 不满足3秒 timeout、99、唯一 readiness JSON、空 stderr、deadline Job active≥2、最终 Job0及原清理，则停止，不作 followup。

## 观察与停止

原生 API 在入口真正获准执行后才装载/调用。记录 creator/probe/control/Job/duplicate-process 的 owned handle ledger、每次 Close 返回/错误；Popen 原 handle 单独注明所有权，不由 observer 关闭。记录保留根 handle 的 signaled 观察时点、GetProcessTimes 实际 exit FILETIME、Job accounting/terminate/drain/close、creator close、首次 Open。FILETIME和monotonic是不同时间域。

存在的 followup 仅对该自有 desktop 调用 EnumDesktopWindows，上限32个顶层窗口；仅查询调用者线程和这些窗口已知线程的 GetThreadDesktop。所得借用 handle 只读，不 CloseDesktop；不扫描其他 desktop/进程句柄或全局线程，不切换 desktop、不输入、不调用服务或个人设置。

OpenDesktop 只有原协议的 error2/3判为该时点 absent；其他错误、任意 Close 失败、枚举/已知线程查询错误、超上限、源/环境/文件 identity 漂移或已有输出目录均停止。失败 Close 不重试；原清理仍限原 gated root/精确 Job，自有剩余 handle 每个只关闭一次。第一 case 机制失败则不执行第二 case。不全局 kill、不重发 GO、不加长原3秒、不自动重跑。

最初 template `experiments/shell/watchdog-desktop-plan.json` 的 false/null 原件与 `a95166e` 身份保持不变；当前入口要求 v2 合同，后续使用新 `experiments/shell/watchdog-desktop-plan-r1.json`。root 另复制到私有新计划，填实际 HEAD、完整 sys.version/platform.version、六份源 SHA、实际 shell EXE、完整运行目录 inventory（相对路径→bytes/SHA）、构建证据文件与旧 archive identity；所有输入验证成功后才建 attempt/output。runtime inventory 应置于运行目录之外。执行命令（**本次未运行**）：

```powershell
python -B -X utf8 experiments/shell/diagnose_watchdog_desktop.py --plan <absolute-private-authorized-plan.json>
```

`batch.json/diagnostic_complete` 和 CLI0只表示两组固定观察机械完成，`product_acceptance/root_cause` 始终null；first false 或未复现均不构成产品修复。单次原监督器已有有限等待、followup最多1秒；Win32同步API/文件I/O没有新强制中断，whole lifecycle bound=null。root执行租约须另给外层监督，不能把后续四个时间点或旧分段期限合称整生命周期硬上限。

## 能区分与仍未知

显式 second handle 对照能验证自持引用确实影响“可重新打开”；ledger可排查本诊断自己的 creator/probe/control 未关闭。已退出 root、精确 Job0与后续 gone 只能描述生命周期顺序；不能独自区分异步内核释放与未观察的外部/无窗口持有者。EnumDesktopWindows 无窗口也不证明无引用，不把 Job0当 desktop 删除；保留 borrowed thread association 的观测限制。任何 root cause 或生产修复均须后续同类 red 和独立证据。

API依据：[CloseDesktop](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-closedesktop)只关闭给定 open handle且不接受GetThreadDesktop结果；[GetThreadDesktop](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getthreaddesktop)返回线程关联的借用handle；[EnumDesktopWindows](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-enumdesktopwindows)只枚举指定desktop的顶层窗口。

## 执行前 P2 修正与纯 seam 证据

`a95166e` 原准备源有两项审查缺口：诊断 `Job.close` 的 observer OSError 可能越过原 execute finally 的 sample写出；新输出可放入冻结 runtime，且批次完成前没有再次复核冻结输入。当前仅修诊断，不改原五份生产/监督源或任何原件。Job close先保留完整失败ledger并延迟observer错误，原 `sample.json` 写出且复制原 stdout/stderr/status/error 后，再失败停止；未成功 Close 不重试，也不将其记为 closed。原首probe、3秒watchdog、四个followup及两case各一次不变。

输出目录按 resolve后的路径比较，拒绝等于runtime及其任何子目录/别名；允许合法新 sibling目录。计划原字节、六源、EXE、完整runtime文件集合、inventory、build证据、原archive和HEAD在创建attempt前及batch完成前均复核；后验漂移或多出runtime文件会令 `diagnostic_complete=false`，错误私有保留。

新增 `test_watchdog_desktop_seams.py` 只AST提取实际校验函数、Native.close、嵌套Job.close和原run.py的execute；不import诊断模块、不执行诊断main，不调用Win32/desktop/target。合成CloseHandle失败在原finally真实路径重现旧样本未写；旧a951的5项seam为2过/3失败（另两红为runtime子目录和`existing/..`别名误放）。修后普通与`-O`各10项通过，其中后验覆盖script/config/source/EXE/inventory/build/archive七种文件漂移、额外runtime成员和HEAD漂移；测试名中的main wiring只静态核对AST调用顺序，未执行main。日志/exit/hash留 `local/watchdog-desktop-p2/`，纯mock不是原生诊断结果。实际编译/desktop/目标执行仍0，实际EXE仍null，root两轴复审及执行租约仍待后续。
