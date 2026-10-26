# Inventory 纯夹具超时阶段观测（#40）

## 已知与未知

[原 CI 40244057401](https://github.com/JavaZeroo/termforge/actions/runs/40244057401) 的 Windows Server 2025 `ime_inventory` 在 `WorkerContract.test_failed_final_progress_cannot_return_complete` 抛出 `TimeoutExpired`，保留原 10 秒。该作业镜像为 `windows-2025-vs2026 / 20261021.278.1`，整套件 45.07 秒。原日志没有子进程启动、AST 或 worker 失败路径的阶段原件，不能据后续快速通过判断原先卡住的调用点。

`b9abcab` 已为纯夹具增加 GO 前 OwnedJob、启动就绪及最终 AST/进度结果。但结果由最后一条 stdout JSON 返回；若在 GO 后、最终 JSON 前超时，`outcome=null`，此前的 AST/进度位置仍会丢失。这里补的是这个观测缺口，**没有确定原 CI 超时的根因，#40 继续开放**。

## 合同

只执行 AST 中选定的定义及 `powershell` 操作边界，系统 inventory worker 的环境守卫、IME、DISM、语言准备和输入桌面均不执行。worker 产品代码未修改。

- 原 10 秒总期限不变，包含 Python 启动 PowerShell 到完成；清理另有原有 5 秒期限。
- 仍先将自有 PowerShell 纳入 kill-on-close Job 再释放 GO。
- 子进程以 CLR 文件 API 写固定阶段：就绪、GO、AST 前后、worker 进入、`started/ended/failed` 回调及 worker 返回。写入不加载模块、不查询环境。
- 父进程清理后最多读取 4097 字节探测上限，只公开不超过 4096 字节、32 行的 ASCII 白名单阶段和非递减毫秒数。未知阶段、非法数值、截断、超限或读取失败不公开原文，保留 `missing/unavailable`。
- 阶段表示已观察到边界，不能证明下一条具体调用正阻塞。子进程 stopwatch 与父进程启动计时各有起点，不能混为同一时间线。
- 超时继续抛原 `TimeoutExpired`；观察和回收成功不转成业务通过，也不补造尚未发生的 `failed`、最终 JSON 或 `complete`。

## 验证

[脱敏结果](../../research/ime/results/2026-10-26-inventory-timeout-checkpoints/validation.json) 记录固定源码哈希和以下对照：

1. 正常纯 AST 三个 fresh 子进程均返回 `status=failed`、`started/ended/failed`，阶段完整；退出 0 是抽取 AST 夹具的进程结果，不能当成 worker 业务成功。进程已 reap、stdout/stderr 已 drain、自有 Job 0。
2. 在实际同一 Try AST 的 `ended` 回调中受控挂起，仍按原 10 秒超时，最后阶段为 `progress-ended`；`outcome=null`，没有伪造 `progress-failed`。清理后进程 reap、流 drain、Job 0。
3. 相同带阶段记录的纯 AST 生产端换回 `65988f5` 的原父 runner，同样超时；已回收临时文件确实存在 `progress-ended`，旧父记录却不包含阶段观察。这是观测缺口的反事实，不是原 CI 原因的复现。
4. 原完整 `ime_inventory` 命令 54/54 通过；额外反例覆盖丢失、权限错误、截断、非 ASCII、未知阶段、时间倒退、格式和大小上限。原 GO 前超时仍保留 `missing`，不能误报已进入 AST。

```powershell
python -m unittest research.ime.test_hosted_inventory research.ime.test_prepare_hosted research.ime.test_product_ime -v
```

这些结果不建立启动性能、永久无超时、真实 IME 或完整 R2 验收。下一次 hosted 失败若留下阶段，可据此构造更窄的复现，再决定产品或测试修复；历史没有的阶段仍为未知。
