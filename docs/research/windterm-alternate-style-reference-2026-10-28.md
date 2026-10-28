# WindTerm 2.7.0：1049 状态、标记和样式观测缺口

[#123](https://github.com/JavaZeroo/termforge/issues/123) 在固定原版上完成一次 11 阶段取证：实际调用主/备用屏状态接口、核对四个边界标记和一个延迟换行往返。**样式观测未成功，进入时继承、退出后当前样式恢复仍未知。** 精确 QPainter 入口经过受限设备过滤后没有匹配，不能把它解释成原版没有样式或样式符合某种默认规则。

完整合成源、DSR、文档投影、标记矩形、真实模式返回值和失败边界见[脱敏摘要](../../research/ui/results/2026-10-28-alternate-style-reference/validation.json)。它补充[首次模式观察](windterm-alternate-reference-2026-10-28.md)，不替代本地 Shell 运输、完整 active grid、颜色或 R2 验收。

## 固定身份与一次执行

基点为 `d91eb589f04be6b54a29d237b87c5b05ad243908`。官方 2.7.0 Windows x64 EXE SHA-256 为 `7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0`；复制的 4,047 个文件由既有完整清单 SHA `e459adf2f12adb6a1b7f55e66a9da1c22794185e66e848057d21e0c60d669203` 守卫核对。动态探针另核对 WindTerm/Qt 模块路径属于自有复制包。

复用独立 WindowStation/Desktop、挂起创建后分配 OwnedJob、独立 HOME 和一个合成 Telnet profile，只监听 `127.0.0.1`。没有切换用户输入桌面、访问剪贴板或修改 DPI。固定计划在运行前保存于 ignored 的 `local/alternate-style-plan.json`，规定最多一次原版执行；实际一次，无重试。REA Frida Evidence ID 为 `ev_8c052576b1180d02c769570300f871cce11cdd828903f04952a3367627115639`。

本次保留的 NAWS **接收块末态**为 `91×24 → 91×25`，等待 UI capacity 与 NAWS 末态连续稳定后才释放源，以 **91×25** 作为源生成尺寸。执行用的 peer 在整个 `recv` 块解析后才记录并检查尺寸；同一块内先改变再恢复可能漏记，因此不能证明冻结后逐事件不变。摘要保留原记录，`naws_changed_after_freeze=null`、`all_naws_events_captured=false`；11 份 UI 快照的 capacity 均为 25，但不是缺失 NAWS 事件的替代品。

11 条精确源各发送一次，再追加 `CSI 6n`；下一阶段必须等待独立 UI 确认文件。收到 11 条无 `?` 前缀的 DSR、11 份 UI 快照和 11 次确认，没有其他应用回复字节。不声称覆盖任意 TCP 分块或逐字节解析。

`rea_capture_seconds=45` 是 REA 捕获时长，`cli_wait_timeout_seconds=60` 是复用启动器的 CLI 等待超时。原版更早恢复运行，中间有单独 2 秒等待，CLI 返回后还会回收资源；没有 45 秒或 60 秒整程生命周期保证。动态插桩只用于正确性，不计时或比较性能。

## 模式与标记的实际观察

`isAltScreen()`、`isMainScreen()` 本次均主动调用，所有结果为 bool 且无异常。主屏阶段得到 `false/true`；首次、重新和重复 `1049h` 后得到 `true/false`；`1049l` 后恢复 `false/true`。这弥补了首次观察只枚举接口名的缺口。

| 固定动作 | 观察 |
|---|---|
| 主屏 `2;3H` 写 `TFM0` | DSR `(2,7)`，`TFM0` 的原版 `visibleRectFromPosition` 为 `x=163,y=18,w=8,h=18` |
| `1049h` 后不改 SGR 写 `TFA0` | DSR `(1,5)`；状态为备用屏。`TFA0` 矩形 y=0，保留文档中的 `TFM0` 矩形 y=-18 |
| 备用屏 `4;5H` 改 SGR 后写 `TFA1`，`1049l` 后不改 SGR 写 `TFM1` | 主屏文字为 `TFM0TFM1`，DSR `(2,11)`；这些是文字/光标事实，颜色未知 |
| 重入后，在第一行、第二行、倒数第二行及最后一行写 `TFC0…3` | 四标记矩形 y 分别为 `0/18/414/432`；最后一个从第 88 列开始占到第 91 列，DSR `(25,91)` |
| 不退出，重复 `1049h` 后写 `TFA3` | 状态仍为备用屏，DSR `(1,5)`；四个 TFC 标记均从完整已读文档投影中消失，`TFA3` 矩形 y=0；保留主屏标记仍在 y=-18 |
| 再退出并写 `TFM2` | 主屏 `TFM0TFM1TFM2`，DSR `(2,15)`，主屏状态恢复 |

矩形是原版接口返回的逻辑坐标，宽度 8 表示标记首字符位置的矩形，不是四字符整体宽度。负 y 与保留文档、scrollTop 一起说明：备用屏期间能从 `lineText` 读到旧主屏文字，不等于主屏仍是 active。它不是截图、像素证据或真实呈现端点；没有把矩形当成完整网格映射。

四个边界标记消失提供了更广的清除观察，仍不证明所有格均清空、空白格带何种样式或所有重复/混合模式转换符合原版。`ESC` 源的完整十六进制及每步返回均在摘要中，未用标准行为补造未知项。

## 延迟换行的有限合同

第 8 阶段先定位主屏 `(3,91)`，写 `W`，紧接 `1049h` 和备用标记 `TFA4`。第 9 阶段为 `1049l` 紧接 `X`，返回 DSR **`(4,2)`**。`W` 与进入备用屏之间、退出备用屏与 `X` 之间均没有插入 DSR。独立控制在 `(6,91)` 连续写 `VZ`，得到 DSR **`(7,2)`**。

这支持该固定输入中退出备用屏后保留延迟换行的可观察效果。原版文档把 `WX`、`VZ` 分别投影为相连文字；协议坐标用于判断换到下一物理行，不能拿逻辑文档行号代替。它不证明内部用了同名 pending-wrap 字段，也不外推到 margin、origin、宽字、resize 或其他保存恢复序列。由于旧 peer 没有保留全部 NAWS 事件，该结论只对应已读接收块末态和快照，不额外证明两个快照间未发生瞬时尺寸改变。

## 颜色观测为何保持未知

准备阶段只读核对了固定 Qt5Gui SHA `5e7d2d41b8b92a880e83b8cc0ca173f5da61218604186196787ee1600956be1e` 的精确导出和局部指令。官方 Qt 5.15.2 [QPainter 头文件](https://raw.githubusercontent.com/qt/qtbase/v5.15.2/src/gui/painting/qpainter.h)给出借用 `const QPen&` 和具体 drawText 签名，[QPen](https://raw.githubusercontent.com/qt/qtbase/v5.15.2/src/gui/painting/qpen.h) 与 [QColor](https://raw.githubusercontent.com/qt/qtbase/v5.15.2/src/gui/painting/qcolor.h)给出颜色返回接口。固定包内 `QPen::color`（RVA `0x274420`）使用 `this=rcx,result=rdx`，写入结果后返回 rdx；探针预备 16B 临时值，没有猜测聚合返回位置。QString::size/constData 和 QBrush::color 也由该包导出与局部指令核对。既有字体观察提供 QWidget→QPaintDevice `+0x10` 的来源。

本次尝试把 `QPainter::drawText(QPointF,QString)` 的已知合成标记、QPainter 设备身份及 pen RGBA 绑定；背景只观测同设备四种 `fillRect(QRect/QRectF,QColor/QBrush)`。每阶段请求自有 viewport update，但设备过滤后的文本绘制调用、标记、填充匹配均为 **0**。因此颜色读取链没有实际取得任何标记颜色，样式字段全部为 `null`。

当前数据不能区分原版用了另一 overload、重定向的 backing-store 设备、未触发该绘制路径或其他原因。不能把 `0` 次匹配解释成 `0` 样式，也没有基于猜测更改探针重跑。没有调用未核实的 QWidget grab/QPixmap-toImage ABI，没有生成栅格图；完整 active-grid/受控栅格/真实 presented 分别保持未验证。下一次颜色调查应先定向确认实际绘制入口或取得经核实的受控栅格路径。

## 离线审计与交付边界

REA 正常返回，无脚本错误、消息截断、清理错误或 stderr；Job 最终 `active=0,total=3,terminated=0`，profile 仍只有一个合成会话。启动器主动结束原版进程，不以 Job 计数声明自然退出。provider subject/environment 为 null，固定包身份由独立守卫提供。

[导出器](../../research/ui/export_alternate_style_reference.py)用显式 `require/ValueError`，在普通与 `python -O` 下对同一原件导出完全相同的 `partial-mode-and-wrap-observation`；校验失败前不创建公开文件。仅导出固定合成源、真实模式值、矩形和脱敏 scalar。原始包、profile、绝对路径、PID、插桩与日志保留在 `local/alternate-style-reference-0`。

[公共回归](../../research/ui/test_export_alternate_style_reference.py)以自造记录验证普通/优化 CLI：无匹配必须导出为样式未知的 partial，非夹具文字、未调用模式 getter、错误原版身份必须拒绝。没有使用原版资源或新增原版执行。

审查后，[当前 peer](../../research/ui/alternate_style_reference.py)已在每条完整 NAWS 解码时立即记录并检查，保留重复及往返事件。[自有 loopback 反例](../../research/ui/test_alternate_style_peer.py)用一个受控 `recv` 批次递交 `91×25 → 92×25 → 91×25`：旧代码漏报、只记录最终值的两项用例先复现失败；修订后冻结时在 `92×25` 立即拒绝，冻结前保留全部三条事件。普通及 `python -O` 的两项 loopback＋四项导出回归均通过，已注册到 `windterm_alternate_style_reference` CTest。测试仅创建自有 TCP peer，不启动原版。

修订后的 peer **没有用于本次原版运行**。执行时的旧生产源已按原字节[归档](../../research/ui/reference_sources/alternate_style_reference_20261028.py.txt)，SHA 与原计划一致；导出用该归档核对执行来源，原 `probe_files_sha256`、注入脚本 SHA、所有原始文件及 11 阶段内容保持不变。普通/优化离线重导一致，不给旧原件补造 NAWS 事件，也不把新 peer 的通过转记成旧运行的逐事件尺寸保证。

本次可为 C++ 切片提供模式状态、四标记消失和该延迟换行往返的原版合同；进入继承/退出当前样式、完整清屏及其他交叉仍未解除。#123、#27 和 R2 不能由本次部分取证关闭；本地 Shell 的原生 margin 问题继续由各自任务处理。
