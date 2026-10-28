# R2 隔离 Windows 11 显示测试环境

[#131](https://github.com/JavaZeroo/termforge/issues/131) 的环境基础已建立：独立 Windows 11 x64 guest 可通过 guest agent 执行命令、读写合成文件，并在交互会话运行可回收的窗口探针。两个实际 Windows 显示输出分别为 96、120 DPI，独立窗口两次完成 96→120→96 往返。这里记录的是测试环境；[#31](https://github.com/JavaZeroo/termforge/issues/31) 的正式产品与实际呈现验收继续保留。

脱敏原始读值见 [JSON](data/r2-win11-display-environment-20261029.json)，采集时间为 2026-10-29 01:00 左右（Asia/Shanghai）。没有提交 ISO、驱动二进制、无人值守配置、口令、原始 XML、截图或宿主私有清单。

## 环境身份与来源

用户已明确授权在提供的宿主创建隔离 VM。创建前检查 VM 名称和独占目录均不存在，UUID、目录、账号、图形 socket 及回收入口留在私有 manifest/交接。只操作这个新 VM，未启停修改已有 guest、宿主显示、默认网络、DNS、路由或防火墙。大文件位于现有数据池，未扩容或清理宿主磁盘。

| 项目 | 实际配置或读值 |
|---|---|
| 虚拟化与资源 | KVM/QEMU、2 vCPU、6144 MiB、64 GiB thin disk；共享宿主，不是性能夹具 |
| 启动 | q35、Microsoft-enrolled UEFI、TPM2；guest `Confirm-SecureBootUEFI=true`，TPM Spec `2.0, 0, 1.64` |
| Windows | CIM `Microsoft Windows 11 企业评估版`，`10.0.26300` / UBR `9457`、`26H2`、`EnterpriseEval`、x64 |
| 图形与网络 | 两个独立 QXL 设备；SPICE 仅 Unix socket；复用已有 NAT，不向 LAN 开放图形或 RDP |
| 驱动 | 签名 VirtIO Serial `100.103.104.30200`；两个签名 Red Hat QXL `10.0.0.21000`，均由 guest WMI 实际读取 |

Windows 来自[微软 Enterprise Evaluation 下载页](https://www.microsoft.com/en-us/evalcenter/download-windows-11-enterprise)的公开中文版 x64 链接。完整 8,548,628,480 字节 ISO 的 SHA256 为 `b37a06d5121eee017826c0b133250bbd21bec694c2d607c60edd29090e667a84`，与[微软对应 26H2 Enterprise Eval ZH-CN 校验页](https://support.microsoft.com/en-us/servicing/os/windows/docs/2026/09/verify-the-authenticity-of-a-windows-11-enterprise-evaluation-iso-file)一致。安装前另用 WIM 元数据确认唯一 index 1、`EnterpriseEval`、x64 architecture 9、build 26300；通过正常无人值守部署创建测试账号，未绕过 TPM、Secure Boot 或软件授权。

驱动来自[官方 virtio-win 项目](https://github.com/virtio-win/virtio-win-pkg-scripts)指向的固定 `virtio-win-0.1.302-1/virtio-win-0.1.302.iso`。完整 877,373,440 字节 ISO 的实际 SHA256 为 `303f7ae40dad495d6ae474fdc571df58958a4dbc5c37a522d80f9a203867949d`。没有找到独立发布方校验值，不把自算 SHA 说成发布方比对成功。使用 `vioserial/w11/amd64`、`qxldod/w10/amd64` 与同 ISO guest-tools bundle；bundle 退出 0。两 QXL 的选择参考 [SPICE Windows 多显示器说明](https://www.spice-space.org/multiple-monitors.html)，能力仍由实际 guest 读值确认。

QGA 报告的 `pretty-name` 保留旧的 `Windows 10 Enterprise Evaluation` 字符串；同时 `version-id=11`、kernel release 26300、CIM Caption 与注册表 DisplayVersion 均另行核实，未靠这个旧名称判定系统身份。

## 自动控制与真实显示读值

guest agent 的 PowerShell 命令在 session 0 退出 0，合成目录写读一致。另将 65,792 字节合成二进制经 guest-file 写入再读回，逐字节一致，SHA256 为 `120c518a83325c66464701a6ee080302f332bc768ea3f60473b209f1bfb091df`。guest 文件 API 只开放自有测试目录。

独立测试账号的交互计划任务在实际 session 2 执行请求。每次窗口探针结束后保留自身进程句柄，通过原生 `WaitForSingleObject=0` 与 `GetExitCodeProcess=0` 证明根进程退出；这不是产品 ConPTY 或任意子树的清理验收。队列、错误和身份记录留在自有目录，常驻 runner 是后续测试入口。原始显示快照只证明正常请求完成；挂起请求的执行时限由下述独立协议和失败场景补充，不能从宿主轮询超时推导。

缩放通过 **guest 自身 Windows Display Settings 的正常 UI Automation 控件**设置：一个输出保持 100%，另一个选择界面实际提供的 125%。未注入受控 DPI 消息，也没有修改个人显示设置。设置请求没有被计为成功；后续新窗口另行读到：

| Windows 输出 | 实际物理矩形 | `GetDpiForMonitor` | 跨屏 `GetDpiForWindow` |
|---|---|---|---|
| `DISPLAY7`，主屏 | `(0,0)–(1024,768)` | 96×96 | 96 |
| `DISPLAY5` | `(1024,0)–(2048,768)` | 120×120 | 120 |

两轮均为 **96→120→96**。每个样本另用 `MonitorFromWindow` / `GetMonitorInfo` 核对实际输出，用 `GetWindowRect` 记录真实矩形，用 `IsWindowVisible` 核对窗口可见标志；全部目标匹配、原生可见，探针进程自然退出 0。没有启动 SPICE 客户端去请求或模拟显示数量。

可复现读值的独立[探针源码](../../tools/probe_windows_display_environment.ps1) SHA256 为 `307122b2c9d8cb12bbc0a54cebb7e95078430874b2e2ea7b676f5c4df80bda80`。在已获授权的隔离 Windows 交互会话执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy RemoteSigned -File tools/probe_windows_display_environment.ps1
```

探针只移动并关闭自身 WinForms 窗口，输出实际 API 读值；不设置系统缩放、监视器或产品状态。直接运行探针没有附带私有队列的进程句柄回收记录，也不能据 `distinct_actual_dpi` 一项判断 #31 通过。

## 请求执行时限与自有 Job

原队列的 `Child.WaitForExit()` 无界，宿主停止轮询仍可能让请求永久堵住队列。`termforge-owned-vm-request-v2` 改为 guest 内执行时限；新的[独立原始读值](data/r2-win11-request-deadline-20261029.json)不修改前面的两轮 DPI 成功数据或旧失败原件。

[队列](../../tools/run_vm_request_queue.ps1)验证每请求的协议、源码 SHA256 和 1–300 秒整数执行时限，再调用[原生监督器](../../tools/owned_vm_request.cs)。控制端默认 30 秒执行时限；宿主轮询时限另算，不作为 guest 执行或回收证明。每次请求都以 `CREATE_SUSPENDED` 创建根进程，验证保留句柄的 PID、创建时间、实际镜像与新建无名 Job 的归属后，才 `ResumeThread`。Job 的 kill-on-close 和禁止 breakaway 标志读回核实，仅三个标准流句柄允许继承。机制参考微软 [Job Objects](https://learn.microsoft.com/en-us/windows/win32/procthread/job-objects) 与 [Process Creation Flags](https://learn.microsoft.com/en-us/windows/win32/procthread/process-creation-flags)。

原生 wait 的执行预算覆盖根进程和仍活动的普通继承子树；根先退出时，只在原执行预算内等待剩余成员。到期枚举这个 Job 的完整、稳定成员清单，保留每个进程句柄并复核精确 Job 归属，再仅终止这个 Job。清理共享一个最多 5 秒的预算，独立核实根和已观测成员句柄 signaled、退出码与 Job active=0。无法证明清理时记录 `cleanup_failed` 并隔离队列，不能继续派发；runner 退出时 kill-on-close 是额外回收后盾，不被当作已观测退出。

实测失败路径与紧接的正常请求使用同一个存活的 session 2 runner：

| 场景 | 执行时限与结果 | 自有进程与队列 |
|---|---|---|
| [Sleep 夹具](../../tools/fixtures/vm_request_sleep.ps1)：根与子 PowerShell 均 Sleep 60 秒 | 3000 ms 原生时限，实测到期 `timed_out`；控制端退出 124，不能算成功 | 实际 4 个 Job 成员（含两个 console host），全部精确归属、保留句柄 wait=0 / exit=124；active 4→0 |
| 紧接的[正常夹具](../../tools/fixtures/vm_request_after_timeout.ps1) | 同为 3000 ms 执行预算，输出 `ok=true`，自然退出 0 | 未请求强制终止，Job active=0，根句柄 wait=0 / exit=0；队列继续，pending/in-flight 均 0 |

超时夹具只能通过队列执行，不能脱离监督器直接运行。请求必须自包含；只支持普通 `CreateProcess` 继承子树，不支持 fire-and-forget、WMI `Win32_Process.Create`、COM/ShellExecute、服务或计划任务等外部代理激活。原始 QGA `exec` 仍是管理控制传输，其宿主轮询超时也不是任意工作负载的执行保证；测试工作负载走上述队列。监督器的 `passed` 只表示请求根退出 0 且自有 Job 已清理，功能断言仍由请求承担，不自动证明 Qt 或产品通过。

首个超时实验曾正确记录 `cleanup_failed`：Job active 已为 0，但一个 console host 的保留句柄尚未立即 signaled。队列因此隔离，没有冒充清理成功。核实原成员已不活动后，在原有 5 秒清理预算内等待各保留句柄；最终新实验全部 signaled，执行时限仍为 3 秒。初次失败原件与最终成功回收记录分开保留。本次没有运行 Qt、安装 CRT 或重跑 DPI 两轮。

## 失败记录与边界

- 首轮 bundle 已装好驱动和 QGA，随后重复 `pnputil vioserial` 返回 259，严格脚本因此中止。原始失败留存；核实已装签名 QXL、运行中的 QGA 后，只补齐交互计划任务，没有把 259 改写成驱动安装成功。
- 首轮 `.NET Process.ExitCode` 为 null，不计进程回收通过。最终 runner 保留进程句柄，改用原生 wait 与 exit code 后重跑。
- 加强原生可见性查询时，首个窗口因隐藏 worker 的启动设置实际为 hidden。该失败留存；最终探针显式 `ShowWindow`，再独立核对 `IsWindowVisible`，两轮全部通过。`WS_VISIBLE` 仍不等于用户实际看见或物理呈现。
- 本环境基础采集期间未传入或执行 Qt。Qt 前快照中，System32 的 `vcruntime140.dll`、`vcruntime140_1.dll`、`msvcp140.dll`、`msvcp140_1.dll`、`msvcp140_2.dll` 均不存在，VC++ 卸载产品列表为空。本次未安装 CRT；不能据本环境基础宣称开发包首次加载可用。

`presented_done=null`。本结果没有覆盖正式 C++/Qt/fixed-ConPTY 的字体、可见/隐藏会话、子进程尺寸、焦点、IME、Find、rename、鼠标命中或实际呈现，也不转移到旧 Win32、Alacritty 或其他构建。没有测量性能等价或设置任何性能容差；R2 整体与 #31 均保持未验收。
