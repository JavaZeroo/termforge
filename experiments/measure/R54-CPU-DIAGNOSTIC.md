# R54：CPU 计量边界的独立诊断

本项接续 [#54](https://github.com/JavaZeroo/termforge/issues/54) 的首次 hosted 停止事件：原批次第24个位置按 v1 口径得到 5.410% 未归属活动估计，超过原5%环境护栏。原记录没有整个子进程及执行器CPU，无法事后区分自身区间外工作和其他guest活动。该事件、失败状态、[v1合同](R54-COST.md)及两批各112位置均保持；此诊断不补样、不重试旧批次、不判性能通过。

## 预声明问题与执行范围

按以下预测分别记录，而不是先假定护栏误报：

1. 若自身区间外工作有贡献，完整子进程CPU会高于其内部连续测量区间CPU；差值包括启动、准备、oracle、输出和退出，不能仅凭差值细分各阶段。
2. 若执行器有贡献，其同一外部区间的进程CPU会大于零。
3. 扣除前两项后的余额仍可能包含其他guest活动、系统归账粒度和非原子采样边界；余额保留正负，不称为精确后台进程CPU。物理宿主争用和频率仍未知。

固定一次六位置序列为 `.3, .4, .4, .3, .3, .4`，全部使用原 `interactive` 22,000B 语料、chunk=1、observe=0、内部CPU/墙钟至少2秒及原正确性校验。沿用已审计的共同harness与两份无allocator包装EXE，不重新构建。只比较计量范围，**不计算A/B成本比或吞吐变化，也不运行空闲合格筛选**。允许在如实声明的共享宿主研究这些计数；这不把共享宿主升级为成本验收环境。

每个进程90秒期限，超时终止直接子进程并最多等待5秒；退出未确认、计数缺失、输出错误或来源变化立即停止，不补跑。固定核心EXE不派生进程，本工具只支持这个直接子进程边界，不宣称通用进程树计量或回收。诊断状态 `accounting_complete` 只说明六次记录/正确性/来源完整，始终 `performance_gate=not_evaluated`。

## 计数与证据

`GetSystemTimes` 先取全机计数，再取runner的 `process_time`，然后启动并等待直接子进程；退出后通过保留的进程句柄读取 `GetProcessTimes`，再取runner和全机末计数。全机计数只支持1–64逻辑CPU。两个采样端点不是原子操作，读取进程计数本身也有成本；诊断不声称无扰动。

每个观察保留全机原始前后计数、runner前后CPU、子进程创建/退出FILETIME及user/kernel/总CPU、原始stdout/stderr、内层区间和以下余额：

- `inner_only_balance = guest_busy - inner_cpu`：本次插桩区间的内层单项扣除，不冒充旧批次的原值。
- `child_outside_inner = child_lifetime_cpu - inner_cpu`。
- `unassigned_balance = guest_busy - child_lifetime_cpu - runner_cpu`：保留负值，不据此修正成本或设新护栏。

Windows进程对象在外部仍持有句柄时可于退出后查询，依据 [GetProcessTimes](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocesstimes) 和[进程终止语义](https://learn.microsoft.com/en-us/windows/win32/procthread/terminating-a-process)。诊断限定Windows CPython3.12，通过Popen持有的句柄避免退出后按PID查询；该私有接口依赖及句柄生命周期按[CPython3.12.10源码](https://github.com/python/cpython/blob/v3.12.10/Lib/subprocess.py)核对，不能静默扩展到其他runtime。

入口要求已提交且干净的工具源码、显式环境声明、原计划及完整构建包，运行前后复核全部来源、编译产物及实际`--identity`。输出目录必须不存在；过程、失败和中断记录保留。原始本地路径和PID留local，公开时单独审核白名单、路径脱敏和原字节流哈希，不递归上传工作目录。

```powershell
python -m experiments.measure.r54_cpu_diagnostic `
  --plan <原plan-v1.json> --build-bundle <原构建包> `
  --environment <含host_kind与known_concurrent_work的JSON> --out <全新目录>
```

本次结果只回答声明宿主上的计量问题，不能补造旧hosted事件的原因。是否改变后续环境测量方法须先依据诊断另定协议，不能倒改原v1或用新口径将旧失败改绿。完整成本批次、allocator/projection及组件归因、数值预算提案与用户确认仍由 #54 接续。
