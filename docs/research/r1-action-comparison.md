# R1 文字动作对照

> 本页保留 R1 限定观察、实现策略与验证。当前方向按 [ADR-0006](../adr/0006-confirmed-project-direction.md) 原版优先；未经原版对照的项目策略继续列为差异或未知，重要差异及原版缺陷修正需用户确认。历史验收不自动成为最终差异获准记录。

本表用于 [R1-07 / #17](https://github.com/JavaZeroo/termforge/issues/17) 的逐项验收。参考观察、产品策略和实际验证分别记录；有实现或纯模型测试不等于真实入口通过。本表已按 R1 合同完成限定验收，执行状态只在 issue 维护。

## 固定依据

参考为官方 WindTerm 2.7.0 Windows x64；身份、已通过的两组各两轮观察与精确字节见 [R1-01 合同](windterm-r1-text-contract.md)。本项目独立实现，未复制参考程序代码、主题或资源。

`action-context` 探针调查 Find 编辑动作和 PasteDialog 实际 Return，入口和隔离见 [UI 调查工具](../../research/ui/README.md)。[有序独立审计](r1-text/hosted-context-40026676722.json)已完整重算两轮各11个检查点；旧局部观察及导出失败各保留原结论。纯 schema 测试只验证审核器，不构成参考事实。

## 固定产品证据

产品固定 `6a165c8`：[主 CI 独立审计](../../experiments/shell/results/2026-10-24-r1-actions-accepted/validation.json)重算 local26/system36 各两轮、Paste v2 四轮及原始字节/日志/帧/回收；[五模式真实 IME](../../research/ime/results/2026-10-24-product-ime-contexts/validation.json)另审28检查点、9组历史/Find上下文与 rename 双 Escape。[逐项合同复核](../../experiments/shell/results/2026-10-24-r1-actions-accepted/contract-coverage.json)将这些证据与已验收的基础切片关联。二进制仅有托管运行记录哈希，未上传供独立重哈希；旧33阶段、旧取消默认和早期失败不追认为新覆盖。

## 动作与入口

| 动作与上下文 | 已观察的参考行为 | 实现与验证入口 | 当前验收边界 |
|---|---|---|---|
| 终端历史首/尾 | Ctrl+Shift+Home/End 到顶部/底部；暂停后普通输入恢复跟随且仅发送该输入 | `text_actions.hpp`、`perform_text_action`；`ui-history`、`ui-actions` | #12 浏览及当前实际快捷键/跨入口已验收；#18 再做完整组合 |
| Select All | Ctrl+Shift+A、实际终端上下文菜单改变终端选区 | `text_interactions.hpp`；`ui-selection`、`ui-actions` | #13 基础选择/Session 菜单及当前真实终端右键已验收 |
| Copy | Ctrl+Shift+C、实际上下文菜单，精确 Unicode、软折行合并、硬 CRLF 与传入模型的尾空格 | `request_copy`；`ui-copy-system`、`ui-copy-bytes-system`、`ui-actions-system` | 基础/失败重试及归属由 #13 验收，当前终端右键实际 CF 逐字重算；普通尾空格仍保留下述限制 |
| Paste | Ctrl+Shift+V、Shift+Insert、实际上下文菜单；单行立即发送，多行确认前零字节，取消不发，确认恰好一次 | `request_paste`、`PastePrompt`；`ui-clipboard-system`、`ui-actions-system` | #14 可靠传输/取消与当前 Shift+Insert/右键/默认 Return/目标切换已有精确系统字节 |
| Find | Ctrl+Shift+F、实际上下文菜单进入查找框；Enter 向前一个、F3 向后一个、循环、Escape 返回终端 | `FindBar`、`search_interactions.hpp`；`ui-find`、`ui-find-stream`、`ui-actions` | #15 结果/持续输出及当前实际快捷键/右键/编辑焦点交叉已验收 |
| 原生 Find/rename 编辑 | 两轮参考 Find 中 All/Copy/Paste/再次 Find 作用于查询，再次 Find 全选查询；Escape 返回终端且 peer 零泄漏 | `FindBar::route`、`Workspace::edit_key`；`ui-actions-system`、`ui-ime-reading/focus` | 产品实际 Find CF sentinel→查询、中文替换/重选、双 PTY 零泄漏已审计；Find/rename 真实组合和双 Escape 另有系统证据，rename 不由参考 Find 推定 |
| 多行确认框 Return | 两轮实际 Return 均接受，稳定阶段及最终精确追加一次；旧 artifact 保留其局部审核限制 | `PastePrompt::route`；`ui-paste` v2、`ui-actions-system`；参考 `action-context` | 默认 Paste/Enter 接受已按实际系统入口验收，Escape 与显式取消独立验证；旧取消默认不算新默认通过 |
| 无会话/无选区/busy/closing | 复杂交叉尚无完整参考观察 | `text_action_state` 与各真实入口 | 产品实际菜单、反馈、原会话粘贴绑定及关闭隔离已按下述策略验收；不据此宣称参考一致 |
| 受控 DPI/zoom 交叉 | 既有参考不证明完整复杂交叉 | 工作区、选择/查找与 IME 场景 | 选区/当前命中/编辑焦点的五组受控几何及真实 IME 交叉已验证；真实跨显示器归 R2，整体八会话资源仍待 #18 |

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
