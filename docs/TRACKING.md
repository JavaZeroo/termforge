# 任务跟踪

本页只导航到 GitHub 任务，不维护第二份进度表。能力与构建配置见 [FEATURES](FEATURES.md)，R1–R5 范围见 [PLAN](PLAN.md)，当前接续顺序与完成条件见 [R2 交付路线与验收](milestones/R2-local-terminal.md)。实时状态、原生父子/阻塞关系和验收记录以各 issue 为准。

执行方向按 [ADR-0006](adr/0006-confirmed-project-direction.md)：优先终端基础能力与性能，首个完整版本为 Windows 11 x64，随后完成 Linux/macOS；原版行为优先。[ADR-0007](adr/0007-bundled-microsoft-conpty.md) / [ADR-0008](adr/0008-cpp-core-qt-widgets.md)确认自主 C++、Qt Widgets 与随包固定微软 ConPTY。[ADR-0009](adr/0009-r2-scope-decisions.md)限定 R2 为本地终端基础和已知差异清单、单屏 96/144 DPI 与回归型性能治理。路线项收口不等于产品验收；性能数值容差仍须用户确认。

## 产品路标

| 路标（远到近） | GitHub milestone | 总项 |
|---|---|---|
| R5 · 完整复刻验收：先 Windows，后全平台 | [milestone 6](https://github.com/JavaZeroo/termforge/milestone/6) | [Windows / 全平台两个出口](https://github.com/JavaZeroo/termforge/issues/10) |
| R4 · Windows 高级功能与性能收敛 | [milestone 5](https://github.com/JavaZeroo/termforge/milestone/5) | [高级工作流与跨平台准备](https://github.com/JavaZeroo/termforge/issues/9) |
| R3 · SSH/SFTP 远程日常版 | [milestone 4](https://github.com/JavaZeroo/termforge/milestone/4) | [远程日常版](https://github.com/JavaZeroo/termforge/issues/8) |
| R2 · 可靠本地终端与开发版 | [milestone 3](https://github.com/JavaZeroo/termforge/milestone/3) | [当前路标](https://github.com/JavaZeroo/termforge/issues/7) |
| R1 · 基础文字交互 | [milestone 2](https://github.com/JavaZeroo/termforge/milestone/2) | [限定验收](https://github.com/JavaZeroo/termforge/issues/6) |

## GitHub Project 总览

[TermForge 交付总览](https://github.com/users/JavaZeroo/projects/2) 把全部 issue 和 R1–R5 里程碑放进同一个看板，用于一次浏览路标、开工队列和阻塞项。它不维护第二份进度：issue 的开闭、标签、原生阻塞/父子关系和 milestone 仍是权威，看板的 `Status`、`主类型`、`执行状态` 由它们派生，两边冲突时以 issue 为准。原生 blocked-by 和父子关系仍在 issue 侧维护，看板不读取它们，`执行状态` 只看三个执行标签。

| 视图 | 内容 |
|---|---|
| 总览 | 全部条目，含已关闭项 |
| R2 当前路标 | milestone 为 `R2 · 可靠本地终端与开发版` 的看板 |
| 可开工队列 | `ready-for-agent` 且未标记 `blocked` |
| 阻塞与待拆分 | `blocked` 或 `needs-breakdown` |
| 研究调查 | `research` |
| 待归类 | 三个执行标签都未标记，需人工归类 |
| 路标 Epic | `epic` 总项的路线图 |
| 已完成 | 已关闭的历史条目 |

派生规则：`主类型` 按主题标签 epic > bug > performance > research > enhancement > compatibility > documentation 取第一个命中项，未命中留空；`执行状态` 按 blocked > needs-breakdown > ready-for-agent > 未标记；`Status` 只在 issue 关闭（Done）、重开或首次加入且为空（Todo）时写入，不覆盖人工设置的 In Progress。

看板由仓库工具幂等同步（只补齐和更新，不删除条目），标签、milestone 或 issue 状态变化后重新同步即可：

```powershell
python tools/sync_github_project.py --check   # 只报告漂移，不写入
python tools/sync_github_project.py           # 同步字段、视图、条目与派生值
```

`--check` 的退出码 0 表示无漂移、1 表示有漂移；运行失败同样是 1，需看错误输出区分。

需要 `gh` 已登录且 token 含 `project` scope（`gh auth refresh -s project,read:project`）。视图的分组、排序、列宽与日期轴，以及 Web UI 的内置工作流（Auto-add to project、Item closed → Done、Item reopened → Todo），API 不支持，需在网页端一次性设置；同步工具只改布局、过滤和可见字段，不会覆盖这些设置。`主类型`、`执行状态` 的选项由工具完全拥有：手工新增选项或改颜色会在下次同步被覆盖，新增主题标签要先改 `tools/sync_github_project.py` 的常量再同步。

## R2 实施与交付入口

当前队列与计数以 [#7](https://github.com/JavaZeroo/termforge/issues/7) 为准。下表按用户结果分组；父项、汇总项和子任务不能重复计算工作量。`ready-for-agent` 只说明独立工作可开始，实际开工仍须核对原生阻塞关系。

| 工作包 | 责任任务 | 交付边界 |
|---|---|---|
| 终端模式、备用屏与滚动区 | [#27](https://github.com/JavaZeroo/termforge/issues/27) | 原版合同下的自主 C++ 模式及交叉验证；已实现切片不等于全部模式验收 |
| 查询回复、应用模式与真实 TUI | [#28](https://github.com/JavaZeroo/termforge/issues/28) | 固定应用矩阵，进入/操作/resize/退出/恢复，产品路径协议与字节独立验证 |
| resize、逻辑行与历史 | [#29](https://github.com/JavaZeroo/termforge/issues/29) | 用户明确保留；软硬行、宽字、历史/淘汰、选择/Find 和真实 child 尺寸 |
| Unicode、IME 与文字交互 | [#30](https://github.com/JavaZeroo/termforge/issues/30)，含 [Qt IME #155](https://github.com/JavaZeroo/termforge/issues/155) | 用户明确保留；#155 是其子项，固定拼音/候选框的局部结果不代替完整文字输入 |
| controller 正常退出 | [#44](https://github.com/JavaZeroo/termforge/issues/44) | close 前自然退出观测与清理分列，不由后续绿解释旧因 |
| hosted 四 shell 退出 | [#80](https://github.com/JavaZeroo/termforge/issues/80) | 原 stage6 事件与已实现就绪握手分别留证 |
| hosted 剪贴板超时 | [#99](https://github.com/JavaZeroo/termforge/issues/99) | 原占用反馈后超时、重试/取消/关闭与回收边界 |
| 最终基础开发包 | [#35](https://github.com/JavaZeroo/termforge/issues/35) | 产品默认入口切换后重建复验、单屏 96/144、声明配置范围、来源许可与逐项已知差异清单 |

[#38](https://github.com/JavaZeroo/termforge/issues/38)汇总可靠性，#7 汇总 R2；两者不重复算作实现工作包。最终包按 [R2 合同](milestones/R2-local-terminal.md)核对保留任务，不能把“附差异清单”解释成任意缺口均可豁免。

## 已确认收口与责任承接

| 原任务 | 处置与后续责任 |
|---|---|
| [架构 #34](https://github.com/JavaZeroo/termforge/issues/34#issuecomment-6554932972) | 按用户决定以架构说明收口；功能验证归 #28/#29/#30，产品交付归 #35，跨屏归 #297；不重新讨论已定路线 |
| [固定后端 #47](https://github.com/JavaZeroo/termforge/issues/47#issuecomment-6554930225) | 路线整合已收口；默认入口切换后的重建复验交 #35，系统 ConPTY/WinPTY 仅作诊断 |
| [margin 裁决 #41](https://github.com/JavaZeroo/termforge/issues/41#issuecomment-6541608255) | 固定包的 VT/末端结果与原生 CHAR_INFO 差异分列；已知差异见 ADR-0007，固定原版缺陷修正见 ADR-0009 决策 6；#27 保留核心合同 |
| [显示 #31](https://github.com/JavaZeroo/termforge/issues/31#issuecomment-6553824433) | 按单屏范围收口；正式 Qt 单屏 96/144 在 #35 复验，真实跨屏转 [#297](https://github.com/JavaZeroo/termforge/issues/297)（R4）；关闭不表示完整显示已验收 |
| [性能 #54](https://github.com/JavaZeroo/termforge/issues/54) | 按回归型治理方向收口；未建立的可靠基线和未确认数值不作门禁，不宣称 WindTerm 等价 |
| [历史诊断与实验后端](https://github.com/JavaZeroo/termforge/issues/38#issuecomment-6554935496) | 按用户接受的未复现批次或范围调整收口；原失败与重开条件留在各 issue，不再次列成全部必须修复的当前队列 |

## 已交付基础与历史入口

| 范围 | 入口 |
|---|---|
| R1 基础文字与同次八会话限定验收 | [R1 总项 #6](https://github.com/JavaZeroo/termforge/issues/6)、[组合验收 #18](https://github.com/JavaZeroo/termforge/issues/18)、[合同](milestones/R1-local-text.md) |
| Alpha 1 Windows x64 体验包 | [发布切片 #22](https://github.com/JavaZeroo/termforge/issues/22)、[发布说明](releases/v0.1.0-alpha.1.md)、[下载与验证](releases/v0.1.0-alpha.1-validation.json) |
| R2 平台调查与候选比较证据 | [平台 #23](https://github.com/JavaZeroo/termforge/issues/23)归属 [全量台账 #19](https://github.com/JavaZeroo/termforge/issues/19)；[候选 #24](https://github.com/JavaZeroo/termforge/issues/24)、[差分 #25](https://github.com/JavaZeroo/termforge/issues/25)、[成本/历史路线 #26](https://github.com/JavaZeroo/termforge/issues/26)归属 [历史比较 #20](https://github.com/JavaZeroo/termforge/issues/20)；限定结项保留为参考，当前正式路线见 ADR-0007/0008，最终产物复验由 #35 承接 |
| 有限本地会话与配置恢复 | [本地会话 #32](https://github.com/JavaZeroo/termforge/issues/32)、[持久化 #33](https://github.com/JavaZeroo/termforge/issues/33)，能力边界见 [FEATURES](FEATURES.md) |
| 首批流式字素、宽度与 glyph | [字素合同 #39](https://github.com/JavaZeroo/termforge/issues/39)、[来源与合同](research/streaming-graphemes.md)，完整输入由 #30 继续验收 |
| 回归、夹具与参考捕获 | #38 的原生子项及 [EVIDENCE](EVIDENCE.md)保留各固定原因、未知及处置；限定修复、未复现与范围收口分别记录 |
| 旧 M0/G1、批次规格与审查 | [归档说明与迁移表](archive/README.md)；历史记录不作为当前 TODO |

## 当前优先及可并行的调查

| 工作 | 责任任务与边界 |
|---|---|
| WindTerm 2.7.0 全量目标与平台台账 | [全量目标 #19](https://github.com/JavaZeroo/termforge/issues/19)，R5 验收基础；#23 平台切片完成不代表其余行为已取证 |
| 自动观测、扰动校准与完整 Schema | [观测工具 #3](https://github.com/JavaZeroo/termforge/issues/3)，当前优先支持 Windows 本地终端对照，扩展覆盖持续至 R4；停止的手工批次不默认重启 |
| 正式 WindTerm 性能基线 | [受控对照 #4](https://github.com/JavaZeroo/termforge/issues/4)，依赖 #3；优先 Windows 11 x64 本地终端，给用户提供容差提案，不能用自有微基准替代 |
| Server 2022 ConPTY 稳态句柄 | [兼容性 #21](https://github.com/JavaZeroo/termforge/issues/21)；按 ADR-0009 改为句柄计数记录，其他生命周期合同保持，不由此扩大产品平台支持声明 |
| 真实异 DPI 跨屏 | [#297](https://github.com/JavaZeroo/termforge/issues/297)，R4；保留原 #31 跨屏合同，不阻塞 R2 单屏出口 |

任务完成时更新责任 issue、能力行与证据索引。修改合同时同步规格和原生关系；记录未知或整理文档本身不解除产品出口。维护规则见 [issue tracker 约定](agents/issue-tracker.md)。
