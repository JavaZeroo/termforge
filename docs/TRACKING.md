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

## 已验收的R1任务

R1已按[限定合同](milestones/R1-local-text.md)验收，[完整组合证据](../experiments/shell/results/2026-10-24-r1-system-workflow/validation.json)与同源码回归分列。下表保留切片及其原始依赖；当前执行状态仍以GitHub为准。

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

## Alpha 交付切片

[#22](https://github.com/JavaZeroo/termforge/issues/22)已交付首个 [v0.1.0-alpha.1](https://github.com/JavaZeroo/termforge/releases/tag/v0.1.0-alpha.1) Windows x64 免安装预发布包；静态运行库、精确清单、最终 ZIP 启动与发布下载均已[核验](releases/v0.1.0-alpha.1-validation.json)。它交付 R1 体验包；R2 的完整开发版出口保持不变。发布说明见 [Alpha 1](releases/v0.1.0-alpha.1.md)。

## R2执行切片

以下覆盖[总项#7](https://github.com/JavaZeroo/termforge/issues/7)的全部工作包；详细范围、验收清单和实时状态只在对应 issue 维护。原生 sub-issue / blocked-by 是依赖关系的权威记录。

| 切片 | GitHub | 可验收交付 | 真实前置 |
|---|---|---|---|
| R2-00 | [#23](https://github.com/JavaZeroo/termforge/issues/23) | 冻结产品路线所需平台、架构与最低系统边界 | 无；父项为全量台账 #19 |
| R2-01 | [#24](https://github.com/JavaZeroo/termforge/issues/24) | 原生候选干净构建及 headless 状态/历史投影 | 无；父项为核心关卡 #20 |
| R2-02 | [#25](https://github.com/JavaZeroo/termforge/issues/25) | 共同轨迹、真实应用、六类差异与持续 fuzz 比较 | #24 |
| R2-03 | [#26](https://github.com/JavaZeroo/termforge/issues/26) | 候选成本测量与 ADR-0003 核心路线决定 | #23、#25 |
| R2-04 | [#27](https://github.com/JavaZeroo/termforge/issues/27) | 备用屏、滚动区域及关联模式 | #26 |
| R2-05 | [#28](https://github.com/JavaZeroo/termforge/issues/28) | 终端回复、输入/输出模式及固定真实 TUI | #27 |
| R2-06 | [#29](https://github.com/JavaZeroo/termforge/issues/29) | resize、逻辑行与历史语义 | #26 |
| R2-07 | [#30](https://github.com/JavaZeroo/termforge/issues/30) | 复杂 Unicode、IME、键盘与选择语义 | #26 |
| R2-08 | [#31](https://github.com/JavaZeroo/termforge/issues/31) | 字体网格及真实 DPI/跨显示器验收 | 无 |
| R2-09 | [#32](https://github.com/JavaZeroo/termforge/issues/32) | 固定本地 shell 与真正的分屏/标签工作流 | 无 |
| R2-10 | [#33](https://github.com/JavaZeroo/termforge/issues/33) | 会话配置、窗口与工作区恢复及迁移/损坏处理 | #32 |
| R2-11 | [#34](https://github.com/JavaZeroo/termforge/issues/34) | 产品 GUI/平台、核心历史接口与呈现隔离决定 | #23、#26、#28–#33 |
| R2-12 | [#35](https://github.com/JavaZeroo/termforge/issues/35) | Windows R2 日常开发版与路标出口验收 | #27–#34、#36 |
| R1 回归 | [#36](https://github.com/JavaZeroo/termforge/issues/36) | 定位持续 cmd 输出时 ETX 不中断并恢复 normal/resize 与完整 CTest | 无；不得减少负载或仅延长超时 |

## 从当前状态接着做

以下为选题顺序；各项已做/待做和验收结果只在对应 issue 维护。`ready-for-agent` 表示可以开工，不代表全仓库所有 ready 项优先级相同。

1. **完成R2核心路线决定。** [#23](https://github.com/JavaZeroo/termforge/issues/23) 已冻结[平台、架构与声明的最低系统边界](../research/windterm-2.7.0-platform-scope.md)，全量功能台账仍由 #19 承接。[#24](https://github.com/JavaZeroo/termforge/issues/24) 的[固定候选适配](../experiments/candidates/alacritty/README.md)已交付并通过本地干净构建/headless 检查；下一步由 #25/#26 完成共同轨迹和 ADR-0003 重审。扩大VT模式/TUI实现前必须完成 #26。
2. **并行推进独立的显示与会话切片。** [#31](https://github.com/JavaZeroo/termforge/issues/31) 已形成[字体/显示合同](milestones/R2-font-display.md)，完成实际 glyph/fallback、私有字体度量/缺失资产检查及两台 96-DPI 显示器的[真实往返证据](../experiments/shell/results/2026-10-24-r2-monitor-font/validation.json)；不同 DPI 与实际呈现保持 OPEN。[#32](https://github.com/JavaZeroo/termforge/issues/32) 已交付[四种固定本地 shell 生命周期](../experiments/shell/results/2026-10-24-r2-shell-lifecycle/validation.json)及[主区一个真实左右分屏](../experiments/shell/results/2026-10-24-r2-main-split/validation.json)；任意嵌套、拖放/搜索/颜色和分屏持久化继续后续。
3. **按依赖整合用户结果。** 核心路线决定后推进 #27–#30，#32 后推进 #33；真实工作流齐备后由 #34 决定产品架构，最终由 #35 验收完整开发版，不能以单项成功替代 #7 出口。
4. **持续保留R1系统回归。** 基础动作、五组IME与同次八会话真实工作流分开运行，再核原始字节、目标、阶段资源和回收。当前 [#36](https://github.com/JavaZeroo/termforge/issues/36) 保留 normal/resize 的真实失败，[输入机制研究](research/conpty-ctrl-c-input.md)确认 ETX 正确，并把未决点收窄到洪流写入与 conhost 控制分派之间；它是 #35 的出口前置，不能用本机跳过或旧证据替代修复后的新源码验收。
5. **保持性能与UI调查并行。** 正式WindTerm基线、观测扰动、实际呈现和长期稳定继续按下表推进；真实跨屏、UI其余状态和空面板能力仍按后续合同交付。停止的手工批次不自动补跑。

未提交草稿和未完成的系统验收均不计入交付。开工前核对工作树，保留已有草稿并明确归属；文档整理本身不解除功能验收门槛。

## 并行调查与遗留门槛

| 项目 | GitHub | 归属与处理 |
|---|---|---|
| 全量 2.7.0 目标/平台台账 | [#19](https://github.com/JavaZeroo/termforge/issues/19) | R5 的验收基础，继续并行推进；平台切片 #23 已完成[范围台账](../research/windterm-2.7.0-platform-scope.md)，其余功能域和运行观察未因此完成；声明、已观察和未知继续分开 |
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
