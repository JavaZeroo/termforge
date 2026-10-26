# 一次性 hosted WSL 检查夹具的收尾合同

本合同只适用于 `tools.prepare_hosted_shells` 在指定的 GitHub hosted Windows VM 上运行的 `check-before` / `check-after`。它不修改产品 ConPTY 会话、四种本地 shell 的正常退出、超时或资源验收。

## 退出与身份是不同的事实

[CI 40265785235](https://github.com/JavaZeroo/termforge/actions/runs/40265785235) 中，固定 `Ubuntu` 的 `/bin/sh`、工作目录、唯一 marker 与 shell 退出码 37 均已验证，Python worker 自然退出 0。worker 的自有 Job 仍有两个成员；每个成员均通过精确 Job 归属检查，但 `QueryFullProcessImageNameW` 返回错误 31。一次 `TerminateJobObject` 后 Job 与 PID 清单均为 0，两个保留的进程句柄均已 signaled，退出码为 99。

错误 31 不是 init、pico 或 wslhost 的身份证明。镜像查询失败记录为 `image_query_error`，角色继续为 `unknown`；不得把它改写成“已识别 WSL helper”或“自然退出”。`membership_query_error` 与 `exit_query_error` 分别记录归属、退出观测失败，不再与镜像查询混用。

微软说明 [`init` 是发行版顶层进程，WSL1 由 wslservice 启动](https://github.com/microsoft/WSL/blob/master/doc/docs/technical-documentation/init.md)。[`NtQueryInformationProcess` 的 class 75](https://learn.microsoft.com/windows/win32/api/winternl/nf-winternl-ntqueryinformationprocess)也可提供 Win32/WSL 子系统诊断。本合同不依赖进一步识别镜像或进程职责；这些资料不证明上述两个进程就是 init。

## 显式的夹具政策

采用 `owned-disposable-wsl-check-tree-v1`，依据是该夹具拥有并能回收本次检查树。允许收尾的条件必须同时成立：

- worker 在发送 GO 前已加入本次新建 Job；只有通过真实 hosted、仓库、平台、工作区和管理员守卫的固定 worker 才能运行检查。
- 执行的是固定 `Ubuntu`、固定 shell/输入与隔离目录；marker、目录、发行版和 shell 退出码验证成功，worker 自然退出 0 且父进程已 reap。worker 超时、被强制终止或验证失败不能获得此政策。
- [`JobObjectBasicProcessIdList`](https://learn.microsoft.com/windows/win32/api/winnt/ns-winnt-jobobject_basic_process_id_list)清单完整且稳定，其人数与 Job 的剩余 active 数一致。每个打开的进程句柄都通过 `IsProcessInJob(handle, exact_job)` 核实；归属未知仍失败。
- 只对这个自有 Job 请求一次终止，记录请求数和目标成员数。随后 Job active 和独立 PID 清单均为 0，每个保留句柄均实际 signaled 且退出码可查询；任何一项未知或失败仍失败。
- `check-after` 必须用新的 worker/Job 再次实际启动并验证同一个 Ubuntu。下载成功、清理成功或第一次 marker 均不能替代第二次启动。

这允许角色未知但归属已核实的**夹具成员**被明确强制收尾，不能据此宣称所有成员都是正常驻留 helper。已核实的 wslhost 数量仍单独记录；未知镜像不会增加该数。Job 的 `terminated` 原始 accounting 字段也不作为本次终止人数使用。

不扫描全局进程，不按进程名结束任务，不调用 `wsl --shutdown` / `--terminate` / `--unregister`，不转换或替换已有发行版。raw PID、镜像路径、命令输出仅留本任务 `_private`；公开摘要保留角色、独立查询结果、自然退出和强制收尾的区别。只有真实 CI 两次检查及后续产品回归实际通过，才能继续关闭相关执行项。

正式入口在守卫通过后仅从环境复制 `ImageOS`、`ImageVersion` 和 `GITHUB_RUN_ID` 到 `hosted_provenance`，供宿主镜像复核；不导出完整环境或其它标识。纯测试直接调用 `prepare` 时此字段可为空，不据此生成真实 hosted 来源。
