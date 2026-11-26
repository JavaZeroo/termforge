# 备用屏 RIS 后的可见正文与回看历史

六个隔离的 WindTerm 2.7.0 Windows x64 进程各执行一个固定的本地 Telnet 计划。每组为 91×25、八个阶段。初始帧用 `HIST`、`MAIN`、`ALTT`、`POST` 建立四种同坐标字形模板。校准后覆盖原标记，建立两行历史，在主屏第 4 行和备用屏分别写入标记，再执行 RIS、主屏写入、显式返回及备用屏复入。所有行列从 1 起算。

| 原版轨迹 | 回看与可见正文 |
|---|---|
| [逐阶段滚动](https://github.com/JavaZeroo/termforge/blob/36971f5df893edb75677829eef86ceeba7f4c99a/research/ui/results/2026-11-26-ris-content/scroll-all.json)、[仅最终滚动](../../research/ui/fixtures/2026-11-26-ris-content/scroll-last.json) | 备用屏第 6 行写入 `ALTT`。返回后滚到底部可见主屏第 4 行的 `MAIN`。滚到顶端，`HIST` 位于第 1 行，`ALTT` 位于第 8 行，`MAIN` 位于第 16 行。最大滚动偏移为 12。 |
| [逐格搜索四种模板](https://github.com/JavaZeroo/termforge/blob/36971f5df893edb75677829eef86ceeba7f4c99a/research/ui/results/2026-11-26-ris-content/all-columns.json) | 在复位后已发出主屏第 8 行第 9 列的 `POST`，当前可见网格未检出其校准字形。最终滚到顶端检出三种旧标记，底部只检出 `MAIN`。 |
| [备用屏写到第 17 行](../../research/ui/fixtures/2026-11-26-ris-content/long-alternate.json) | 主屏原有 10 行有效内容，同一滚动范围内未检出 `ALTT`。原有 `HIST` 和 `MAIN` 保持可读。 |
| [主屏有效内容增至 16 行](../../research/ui/fixtures/2026-11-26-ris-content/long-primary.json) | 备用屏第 14 行写入 `ALTT`。返回后最大滚动偏移增至 18，滚到顶部时 `ALTT` 出现在第 16 行，`MAIN` 在第 22 行。 |
| [再次进入备用屏并写入](../../research/ui/fixtures/2026-11-26-ris-content/reentry-write.json) | 复入后立即在备用屏第 11 行写入 `POST`，可见网格检出该标记。返回主屏后仍能向上回看旧 `ALTT`。仅发送复入命令时，原版快照仍呈现上一主屏的 `MAIN`。 |

像素观察取自原版编辑器视口的 QWidget grab。记录中的 `?` 表示有笔画但不能与四个校准模板精确匹配。活动备用屏的首个 `ALTT` 属于这种情况，其写入字节、文档索引和有笔画的目标格均保留。回看时该字形可以精确识别。逐格搜索测试的是这四种模板，字形相同或不同均不能揭示内存中的内部样式位。

每份摘要记录采集源码提交、固定程序与计划 SHA-256、完整严格导出及原始结果 SHA-256、阶段输入与 DSR、实际滚动位置和清理结果。原始截图和发行物保留在受控环境。

## 产品决定与验证

项目负责人确认复刻旧文字回看行为，并让复位后新写入的文字在目标位置可见。带实际输出的备用屏复入有独立的字形对照，空复入时的逐格外观由 #335 跟踪。[ADR-0011](../adr/0011-alternate-ris-output.md)

旧固定宿主 `termforge.4` 的八次 DSR 均吻合。复位后 Qt 侧历史仍只有两行，且 Win32 读回的主屏丢失了 `MAIN`。修订版在自主核心保留受主屏有效内容高度限制的备用屏行，遵守主屏历史容量。固定宿主切回主屏时保留其正文并复位光标，主屏自身执行 RIS 时仍清除内容。[修复前逐阶段对照](https://github.com/JavaZeroo/termforge/actions/runs/45807335065/job/139865917733)

四组产品回归各八阶段通过，逐阶段核对主备屏、Qt 与 Win32 网格、主屏历史及子进程 DSR，结束后操作产品滚动条并读取实际绘制页。原有 19 项 Qt 离屏回归和四组跨尺寸 RIS 对照通过，本地核心 38/38。[正式 Qt 日志](https://github.com/JavaZeroo/termforge/actions/runs/45809393299/job/139872973953)
