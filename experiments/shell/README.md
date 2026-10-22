# A1：固定网格本地 shell 窗口

这是可从源码运行的 Windows 本地交互原型：真实 `cmd.exe` → ConPTY → 有界输出队列 → Recovery 核心 → 只读快照 → DirectWrite/Direct2D → 自有 HWND；字符/方向键从窗口回到 ConPTY。它还不是日常开发版，完整 G1 仍需要 C5 的核心 resize。

## 运行

先按根 CONTRIBUTING 构建。Visual Studio Release 的入口为：

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --target termforge_shell --parallel 2
.\build\bin\Release\termforge_shell.exe
```

程序为 Windows GUI 子系统，不额外打开宿主控制台。默认打开真实可见窗口，在启动目录运行系统 `cmd.exe /d /q`，关闭 AutoRun，设置 UTF-8 code page 与固定 `TF>` 提示符。支持普通字符、中文字符消息、回车、退格、方向键和 Ctrl-C。输入 `exit` 或关闭窗口会结束本会话并回收其自有子树。用户启动的进程仍可访问用户自己的文件；这里是本地 shell，不是沙箱。

当前固定 80×24、100 行内部历史、96 逻辑 DPI；窗口不可拖动缩放。没有历史浏览、选择/复制/粘贴、IME 支持声明、标签页、SSH、备用屏或完整 TUI 兼容性。不实现终端查询回复，未知序列按 [Recovery 契约](../core/RECOVERY.md)消费。固定字体网格与逐格布局来自 [V1](../presentation/README.md)，布局缓存至多 1,024 项，满时清空重建；尚未做产品级缓存策略或性能对比。

## 所有权与调度

- ConPTY 输出线程将最多 64 KiB 的读取块放入 256 KiB 队列；满时等待窗口消费，正常运行不丢弃字节。
- UI 每个 16 ms 定时消息以 8 KiB 小块消费，最多 64 KiB；块间检查 4 ms 预算，随后发布一次快照。单次 feed、快照和绘制仍可能超过预算，它不是硬实时保证。解析和快照发布同属 UI 线程；dirty 范围包含内容与旧/新光标行，多次失效可合并为一次 WM_PAINT。最初每 tick 仅取 8 KiB 导致合成洪流积压，修正依据见批次记录。
- 输入写线程独立服务至多 16 KiB 的队列，每条消息不超过 1 KiB。UI 不等待 WriteFile；容量满时拒绝该次输入并发出提示，不宣称输入已成功。关闭期间停止接收新输入。
- 关闭工作在后台执行，UI 继续消费尾部输出；停止输入写入并反复取消该线程的同步 I/O，终止本会话 Job，关闭 ConPTY，等待读取线程退出。队列排空后执行 `finish()`、提交最终画面并销毁窗口。只终止自有 Job 内的进程，包括 shell 留下的后代。
- 正常 `exit 37` 保留实际退出码 37；关闭正在输出的会话通过 Job 取消。外部自动监督器另建拥有范围明确的 Job：宿主收到 `GO` 前不创建 shell，超时后回收整个自有树。

当前同步 I/O 取消与 ConPTY 关闭不提供跨所有 Windows 版本的硬截止时间保证；UI 仍能处理消息，自动测试有外部时限。Server 2022 已知资源门槛问题继续保留，未扩大已验收平台范围。

## 无人值守验证

```powershell
$env:TERMFORGE_BIN_DIR = (Resolve-Path build/bin/Release).Path
python -m unittest experiments.shell.test_shell -v
python experiments/shell/run.py --bin-dir build/bin/Release --out local/experiments/shell-new --repeats 3
```

测试启动真实 cmd，不使用假 shell；窗口放在独立非输入 desktop，从不切换用户桌面。输入通过队列化 `PostMessage(WM_CHAR/WM_KEYDOWN)` 进入同一个窗口处理路径，不冒充物理键盘操作。断言匹配完整独立输出行，不能用输入命令中的标记冒充执行结果。

每次运行依次验证普通命令、中文、退格、左右方向键编辑，再运行一百万轮的输出命令。至少持续一秒且消费 64 KiB 后发送 Ctrl-C，再执行 `echo TF_AFTER`；确认对应状态与已提交帧后 `exit 37`。另测输出中关闭，正常/关闭各重复三次，检查 Job 活跃进程归零与自有 desktop 消失。监督器用真实存活 shell 验证超时清理。独立队列探针验证容量、背压、顺序、停止唤醒与输入拒绝。短回归不代表长时无泄漏。

测试的 `scripted_input_to_state_ms` 从 Ctrl-C 的 WM_CHAR 被处理开始，到后续 `TF_AFTER` 完整输出行进入快照；`scripted_input_to_submit_ms` 到包含该行的 WM_PAINT BitBlt/GdiFlush 完成。两者包含中断、后续命令和调度，不能当作单键延迟。自有 DIB 截图不是合成器呈现，`presented_done=null`。关闭场景没有该标记，相关字段为 null，不参与延迟统计。

每个子进程限时 40 秒，原生场景限时 25 秒，watchdog 场景 3 秒强制回收；stdout/stderr、失败状态、最终自有 BMP、身份与环境均保留。CTest 产物在 `TERMFORGE_TEST_ARTIFACTS/shell`，默认 `build/test-artifacts/shell`；runner 输出目录必须不存在。只有审核过的合成记录可公开，日常用户会话不会自动录制或上传。

## 依据

独立实现，接口语义核对 [Microsoft ConPTY 会话文档](https://learn.microsoft.com/en-us/windows/console/creating-a-pseudoconsole-session)、[CancelSynchronousIo](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelsynchronousio) 和 [VT 输入序列](https://learn.microsoft.com/en-us/windows/console/console-virtual-terminal-sequences#input-sequences)。Windows 窗口退格消息的 U+0008 映射为 VT 的 DEL；首轮将其原样发送导致删词，已由真实 shell 编辑用例检出并修正。
