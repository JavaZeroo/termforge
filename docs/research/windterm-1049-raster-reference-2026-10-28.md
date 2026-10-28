# 1049 栅格样式调查：阶段 0 守卫停止，颜色仍未知

[#126](https://github.com/JavaZeroo/termforge/issues/126) 的一次已授权原版执行在 **抓图之前**结束。阶段 0 的 `selectionText` 返回非空字符串，执行脚本把它当作停止条件；没有调用 QWidget grab、QPixmap toImage 或 QImage pixel，没有生成栅格、ROI 像素、颜色或 presented 结果。当前样式继承与恢复仍未知。

这是调查守卫的语义假定有误，不能称为原版存在选区遮挡或样式错误。原执行源码和原始记录保持不变，没有改条件再抓取、补跑阶段或第二次启动原版。

## 固定执行身份

- 生产源码：`15c736b9274ed6bd2ba93842d27da8bc2eb87008`；[五阶段计划](windterm-1049-raster-plan.md)。在启动前提交源码，再生成唯一计划，计划 SHA 为 `deb78215e2a12108d73b0fc82fa6f5bf8e979f28ba71013d72f28818ef3e085c`。
- 固定官方 2.7.0 Windows x64 EXE SHA：`7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0`；4,047 文件清单 SHA：`e459adf2f12adb6a1b7f55e66a9da1c22794185e66e848057d21e0c60d669203`。
- 启动前核对 WindTerm/Qt5Core/Qt5Gui/Qt5Widgets 的**完整文件 SHA**，复制后完整清单再次核对。离线重审复制文件，四个完整 SHA 均与计划一致；运行中四个模块实际路径及所需导出 RVA 守卫通过。这不等于实际调用过栅格函数。
- 独立 WindowStation/Desktop、OwnedJob、独立 HOME/profile 和一个合成 loopback Telnet 会话；没有用户输入桌面、剪贴板或 DPI 变更。计划的单次 marker 保留，实际仅启动原版一次。
- REA capture=45 秒、CLI wait timeout=60 秒；原版更早启动并另有 2 秒等待，回收在 CLI 返回后进行，不提供整程 45/60 秒保证。动态插桩只用于正确性，不计性能。

REA Evidence ID 为 `ev_cedd36bf326d1769fa8d0d7b1cc508314eb22da43ba97faf80debbcf9b085e63`。完整脱敏标量及原件哈希见[白名单结果](../../research/ui/results/2026-10-28-raster-style-reference/validation.json)。

## 本次实际发生的步骤

五阶段分别计划主屏 SGR31/44 同图双 H 校准、首次 1049 进入、备用 SGR32/45、重复进入及退出后写 H。实际只发送了第一阶段的 31B 源及 `CSI 6n`，没有确认文件释放后四阶段。

阶段 0 精确源为 `ESC[0mESC[2JESC[HESC[2;3HESC[31;44mM0 H H`；DSR 回复为 `(2,9)`，主/备用状态为 `false/true`。NAWS 完整事件历史为 `91×24 → 91×25`，源冻结时事件数为 2；这次使用逐条完整 NAWS 检查的修订 peer，没有观察到冻结后的改变。

原版文档 API 返回了两个 H 标签位置。`selectionText` 返回 string，长度 10；`caretPosition=9`，caret rect 为 `x=211,y=18,width=0,height=18`。执行中的判断是 `selectionText === ''`，因此以原码 `selection-overlaps-observation` 停止：

| 指标 | 实际结果 |
|---|---|
| 已观察输入阶段 | 1 / 计划 5 |
| grab 尝试 | 0 |
| QPixmap/QImage 已构造与析构 | 均为 0 |
| 栅格与 H/space ROI 像素 | 无 |
| 颜色校准、比较 | 未执行，保持未知 |
| 原版真实选区或遮挡 | 未证实 |

原停止码保留原样，但导出另列 `actual_selection_or_overlap_proven=false`。非空 `selectionText` 不是这里可靠的选区判据，也没有因为未读到像素就声称 H 无墨迹。

## 既有原件对守卫假定的反证

停止后只读复核[既有原版样式/区域观察](https://github.com/JavaZeroo/termforge/blob/dce2ca799a25cf32f3cf2bacf1b9f7c915945a57/docs/research/windterm-styled-margin-reference.md)的本地原件，未运行原版。其 Evidence ID 为 `ev_8fbd9aead9aa5d60f49e05112c31eb542febf65084643c025eb8a52ffd590048`，`evidence.json` SHA 为 `be349abb34cf28f42b634ccf65bd0749e1e9940856059156b794e7968b7ef83c`。

该原件的 shell-ready、actual80x24 快照均为 `caret=anchor=97`，但 `selectionText` 长度为 5；styled-final 为 `caret=anchor=400`，但文本长度为 10。这是“非空返回值足以证明有选区”的直接反例。它不替代本批未采集的 anchor，也不证明本批实际选区已折叠；只说明本批使用的守卫判据不成立。下一次调查应先按原版实际 anchor/caret 合同明确选区状态，再决定是否抓取。

[两条脱敏反例](../../research/ui/results/2026-10-28-raster-style-reference/selection-getter-counterexample.json)只发布 caret、anchor 和 selection_length 三个值，并绑定原 Evidence ID、文件 SHA 与固定 EXE 身份；没有文字或路径。[专用离线导出器](../../research/ui/export_selection_counterexample.py)从固定原件核对并投影这些值，不启动原版。

## 离线审计与剩余边界

REA 正常结束，无脚本错误、消息截断、清理错误或 stderr；Job 最终 `active=0,total=3,terminated=0`，合成 profile 仍为一个会话。启动器主动结束原版，不以 Job 计数声称自然退出。provider 的 subject/environment 为 null，包身份由独立准备及模块路径守卫核对。

[生产探针](../../research/ui/raster_style_reference.py)、[GUI 脚本](../../research/ui/raster_style_reference.js)和原计划中的执行 SHA 未变。[离线导出](../../research/ui/export_raster_style_reference.py)使用显式 `require/ValueError`，重新核对源字节、消息、对象生命周期、像素派生值及单次抓取边界；本批为 partial，所以没有填充任何颜色结论。普通及 `python -O` 对同一原件导出字节相同。

[回归](../../research/ui/test_raster_style_reference.py)在普通/优化解释器下各 11 项通过，覆盖完整文件哈希尾部变更、模块路径、32B sret 正常返回后的析构顺序、ROI/墨迹边界、空图、额外抓取、私有文本拒绝、反例仅投影端点/长度，以及守卫停止不得冒充实际选区/像素证据。原生调用边界使用合成对象验证，不能转记成这次未执行的 Qt 栅格调用已通过。CTest `windterm_raster_style_reference` 单独通过。

原始日志、路径、PID、profile、复制资源和可能的 ROI 原始数据均留在 `local/r126-raster-0`；公开只有合成源、身份、停止原因、标量和哈希。进入/退出当前样式、受控栅格、完整网格及真实 presented 均未验证；本次不关闭 #126、#123、#27 或 R2。
