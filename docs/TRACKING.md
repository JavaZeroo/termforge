# 任务跟踪

本页只导航到 GitHub 任务，不维护第二份进度表。能力与构建配置见 [FEATURES](FEATURES.md)，R1–R5 范围见 [PLAN](PLAN.md)，当前接续顺序与完成条件见 [R2 交付路线与验收](milestones/R2-local-terminal.md)。实时状态、原生父子/阻塞关系和验收记录以各 issue 为准。

## 产品路标

| 路标（远到近） | GitHub milestone | 总项 |
|---|---|---|
| R5 · WindTerm 2.7.0 完整复刻验收 | [milestone 6](https://github.com/JavaZeroo/termforge/milestone/6) | [完整目标验收](https://github.com/JavaZeroo/termforge/issues/10) |
| R4 · 高级功能、跨平台与性能收敛 | [milestone 5](https://github.com/JavaZeroo/termforge/milestone/5) | [高级工作流](https://github.com/JavaZeroo/termforge/issues/9) |
| R3 · SSH/SFTP 远程日常版 | [milestone 4](https://github.com/JavaZeroo/termforge/milestone/4) | [远程日常版](https://github.com/JavaZeroo/termforge/issues/8) |
| R2 · 可靠本地终端与开发版 | [milestone 3](https://github.com/JavaZeroo/termforge/milestone/3) | [当前路标](https://github.com/JavaZeroo/termforge/issues/7) |
| R1 · 基础文字交互 | [milestone 2](https://github.com/JavaZeroo/termforge/milestone/2) | [限定验收](https://github.com/JavaZeroo/termforge/issues/6) |

## R2 实施与联合验收入口

`ready-for-agent` 表示任务的独立工作可以开始，不表示优先级相同或最终联合配置已经通过。聚合项有未完成硬前置时保持 `blocked`；旧配置的独立诊断与最终联合验收条件分别写明。

| 工作 | 责任任务 | 硬前置或联合条件 |
|---|---|---|
| 主线候选与官方后端的联合基线 | [联合配置基线 #55](https://github.com/JavaZeroo/termforge/issues/55) | 从明确的 main 与 Draft 身份整合；可带已知 red 如实结项，不等待所有后端问题先修好 |
| 原生 API / VT 末端 margin 的处理路径 | [平台语义与方案 #56](https://github.com/JavaZeroo/termforge/issues/56) | 可用固定探针独立推进；不能以策略说明豁免 #41 原合同 |
| 实验后端连续 resize | [残尾与 Unicode 重放 #51](https://github.com/JavaZeroo/termforge/issues/51) | 原冻结配置可诊断；最终修复验收使用 #55 联合基线 |
| 实验后端 Vim | [Console-width Vim #52](https://github.com/JavaZeroo/termforge/issues/52) | 同上；GUI stage59 与 native new-buffer 分列，早期 flags0 成功不替代最终配置 |
| 实验后端未完成粘贴取消 | [确定性 sender fence #53](https://github.com/JavaZeroo/termforge/issues/53) | 保留 3MB/768 checkpoint/16KiB 门槛；最终验收使用 #55，completed 不算 cancelled |
| 终端模式、备用屏与滚动区 | [终端行为 #27](https://github.com/JavaZeroo/termforge/issues/27) | 核心前置 #26 已验；整体验收由 #41 阻塞 |
| resize、逻辑行与历史 | [resize 合同 #29](https://github.com/JavaZeroo/termforge/issues/29) | 核心前置 #26 已验；与 #27/#28 联合核对模式、区域、历史和实际 child 尺寸 |
| 查询回复、应用模式与真实 TUI | [固定应用矩阵 #28](https://github.com/JavaZeroo/termforge/issues/28) | #27；完成时核对 #29 的 resize 合同及目标配置 |
| 完整 Unicode、IME 与选择/键盘 | [输入工作流 #30](https://github.com/JavaZeroo/termforge/issues/30) | #26/#39 已验；首批字素基础不替代最终配置的真实输入与同次八会话 |
| 字体、真实不同 DPI 与跨屏 | [真实显示 #31](https://github.com/JavaZeroo/termforge/issues/31) | 当前为实际环境缺口；自动环境准备可并行，不反复运行相同 96-DPI 场景 |
| 字素吞吐成本定位与预算 | [受控性能 #54](https://github.com/JavaZeroo/termforge/issues/54) | 独立并行；固定负载/端点/重复方法与预算依据，再优化，不等 #34 最后才研究 |

## R2 可靠性与聚合关卡

| 工作 | 责任任务 | 完成条件的归属 |
|---|---|---|
| 持续输出中的 Ctrl-C 历史失败 | [原始 ETX 回归 #36](https://github.com/JavaZeroo/termforge/issues/36) | 有界定位历史因果与原生交付；后续通过不回填旧因 |
| 原 10 秒纯夹具超时 | [IME inventory 定位 #40](https://github.com/JavaZeroo/termforge/issues/40) | 阶段与清理已补齐；下一步是固定源码/宿主的有界原因定位 |
| controller 自然退出 | [close 前退出 #44](https://github.com/JavaZeroo/termforge/issues/44) | 保留 send/wait/pre-close 与清理差异；不能以 close 后结果当自然退出 |
| 旧 normal-1 未知异常 | [事件转换边界 #57](https://github.com/JavaZeroo/termforge/issues/57) | 独立于已修合法空标题 #45；原事件 kind/载荷未知，不能回填根因 |
| Windows 工作流可靠性 | [回归汇总 #38](https://github.com/JavaZeroo/termforge/issues/38) | #40/#44/#57；已闭合切片和旧失败保留在该总项，不再排入当前队列 |
| 显式官方 ConPTY 整合 | [后端汇总 #47](https://github.com/JavaZeroo/termforge/issues/47) | #55/#51/#52/#53，实验可选不等于产品默认或日常验收 |
| 每缓冲区 margin 端到端合同 | [margin 汇总 #41](https://github.com/JavaZeroo/termforge/issues/41) | #47/#56；原生 API、transport 和末端分别验证 |
| 产品 GUI/平台与核心边界 | [架构定案 #34](https://github.com/JavaZeroo/termforge/issues/34) | #28–#31/#54，结合已验 #23/#26/#32/#33；维护正式 ADR |
| 最终 Windows 开发版 | [产物与持续运行 #35](https://github.com/JavaZeroo/termforge/issues/35) | #27–#34/#36/#38；在最终产物上执行完整合同后核对 #7 出口 |

## 已交付基础与历史入口

| 范围 | 入口 |
|---|---|
| R1 基础文字与同次八会话限定验收 | [R1 总项 #6](https://github.com/JavaZeroo/termforge/issues/6)、[组合验收 #18](https://github.com/JavaZeroo/termforge/issues/18)、[合同](milestones/R1-local-text.md) |
| Alpha 1 Windows x64 体验包 | [发布切片 #22](https://github.com/JavaZeroo/termforge/issues/22)、[发布说明](releases/v0.1.0-alpha.1.md)、[下载与验证](releases/v0.1.0-alpha.1-validation.json) |
| R2 平台与核心前置 | [平台 #23](https://github.com/JavaZeroo/termforge/issues/23)归属 [全量台账 #19](https://github.com/JavaZeroo/termforge/issues/19)；[候选 #24](https://github.com/JavaZeroo/termforge/issues/24)、[差分 #25](https://github.com/JavaZeroo/termforge/issues/25)、[成本/路线 #26](https://github.com/JavaZeroo/termforge/issues/26)归属 [核心关卡 #20](https://github.com/JavaZeroo/termforge/issues/20) |
| 有限本地会话与配置恢复 | [本地会话 #32](https://github.com/JavaZeroo/termforge/issues/32)、[持久化 #33](https://github.com/JavaZeroo/termforge/issues/33)，能力边界见 [FEATURES](FEATURES.md) |
| 首批流式字素、宽度与 glyph | [字素合同 #39](https://github.com/JavaZeroo/termforge/issues/39)、[来源与合同](research/streaming-graphemes.md)，完整输入由 #30 继续验收 |
| 已闭合的回归/夹具与参考捕获 | #38 的原生子项及 [EVIDENCE](EVIDENCE.md)保留 #37/#42/#45/#46 等固定原因和证据，不再次当作待做功能 |
| 旧 M0/G1、批次规格与审查 | [归档说明与迁移表](archive/README.md)；历史记录不作为当前 TODO |

## 可提前推进的远期调查

| 工作 | 责任任务与边界 |
|---|---|
| WindTerm 2.7.0 全量目标与平台台账 | [全量目标 #19](https://github.com/JavaZeroo/termforge/issues/19)，R5 验收基础；#23 平台切片完成不代表其余行为已取证 |
| 自动观测、扰动校准与完整 Schema | [观测工具 #3](https://github.com/JavaZeroo/termforge/issues/3)，R4 收敛；停止的手工批次不默认重启 |
| 正式 WindTerm 性能基线 | [受控对照 #4](https://github.com/JavaZeroo/termforge/issues/4)，依赖 #3；不能用自有微基准替代 |
| Server 2022 ConPTY 稳态句柄 | [兼容性 #21](https://github.com/JavaZeroo/termforge/issues/21)，R4；严格门槛与已知失败保留，不扩大支持声明 |

任务完成时更新责任 issue、能力行与证据索引。修改合同时同步规格和原生关系；记录未知或整理文档本身不解除产品出口。维护规则见 [issue tracker 约定](agents/issue-tracker.md)。
