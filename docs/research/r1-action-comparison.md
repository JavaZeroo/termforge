# R1 文字动作对照

本表用于 [R1-07 / #17](https://github.com/JavaZeroo/termforge/issues/17) 的逐项验收。参考观察、产品策略和实际验证分别记录；有实现或纯模型测试不等于真实入口通过。当前整项仍未验收，执行状态只在 issue 维护。

## 固定依据

参考为官方 WindTerm 2.7.0 Windows x64；身份、已通过的两组各两轮观察与精确字节见 [R1-01 合同](windterm-r1-text-contract.md)。本项目独立实现，未复制参考程序代码、主题或资源。

`action-context` 探针调查 Find 编辑动作和 PasteDialog 实际 Return，入口和隔离见 [UI 调查工具](../../research/ui/README.md)。[有序独立审计](r1-text/hosted-context-40026676722.json)已完整重算两轮各11个检查点；旧局部观察及导出失败各保留原结论。纯 schema 测试只验证审核器，不构成参考事实。

## 动作与入口

| 动作与上下文 | 已观察的参考行为 | 实现与验证入口 | 当前验收边界 |
|---|---|---|---|
| 终端历史首/尾 | Ctrl+Shift+Home/End 到顶部/底部；暂停后普通输入恢复跟随且仅发送该输入 | `text_actions.hpp`、`perform_text_action`；`ui-history`、`ui-actions` | #12 已验收浏览；新增实际快捷键与交叉入口须按 #17 验收 |
| Select All | Ctrl+Shift+A、实际终端上下文菜单改变终端选区 | `text_interactions.hpp`；`ui-selection`、`ui-actions` | #13 的 Session 菜单与基础选择已验收；终端右键入口单独验证 |
| Copy | Ctrl+Shift+C、实际上下文菜单，精确 Unicode、软折行合并、硬 CRLF 与已传入模型的尾空格 | `request_copy`；`ui-copy-system`、`ui-copy-bytes-system`、`ui-actions-system` | #13 已有实际 CF、失败/重试及归属证据；新增右键入口须核验 CF，不以命令返回值冒充成功 |
| Paste | Ctrl+Shift+V、Shift+Insert、实际上下文菜单；单行立即发送，多行确认前零字节，取消不发，确认恰好一次 | `request_paste`、`PastePrompt`；`ui-clipboard-system`、`ui-actions-system` | #14 已验收可靠传输与取消；新增 Shift+Insert/右键入口须检查接收文件与目标绑定 |
| Find | Ctrl+Shift+F、实际上下文菜单进入查找框；Enter 向前一个、F3 向后一个、循环、Escape 返回终端 | `FindBar`、`search_interactions.hpp`；`ui-find`、`ui-find-stream`、`ui-actions` | #15 已验收结果与持续输出；实际快捷键/右键及编辑焦点交叉须由 #17 闭合 |
| 原生 Find/rename 编辑 | 两轮实际观察：Find 中 All/Copy/Paste/再次 Find 作用于查询，再次 Find 全选查询；Escape 返回终端且 peer 零泄漏 | `FindBar::route`、`Workspace::edit_key`；`ui-actions`、`ui-ime-reading` | 真实 Ctrl+A 与再次 Ctrl+Shift+F 后的查询全选/整体替换已有窗口回归；真实组合/取消、系统编辑命令及 PTY 零泄漏仍分别验收；rename 不由 Find 结果推出 |
| 多行确认框 Return | 两轮实际 Return 均接受，稳定阶段及最终精确追加一次；旧 artifact 保留其局部审核限制 | `PastePrompt::route`；`ui-paste` v2、`ui-actions-system`；参考 `action-context` | 原默认 Cancel 差异已修复为 Paste/Enter 接受，实际系统入口待当前源码托管验收；不以旧取消证据记作新默认通过 |
| 无会话/无选区/busy/closing | 复杂交叉尚无完整参考观察 | `text_action_state` 与各真实入口 | 采用下述明确项目策略；真实菜单、反馈和目标仍须验证，不因此记为参考一致 |
| 受控 DPI/zoom 交叉 | 既有参考不证明完整复杂交叉 | 工作区、选择/查找与 IME 场景 | 单项 DPI/选区/IME 证据不代替组合验证；真实跨显示器归 R2 |

## 产品策略与差异处置

- 菜单与执行共用动作规则，执行时重查目标。无会话、关闭中、原生编辑焦点及真实 IME 组合不得把文字动作发往其他终端；Find/rename 的普通编辑命令保留在实际 EDIT。
- 粘贴绑定原会话；忙碌时拒绝第二次粘贴，保留当前任务并提供明确反馈。Escape 处理所属粘贴/编辑/组合，不能产生额外 PTY 字节或取消另一会话任务。
- 没有选区、内容覆盖使选区失效、容量超限、系统剪贴板失败是不同结果。菜单可禁用失效选区，但快捷键仍须保留“内容已改变”的精确诊断。
- 普通尾空格可能已由 ConPTY 规范化为擦除序列，不能恢复已消除的 producer 来源；原始 VT 与带样式尾空格证据分别保留。显式空白端点和矩形等新选择模式不由基础样例推定。
- 词规则、容量及持续扫描时有限局部命中不循环属于项目策略。目标样例通过不代表任意标点、完整 Unicode caseless 或百万行历史等价。
- Copy 菜单 Shift+Del 仅是参考可见标注，不据此新增键位。多行 Return 按已观察的接受行为对齐；旧取消证据只代表原版本，不追认为新默认通过。

## 结项依据

每个合同入口关联固定源码/二进制、独立预期、实际动作、目标、反馈和最终字节；有效系统 Copy/Paste 与 IME 只在指定一次性托管环境执行。真实 popup/排队键与直接业务调用分开；纯模型、控件、窗口和系统验证各自说明范围。失败保留，自有任务/线程/进程/Job/desktop 回收须有实际证据；输入桌面不创建、切换或删除。

本表不关闭 R1-08 的完整流程与多会话资源门槛，也不证明完整 UI、TUI、远程能力或性能等价。
