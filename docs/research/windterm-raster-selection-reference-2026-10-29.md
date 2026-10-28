# #129：三阶段受控栅格，主要墨迹并列后停止

[#129](https://github.com/JavaZeroo/termforge/issues/129) 按[固定五阶段方案](windterm-raster-selection-plan.md)完成了一次新原版执行。phase0/1 的 H/space 校准通过；phase2 的第一个 H 有两种最高频非背景色，频数同为 16，按原门槛以 `dominant-ink-ambiguous` 停止。实际共三次 grab，后两个阶段未发送，没有移动 ROI、追加抓取或重跑。

局部可观察事实是：首次 `1049h` 后未经 SGR 修改的 A0 H，与**同帧、同一行**显式 `SGR0` 正控的输出颜色相同，与 `SGR31;44` 正控不同。该事实不等于读取了内部 current_style、pen 或证明 reset。备用 S2 完整校准、重复进入与退出后的样式恢复仍未知。

## 执行与源身份

- 固定生产源为 `e302fa3acb6a2ed2982aad5550e027d37c772da6`，执行前准备 HEAD 为 `1aa8054c247445860442624cf07c4313f43f938d`。七个生产文件分别与固定生产提交、不可运行 proposal 及新授权计划逐字节核对一致。
- 原 `execution_authorized=false` proposal 未修改。最终复核明确授权后另生成 `true` 的新私有计划，SHA 为 `2fc02922d532766442913d87038cf4c0311c125c5101eb40220429fdf12b2509`；启动前新目录和 attempt marker 均不存在。单次 marker 随后保留，没有第二次运行。
- [新授权计划的脱敏投影](../../research/ui/results/2026-10-29-raster-selection-reference/plan.json)列出全部五段 `43/38/20/53/54` 字节及各自 hex/SHA；每段另追加 `CSI 6n`。只有前三段实际发送，不把计划输入计作已执行行为。
- 固定官方 WindTerm 2.7.0 EXE SHA 为 `7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0`，4,047 文件清单 SHA 为 `e459adf2f12adb6a1b7f55e66a9da1c22794185e66e848057d21e0c60d669203`。准备与离线重审均核对 WindTerm/Qt5Core/Qt5Gui/Qt5Widgets 四个完整文件 SHA；运行中的实际模块路径及所需导出 RVA 守卫通过。
- 独立 WindowStation/Desktop、OwnedJob、隔离 HOME/profile、单个合成 loopback Telnet 会话。没有操作用户输入桌面、剪贴板或 DPI。Telnet 直达终端观察不代表 WinPTY/ConPTY local-shell 合同。

REA Evidence ID 为 `ev_432abe8baf4fc86efd4994cd12cc3ab5fe0b5120476d22540c5b26e2e3c693a4`。完整白名单标量、源身份及原件 SHA 见[脱敏结果](../../research/ui/results/2026-10-29-raster-selection-reference/validation.json)。旧 #126 的源 `15c736b`、0 grab/0 pixel 停止记录保持独立，未移植历史端点或拼接成功步骤。

## 实际观察

| 阶段 | DSR 回复 | isAltScreen / isMainScreen | P caret / anchor | 选区文本 type / length | 栅格校准 |
|---|---|---|---|---|---|
| 0 D0/M0 双 H | `(4,9)` | false / true | 19 / 19 | string / 10 | D0、S1 各双 H 一致 |
| 1 A0 与同帧正控 | `(1,19)` | true / false | 38 / 38 | string / 20 | 三个 H 均通过 |
| 2 A1 S2 双 H | `(4,11)` | true / false | 51 / 51 | string / 12 | 第一个 H 墨迹频数并列，停止 |

三条回复均为无 `?` 前缀的 `CSI row;column R`，没有额外应用字节。逐条完整 NAWS 事件为 `91×24 → 91×25`，冻结事件数 2，冻结后未观察到变化；终端 API capacity 为 25。只出现 ack0/ack1，phase2 未确认，因此没有释放 phase3/phase4。

本批主动读取 `selectionCaretP` 与 `selectionAnchorP`，三次都是已知相等的安全整数。非空 `selectionText` 只保留类型与长度，没有参与选区判断或进入公开文本。H/space ROI 与实际 caret 占用不相交，零宽 caret 仍按至少一个逻辑像素保守检查；标签使用完整 token 唯一绑定，没有把 `M1S1` 等前缀当作目标。

三张图均为 `888×451`，QPixmap/QImage DPR 均为 1，inclusive grab QRect 为 `[0,0,887,450]`。采样 H 与前置 space 均为 `8×18`，每个 ROI 144 个 QRgb 值。Qt 的 32B sret 对象未预构造，每阶段正常返回后均按 image→pixmap 普通析构。这里观察到的是 QWidget 的受控栅格，未验证物理显示或 presented。

## 颜色事实与未知

颜色字段为 `background_rgba` 与 `dominant_ink_rgba`，表示 ROI 输出；主要墨迹不等价于字形的真实前景 pen。phase0 的 D0 双 H 输出一致，S1 双 H 输出也一致，且两组可区分。phase1 的同帧比较为：

| phase1 H | 背景 RGBA | 主要墨迹 RGBA |
|---|---|---|
| 未修改 SGR 的 A0 | `[40,40,50,255]` | `[40,75,140,255]` |
| 显式 S1 (`31;44`) | `[61,104,169,255]` | `[61,95,137,255]` |
| 显式 D0 (`0`) | `[40,40,50,255]` | `[40,75,140,255]` |

这支持 A0 与同帧 D0 的局部唯一输出匹配。跨 frame 的 phase0 D0 输出与 phase1 D0 并不相同，不能据输入 SGR 编号假定所有 frame 输出恒定，更不能借此填充内部样式值。

phase2 的 space 为单一背景 `[138,110,201,255]`。首个 H 中 `[137,110,201,255]` 与 `[138,120,137,255]` 各有 16 个样本并列最高；`colors=null`，第二个 H 尚未采样。没有选择其中一种颜色、改用新统计规则或重新抓取。整批 `sample_colors_verified=false`、五阶段 `sample_color_comparisons=null`，已完成的 phase0/1 校准事实单独保留。

阶段 3 重复 `1049h`、阶段 4 `1049l` 后写字均未发生，本批不提供它们的清空、样式继承或恢复结论。完整 current_style、非颜色属性、全部 active grid、local-shell、性能等同与 R2 验收均未验证。

## 离线校验与清理

CLI 已真正结束后才导出和交付，exit=0，stderr 为空，REA 无脚本错误、消息截断或清理错误；provider 的 subject/environment 仍为 null。Job 为 `active=0,total=3,terminated=0`，合成 profile 仍只有一个会话。启动器主动结束原版，不以 Job 计数声称自然 GUI 退出。

第一次普通/`-O` 离线导出都拒绝了真实 `QImage::isNull` 返回值：Frida 把 NativeFunction bool 记录为数字 `0`，审计器原先只接受 Python bool。该失败没有产生公开输出。仅修订[离线分析器](../../research/ui/raster_selection_analysis.py)：显式接受精确 bool 或整数 0/1，拒绝 float/string/null/其他整数；公开规范布尔值并另保留 `is_null_raw_value=0`、`is_null_raw_type="number"`。七个生产源、授权计划和全部原件均未修改。

[公共回归](../../research/ui/test_raster_selection_reference.py)先复现数值 0 被拒绝，再验证精确类型门槛与普通/优化 CLI；新增三阶段墨迹歧义的合成反例，确保 partial 不提升为五阶段通过。普通及 `python -O` 各 13 项通过，CTest `windterm_raster_selection_proposal` 在 Debug 配置下 1/1 通过。对同一真实原件的两种解释器导出逐字节相同，SHA 为 `c29022645128d14b77b764f473fa7a2c0e467ce5f66db47de8b589128de7fc84`。独立只读审计另核对 provider 消息、源、完整 token、像素派生标量、停止点与清理，结论一致。

原始图像/ROI 像素、日志、路径、PID、复制资源和 profile 均留在唯一 `local/r129-raster-authorized-0`；公开只有合成输入、身份、颜色/尺寸/频数标量与哈希。本批保持 partial，不关闭 #129、#126、#123、#27 或 R2。
