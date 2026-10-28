# #129 raw-capture-v1：采集与分析分离，等待复核

本方案只完成离线准备，**没有启动新原版**，私有计划为 `execution_authorized=false`。旧 [#126 的 0 grab 停止](windterm-1049-raster-reference-2026-10-28.md)和 [#129 的 3 grab partial](windterm-raster-selection-reference-2026-10-29.md)随 [PR #132](https://github.com/JavaZeroo/termforge/pull/132) 独立保留，均未改写，也没有关闭 parent 颜色调查。

新 case 为 `raw-capture-v1`，独立工作树及分支 `research/windterm-raster-raw-capture`，固定生产源 `f6d9a97e2b2685b6ce39c83dc7107cb52aec7a6a`。[不可运行计划](../../research/ui/results/2026-10-29-raster-raw-proposal/plan.json)列出七个生产源 SHA、分析源 SHA、四个完整模块 SHA、五段输入的 hex/长度/SHA 与单次预算。最终复核及明确运行指令后，必须另建新授权计划和唯一目录，不能把此 false proposal 改为 true 或复用旧批目录。

## 本次改变的合同

[GUI 采集器](../../research/ui/raster_raw_reference.js)在一次 grab 得到的同一 QImage 内，先读完本阶段每个已绑定 H 与其前置 space 的完整 ROI 数组。它不计算主要墨迹、不比较颜色、不以双 H 颜色是否相等作为推进条件。只有结构验证和原生对象回收完成后，`capture_valid=true` 并写 `captured` 确认，允许 peer 发送下一阶段。

| 情况 | 采集推进 | 离线处理 |
|---|---|---|
| 主要墨迹频数并列 | 收完全部 ROI 后继续 | dominant 为 null，保留全部并列候选及频数 |
| space 非单色 | 收完全部 ROI 后继续 | 背景及墨迹分类未知，保留完整 histogram/hash |
| H 与背景无差异 | 收完全部 ROI 后继续 | `no-marker-ink`，不据此匹配样式 |
| 双 H 的输出不一致、正控别名或无匹配 | 继续采集后续阶段 | 保留各自签名，多义或无匹配为 null，不推定 reset |
| 身份、端点、标签、caret、ROI/DPR/空图或原生调用失败 | 保存真实 partial 并停止 | 不移动 ROI、不补抓、不重跑 |

`capture_valid` 只指结构采集有效，不表示颜色、样式、active grid、presented 或产品验收通过。结构失败允许保留已取得的部分 ROI，不能把缺失像素补造为完整阶段。

## 不变的五阶段及边界

五段仍为 `43/38/20/53/54` 字节，各另追加 `CSI 6n`，详见新计划及[生产器](../../research/ui/raster_raw_reference.py)。D0=`SGR0`，S1=`SGR31;44`，S2=`SGR32;45`。

| 阶段 | 内容 | 结束前 SGR |
|---|---|---|
| 0 | 一图内 D0 双 H、M0/S1 双 H | S1 |
| 1 | `1049h` 后原样 A0 H，同一行 S1/D0 正控 | D0 |
| 2 | S2 的 A1 双 H | S2 |
| 3 | 重复 `1049h` 后原样 A2 H，同一行 D0/S1/S2 正控 | S2 |
| 4 | `1049l` 后原样 M1 H，同一行 S1/S2/D0 正控 | D0 |

保留每个前导空格和完整 token 匹配，M1 不匹配 M1S1。运行中逐条完整 NAWS 事件守卫、独立 DSR 回复与阶段确认均保留。GUI 主线程主动读取实际 isAltScreen/isMainScreen 和有限非负 P 端点；caret=anchor 才允许采集，selectionText 只记 type/length。实际 caret（零宽仍按至少 1 逻辑像素）不得与任何 H/space 相交。marker 的 ordinal/完整 label/group/sample 必须按固定身份一一对应，不能以重复条目补齐数量。

仍使用固定官方 WindTerm 2.7.0、隔离 WindowStation/Desktop、OwnedJob、单合成 loopback/profile。最多一次原版运行、每阶段一次 grab、总最多五次；REA capture45、CLI wait60，原版更早启动并另等 2 秒，整程上限未知。禁止用户输入桌面、剪贴板、DPI 变更及计时结论。

Qt5 API/ABI 沿用已核实的完整模块 SHA、实际模块路径与固定导出 RVA：QWidget::grab→QPixmap::toImage→QImage::pixel，QRect 为 inclusive 边界；QPixmap/QImage 各分配 32B、不预构造，正常返回后 image→pixmap 普通析构。仍以 QImage::pixel 逐点读 QRgb，不猜 format/stride。原生 bool 的精确 bool 或整数 0/1 表示显式核对并保留原类型。

## 离线分析与公开出口

[分析器](../../research/ui/raster_raw_analysis.py)先独立检查结构、完整 token 位置、ROI 顺序/唯一身份、DPR、样本数量和 uint32 值，再为每个 H/space 保留完整 RGBA histogram、样本数和有序 QRgb 数组 SHA。数组本身留 local。主墨迹并列只生成 null 和候选，不抛出采集失败，也不改变原批结果。

同帧比较要求目标与正控都在同一文档行及逻辑行，并且都观察到墨迹。匹配条件是 H 和 space 各自的**有序数组 SHA、宽高与样本数完全相同**；只靠主要颜色或 histogram 相同不足以匹配。匹配多个控或未匹配时唯一结论为 null。即使匹配唯一，也只称该同帧原样 H/space 的输出签名与显式正控一致，不声称读到了内部 pen、完整 current_style 或 reset。

[公开 CLI](../../research/ui/export_raster_raw_reference.py)使用显式 require/ValueError，只有身份、合成文本、消息、生命周期及结构审计全部完成后才创建输出目录。公开只有合成源、histogram/频数/尺寸标量和 hash；raw 像素、日志、路径、PID、资源及 profile 均留 local。`raw_capture_complete=true` 与颜色分类未知可以同时成立，不转化为五阶段样式对齐通过。

## 离线复现与验证

[既有 phase2 回放](../../research/ui/results/2026-10-29-raster-raw-proposal/replay.json)绑定原 Evidence ID `ev_432abe8baf4fc86efd4994cd12cc3ab5fe0b5120476d22540c5b26e2e3c693a4` 和原 raster-result SHA `3887faaa8d055f42388eafc4b0a9ee2e2b8111d3fa1384e07ec5f0a50585f34d`。新分析器正确保留两个 16 次候选；旧批第二个 H 没有采样、phase3/4 没有发送，这些缺口均未补造。既有 phase1 的完整 raw 签名也唯一匹配同帧 D0。该回放的新原版运行数为 0，不能把旧 partial 变成新完整采集。

[公共测试](../../research/ui/test_raster_raw_reference.py)与[合成 Qt 边界](../../research/ui/test_raster_raw_native.js)按已确认的采集入口/离线 CLI seam 逐项红→绿：旧逻辑遇 tie 只留 1/4 ROI，新逻辑收全 4；tie 分类从异常变为 null/candidates；重复 marker、重复 ROI 和错误完整 label 从可通过变为拒绝。完整合成五阶段即使 phase2 tie 也能导出全部 ROI；同 histogram 但不同排列不匹配，别名及无墨迹保持未知。另覆盖普通/优化解释器的隐私/身份/结构拒绝不得生成公开输出、32B sret 和异常析构边界，以及 false proposal 不得启动或产生 attempt。

离线普通及 `python -O` 各 11 项通过，相关 CTest `windterm_raster_style_reference`、`windterm_raster_selection_proposal`、`windterm_raster_raw_proposal` 在 Debug 配置下 3/3 通过。`core.autocrlf=true` 的 fresh checkout 也核对新 JS 与 Qt 前缀 SHA 一致。未启动新原版，不提供新的样式或完整采集事实；最终源与计划交 root 复核后等待独立明确授权。
