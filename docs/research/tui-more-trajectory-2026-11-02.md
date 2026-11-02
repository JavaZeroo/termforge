# More 分页器轨迹冻结与独立协议预期（#28 首批实际应用）

本文件冻结 TermForge 在 Windows 11 x64 下驱动系统自带 More.com 的第一批应用身份、入口、平台与操作轨迹，并按其逐步写出独立协议字节预期。来源为既有捕获证据、本机 2026-11-02 实测与公开行为，未观测处明确标为未知。本文只冻结轨迹与预期，不改产品行为，也不代表 #28 整体验收。

## 冻结身份

| 项目 | 值 | 证据/来源 |
|---|---|---|
| 应用 | More.com（Microsoft More Utility） | 本机 PE 资源：FileDescription 为 More Utility |
| 文件版本 | 10.0.26100.1 (WinBuild.160101.0800) | 本机 FileVersion/ProductVersion |
| 文件路径 | C:\Windows\System32\more.com | 本机实测 |
| 文件大小 | 53,248 字节 | 本机实测（LastWrite 2024-04-01T07:22:16Z） |
| 文件哈希 | SHA-256 dea4ed8963aef4c8ce4aa1491410f614fd6d9b0e07737a9b634612adad731630 | 本机实测 |
| 平台 | Windows 11 x64，OS build 10.0.26200 | 本机 systeminfo |
| 终端载体 | ConPTY 80×24（进入页 23 行正文 + 末行提示） | 捕获设置与提示百分比 56% 相互印证 |
| 入口命令 | more.com pager.txt（工作目录内，文件用 CRLF 行结尾） | 见 experiments/conpty/real_app_capture.cpp:91-101 |

提示百分比随文件大小变化：40 行文件首屏显示 TF_PAGE_1 到 TF_PAGE_23 共 23 行，提示为 -- More (56%) -- ；100 行文件同屏 23 行，提示为 -- More (22%) -- 。页面容量 23 行与百分比算法是 More 内部行为，本文件只冻结观测到的字节，不宣称已还原其计算式。

## 动作序列

进入（首屏）→ 翻页（空格）→ 行滚（回车）→ 退出（q）。搜索不适用：Windows more.com 没有 Unix less 的 /pattern 搜索，交互键为 空格（下一页）、回车（下一行）、= （行号）、q（退出）。这是公开行为，本切片不把搜索列入轨迹。

## 进入序列（逐字节协议预期）

固定 ConPTY 1.25 与系统 ConPTY 对同一 More.com 产生的字节不同，见下一节的已知差异。以固定 ConPTY 1.25 的 411 字节进入样本为准（本切片的产品配置基线），逐段如下：

| 字节 | 序列 | 含义 | 核心处理 |
|---|---|---|---|
| 1B 5B 31 74 | ESC[1t | XTWINOPS 去图标化窗口操作 | strict/recovery 均接受忽略（terminal.hpp 的 CSI t 分支） |
| 1B 5B 63 | ESC[c | DA1 主设备属性查询（ConPTY 启动包） | strict 回复 ESC[?6c；recovery 吞掉不回复（见 #272） |
| 1B 5B 3F 31 30 30 34 68 | ESC[?1004h | 焦点上报开 | 接受无副作用 |
| 1B 5B 3F 39 30 30 31 68 | ESC[?9001h | Win32 输入模式开 | 接受无副作用 |
| 0D | CR | 回列 0 | 列置 0 |
| 0x20 × 78 | 78 个空格 | 用空格清空首行（固定 ConPTY 不用 ED2） | 文本写入 |
| 0D | CR | 回列 0 | 列置 0 |
| TF_PAGE_1 0D 0D 0A | 文本 + CR CR LF | 首行正文，行尾为 CR CR LF | 文本写入 |
| TF_PAGE_2 到 TF_PAGE_23 各 + 0D 0D 0A | 文本 | 剩余 22 行正文 | 文本写入 |
| 2D 2D 20 4D 6F 72 65 20 (nn%) 20 2D 2D 20 | -- More (nn%) -- | 末行提示（17 字符，含尾空格） | 文本写入 |

固定 ConPTY 进入样本共 411 字节，SHA-256 7a44e90460a794a0afa043c4924b2de0af79f0a8c2629ec4fc06f9301eec6d08，已冻结为 experiments/core/fixtures/more-pager-fixed-411.bin。该样本不含 OSC 标题，因此 strict 路径可完整回放。

### 系统 ConPTY 的进入字节（两种绘制变体）

系统 ConPTY 的 More 进入序列非确定，本机 4 次捕获得到两种变体（各 2 次）：

变体 B（360 字节，SHA-256 d035d53385788d2b68dcc0d866b5a2fe68897cdcb28a761bd4d36efcc6df6cf9，等于已冻结的 more-pager-360.bin）：

- ESC[?9001h ESC[?1004h ESC[?25l ESC[2J ESC[m ESC[H
- OSC 0;C:\Windows\System32\more.com BEL（设置标题，ST 类 OSC）
- ESC[?25h
- TF_PAGE_1 ESC[70X 0D 0A（首行 + ECH 70）
- TF_PAGE_2 到 TF_PAGE_23 各 + 0D 0A
- -- More (56%) --

变体 A（375 字节，SHA-256 c2fee3acc9e9d769bb21070b257525e46d06f7dcf55334904994a3599af46e14，已冻结为 more-pager-system-375.bin）：

- ESC[?9001h ESC[?1004h ESC[?25l ESC[2J ESC[m ESC[H
- ESC[79C（CUF 79，移动到第 80 列）
- OSC 标题 BEL
- ESC[?25h ESC[?25l ESC[H（光标显/隐/归位）
- TF_PAGE_1 0D 0A（无 ECH）
- TF_PAGE_2 到 TF_PAGE_23 各 + 0D 0A
- -- More (56%) -- ESC[?25h

两种系统变体的共同点：都含 OSC 标题（strict 路径因此以 unsupported escape 失败）、都用 ED2 ESC[2J 清屏、都含 ESC[?25l/ESC[?25h 光标显隐切换。差异只在首行余部清除手段（ECH 70 或 CUF 79 加重写）与提示后的光标字节。该非确定性来自系统 conhost 对 More.com 同一批控制台 API 调用的 VT 翻译时序，本切片只记录，不定位 conhost 根因。

## 已知差异：固定 ConPTY 1.25 vs 系统 ConPTY

| 维度 | 固定 ConPTY 1.25 | 系统 ConPTY（本机 build 26200） |
|---|---|---|
| 进入字节 | 411 字节，4 次捕获确定一致 | 360（变体 B）或 375（变体 A），非确定 |
| 启动包 | 有 ESC[1t + ESC[c（DA1） | 无 |
| OSC 标题 | 无 | 有 OSC 0;路径 BEL |
| 清屏 | 空格 + CR（无 ED2） | ED2 ESC[2J + CUP ESC[H |
| 首行余部 | 无（首行直接接 CR CR LF） | ECH 70（变体 B）或 CUF 79（变体 A） |
| 行结尾 | CR CR LF | CR LF |
| 光标显隐 | 无 ESC[?25l/ESC[?25h | 有 |
| 模式顺序 | ?1004h 在前，?9001h 在后 | ?9001h 在前，?1004h 在后 |
| strict 回放 | 通过（exit 0） | 失败（exit 2，unsupported escape 于 OSC） |

该差异说明 More.com 的 VT 字节由 ConPTY 主机（OpenConsole/conhost）版本决定，而非仅由 more.com 二进制决定。ADR-0007 随包固定 ConPTY 正是为了消除这类主机版本漂移；本切片实测固定 1.25 路径确定且 strict/recovery 双通，系统路径非确定且 strict 不通过，属于对固定路线的支持证据，但不构成完整验收。

## 翻页、行滚与退出

### 翻页（空格）

固定 ConPTY：提示 -- More (56%) -- 后收到空格，输出 0D 加若干空格清空提示行，再加 0D，然后下一页正文（首行 TF_PAGE_24 + 行尾 CR CR LF，后续行同）。40 行文件下一页即 TF_PAGE_24 到 TF_PAGE_40（17 行）到达文件尾。系统 ConPTY 变体 B 的空格输出为 0D + TF_PAGE_24 ESC[69X 0D 0A + 后续行（首行用 ECH 69 清余部）。

### 行滚（回车）

100 行文件固定 ConPTY 侧，翻页后提示 -- More (45%) -- 收到回车，输出 0D + 下一行 TF_PAGE_47 + 行尾（清余部）。系统 ConPTY 变体 B 为 0D + TF_PAGE_24 ESC[69X 0D 0A 形态。行滚每次推进一行并清提示行余部。

### 退出（q）

固定 ConPTY 侧退出尾部为 ESC[?1004l ESC[?9001l（与各自进入同序：固定 ?1004 在前，系统 ?9001 在前），文件尾场景无 ECH（提示行已被正文覆盖）。系统 ConPTY 在首屏提示处直接 q 的退出尾部为 0D ESC[?9001l ESC[?1004l ESC[79X（CR + 关两个模式 + ECH 79 清提示行），对应既有 382 字节新鲜样本与 more-pager-366.bin 的 0D ESC[79X 前缀。退出码均为 0。

本切片用 40 行文件观察翻页与退出，用 100 行文件观察行滚与多页；未捕获的边界（= 行号、F 下一文件、P<n>/S<n> 定位）不在冻结范围内，标为未观测。

## 查询回复 6n/DA、1049、ED、滚动区（逐项核对）

| 协议项 | More 轨迹中是否出现 | 结论与来源 |
|---|---|---|
| DSR 6n（ESC[6n 光标位置查询） | 未出现 | More.com 不查询光标位置；6n 回复能力见 experiments/core/terminal.hpp 的 CSI n 分支与 reply_test.cpp，不在本轨迹触发 |
| DA1（ESC[c 主设备属性查询） | 固定 ConPTY 启动包出现 | strict 回复 ESC[?6c，recovery 静默，见 #272 与 terminal.hpp 的 CSI c 分支；本轨迹实测固定路径 strict 通过 |
| 1049（备用屏） | 未出现 | More.com 使用主屏，不进备用屏；主屏恢复即清提示行，不涉及 1049 保存/恢复 |
| ED（ESC[2J 擦除显示） | 系统 ConPTY 进入出现，固定 ConPTY 不出现 | 系统路径进入用 ESC[2J 清屏；固定 1.25 用空格 + CR 清屏，不触发 ED 分支 |
| 滚动区（DECSTBM ESC[r） | 未出现 | More.com 不设滚动区 |

## 产品配置冒烟结论

固定 ConPTY 1.25 的进入与退出字节经自主 C++ 核心 strict 与 recovery 两路回放均通过（exit 0，屏幕正确：首屏 TF_PAGE_1 到 TF_PAGE_23 + 提示，退出后正文进历史、末行空）。系统 ConPTY 的进入字节 recovery 通过、strict 以 unsupported escape 失败于 OSC 标题。逐阶段通过/失败/跳过见 experiments/shell/results/2026-11-02-tui-more/validation.json。

## 未知项与未验证

- 系统 conhost 双变体（变体 A/B）的根因未定位；本切片只记录现象。
- 固定 ConPTY 1.25 与系统 ConPTY 的字节差异只对比了 40/100 行文本文件的进入与退出，未覆盖二进制输入、长行折行、宽字符、极窄/极宽尺寸。
- = 行号、F 下一文件、P<n>/S<n> 定位等交互键未观测。
- 主屏恢复只核对到末行清空与正文进历史，未核对完整样式/光标/滚动区恢复（More 不设滚动区与备用屏，故本项风险低，但仍属未逐项验证）。
- 其他语言版本的 More 提示未验收（既有合同已声明只绑定英文提示）。
- 未在固定 ConPTY 路径观测到 More 发送 DSR 6n；本轨迹不含 6n 回复的实测。

## 复验入口

固定 ConPTY 捕获需随包固定 ConPTY 文件对（third_party/conpty/manifest.json 的 conpty.dll + OpenConsole.exe），置于捕获程序旁的 conpty-backend 目录；本切片用实验探针 local/more_traj_probe.cpp（未提交）驱动 more.com pager.txt 记录 stage-enter/page/line/quit。核心回放用已构建的 recovery_probe（recovery/strict）与 replay（strict）。已冻结样本的 SHA-256 见各节。
