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

工作包、责任 issue 和完成条件见 [R2 交付顺序与责任](milestones/R2-local-terminal.md#交付顺序与责任)。当前队列与计数见 [R2 总项 #7](https://github.com/JavaZeroo/termforge/issues/7)。开工前核对 issue 的状态与原生阻塞关系。

## 研究、兼容性与历史入口

- [WindTerm 目标台账 #19](https://github.com/JavaZeroo/termforge/issues/19)、[自动观测 #3](https://github.com/JavaZeroo/termforge/issues/3)和[正式性能基线 #4](https://github.com/JavaZeroo/termforge/issues/4)
- [Server 2022 系统 ConPTY 诊断 #21](https://github.com/JavaZeroo/termforge/issues/21)和[R4 真实跨屏 #297](https://github.com/JavaZeroo/termforge/issues/297)
- [历史证据索引](EVIDENCE.md)和[旧任务迁移记录](https://github.com/JavaZeroo/termforge/blob/e9982e9c70e8496f4de9179c50ea781ba4318b09/docs/TRACKING.md#已确认收口与责任承接)

任务完成时更新责任 issue、能力行与证据索引。修改合同时同步规格和原生关系；记录未知或整理文档本身不解除产品出口。维护规则见 [issue tracker 约定](agents/issue-tracker.md)。
