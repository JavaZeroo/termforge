# Vim for Windows 轨迹冻结与独立协议预期（#28 应用矩阵第 2 项）

本文件冻结 TermForge 在 Windows 11 x64 下用固定 ConPTY 1.25 驱动固定 Vim for Windows 的第二批应用身份、入口、平台与操作轨迹，并按其逐步写出独立协议字节预期，再对照自主 C++ 核心的 strict/recovery 两路回放逐项核对。来源为 [WindTerm 原版对照](vim-windterm-reference.md)、本机 2026-11-02 实测与 Vim 公开行为，未观测处明确标为未知。本文只冻结轨迹与预期，不改产品行为，也不代表 #28 整体验收。

复用 More 切片的成熟模式（PR #306，轨迹见 [tui-more-trajectory-2026-11-02.md](tui-more-trajectory-2026-11-02.md)）。与 More 的关键差别：Vim 是全屏 TUI，轨迹中出现 1049 备用屏进出、DA1/DA2、DSR 6n、DECSTBM 与 XTWINOPS 标题保存/恢复，这些在 More 轨迹中均不出现。

## 冻结身份

| 项目 | 值 | 证据/来源 |
|---|---|---|
| 应用 | Vim 9.1，patches 1–532，Huge 版（无 GUI），msys2 构建 | 本机 vim --version 与既有 [原版对照](vim-windterm-reference.md) 同源 |
| EXE SHA-256 | 2e0fda63a89171adfc46c9417d5105cc39ed778cc69a890b83a4eabc34e7646a | 本机实测，与 vim-windterm-reference.md 一致 |
| 来源位置 | C:\Program Files\Git\usr\bin\vim.exe（Git for Windows 自带） | 本机实测；Windows 不带 vim，Git for Windows 是固定可用来源 |
| 相邻 MSYS DLL | msys-2.0.dll SHA-256 3096d2ba9be20e90be1399edd391bac65d84bc387f5a419b502c9b326081930f | 本机实测，与 vim-windterm-reference.md 一致 |
| 平台 | Windows 11 x64，OS build 10.0.26200.9711（25H2） | 本机注册表 CurrentVersion/UBR |
| 终端载体 | 固定 ConPTY 1.25.260930003（随包，非系统 conhost） | third_party/conpty/manifest.json |
| 屏幕尺寸 | 80×24 主轨迹；60×12 变体（60 列 × 12 行） | 本机实测 |
| 入口命令 | vim.exe -Nu NONE -n -i NONE --cmd 设置 VimEnter/InsertEnter 事件文件 editor.txt | experiments/conpty/real_app_capture.cpp:135 |

入口命令沿用 real_app_capture.cpp:135 的编辑器块：-Nu NONE 禁用 vimrc、-n 禁用交换文件、-i NONE 禁用 viminfo，两个 --cmd autocmd 分别写 READY（VimEnter）与 INSERT（InsertEnter）事件文件，编辑目标为工作目录内 editor.txt。该入口与本机驱动探针（未提交 local/vim_traj_probe.cpp）完全一致。

## 动作序列

进入（首屏）→ 插入（i）→ 写入正文 → 保存退出（ESC 后 :wq）。与 [原版对照](vim-windterm-reference.md) 记录的原 Vim 编辑过程同一动作集合：原版证据为一次插入、60×12 resize、写盘、恢复主提示符，整条方法停在尺寸检查；本切片在固定 ConPTY 产品路径独立重做进入/插入/保存/退出，不与原版 WinPTY 样本拼成一次通过。

## 进入序列（逐字节协议预期，80×24）

固定 ConPTY 1.25 下 Vim 进入字节 3 次捕获逐字节一致（确定性），整条轨迹 2424 字节。进入段逐段如下（偏移为 0 基字节偏移，相对整条轨迹）：

| 偏移 | 序列 | 含义 | 核心处理 |
|---|---|---|---|
| 0 | ESC[1t | XTWINOPS 去图标化窗口操作 | 单参数 CSI t，接受忽略（terminal.hpp:100-108） |
| 4 | ESC[c | DA1 主设备属性查询 | strict 回复 ESC[?6c，recovery 静默（terminal.hpp:124-137，#272） |
| 7 | ESC[?1004h | 焦点上报开 | 接受无副作用（terminal.hpp:49-54） |
| 15 | ESC[?9001h | Win32 输入模式开 | 接受无副作用（terminal.hpp:49-54） |
| 23 | ESC[?1049h | 备用屏进入（保存主屏、清屏、光标归位） | Screen 切备用帧并 ED2 归位（terminal.hpp:55-78） |
| 31 | ESC[22;0;0t | XTWINOPS 保存标题（3 参数） | strict 在参数门限失败，见下文差异 |
| 40 | ESC[?1h | DECCKM 应用光标键 | mode_bits 置位（terminal.hpp:38-41） |
| 47 | ESC[?2004h | 括号粘贴开 | 未实现，recovery 忽略、strict 失败 |
| 63 | ESC[1;24r | DECSTBM 滚动区 1..24 | set_scroll_region（terminal.hpp:95-98） |
| 82 / 89 | ESC[22;2t / ESC[22;1t | XTWINOPS 标题（2 参数） | strict 参数门限失败 |
| 96–111 | ESC[27m ESC[23m ESC[29m ESC[m | SGR 复位/撤销 | SGR 状态 |
| 114 | ESC[H | 光标归位 | move |
| 117 | ESC[2J | ED2 清屏 | erase_display(2) |
| 161 / 190 / 217 | ESC[6n | DSR 光标位置查询（3 次） | 回复 ESC[row;colR（terminal.hpp:112-123） |
| 179 | DCS zz | 终端同步能力查询 | recovery 忽略、strict 失败 |
| 221 | ESC[>c | DA2 次设备属性查询 | 未实现，recovery 忽略、strict 失败 |

进入后画面：备用屏内显示 22 行波浪号填充（SGR 亮蓝 fg=12）加末行 "editor.txt" [New]，与 recovery 回放屏一致。

## 插入与保存

- 插入模式：发送 i，Vim 在末行显示 -- INSERT --，正文逐字回显 TF_CANDIDATE_TUI（16 字符）。
- 保存退出：发送 ESC（退出插入模式）后 :wq 加回车，Vim 写盘并退出码 0，写盘确认 "editor.txt" [New][unix] 1L, 17B written（17 字节 = 16 字符标记加 LF，与 vim-windterm-reference.md 记录一致）。
- 保存文件 editor.txt 经逐字节校验包含 TF_CANDIDATE_TUI，事件文件 READY/INSERT 均已观测。

## 退出序列（含 1049 主屏恢复）

| 偏移 | 序列 | 含义 |
|---|---|---|
| 2289 | ESC[?2004l | 括号粘贴关 |
| 2338 / 2345 | ESC[23;2t / ESC[23;1t | XTWINOPS 恢复标题 |
| 2378 | ESC[?1l | DECCKM 关 |
| 2385 | ESC[?1049l | 备用屏退出（恢复主屏） |
| 2393 | ESC[23;0;0t | XTWINOPS 恢复标题（3 参数） |
| 2408 | ESC[?1004l | 焦点上报关 |
| 2416 | ESC[?9001l | Win32 输入模式关 |

退出后主屏恢复为空屏（进入前主屏即空），回滚历史为空，与该帧语义一致。1049 进出的保存/恢复由 Screen 双帧承担，退出只把主帧换回，不把编辑器内容并入历史——这是 Vim 全屏编辑的预期行为，与 More 主屏滚动进历史不同。

## 逐项协议核对（1049 / DA / DSR / DECSTBM / ED）

| 协议项 | Vim 轨迹是否出现 | 结论与来源 |
|---|---|---|
| 1049 备用屏 | 出现：ESC[?1049h 进（偏移 23）、ESC[?1049l 出（偏移 2385） | 核心已实现（terminal.hpp:55-78 与 widechar_1049_test），recovery 回放进出正确 |
| DA1（ESC[c） | 出现（偏移 4，固定 ConPTY 启动包） | strict 回复 ESC[?6c，recovery 静默，#272（terminal.hpp:124-137） |
| DA2（ESC[>c） | 出现（偏移 221） | 未实现；recovery 忽略、strict 失败，属已知缺口 |
| DSR 6n（光标位置） | 出现 4 次（偏移 161/190/217/2153） | 核心回复 ESC[row;colR（terminal.hpp:112-123 与 reply_test），Vim 用其探测尺寸 |
| DECSTBM（ESC[r） | 出现（偏移 63，1..24 全区） | 核心已实现（terminal.hpp:95-98 与 edit_csi_test） |
| ED2（ESC[2J） | 出现（偏移 117） | 核心已实现 erase_display(2) |
| DECCKM（ESC[?1h/l） | 出现（偏移 40 开、2378 关） | 核心已实现（terminal.hpp:38-41 与 keyboard_test） |
| DECTCEM（ESC[?25h/l） | 出现（多处在光标显隐） | 核心已实现（terminal.hpp:38-41 与 reply_test） |
| 括号粘贴（ESC[?2004h/l） | 出现（偏移 47 开、2289 关） | 未实现；recovery 忽略、strict 失败 |

## 已知差异：strict 与 recovery

固定 ConPTY 1.25 下 Vim 轨迹在自主 C++ 核心的 strict 与 recovery 两路结果不同：

- recovery：进入、插入、保存、退出、主屏恢复全程回放通过（exit 0），屏幕正确（进入后备用屏显示编辑器，退出后主屏空）。忽略 19 条序列（多参数 XTWINOPS t、DCS 同步、DA1/DA2、未实现模式）。
- strict：在偏移 31 的 ESC[22;0;0t 失败，错误 too many CSI parameters。根因在 terminal.hpp:81-82 的参数门限：对非 H/f/r 终字节的 CSI 只允许 1 个参数，而 Vim 的标题保存/恢复序列（ESC[22;0;0t、ESC[22;2t、ESC[22;1t、ESC[23;2t、ESC[23;1t、ESC[23;0;0t）携带 2–3 个参数，未到 terminal.hpp:100-108 的 t 分支就被门限拒绝。另 ESC[?2004h/l、DCS zz、DA2 也属 strict 拒绝项。

该差异与 More 切片相反：More 固定 ConPTY 进入不含这些序列，strict 双通；Vim 全屏协议更丰富，strict 尚不能承载。这是对固定路线支持证据的边界标注，不构成完整验收。

## 60×12 变体

固定 ConPTY 60 列 × 12 行的 Vim 轨迹 1180 字节，进入/插入/保存/退出与 80×24 同构，退出同样以 ESC[?1049l 恢复主屏。recovery 回放通过（进入后 10 行波浪号加 [New]，退出后 12 行空），strict 同样在 ESC[22;0;0t 失败。

与 [原版对照](vim-windterm-reference.md) 的 60×12 关系：原版证据中缓冲区为 60×3000、可见窗口为 60×12（dwSize 与 srWindow 两字段），本切片的 60×12 是 ConPTY 的 60 列 × 12 行视口，没有 3000 行缓冲区概念。二者不可合并；原版 resize 中途丢后续输入的问题（#52/#56 的 MSYS 运行时诊断）本切片未在 ConPTY 产品路径复现，标为未验证。

## 产品配置冒烟结论

| 检查 | 尺寸 | 模式 | 结果 | 屏幕 |
|---|---|---|---|---|
| vim-enter | 80×24 | recovery | 通过 | 备用屏 22 行波浪号 + [New]，光标列 0 |
| vim-exit | 80×24 | recovery | 通过 | 主屏 24 行空，历史 0 |
| vim-full | 80×24 | recovery | 通过 | 主屏恢复空 |
| vim-full | 80×24 | strict | 失败 | too many CSI parameters（偏移 31） |
| vim-enter | 60×12 | recovery | 通过 | 备用屏 10 行波浪号 + [New] |
| vim-exit | 60×12 | recovery | 通过 | 主屏 12 行空 |
| vim-full | 60×12 | strict | 失败 | 同上 |

逐阶段记录见 experiments/shell/results/2026-11-02-tui-vim/validation.json，冻结样本见 experiments/core/fixtures/vim-fixed-80x24-trajectory.bin 与 vim-fixed-60x12-trajectory.bin。

## 未知项与未验证

- strict 拒绝多参数 XTWINOPS t、括号粘贴、DCS 同步与 DA2 是否应实现、以何种字节回复，未决定；本切片只记录失败位置，不猜原版回复。
- 原版 WindTerm 的 60×3000 缓冲区与 60×12 窗口（dwSize vs srWindow）与 ConPTY 60×12 的映射关系未验证。
- #52/#56 记录的 resize 中途丢输入（MSYS 运行时 SIGWINCH 竞态）未在 ConPTY 产品路径复现，本切片未做中途 resize，只做 60×12 固定起点变体。
- DSR 6n 的回复字节（ESC[row;colR）在本切片只核对到核心有回复能力（reply_test），未核对 Vim 实际消费回复后继续的行为（本切片 Vim 在 ConPTY 下不等待回复即可进入编辑，ConPTY 由 conhost 代答）。
- 其他终端类型（TERM 变量）、其他 vim 启动参数、多文件/多窗口、鼠标模式、宽字符正文均未覆盖。
- 未观测 DA1/DA2 的原版 WindTerm 回复字节（既有 issue28-probe.md 记录原版 DA/OSC 探针未能采集）。

## 复验入口

固定 ConPTY 捕获用未提交探针 local/vim_traj_probe.cpp 驱动 vim.exe 记录 stage-enter/insert/text/exit；核心回放用已构建 recovery_probe（strict/recovery）与 replay（strict）。固定 ConPTY 文件对为 third_party/conpty/manifest.json 声明的 conpty.dll + OpenConsole.exe，置于捕获程序旁的固定目录并传绝对路径。已冻结样本 SHA-256 见各节与 validation.json。
