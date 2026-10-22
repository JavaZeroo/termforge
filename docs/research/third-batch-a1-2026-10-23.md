# 第三批：A1 固定网格本地 shell

日期：2026-10-23，实施基点 `a559541`。范围是本地交互闭环，不含 C5 resize、完整 G1、日常开发版或 WindTerm 性能对等。

## 交付与运行

入口 `termforge_shell.exe` 是 Windows GUI 程序，启动真实系统 cmd，经过 ConPTY、双向有界队列、Recovery 和只读快照绘制到窗口。支持普通输入、中文字符消息、退格、左右方向键、Ctrl-C 和退出。普通使用不向 stdout 输出屏幕诊断，不自动保存用户会话。

当前本机构建可直接运行 `D:\Code\termforge\build\a1\bin\Release\termforge_shell.exe`；其他环境按[构建步骤](../../experiments/shell/README.md)生成自己的二进制。固定 80×24、100 行内部历史，无历史浏览、IME 声明、复制粘贴、备用屏、SSH 或安装包。

复用 V1 的原创建模/绘制代码为 `render.hpp`，布局缓存至多 1,024 项。ConPTY `Session` 新增可选流消费入口，默认 T1 捕获/受控子进程契约不变。核心仍无 Windows API。UI 独占解析器、快照和窗口；读写线程不访问屏幕网格。

## 验证

本机环境：Windows 11 build 26200、i7-9700、MSVC 19.42.34433、CMake 3.31.0、Python 3.12.10，x64 Release。

- 新增 5 项 shell 测试，包括正常/输出中关闭各三次、真实 shell 的监督器超时、队列容量/背压/顺序/停止、错误参数。
- 普通命令、中文、退格、方向键编辑均检查独立完整输出行，避免将输入回显误认作执行成功。连续输出至少一秒且已消费 64 KiB 后通过队列化 WM_CHAR 发送 Ctrl-C，随后 `echo TF_AFTER`，检查状态与帧提交，再 `exit 37`。
- 每次检查内层会话及外层监督 Job 的活跃进程归零，自有 desktop 已消失。正常退出保留 37；输出中关闭通过 Job 取消。没有终止其他应用的进程。
- 原有核心、恢复、差分、计量与 ConPTY 回归通过；旧 6 类差异保持精确匹配。ASCII/Unicode 各 17 次完整状态回放通过。
- 首次全套回归发现 V1 桌面退出失败；修复后两个窗口套件定向重测通过。最终本机 11 套共 131 项通过（含定向重测），远端干净构建由[根 CI](../../.github/workflows/build.yml)执行。

## 窗口退出问题

第一次失败为 `private desktop detach failed`，改进诊断并提前释放显式绘制资源后仍在重复实验中复现 `ERROR_BUSY (170)`。不能将“释放显式对象”当作问题已解决。

[SetThreadDesktop 文档](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setthreaddesktop)明确线程仍有窗口或 hook 时会失败；本轮未逐项确定哪一个系统资源持有附着。最终让私有 desktop 的 GUI 工作在独立线程中运行，线程退出后由未附着的父线程关闭 desktop，不再要求活跃 GUI 线程切回原 desktop。

修复后 GDI/DirectWrite × 96/144 逻辑缩放重复五轮，共 20 次和超时清理通过。[该轮身份](../../experiments/shell/results/2026-10-23/window-regression/manifest.json)、[原始样本](../../experiments/shell/results/2026-10-23/window-regression/samples.jsonl)。随后 runner 仅增强了非零退出的错误信息，本轮样本仍保留运行时原始 runner 哈希。最终窗口套件也通过定向重测。历史 V1 结果未改写。

## 有界调度与前后观察

最初 UI 每 16 ms 仅消费 8 KiB，合成洪流会接近 256 KiB 输出队列上限。修正为每个 tick 以 8 KiB 小块消费，最多 64 KiB，块间检查 4 ms 预算，然后只发布一次快照；队列满时读线程背压，输入线程独立。单次解析/快照/绘制仍可能超过时间预算，不能声称硬实时。

两个版本都运行三次正常场景、三次输出中关闭和一次监督器；没有丢弃正常输出来换取结果。前后是同一脚本场景的机制观察，不是控制所有环境变量的正式基准。实际输出量随运行时序变化，未统一字节总数，也没有采样物理键盘或合成器上屏。

| 版本 | 原始数据 | 三次正常场景观察 |
|---|---|---|
| 初始 8 KiB/tick | [身份](../../experiments/shell/results/2026-10-23/manifest.json)、[7 场景](../../experiments/shell/results/2026-10-23/samples.jsonl) | Ctrl-C 到后续标记状态 670.007–816.838 ms；帧提交 680.149–824.466 ms；队列高水位 214,831–262,143 字节 |
| 有界批量消费 | [身份](../../experiments/shell/results/2026-10-23-budgeted/manifest.json)、[7 场景](../../experiments/shell/results/2026-10-23-budgeted/samples.jsonl)、[自有画面](../../experiments/shell/results/2026-10-23-budgeted/frame.png) | 同一端点状态 14.6096–28.5255 ms；帧提交 21.5379–36.2011 ms；队列高水位 22,351–34,861 字节 |

上述时长从**脚本 Ctrl-C 消息实际被处理**到后续 `TF_AFTER`，包含中断、另一条命令、调度和输出，不是单键延迟。提交仅到 GDI BitBlt/Flush；`presented_done=null`。样本很少，不能发布 P95/P99、跨终端速度排名或 WindTerm 差距；它们支持继续检查队列积压这一机制。BMP 仅来自本任务窗口的 retained DIB，PNG 只做无损格式转换并经过实际查看。

所有公开记录只含原创合成命令和输出，未包含用户会话、系统字体文件或 WindTerm 私有资源。未成功的早期运行保留在 `local/experiments/a1-first`、`a1-render-shared` 与 CTest 合成产物目录；没有覆盖为成功。首轮退格原样发送 U+0008 导致删词，依据 [Windows VT 输入约定](https://learn.microsoft.com/en-us/windows/console/console-virtual-terminal-sequences#input-sequences) 改为 DEL，真实编辑用例通过。

下一步 C5 明确核心 resize 内容/坐标规则，并同步 HWND、屏幕状态和 ConPTY 尺寸；A1 与 C5 合流后验收 G1。C3/C4、U1 与 SSH 仍独立推进，当前不支持完整 TUI 或日常开发版。
