# hosted shell 准备的子命令观测边界

[#81](https://github.com/JavaZeroo/termforge/issues/81) 保存 [PR #76 的 preflight 失败](https://github.com/JavaZeroo/termforge/actions/runs/40366292708/job/121724790152)：`command_deadline`，产品测试未运行。原摘要只知道 worker 退出 1、Job active0/total6/terminated0，不能识别 cmd、PowerShell 5.1、PowerShell 7 或 wsl-list 中哪条命令超时，也不能从进程总数推定。

准备程序现在在实际 `run_command` 调用处写一个有界原子记录，父进程确认 worker 已回收后才读取，并把通过严格白名单验证的字段放入公开摘要。原命令参数、工作目录、stdout/stderr原文、环境和异常文本仍保留在私有目录，不进入记录。

| 阶段 | 能证明什么 | 不能证明什么 |
|---|---|---|
| `invoking` | 即将进入 `subprocess.run`，固定命令 ID 已明确 | OS 已经创建子进程、命令已成功启动 |
| `returned` | `subprocess.run` 返回及其实际 returncode；记录该调用的单调时钟耗时 | shell 标记正确、WSL 已可用、整个准备或 Job 清理通过 |
| `timed_out` | 实际调用抛出 `TimeoutExpired`；退出码保持 null | 子进程自然退出，或已知其最终退出码 |

记录只允许各 step 的固定命令顺序，最多 8 个边界、8192 B；未知字段、枚举、类型、乱序或过大数据整体不公开。`observed` 可以只含最后一条 `invoking`，不表示命令完成。缺记录、无子命令的 download、无效记录及 worker 尚未回收分别报告；这些诊断状态不替代原准备结果与清理判据。任一边界写入失败只留下缺失或部分记录，实际命令仍按原返回码、输出大小和 marker 判定；超时仍报告 `command_deadline`。

单命令 10 秒、preflight 45 秒、import 80/90 秒及 startup 20/25 秒等既有预算不变；不增加 sleep、重试或跳过。hosted、工作目录与自有 Job 保护保持原位，不在个人 Windows 环境运行准备流程或改动 WSL。

验证使用现有 `run_command` 和 worker→parent 摘要入口：模拟超时后，旧实现因缺记录出现红例；新实现保留固定命令身份。故障注入另证明记录失败不能覆盖原超时；开始/返回记录写失败与命令返回 0/7 的四种组合也分别保留真实结果及 marker 判定。外层 worker 期限留下的部分记录、退出/回收区分、未知数据不泄漏均有回归。准备与清理两模块共 23 项通过，其中自有 Job API 用真实隔离 Python 子树，其余受控过程使用模拟，未据此声称重现原 hosted 超时。

```powershell
python -m unittest tools.test_prepare_hosted_shells tools.test_owned_job_cleanup -v
```

原 artifact 与本次代码/验证身份见[限定记录](../../experiments/shell/results/2026-10-27-hosted-command-boundaries/validation.json)。后续 hosted 运行继续沿用原 workflow；即使不再超时，也只证明该次运行通过，#81 原根因仍待定位。#80 的四 shell 交接与此准备阶段没有同因证据。

## 收尾后的输出事实

[#158](https://github.com/JavaZeroo/termforge/issues/158)增加可选的 `command_output_observation`，保留原schema及 `command_observation`。它在worker已reap、Job审计/membership稳定且最终active/PID计数均明确为0后，按已验证的invoking命令前缀读取固定私有文件。条件不足时为 `writers_unverified`；未知step或trace不能选择任意文件。无命令的download为 `not_applicable`。

每流最多读取65537B，只在完整、稳定、普通文件且不超过65536B时发布可信字节数。缺失、超限、读错和读取期间变化分别报告 `not_observed/oversize/unreadable/unstable`，字节数为null；不存在的文件不等于空输出。

stdout还可有固定marker的 `marker_status` 和 `marker_complete_line_seen`：

| 观测 | 字段含义 |
|---|---|
| `observed`、true/false | 有界完整流中是否包含固定ASCII marker的完整LF/CRLF行；仅去除流开头UTF8 BOM，不接受子串或未结束片段 |
| `unknown`、null | 缺失、不稳定、超限或读取失败，不能判定 |
| `not_applicable`、null | list/import等命令没有固定marker定义，不猜测标记 |

其他输出不解码/公开，stderr仅有状态和数量。即使流含marker和额外私密文字，也只导出计数/布尔；原严格marker oracle仍拒绝额外内容。读/解析/报告异常只能降低该诊断的完整性，不能覆盖原成功、非0、command/worker期限或cleanup失败。新增读取在既有worker/Job收尾后进行，不延长原命令或worker等待预算；没有据此建立整程硬上限。

完整marker只证明最终捕获流里存在该行，不证明它在10秒截止前写出；未见marker也不能证明进程尚未创建或PowerShell引擎未启动。`subprocess.run`的超时覆盖调用/等待及超时收尾边界，不能把整段耗时直接称“启动耗时”。

首次真实文件seam红例为 `39921c1`：fake subprocess向传入stdout写完整PS51 marker再抛TimeoutExpired，父进程原判定保持失败但旧实现无输出事实。`d63be02`补齐观测；`87deb48`的普通/优化解释器相关纯/mock回归均32/32、0skip，覆盖10项新文件/父进程边界。未运行真实OwnJobApi进程测试、hosted setup、PS/WSL或其他目标；[脱敏身份与原件守卫](../../experiments/shell/results/2026-10-29-hosted-output-boundary/validation.json)保留来源。

PR150/153旧CI的8份summary/ZIP/job日志前后未变，原artifact没有私有输出，marker仍未知。本切片没有新真实deadline观测，不定位或关闭#81旧根因，也不重跑旧job。后续实际hosted观测由既有CI自然产生，并在责任issue独立记录。
