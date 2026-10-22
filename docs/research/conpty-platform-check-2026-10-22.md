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

额外的[有界 PSS 诊断 / `2bd61ec`](https://github.com/JavaZeroo/termforge/actions/runs/39807637556) 在 Server 2022 输出首次快照后以 `0xC0000005` 异常退出，未获得稳态残留句柄的有效归属证据；该快照路径已从默认代码移除。不能据首次快照或其他项目的历史 bug 认定这 1 个句柄属于哪个系统组件。保留约 1 秒回收等待、严格数量检查和有界 HEX 失败记录。

## 本批范围决定

[`75e39f9` 的三平台运行](https://github.com/JavaZeroo/termforge/actions/runs/39807244160)及上述 `2bd61ec` 运行中，Server 2025 和 Ubuntu 均通过全部对应构建、测试和 ASCII/Unicode 回放。Server 2022 没有通过。主 CI 因而固定为 `windows-2025` / `ubuntu-24.04`，与本机 Windows 11 的验证共同构成本批已验收范围；这不代表 Windows 10 全系列或真实桌面应用均已支持。

Server 2022 的兼容性问题保留为后续任务，不扩大本批去修改私有 HPCON 布局。独立[兼容性 workflow](../../.github/workflows/conpty-compat.yml)可由代理 `gh workflow run conpty-compat.yml` 无交互触发，使用相同严格 T1 断言，失败仍使运行失败。原始已知失败和[审阅后的摘要](../../experiments/conpty/results/2026-10-22-server2022/summary.json)保留。此范围决定没有把资源增长改成合格，也没有据此宣称产品已经无泄漏。
