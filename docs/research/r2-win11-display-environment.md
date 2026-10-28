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

独立测试账号的交互计划任务在实际 session 2 执行请求。每次窗口探针结束后保留自身进程句柄，通过原生 `WaitForSingleObject=0` 与 `GetExitCodeProcess=0` 证明根进程退出；这不是产品 ConPTY 或任意子树的清理验收。队列、超时、错误和身份记录留在自有目录，常驻 runner 是后续测试入口。

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

## 失败记录与边界

- 首轮 bundle 已装好驱动和 QGA，随后重复 `pnputil vioserial` 返回 259，严格脚本因此中止。原始失败留存；核实已装签名 QXL、运行中的 QGA 后，只补齐交互计划任务，没有把 259 改写成驱动安装成功。
- 首轮 `.NET Process.ExitCode` 为 null，不计进程回收通过。最终 runner 保留进程句柄，改用原生 wait 与 exit code 后重跑。
- 加强原生可见性查询时，首个窗口因隐藏 worker 的启动设置实际为 hidden。该失败留存；最终探针显式 `ShowWindow`，再独立核对 `IsWindowVisible`，两轮全部通过。`WS_VISIBLE` 仍不等于用户实际看见或物理呈现。
- Qt 尚未传入或执行。Qt 前快照中，System32 的 `vcruntime140.dll`、`vcruntime140_1.dll`、`msvcp140.dll`、`msvcp140_1.dll`、`msvcp140_2.dll` 均不存在，VC++ 卸载产品列表为空。没有安装 CRT；不能据本环境基础宣称开发包首次加载可用。

`presented_done=null`。本结果没有覆盖正式 C++/Qt/fixed-ConPTY 的字体、可见/隐藏会话、子进程尺寸、焦点、IME、Find、rename、鼠标命中或实际呈现，也不转移到旧 Win32、Alacritty 或其他构建。没有测量性能等价或设置任何性能容差；R2 整体与 #31 均保持未验收。
