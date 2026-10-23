# 当前任务来源

仓库及 issue tracker 为 [JavaZeroo/termforge](https://github.com/JavaZeroo/termforge)，CLI 使用 `gh`。
当前执行规格是 [STATUS-AND-BACKLOG](../STATUS-AND-BACKLOG.md)；E1/C1/T1 等为文档任务标识，不是 GitHub issue 编号。
已有 #1–#5 的状态以 GitHub 为准。未配置自动 triage 或外部 PR 处理，代码审查不修改 issue 状态。

第一批实施的审查起点固定为 `acc38c3`（本批开始前 HEAD）；规范来源为 AGENTS.md、CONTRIBUTING.md、PROVENANCE.md，需求来源为上述任务地图与用户批准的第一批工作。

第二批实施的审查起点固定为 `51b50ef`。范围为 C2、V1 及 P1 计量校准；P1 的完整分层归因和 A1 的 shell 整合不在本批验收范围内。

第三批 A1 的审查起点固定为 `a559541`，范围为固定网格本地 shell 闭环、基础输入、有界调度与回收。C5 resize 与完整 G1 验收仍属后续任务。

第四批 C5 的审查起点固定为 `8e77b0d`。范围为核心物理行 resize、窗口/ConPTY 尺寸同步，以及 G1 基础本地交互验收；详细行为见 ADR-0005。不包含 reflow、IME、全屏 TUI、SSH 或性能等价声明。

UI-B 的审查起点固定为 `905c0fc`。范围为五条分隔线拖动、标签重命名与布局比例/显隐保存恢复；需求来源为 UI 调查报告中的 UI-B 切片及用户继续实施的指令。比较包含该基点之后的工作区变更和新增文件，不包含 WindTerm 完整行为等价、主连接标签、字体/DPI 或会话恢复。
