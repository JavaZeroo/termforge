# 工作跟踪约定

仓库：[JavaZeroo/termforge](https://github.com/JavaZeroo/termforge)，CLI：`gh`。当前入口为 [路标](../PLAN.md)、[能力矩阵](../FEATURES.md)、[R2 交付路线与验收](../milestones/R2-local-terminal.md)和 [GitHub 映射](../TRACKING.md)。[R1 规格](../milestones/R1-local-text.md)是已验收的回归合同。

- milestone 对应 R1–R5；每个路标有一个总项。远期总项先保留工作包，进入实施前才拆小。
- 近期任务使用 `ready-for-agent` / `blocked` / `needs-breakdown` 标记；完成以 issue 状态为准。`ready-for-agent` 只用于范围明确且真实阻塞已解除的切片，不是“已开始”或“已完成”。
- 聚合项有未完成硬前置时用 `blocked`；可执行工作放在独立、未阻塞的子项中。外部环境缺口在正文写清实际缺什么，不把相同环境的反复运行当作推进。关闭项移除 ready/blocked 等执行标签，保留主题标签与证据。
- 原生 blocked-by 表示硬前置；尚可在旧配置独立诊断、但最终须在联合配置复验的条件，分别写清诊断范围和完成条件，避免人为制造串行或父子依赖环。
- 使用原生 sub-issue 和 blocked-by 关系；任务正文也列出依赖，便于源码环境阅读。修改依赖时同步正文和关系，不以大路标的顺序制造不必要串行。
- 开工先核对 issue、当前 milestone 和工作树，固定本次审查基点；提交验收结果后更新对应能力行并关闭任务。不要把完整产品工作长期塞进旧研究 issue。
- 改范围先改规格，再同步 issue；移动到归档的记录是当时的证据，不能作为当前待办。原始结果/已知失败不因文档整理而改写。
- 规划整理不自动算功能交付；功能开始实施时另行记录源码基点与验收。旧任务和新路标的映射见 [归档说明](../archive/README.md)。
