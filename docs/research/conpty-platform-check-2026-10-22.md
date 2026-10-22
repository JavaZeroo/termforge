# ConPTY 跨环境检查记录

本记录保留第一批 CI 暴露的真实差异。使用自有受控 child 和合成命令，不涉及 WindTerm 或个人 shell。旧实验结果保持原样；对应代码和实际运行链接分开列出。

## UTF-8 输入路径

- 本机 Windows 11 build 26200 的初始实验成功。
- [Server 2022 首次 CI](https://github.com/JavaZeroo/termforge/actions/runs/39805041604) 在启动成功后，中文回显检查失败；其他测试组通过。
- [诊断 CI / `0c88664`](https://github.com/JavaZeroo/termforge/actions/runs/39805975727) 保留了失败输出首尾字节，并让 child 以 ASCII hex 报告收到的命令。预期 `ECHO:alpha-汉字-123`，实际读到 `ECHO:alpha-88-123`；hex 为 `4543484f3a616c7068612d38382d313233`。说明差异已发生在 child 读取的输入中，不能用输出中存在 VT 控制序列解释。
- [宽字符 API CI / `b4a391c`](https://github.com/JavaZeroo/termforge/actions/runs/39806587262) 将受控 child 的读取改为 `ReadConsoleW`，累计完整命令后严格转 UTF-8；ConPTY 管道及输出字节检查仍为 UTF-8。所有模式的回显、resize 与预期退出恢复，未放宽中文验收。

Microsoft 的 [console UTF-8 A API 问题讨论](https://github.com/microsoft/terminal/issues/4551) 提供排查线索，但讨论中的 NUL 症状并不等于本次 `88` 现象。这里记录可复现的 API 路径差异，不宣称已定位旧系统内部具体函数。

## 资源回收

`b4a391c` 在 Server 2022 仍未通过完整 T1：首次会话宿主句柄从 41 到 55（另一个宿主从 47 到 61），后续正常退出、取消、关闭和大输出场景每轮增加 1 个句柄。线程数保持 4，child/孙进程树归零，未触发强制进程/读线程兜底。UTF-8 修复没有修复资源门槛。

本机最初的短批次则是首次增加 6 个句柄，后续稳态差为零。两个环境的差异不用于推导任何性能优劣。现代 Windows runner 同时改变系统和工具链，不能只凭两边通过/失败就断定唯一根因。

资源验收继续要求回到基线；失败结果没有被跳过或改成通过。后续有界诊断与最终支持范围以[当前状态表](../STATUS-AND-BACKLOG.md)及本记录更新为准。
