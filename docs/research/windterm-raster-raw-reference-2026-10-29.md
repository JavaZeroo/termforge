# #129 raw-capture-v1：五阶段原始 ROI 完整采集与离线签名

按[固定方案](windterm-raster-raw-capture-plan.md)完成一次新原版运行：5 个阶段各一次 grab，17 组 H/前置 space、34 个完整 QRgb 数组全部取得。phase2 两个 H 都有 16/16 主要墨迹并列，离线分类保留 `dominant_ink_rgba=null`，采集仍完成 phase3/4。`raw_capture_complete=true` 只表示这批固定 ROI 完整，不表示五阶段样式、内部 pen 或完整终端行为验收通过。

同帧、同文档行及逻辑行的完整有序 H/space 输出签名比较：phase1 首次 `1049h` 后的原样 A0，以及 phase3 重复 `1049h` 后的原样 A2，均唯一匹配该帧显式 `SGR0` 正控；phase4 `1049l` 后的原样 M1 唯一匹配该帧显式 `SGR31;44` 正控。这里的匹配要求 H 和 space 各自有序 QRgb 数组、尺寸和数量完全相同，不依据主要颜色或 histogram 相同推断内部样式、reset 或完整恢复语义。

## 执行与源身份

- 固定生产源为 `f6d9a97e2b2685b6ce39c83dc7107cb52aec7a6a`，执行前准备 HEAD 为 `46f90d89e5b80b96989861f437b4b10e75ccb199`。七个生产源逐字节匹配固定生产提交、false proposal 与新授权计划；本次证据收尾未改生产源或原件。
- 原 `execution_authorized=false` proposal 保持不变。复核明确授权后另建新计划，私有 SHA 为 `8dbeaae26ed19c6d8f3c22a16308fdf3b85f1d02a14aaa7e20af96c76405d288`；启动前新输出目录及 attempt marker 均不存在，唯一 attempt 随后保留，没有第二次原版运行。
- [授权计划的脱敏投影](../../research/ui/results/2026-10-29-raster-raw-reference/plan.json)列出全部五段 `43/38/20/53/54` 字节及 hex/SHA，每段另追加 `CSI 6n`。本批五段均实际发送。
- 固定官方 WindTerm 2.7.0 EXE SHA 为 `7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0`，4,047 文件清单 SHA 为 `e459adf2f12adb6a1b7f55e66a9da1c22794185e66e848057d21e0c60d669203`。离线重审逐个核对实际复制文件，以及 WindTerm/Qt5Core/Qt5Gui/Qt5Widgets 四个原件和副本的完整 SHA；运行时实际模块路径与固定导出 RVA 守卫通过。
- 独立 WindowStation/Desktop、OwnedJob、隔离 HOME/profile、单个合成 loopback Telnet 会话。没有操作用户输入桌面、个人剪贴板或 DPI。Telnet 直达终端观察不代表 WinPTY/ConPTY local-shell 合同。

REA Evidence ID 为 `ev_931e2e152246d132fdf497ec9f4180843ef6244beb4cc9c1ec2fb6e19805326f`。源、模块、输入、白名单标量、histogram/有序签名和原件 SHA 见[脱敏结果](../../research/ui/results/2026-10-29-raster-raw-reference/validation.json)。

## 五阶段实际观察

| 阶段 | DSR 回复 | isAltScreen / isMainScreen | P caret / anchor | selectionText type / length | 完整 H/space 组数 |
|---|---|---|---|---|---|
| 0 D0/M0 双 H | `(4,9)` | false / true | 19 / 19 | string / 10 | 4 |
| 1 A0 与同帧正控 | `(1,19)` | true / false | 38 / 38 | string / 20 | 3 |
| 2 A1 S2 双 H | `(4,11)` | true / false | 51 / 51 | string / 12 | 2 |
| 3 A2 与同帧正控 | `(1,26)` | true / false | 45 / 45 | string / 27 | 4 |
| 4 M1 与同帧正控 | `(4,35)` | false / true | 45 / 45 | string / 36 | 4 |

五条 DSR 回复均为无 `?` 前缀的 `CSI row;column R`，没有额外应用字节。逐条完整 NAWS 事件为 `91×24 → 91×25`，冻结事件数 2，冻结后未观察到变化；终端 API capacity 为 25。五个 ack 文件内容均为 `captured`，只在完整读取该阶段所有 ROI 并完成 image→pixmap 析构后释放下一阶段。所有 `capture_valid=true`，不是颜色分类通过标志。

五次选择端点都是已知、相等、有限非负安全整数；`selectionText` 仅保留 type/length。每个 marker 的完整 token、ordinal/group/sample 和位置独立核对，未将 `M1S1` 等前缀绑定为 `M1`。H/space 与实际 caret 占用均不相交，零宽 caret 按至少一个逻辑像素检查。

五张图均为 `888×451`，QPixmap/QImage DPR 均为 1，inclusive grab QRect 为 `[0,0,887,450]`。每个 H 和 space 均为 `8×18`，各有 144 个 uint32 QRgb；合计 34 个数组、4,896 个采样值。Qt 的 QPixmap/QImage 各分配 32B、不预构造，正常返回后每阶段均按 image→pixmap 普通析构。真实 `QImage::isNull` 原值为整数 `0`，公开保留精确原值/类型并规范为 false。

## 输出签名事实与未知

phase0 的 D0 两组完整 H/space 签名相同，S1 两组也相同，D0 与 S1 可区分。phase2 两个 H 的完整有序签名也相同：其 space 为单一背景 `[138,110,201,255]`，H 中 `[137,110,201,255]` 与 `[138,120,137,255]` 各 16 个样本并列最高；两组均为 `dominant-ink-ambiguous`、`dominant_ink_rgba=null`，保留所有候选、频数和 histogram，没有选定一种颜色。

| 目标 | 同帧显式正控 | 完整 H/space 有序签名结果 |
|---|---|---|
| phase1 A0，首次 `1049h` 后原样写入 | S1、D0 | 唯一匹配 D0 (`SGR0`) |
| phase3 A2，重复 `1049h` 后原样写入 | D0、S1、S2 | 唯一匹配 D0 (`SGR0`) |
| phase4 M1，`1049l` 后原样写入 | S1、S2、D0 | 唯一匹配 S1 (`SGR31;44`) |

匹配只陈述这几个合成 H/space 的局部输出事实。没有读取内部 pen、完整 current_style、非颜色属性或全部 active grid；不据此声称 reset、完整屏幕清空、局部滚动区域、resize、WinPTY/ConPTY local-shell、实际 presented 或性能等价已验证。主要墨迹并列与完整 raw 采集同时成立，不能将 `raw_capture_complete` 改称五阶段样式对齐通过。

## 离线审计与回收

原始 CLI 真正结束、exit=0 后才导出，stderr 为空，REA 无脚本错误、消息截断或清理错误；provider subject/environment 仍为 null。Job 为 `active=0,total=3,terminated=0`，profile 保持唯一合成会话。启动器主动结束原版，Job0 不证明自然 GUI 退出。REA capture 为 45 秒、CLI wait 为 60 秒，原版在单独 2 秒等待前已启动，后续还有清理；整程上限未知，不提供计时结论。

普通和 `python -O` 对同一真实原件的公开出口逐字节相同，SHA 为 `a93b34233806fe3f32bf5fe70a0a5953ad43156d0a574cb79677784e9f3b9cca`，公开 validation 原样保留该出口。独立审计不调用现有分析器：从原消息核对五阶段/最终结果，重建实际注入脚本（含 Windows 写入换行），逐一重算 34 个数组的 histogram、有序 SHA、尺寸和数量，并以完整数组直接比较同帧正控；另核对源/模块/包、plan/attempt、DSR、ack、NAWS、marker/端点/caret/DPR/边界、构造析构和回收，结果一致。

将 main `4f34146ab81f5b156537b080df2156f484793cc4` 合入交付分支后，七个生产源、false proposal、旧批公开原记录与实际执行身份保持不变；独立离线审计再次通过，Debug 配置的 CTest `windterm_raster_style_reference`、`windterm_raster_selection_proposal`、`windterm_raster_raw_proposal` 为 3/3。只运行合成/离线测试，没有重新启动原版；这些测试不增加原版样本数。

原始 ROI 像素、日志、路径、PID、复制资源和 profile 留在唯一私有目录 `local/r129-raw-capture-v1-0`；公开仅有合成输入、审计后的颜色/尺寸/频数标量及哈希。[旧 #126 的 0 grab 停止](windterm-1049-raster-reference-2026-10-28.md)、[旧 #129 的 3 grab partial](windterm-raster-selection-reference-2026-10-29.md)、旧 false proposal 和原件均未改写；旧批第二个 H 与 phase3/4 的缺口没有补造或与本批拼接。本批不关闭 #129、#126、#123、#27 或 R2。
