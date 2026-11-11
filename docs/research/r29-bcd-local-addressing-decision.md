# #29/#27 决策材料：旧六阶段 B/CD 的本地终端寻址

- 状态：**方案材料，未改变产品行为、测试或用户已确认差异**；等待用户决定
- 日期：2026-11-05
- 取证状态：2026-11-05 已按 §6 授权的单次原版尝试失败于初始几何（目标 `80×24`，实际 `80×25`，未进入正文），保留[失败记录](../../research/ui/results/2026-11-06-resize-bcd-address-pre-write-failed/README.md)；未重跑、未形成 A/B/C 结论。
- 范围：[#29](https://github.com/JavaZeroo/termforge/issues/29) / [#27](https://github.com/JavaZeroo/termforge/issues/27) 联合出口中的“旧六阶段 `--resize` B/CD 同步失败”
- 依据：[ADR-0010](../adr/0010-bounded-resize-retention.md)、[#308 顺序宽高样本](../../research/ui/results/2026-11-02-resize-combined-reference/README.md)、[#309 只缩宽样本](../../research/ui/results/2026-11-02-resize-state-reference/README.md)、[#316 双轴写后端点](../../research/ui/results/2026-11-04-resize-dual-axis-reference/README.md)、[#317 同次 Qt/固定 ConPTY trace](../../experiments/shell/results/2026-11-05-qt-resize-divergence/README.md)

本文只整理“用户需要决定什么”。旧六阶段测试与失败保留，不把任何差异记为用户已接受，也不预先批准下面的路线。

## 1. 一句话冲突

TermForge 在缩窄/缩高后**保留源正文并以源坐标解释后续 CUP/ED/DSR**，再把保留行投影为当前宽度的显示页；固定 ConPTY 的**原生网格会自行 reflow**。同一个应用写入 `CUP 12;1`，在原生网格落在一处，在 TermForge 的保留源/显示投影落在另一处。旧六阶段测试把“原生网格行”当作核心应同步的目标，因此第一次在 stage 6 失败。

## 2. 固定 80×24→60×12 例子（均为实测，来自 #317 同一次 SessionModel 会话）

初始 80×24 主屏写入 `B`（源行 7、列 79）、`C`（源行 9、列 79）、`D`（源行 10、列 0）。公开 Qt resize 到 60×12；随后 child 发送固定 `CUP 12;1 + "RESIZED" + CUP 1;1 + "S6"`（CUP 为一基协议坐标，0 基行 11）。

| 观察层 | resize 后、写入前 | 写入 `CUP 12;1` 后 |
|---|---|---|
| 应用/child 所想的落点 | 尚未写入 | 它按自身 `60×12` 网格在 0 基行 11 写入 marker |
| 原生 ConPTY 网格（ACK） | 未采到（ACK 只在写入后返回） | row 7/9/10 为空；row 8=`19 空格+B`；**row 11=`RESIZED`+12 空格+`CD`**；row 12 为空 |
| 核心保留源正文 | B 仍在源 row 7、C 在 row 9、D 在 row 10 | CUP 12;1 落在保留源 row 11；源 row 11=`RESIZED`，B/C/D 仍保留 |
| Qt 显示投影页 | B 投影到 row 8，`CD` 投影到 row 11 | **row 11=`19 空格+CD`；row 12=`RESIZED`**；B 仍在 row 8 |
| 光标/DSR | — | 原生与核心 cursor 均为 `[0,2]`；无查询光标副作用差异 |

同次 trace 的事实边界：

- transport 在 resize 后只新增 **26 B**（stage 6 源 22 B + DSR 查询 4 B，offset 194–220），**没有 B/C/D 的搬移、擦除或整屏重绘字节**；原生 reflow 不经 transport 下发。
- resize 完成到 stage 6 ACK 之间的原生网格**没有观测**；不能用最终 ACK 反推该时点的中间态。
- 因此“只是把原生重排数据转发给 parser”不是可实施解释；差异来自原生网格与保留源/显示坐标层，而不是 transport 缺字节。

最直接的可观察结果：在这个固定样本中，应用写入的 marker 在原生网格位于 0 基 row 11，在 TermForge 显示页位于 0 基 row 12；`CD` 在 TermForge 显示页占 row 11，而原生 row 11 同时有 `RESIZED` 和 `CD` 尾文。旧 `--resize` 的失败信息仍是 `stage 6 failed: core resize is not synchronized with the literal native B/CD rows`，exit 1，未弱化。

## 3. 已观测 vs 推断

### 已观测

- 上述固定会话的 native ACK、核心源行、显示页、cursor、transport 字节与首次失败点（stage 6）。
- #316 的另一固定样本（窗口动作 `91×20→61×14`、固定 `CUP(7;2)+DQ`）只给写后端点：scroll 5、DSR 7;4、文档行 11 为 62 字符；没有写前原版正文，裁剪归因未知。
- #308 顺序宽高样本、#309 只缩宽样本的 WindTerm 文档/可见矩形/DSR 观察；它们**没有**本固定 80×24→60×12 的同场景 CUP 写入，也没有预写前正文快照。
- ADR-0010 已确认“保留完整正文”方向，以及隐藏帧返回按原版会裁剪但产品有意保留该差异（2026-11-04）。

### 推断（未在本仓库实测）

- 依赖绝对光标寻址的真实 shell/TUI（如全屏 TUI 重绘、提示符用 CUP 定位）可能在 TermForge 看到写入行下移；浏览器/日志/选择也可能与原生坐标不一致。
- 通过 `SetConsoleCursorPosition`、`WriteConsoleOutputW`、`ReadConsoleOutputW`、`GetConsoleScreenBufferInfo` 等 Console API 使用原生缓冲/网格坐标的应用，预期会受同一坐标层差异影响。
- 以上是应用类别推断，不是已复现的 shell/TUI/Console API 失败；没有真实应用探针或性能数据。

### 现有“保留完整正文”决定覆盖到哪里

ADR-0010 的确认覆盖：resize 不物理裁剪正文、历史/显示投影/行身份方向，以及隐藏帧返回保留完整正文这一处与 WindTerm 的有意差异。它**没有**决定活跃屏的 CUP/ED/DSR/滚动在“保留源坐标”还是“原生 reflow 网格”中解释，也没有批准把 native-grid 差异登记为已接受。因此本决策不能用“已经决定保留正文”代替。

## 4. 行为上互斥的路线

“继续按保留源解释 CUP”和“把 native-grid 列入差异”是同一行为（当前路线）的两种描述，不能作为两个选项。下面三条会改变用户能观察到的寻址行为：

| 路线 | 活跃屏地址权威 | 显示/内容 | 主要兼容后果 | 范围与可恢复性 | 批准状态 |
|---|---|---|---|---|---|
| **A. 保留源权威（当前）** | CUP/ED/DSR/滚动继续在保留源坐标层解释 | 保留完整正文；显示页按当前宽度从源锚点投影，选择/Find 用源身份 | 原生网格相对寻址的应用可能错行；正文不丢；旧字面断言继续失败 | 无需产品改动；可逆性最高（只保留现有测试/文档）；旧失败保留 | 已实施；差异**未**被用户接受 |
| **B. 原生网格权威** | resize 后以 ConPTY 原生 reflow 网格为活跃屏地址权威（写入/光标/选择/读回） | 显示可直接对应原生网格；保留正文若仍要保存只能是只读档案，不能成为第二活跃权威 | 更接近原生应用预期；可能破坏当前源阅读锚点/选择/Find/保留正文语义；跨平台行为分叉；是否符合 WindTerm 目标未知 | 需要 Windows ConPTY 读回、resize 原子事务、显示/选择/历史重接线；实现大、需 feature 回退；数据迁移需明确 | 未批准；需用户确认行为取舍与新范围 |
| **C. Windows 原生坐标→源坐标归一化** | 仍以保留源为唯一权威；仅在 Windows 适配层用可验证的 native→source 映射翻译应用写入/CUP/ED/光标，源正文不复制 | 保留完整正文与显示投影；写入落点更接近原生；映射缺失时回退 A 并显式标记 | 可同时保留内容决定与原生兼容，但需要真实的原生行↔源行映射；映射可能不完整/不可逆，复杂度高 | 不新增第二权威存储；适配层可开关；需要映射证据、回退测试 | 未批准；只有取得映射证据后才可评估 |

关于 C 的约束（用户已特别指出）：不得靠猜测注入重排序列，也不得建立另一套权威存储。C 只在“映射可观测、可验证、缺失即显式回退”时成立；如果无法证明映射，C 不成立，应保留 A 或转入 B 的讨论。

### 各路线验收如何证明正确

- **A**：现有 core retained/Qt 路径回归通过；固定 trace 的源行/显示/transport 复现；旧 `--resize` 继续失败且不被改写。若用户最终接受该差异，需要单独的决定文本，不能由测试绿灯推导。
- **B**：固定 80×24→60×12 的 marker 必须落在原生 ACK 的 row 11，显示/选择/读回一致；保留正文与锚点合同需重新验收（或明确删除）；真实 shell/TUI 与 Console API 探针；旧断言变更须经用户批准；提供 feature 关闭路径。
- **C**：固定样本的写入落点与原生一致或按声明回退；native→source 映射可逐行核对；无第二权威存储；映射缺失/陈旧时显式回退 A 并有测试；真实应用探针。

## 5. 当前能否让用户作决定

**不能形成“原版已证明应选哪条”的结论。** 现有 WindTerm 样本要么只缩宽、要么顺序宽高、要么双轴写后端点，几何/写入/阶段不同，不能拼成一个同场景原版结论；#317 是 Qt/固定 ConPTY trace，不是 WindTerm oracle。真实应用影响也只有推断。缺的不是更多同义转述，而是下列一个最小观察。

## 6. 单个最小取证计划（已授权尝试一次，因几何失败）

**2026-11-05 执行结果**：计划 `windterm-bcd-address-pre-write-v1` 已冻结并由调度者授权一次运行；实际初始 NAWS 为 **80×25**（非目标 80×24），控制器在 `fixed initial grid differs` 停止，未发送 B/C/D、未产生写前/写后端点，严格导出未运行。失败身份、计划/attempt 哈希与精确阻塞见[失败记录](../../research/ui/results/2026-11-06-resize-bcd-address-pre-write-failed/README.md)；同一计划不重放，也未据此选择 A/B/C。下面是原计划的目标与区分力，保留为下一次新计划的设计依据。

**2026-11-09 恢复进展（blocked）**：主机重新可达后完成严格身份核验；`1228×839` 初始窗口实测为 **80×24**，但固定 `988×509` 的 `60×12` resize 在校准 peer 报 `Resize changed row count`，仅 phase0 完成。未发送 B/C/D 正文，未执行原 v2 正式重试；旧 plan/marker 未变。一次校准计划 SHA `954636ee…`，详见[恢复记录](../../research/ui/results/2026-11-09-bcd-recovery/README.md)。解除条件：另一次授权的 60×12 校准/诊断解释该错误并成功，之后才可冻结 v2。

**固定问题**：WindTerm 2.7.0 在同一轮 `80×24→60×12` 主屏会话中，缩高后、写入前的正文/滚动/DSR 是什么；随后固定 `CUP 12;1+marker+DSR` 的 marker 落在保留源的哪一行、显示在哪一行。

- 唯一计划 `windterm-bcd-address-pre-write-v1`，一次授权运行；固定窗口请求 `80×24→60×12`，先写稳定 B/C/D，resize 后**不写入**先读快照，再仅发送固定 CUP/marker/DSR；不重跑、不补写。
- 前置：用户授权一次原版运行；固定 QEMU/WindTerm 2.7.0 身份；在现有探针中增加一个只读 pre-write 阶段（读取可见/文档行、scroll/DSR，不产生写入）；计划冻结与 `.attempted` 单次守卫；严格出口与公开件审核；不发布私有 raw。
- 产出应能区分：
  - 原版保留缩前源行、CUP 落保留源行 → A 更接近目标；
  - 原版显示/寻址跟随 reflow 网格 → 目标支持 B/C，需再用 C 的映射证据决定；
  - 原版裁剪/滚动出第三种行为 → 不能映射到 A/B/C，需要新方案。
- 该计划不测量真实 shell/TUI 兼容性；若选择 B/C，还需一个独立的真实应用探针作为后续验收，不应与本计划混合成一次“原版等价”结论。

## 7. 建议

在现有证据下不建议现在切换 B；C 也只有在取得原生↔源映射后才能评估。当前默认 **A** 是唯一不新增行为风险的选择，但它是**暂定保持**，不是已证明的原版等价或用户已接受的差异。建议按第 6 节完成一次最小原版观察，再由用户决定：

1. 若原版表现为源保留/源寻址，A 保持并明确旧测试的已知差异记录方式；
2. 若原版表现为原生 reflow 寻址，优先评估 C（保留正文、单一权威、显式回退），只有 C 不可行时才讨论 B 及其对保留正文/锚点/跨平台的影响；
3. 若结果不属于上述两者，先记录新行为，不强行二选一。

## 8. 现有批准与未批准

- **已批准/已实施**：ADR-0010 的保留正文与显示投影方向；隐藏帧返回保留完整正文并接受该处原版差异；当前历史/锚点/滚轮切片；旧六阶段失败保留。
- **未批准**：改变旧六阶段断言或把 native-grid 差异记为用户已接受；B 的原生权威；C 的归一化适配层；第二权威存储；任何数值性能门槛或 WindTerm 完整等价声明。
- #27/#29/R2 完整出口继续 OPEN；本材料不修改产品行为、不修改测试、不触发 Codex review、不自行合并。

## 9. 固定源码与证据索引

- 旧六阶段入口与断言：[`src/qt/terminal_modes_test.cpp`](../../src/qt/terminal_modes_test.cpp)、[旧模式报告](../../experiments/shell/results/2026-11-02-qt-terminal-modes/README.md)；`--resize` 仍为显式失败诊断。
- 正式 Qt 保留显示与历史/锚点： [`src/qt/terminal_widget.hpp`](../../src/qt/terminal_widget.hpp)、[`experiments/core/history_view.hpp`](../../experiments/core/history_view.hpp)、[只缩宽契约](../../experiments/shell/results/2026-11-03-qt-retained-display/README.md)、[历史浏览结果](../../experiments/shell/results/2026-11-05-qt-history-anchor/README.md)。
- 同次 Qt/固定 ConPTY trace：[README](../../experiments/shell/results/2026-11-05-qt-resize-divergence/README.md)、[trace.json](../../experiments/shell/results/2026-11-05-qt-resize-divergence/trace.json)；源码提交 `5e29437`，合并 main `c11e85e`。
- #316 双轴写后端点：[结果](../../research/ui/results/2026-11-04-resize-dual-axis-reference/README.md)；冻结运行源 `dee3c78`，离线出口/核心回放源 `2fc3f25`。
- #308/#309：[顺序宽高](../../research/ui/results/2026-11-02-resize-combined-reference/README.md)、[只缩宽](../../research/ui/results/2026-11-02-resize-state-reference/README.md)。
- ConPTY 输出/尺寸观察：[conpty-resize-output-2026-11-02.md](conpty-resize-output-2026-11-02.md)。
- 已确认架构：[ADR-0010](../adr/0010-bounded-resize-retention.md)、[ADR-0005](../adr/0005-physical-row-resize.md)、[r27-r29 方案](r27-r29-resize-proposal.md)。
