# Hosted 剪贴板超时的有界阶段观测

[#99](https://github.com/JavaZeroo/termforge/issues/99) 的原 `ui-clipboard-system` 在 occupied 错误反馈后触发 launcher 45 秒期限。原件无法区分后续等待边界。本调查加入临时阶段记录，完成预先固定的一个批次：**两轮均通过，原超时未复现，原因仍未知**。完整 CI 的其他失败分别保留，可靠性缺口没有关闭。

## 方法与身份

观测源码 [`b9789543a792b0a0b96139377ea43f4dfab46626`](https://github.com/JavaZeroo/termforge/commit/b9789543a792b0a0b96139377ea43f4dfab46626)，tree `bb8d6ce8464ec8b50ff86000dfb1f0b39b3b9888`，基于 #98 的 `678804ff5201fab4296b133af9457b7120739b68`。只在诊断分支首轮 push/default Windows hosted VM 启用；PR 与 external candidate 检查关闭记录。

原 `ShellTests.test_system_clipboard_product_read_errors_cancel_and_close` 的两轮、四 case、256 B 基线、精确重试字节、45/60 秒期限及完整回归上下文未改。本机仅构建和验证非 hosted 拒绝守卫，没有调用真实系统剪贴板。

记录白名单为 case/stage、callback 计数、单调时长、固定动作前后事件、paste busy、helper 零等待状态及 owner 固定步骤/错误数字。稳定阶段最多每秒一条，总量最多 256 条；不含剪贴板或终端文字、PID/HWND、路径或凭据。owner 多个原子字段不是一致快照；事件只表示调用边界，文件 I/O 可能影响时序。

观测结束后，临时 C++ 记录与 CI 启用步骤全部撤回，三个文件恢复到上述父提交；固定观测提交保留原代码供审查。当前代码不会自动继续这个诊断批次，没有因本调查改变产品行为。

## 实际结果

[push/default run 40459936505，Windows job 122041868650](https://github.com/JavaZeroo/termforge/actions/runs/40459936505/job/122041868650) 使用默认核心与系统 ConPTY。[白名单摘要](../../experiments/shell/results/2026-10-27-clipboard-stages/validation.json)记录来源、构建清单和原件哈希；[trace 0](../../experiments/shell/results/2026-10-27-clipboard-stages/trace-0.jsonl)及[trace 1](../../experiments/shell/results/2026-10-27-clipboard-stages/trace-1.jsonl)经字段检查后保留原字节。

| 检查 | 第一轮 | 第二轮 |
|---|---|---|
| occupied / oversize / cancel / close 接收字节 | 270 / 270 / 268 / 256 | 270 / 270 / 268 / 256 |
| 记录条数 / callback 次数 | 72 / 45 | 72 / 45 |
| complete-before-close 记录 | 1614 ms | 1527 ms |
| launcher 退出 / outer 超时 / stderr | 0 / false / 空 | 0 / false / 空 |
| Job active / total / terminated | 0 / 27 / 0 | 0 / 27 / 0 |
| desktop 回收 / 记录截断 | true / false | true / false |

原 `validate_clipboard_workflow` 与结果 validator 离线重算通过，包含四 case 的独立字节预期、owner 释放/退出、helper 回收及错误反馈。cancel/close 的晚发布按既有合同观察到 1418、requester 已退出，没有改记为成功发布。另核对 trace 字段集合/类型、记录序号、时间和 callback 单调性、各 case 阶段/动作顺序及条数上限。1614/1527 ms 是带诊断的关闭前记录，不是自然退出、性能或呈现端点。

[artifact 12714865514](https://github.com/JavaZeroo/termforge/actions/runs/40459936505/artifacts/12714865514) 为 4269771 B，ZIP SHA-256 `4b597e169d36624c87a6a9859e0fc3b2a37846569bca3eaca4c51322ec7d2791`，与 GitHub digest 一致。完整 ZIP、原文件和离线审计保留在 ignored `local/r99`；首次下载的 1572864 B 不完整文件保留并排除。CI 清单记录该次 clean build 的 shell、launcher、clipboard reader、input client 四个 EXE 哈希及 checkout；EXE 本体未归档，没有声称在本地重新哈希。含进程身份的原 workflow/sample 不直接公开。

## 完整 CI 与独立失败

| 检查 | Windows | Linux | 状态 |
|---|---|---|---|
| push/default，40459936505 | 50/52，280.05 s | 32/33，12.67 s | 整体失败，Windows 启用诊断 |
| PR/default，40459942201 | 51/52，276.72 s | 32/33，12.95 s | 整体失败，诊断关闭 |
| push/external candidate，40459936582 | success | success | 独立候选配置 |
| PR/external candidate，40459942259 | success | success | 独立候选配置 |

PR 实际 checkout 为 `b58c029fc0f861c2e7d251d125b18ec70e80dda9`。四个 default job 均因本次新增清单路径触发既有 workflow 路径计数断言 `2 != 1`。本地单独复现该断言；撤回临时工作流后，原 ActionOracleTest 7/7 通过（0.487 s）。这是诊断配置回归的修正，不是 #99 超时修复；后续 CI 不回填这一批为通过。

push Windows 另有 `test_mouse_selection_to_real_system_clipboard` 的 `ui-copy-system` 第二轮 `OpenClipboard: 5`。第一轮原 validator 离线通过；第二轮已有 12 条基础记录、22 条交互记录，最后为实际 Copy popup 成功读回。之后、构造自有占用 owner 之前有三个夹具剪贴板调用，具体哪个失败及原因未知。此入口补入 [#85](https://github.com/JavaZeroo/termforge/issues/85)，与旧入口及 #99 分列，不由同一 API 错误推断同因。

本批已按预定停止条件结束，没有追加重试或改变超时、字节门槛。#99 继续开放；本报告不声称原因修复、完整产品验收或 WindTerm 等价。后续执行状态由 issue 维护。
