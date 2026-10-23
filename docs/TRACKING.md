# 任务跟踪

执行状态以 [GitHub issues](https://github.com/JavaZeroo/termforge/issues) 和原生父子/阻塞关系为准。本页维护路标与任务映射，不再复制逐批进度。当前能力见 [FEATURES](FEATURES.md)，交付合同见 [PLAN](PLAN.md)，旧 TODO 归属见[迁移表](archive/README.md)。

## 路标（远到近）

| 路标 | GitHub milestone | 总项与工作包 |
|---|---|---|
| R5 · WindTerm 2.7.0 完整开源复刻验收 | [milestone 6](https://github.com/JavaZeroo/termforge/milestone/6) | [#10](https://github.com/JavaZeroo/termforge/issues/10) |
| R4 · 高级功能、跨平台与性能收敛 | [milestone 5](https://github.com/JavaZeroo/termforge/milestone/5) | [#9](https://github.com/JavaZeroo/termforge/issues/9) |
| R3 · SSH/SFTP 远程日常版 | [milestone 4](https://github.com/JavaZeroo/termforge/milestone/4) | [#8](https://github.com/JavaZeroo/termforge/issues/8) |
| R2 · 可靠本地终端与开发版 | [milestone 3](https://github.com/JavaZeroo/termforge/milestone/3) | [#7](https://github.com/JavaZeroo/termforge/issues/7) |
| R1 · 基础文字交互复刻 | [milestone 2](https://github.com/JavaZeroo/termforge/milestone/2) | [#6](https://github.com/JavaZeroo/termforge/issues/6) |

路标总项保存出口条件；近期任务是可验收切片，远期总项保持工作包粒度。R1 → R5 是交付主线，不意味着每项调查必须等前一路标全部完成。

## R1：下一路标的执行任务

详细事项、数据边界与自动验收见 [R1 合同](milestones/R1-local-text.md)。下表是规划时的依赖，不是第二份实时状态表。

| 切片 | GitHub | 交付 | 真实前置 |
|---|---|---|---|
| R1-01 | [#11](https://github.com/JavaZeroo/termforge/issues/11) | 冻结 WindTerm 2.7.0 基础文字交互契约 | 无 |
| R1-02 | [#12](https://github.com/JavaZeroo/termforge/issues/12) | 浏览有界历史并保持每会话阅读位置 | 无 |
| R1-03 | [#13](https://github.com/JavaZeroo/termforge/issues/13) | 选择屏幕与历史文字并精确复制 | [#11](https://github.com/JavaZeroo/termforge/issues/11)、[#12](https://github.com/JavaZeroo/termforge/issues/12) |
| R1-04 | [#14](https://github.com/JavaZeroo/termforge/issues/14) | 将大段 Unicode 文本可靠、可取消地粘贴到目标会话 | 无 |
| R1-05 | [#15](https://github.com/JavaZeroo/termforge/issues/15) | 在有界历史中查找、跳转和取消搜索 | [#11](https://github.com/JavaZeroo/termforge/issues/11)、[#12](https://github.com/JavaZeroo/termforge/issues/12) |
| R1-06 | [#16](https://github.com/JavaZeroo/termforge/issues/16) | 基础键盘与中文 IME 完整输入闭环 | 无 |
| R1-07 | [#17](https://github.com/JavaZeroo/termforge/issues/17) | 统一文字动作上下文并完成 WindTerm 交互对照 | [#11](https://github.com/JavaZeroo/termforge/issues/11)、[#13](https://github.com/JavaZeroo/termforge/issues/13)、[#14](https://github.com/JavaZeroo/termforge/issues/14)、[#15](https://github.com/JavaZeroo/termforge/issues/15)、[#16](https://github.com/JavaZeroo/termforge/issues/16) |
| R1-08 | [#18](https://github.com/JavaZeroo/termforge/issues/18) | 验收完整文字工作流与多会话资源边界 | [#17](https://github.com/JavaZeroo/termforge/issues/17) |

## 从当前状态接着做

以下为选题顺序；各项已做/待做和验收结果只在对应 issue 维护。`ready-for-agent` 表示可以开工，不代表全仓库所有 ready 项优先级相同。

1. **开始统一文字动作与参考对照。** [参考契约](https://github.com/JavaZeroo/termforge/issues/11)、[历史](https://github.com/JavaZeroo/termforge/issues/12)、[选择复制](https://github.com/JavaZeroo/termforge/issues/13)、[粘贴](https://github.com/JavaZeroo/termforge/issues/14)、[查找](https://github.com/JavaZeroo/termforge/issues/15)、[固定拼音输入](https://github.com/JavaZeroo/termforge/issues/16)均已按限定合同验收。[统一文字动作](https://github.com/JavaZeroo/termforge/issues/17)的真实前置已解除，接着核对菜单/快捷键/焦点与参考行为；未知基础动作保持调查状态，自定策略单列。
2. **保留基础能力的系统回归。** 选择复制、粘贴、查找及输入继续使用独立预期和真实系统证据；共用只读文本投影与稳定位置，不另建并行状态来源。完整组合由统一动作和工作流承接，底层 API、合成消息或本地跳过不替代系统验收。
3. **最后验收完整工作流并关闭 R1。** [完整工作流验收](https://github.com/JavaZeroo/termforge/issues/18)在 #17 完成后，以最终源码和真实系统环境验证全部流程及多会话资源边界。基础事项关闭不等于组合流程通过；单个测试成功或系统测试跳过不能解除该门槛。
4. **并行推进影响后续决策的调查。** [全量目标台账](https://github.com/JavaZeroo/termforge/issues/19)先冻结平台/架构/最低系统边界，作为 R2 核心与 GUI 产品路线决定的输入；候选构建可与范围调查并行。外部核心和测量工具先按下表拆小。Server 2022 专项可独立调查，但不挤占近期文字交互主线。

未提交草稿和未完成的系统验收均不计入交付。开工前核对工作树，保留已有草稿并明确归属；文档整理本身不解除功能验收门槛。

## 并行调查与遗留门槛

| 项目 | GitHub | 归属与处理 |
|---|---|---|
| 全量 2.7.0 目标/平台台账 | [#19](https://github.com/JavaZeroo/termforge/issues/19) | R5 的验收基础，现在并行推进；平台/架构/最低系统边界须在 R2 产品路线决定前冻结；将声明、已观察和未知分开 |
| 实际外部核心候选与 ADR-0003 重审 | [#20](https://github.com/JavaZeroo/termforge/issues/20) | R1 可先细拆并行的候选构建/适配；R2 扩大模式与常用 TUI 实现投入前完成路线决定 |
| Server 2022 ConPTY 稳态 +1 句柄问题 | [#21](https://github.com/JavaZeroo/termforge/issues/21) | R4，保留严格探针和已知失败，不扩大支持声明 |
| 自动观测、开销校准、完整记录 Schema | [#3](https://github.com/JavaZeroo/termforge/issues/3) | R4 收敛，工具工作可提前推进；停止的手工批次不补跑 |
| 正式 WindTerm 性能基线 | [#4](https://github.com/JavaZeroo/termforge/issues/4) | R4，依赖 #3；节流/交互驱动、正确性、无插桩对照与重复批次仍需完成 |

自有分层计量的剩余归因/Unicode/大历史/无插桩工作由 R4 总项承接，相关基础测量贯穿 R1–R3。官方 baseline、真实呈现端点、完整 TUI 和跨平台客户端均未因本次整理变成已完成。

## 旧任务处理与维护

- #1/#2 保留关闭，正文压缩为已交付范围和证据。
- #5 按最小规格、临时 ADR 和首个自动实验的原范围结项；外部原生候选及最终核心路线由 [#20](https://github.com/JavaZeroo/termforge/issues/20) 承接，产品架构决策留在 R2。
- #3/#4 保留开放并归入 R4。旧 [M0 milestone](https://github.com/JavaZeroo/termforge/milestone/1)以“已迁移”归档，不表示其正式基线目标完成。
- `ready-for-agent` 表示范围明确、阻塞已解除；`blocked` 表示存在真实前置；`needs-breakdown` 表示进入实施前需细拆。每次开工核对实时状态。
- 变更合同先更新对应规格，再同步 issue；完成任务填写验收证据并更新功能矩阵，不能只在聊天或批次记录中报完成。关系使用 GitHub 原生 sub-issue / blocked-by，正文同步可阅读的前置链接。
