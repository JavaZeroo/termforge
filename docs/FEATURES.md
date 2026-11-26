# 当前能力与目标矩阵

本文件维护所读源码与固定证据的能力及已知差距；未合并分支不代表 main 已包含相应实现。R1 已按限定合同验收；R2 仍在实现与整合，尚未完成基础开发包交付。按 [ADR-0009](adr/0009-r2-scope-decisions.md)，其出口为“本地终端基础＋已知差异清单”，不宣称日常可用或完整等价。路标见[总体计划](PLAN.md)，验收合同见 [R2 出口检查表](milestones/R2-local-terminal.md)，执行状态见 [GitHub 任务入口](TRACKING.md)，结果与来源见[证据索引](EVIDENCE.md)。每份证据只证明其固定源码、构建配置和场景；局部测试、未合并分支及历史成功不自动代表当前产品整体验收。

比较目标固定为 **WindTerm 2.7.0**。功能范围依据[固定提交的官方 README](https://github.com/kingToolbox/WindTerm/blob/a8336c1d1e3dd1a981500dae8ef9eb42066de95f/README.md)、[2.7.0 release](https://github.com/kingToolbox/WindTerm/releases/tag/2.7.0)及仓库内的发行物/运行时研究。官方声明说明目标包含什么，不证明已经在本机验证了其行为，更不证明 TermForge 已实现。各平台、版本限制和具体交互仍需逐项形成可重复场景。

用户确认的目标和权限见 [ADR-0006](adr/0006-confirmed-project-direction.md)：首个完整版本验收 Windows 11 x64，最终覆盖 Windows/Linux/macOS；原版实际行为优先。正式核心已确认继续自主开发 C++，当前 GUI 采用 [Qt Widgets 路线](adr/0008-cpp-core-qt-widgets.md)；Windows PTY 采用[随包固定微软 ConPTY](adr/0007-bundled-microsoft-conpty.md)，新配置尚待整合验收；Alacritty 保留为参考，关键性能指标逐项达标，具体波动范围后续与用户明确。以下历史限定验收描述实现证据，不自动表示用户批准了最终策略或已达到原版一致性。

状态定义：**限定验收**指表中明确范围已有实现与独立回归；**实现中，未验收**指已有实现，但整项目标场景或整合验收未完成；**未实现**指尚无相应产品能力；**待取证**指目标行为或比较结论未知；**限定测量**仅说明已有指定范围的观测，不代表性能预算通过。每行的“路标”表示主要交付位置，不表示之前的路标已完成。

## 构建配置与证据边界

| 配置 | 当前定位 | 可以引用的证据与限制 |
|---|---|---|
| 正式配置：自主 C++ + Qt Widgets + 固定源码 ConPTY | 显式启用 `TERMFORGE_BUILD_QT_SHELL=ON` 时构建 `termforge_qt_shell.exe`。默认校验、部署并运行固定源码修订的微软 ConPTY 文件对，所需文件缺失或身份不符即报错。`TERMFORGE_CONPTY_SOURCE_BUILD=OFF` 为早期官方包对照。 [构建与运行](../src/qt/README.md#构建与运行) · [源码身份](../third_party/conpty/source-revision.json) | [专用 CI](../.github/workflows/qt-cpp.yml)独立构建固定依赖、测试会话和工程 staging。R2 最终包与完整工作流仍待验收 |
| 既有默认/Alpha：自主 C++ + Win32 + 系统 ConPTY | 默认构建入口为 `termforge_shell.exe`，发布的 R1 体验包同属该路线。 [Alpha 发布说明](releases/v0.1.0-alpha.1.md) | 已有 R1 限定验收。各历史记录使用对应提交、系统与构建配置 |
| Alacritty 参考构建：`termforge.4` + 系统 ConPTY | `TERMFORGE_BUILD_CANDIDATE_ADAPTER=ON`；保留[历史核心比较](adr/0003-core-reference-and-reuse.md)与差分，不继续作为正式产品核心扩展 | [固定来源](../experiments/candidates/alacritty/PROVENANCE.md)、[适配边界](../experiments/core/CANDIDATE.md)与 [#39 字素合同](research/streaming-graphemes.md)均只证明其固定源码/配置 |
| 历史 Alacritty/固定后端实验：`termforge.5` + 固定官方 ConPTY | 历史联合测试的适用范围与原失败由[固定基线](research/joint-conpty-baseline.md)和[实验 PR](https://github.com/JavaZeroo/termforge/pull/50)保存 | 只支持当时实验配置的观察 |

## 基础文字交互与本地终端

| 正式 Qt 功能 | 当前状态与边界 | 目标差距 | 路标 |
|---|---|---|---|
| cmd、文字输入与会话 | **实现中，未验收**：可新增、切换和独立关闭随窗口调节网格的 cmd 标签。已有有限导航键、Unicode 文本字节、16 色和组合/提交/取消。真实子进程的退出与八会话隔离已按固定样本核对。 [Qt 入口和复验](../src/qt/README.md) | 其他 shell、工作区/分屏、完整标签行为、键盘/原生 IME 与系统剪贴板的组合、实际 DPI 和呈现仍待验证。八会话证据限离屏合成输入与有界后台输出，第 9 个会话行为未验收 | R2 |
| 历史、选择和复制 | **实现中，未验收**：滚轮、`Ctrl+Shift+Home/End` 和 `QScrollBar` 支持保留历史与阅读锚点。已接入跨历史拖选、自动滚动和有界系统复制。独立固定样本分别核对源位置、重排、淘汰和 Windows QPA 剪贴板读回。 [历史](../experiments/shell/results/2026-11-05-qt-history-anchor/README.md) · [滚动条](../experiments/shell/results/2026-11-07-qt-history-scrollbar/README.md) · [自动滚动](../experiments/shell/results/2026-11-07-qt-selection-autoscroll/README.md) · [系统复制](../experiments/shell/results/2026-11-06-qt-system-copy/README.md) | 历史翻页键映射、选词/选行以及更广 Unicode 和跨场景工作流仍有缺口。大历史搜索和滚动性能仍待测 | R2 / R4 |
| 粘贴 | **实现中，未验收**：键盘和菜单有界读取 `CF_UNICODETEXT`，绑定发起会话，多行先确认。固定 Windows QPA hosted 样本对真实系统剪贴板确认了取消零字节与接受恰好一次，原失败按各次身份保留。 [系统粘贴](../experiments/shell/results/2026-11-06-qt-system-paste/README.md) · [多行尝试记录](../experiments/shell/results/2026-11-06-qt-system-paste/attempt-ledger.md) | 粘贴回底、bracketed paste 与 TUI 组合验证仍待完成 | R2 |
| Find | **实现中，未验收**：`Ctrl+Shift+F`、字面量查询、前后命中和关闭。主屏及历史页按源位置导航及高亮，查询限 4096 字节和 1024 结果，大小写折叠限 ASCII。 [固定样本](../experiments/shell/results/2026-11-06-qt-find/README.md) | 更广 resize、Unicode 与全部文字操作交叉仍待验收，正则与大历史见 R4 | R2 / R4 |

Qt 的 `HistoryViewport` 以源锚点和 `ViewMapping` 将显示页、光标、选区及 Find 命中定位到保留文档。独立核心样本已核对行身份、有界 Unicode Cell 及增量投影。任意显示位置定位与持续输出下的投影进展保证仍待验证。 [核心投影证据](../experiments/core/results/2026-11-02-projection-job/README.md) · [保留与重排](adr/0010-bounded-resize-retention.md)

正式 Qt 配置有冻结的部署文件、固定 app-local CRT 和许可证据，可生成逐文件校验的工程 staging。对应源码/替换条件审查、干净 Windows 运行、单屏 96/144 DPI 及最终产品包仍待验收。 [staging 清单](../packaging/qt-product/manifest.json) · [Qt 部署与许可](../third_party/qt/README.md#产品-staging-与部署边界)

下表的 Win32 限定验收来自固定旧配置。原版行为依据见 [R1-01 合同](research/windterm-r1-text-contract.md)。

| 能力 | 当前状态与边界 | 目标差距 | 路标 |
|---|---|---|---|
| 本地命令输入/输出 | **实现中，未验收**：Windows shell → ConPTY → 有界队列 → 终端核心 → HWND；普通字符、中文字符消息、基础编辑和回收有固定场景。[持续 cmd 输出/Ctrl-C 回归](../experiments/conpty/README.md#持续-cmd-输出与-ctrlc-独立回归)保留完整负载、单次 ETX、原始字节和清理 | [#36](https://github.com/JavaZeroo/termforge/issues/36) 的历史 normal/resize 超时原因未知；旧、新探针的后续成功不是因果修复。原生控制处理函数、完整键盘模式及四 shell 交叉仍需验收；中文字符消息不等于 IME | R1 / R2 |
| 历史浏览 | **Win32 限定验收**：100 行有界物理行历史、滚轮/翻页/原生滚动条与每会话阅读锚点。固定回归包括编号内容、隐藏输出、淘汰、缩放与回收。 [R1 基础回归](../experiments/shell/results/2026-10-23-r1-foundations/validation.json) | Qt 的独立能力见上表。完整 reflow 和历史规模性能仍待验证 | R1 / R2 / R4 |
| 选择与复制 | **限定验收**：屏幕/100行有界历史的拖选、词/行、全选、自动滚动及每会话高亮；菜单/快捷键和实际 CF_UNICODETEXT 精确往返。[最终系统审计](../experiments/shell/results/2026-10-24-r1-selection-accepted/validation.json)覆盖占用反馈/保持/重试、live覆盖失效、同组标签、原生rename EDIT复制与关闭后目标隔离；[带样式尾空格/软硬行/受控DPI](../experiments/shell/results/2026-10-24-r1-styled-system-copy/validation.json)另有两轮系统证据 | [#66](https://github.com/JavaZeroo/termforge/issues/66) 已定位为场景只等待单会话，改为[双方各三 tick 的阶段门槛](research/selection-session-tick-gate.md)；本地数据两轮与[同实现 hosted CF 两轮](../experiments/shell/results/2026-10-26-selection-tick-gate/hosted-validation.json)已分配置复核，#66 限定闭合；普通尾空格经 ConPTY 规范化后不保留；显式空白端点策略未对照，词规则与容量为本项目策略。限定动作与参考差异见统一文字动作，完整组合见 #18 的独立验收；矩形选择归 R4，受控 DPI 不等于真实跨显示器 | R1；扩展 R2 / R4 |
| 粘贴 | **限定验收**：有界传输、异步读取、确认与目标绑定；正常系统粘贴、占用/超限错误与重试、读取和发送途中取消/关闭、隐藏目标、洪流及精确接收前缀均有重复验证。[最终系统证据](../experiments/shell/results/2026-10-24-r1-clipboard-accepted/validation.json)保留晚发布原始 1418 和完整回收 | 默认 Return 等限定参考动作已在统一文字动作验收，全流程交叉见 #18 的独立验收；bracketed paste/应用模式归 R2，未宣称全部系统或任意负载验收 | R1；应用模式 R2 |
| 查找与定位 | **限定验收**：有界增量字面量搜索、原生查找框、方向/循环/高亮、中文、追加与淘汰；持续输出时可显式使用有效局部命中，运行中查询替换/取消/关闭均有[托管两轮独立证据](../experiments/shell/results/2026-10-24-r1-find-accepted/validation.json) | 局部结果不表示全局末端/循环；动作与受控 DPI 交叉见统一文字动作，组合与资源见 #18 的独立验收。正则、跨会话、完整 Unicode caseless 和大历史规模归 R4 | R1 / R4 |
| 统一文字动作 | **限定验收**：共用目标/可用状态，实际终端右键/快捷键、原生 Find/rename 编辑、busy/closing/失效选区反馈及受控 DPI/zoom 保持；[最终系统审计](../experiments/shell/results/2026-10-24-r1-actions-accepted/validation.json)与[逐项对照](research/r1-action-comparison.md)覆盖 Find 实际 Ctrl+C/V、中文重选、默认 Return 和会话隔离 | 复杂 busy/closing 参考行为仍未知，采用明确项目策略；完整流程与八会话有限资源已由 #18 另验收，真实跨显示器及完整 WindTerm 等价未验收 | R1 |
| 中文 IME 与键盘 | **限定验收**：基础按键/修饰键/UTF-16 与实际接收字节；[固定 ZWJ/keycap 输入](../experiments/shell/results/2026-10-26-unicode-keyboard/validation.json)在默认/候选各两轮精确接收。固定 Microsoft 拼音[产品场景](../research/ime/results/2026-10-24-product-ime-contexts/validation.json)覆盖两区域组合/提交/取消、候选、输出/resize/zoom/受控 DPI、隐藏/关闭、历史阅读和原生控件 | 限定 Windows Server 2025 固定拼音及已有场景；同次组合由八会话工作流另验收。固定字素绘制/选择已由 #39 限定完成，但所有输入法、应用键盘模式、完整 IME 和真实不同 DPI 跨屏仍未完成 | R1 / R2 |
| VT 核心 | **实现中，未验收**：自主核心支持基础光标、擦除及编辑、部分 SGR、主/备用屏、每屏 origin 与滚动区域、DECAWM/DECCKM/DECTCEM、ED3/RIS、DSR 6n。已在备用屏时重复进入会将该屏滚动区域复位为全高；返回主屏恢复原区域。正式 Qt 子进程已分别核对 RIS 和重复进入两组各 14 阶段的光标回复。在备用屏将窗口从 91 列缩至 61 列后返回主屏，保存光标停在第 61 列。先扩宽再返回可保留原第 91 列字符。备用屏从 20 行缩至 14 行后，从主屏第 19 行保存的光标返回到第 14 行，先增高再返回也在该行继续写入。 [核心支持表](../experiments/core/README.md) · [原版与回复证据](../research/ui/results/2026-11-24-alternate-ris-reference/README.md) · [Qt 运行](https://github.com/JavaZeroo/termforge/actions/runs/45574242423/job/139068096209) · [两种返回宽度与写入结果](../research/ui/results/2026-11-02-resize-frame-reference/README.md#2026-11-25-补充主屏保存格超出新宽度) · [RIS 后四组跨尺寸恢复与实际写入](../research/ui/results/2026-11-26-ris-resize/README.md) | RIS 后正文与历史归属、完整网格和样式仍待验。`DA1` 仅 strict/replay 回复，recovery 保持静默。`?1004`、`?9001`、单参数 `CSI 1t/2t/3t` 接受后忽略，`?2004`、鼠标模式、`DA2`、`OSC 10/11` 及部分样式未实现 | R2 / R4 |
| Unicode 与文字显示 | **实现中，未验收**：自主核心使用 Unicode 15。Qt 已将固定 ASCII/CJK/补充平面/组合标记样本贯通到真实子进程接收与离屏绘制，使用宿主 fallback。Alacritty 参考的 Unicode 17 流式字素、原子选择与单列 resize 由该配置独立验证。 [Qt 字形与接收证据](../experiments/shell/results/2026-11-04-qt-unicode-presentation/README.md) · [历史候选字素合同](research/streaming-graphemes.md) | 自主核心完整字素与整形、通用 fallback、实际呈现、完整输入与 IME 交叉仍待验证。Qt 固定样本中的 U+200D/U+FE0F 仍被 recovery parser 替换为 U+FFFD，行为差异待裁决 | R2 / R4 |
| resize 与 reflow | **实现中，未验收**：旧 Win32 路径仍按物理行裁剪，Qt 采用有界源行保留和显示投影。固定源码 ConPTY 报告实际网格重排，Qt 按序应用再解析应用输出。固定对照通过原样六阶段、真实 Win32 读写/事件与 WindTerm 本地 Shell 同缓冲深度四阶段。隐藏主屏返回保留裁剪外正文是用户确认的产品差异。 [正式固定源码对照](../experiments/shell/results/2026-11-25-bcd-native-depth/same-source-ab.md) · [旧官方包失败](../experiments/shell/results/2026-11-05-qt-resize-divergence/README.md) · [保留策略](adr/0010-bounded-resize-retention.md) | 原版默认深度/直接 Telnet 行为分列。应用设置不同原生缓冲深度、更广双轴变换及持续输出、模式/编辑交叉、物理呈现和完整 reflow 仍待验证 | R2 |
| 本地 shell 选择 | **实现中，未验收**：Qt 入口提供 cmd。旧 Win32/系统 ConPTY 有 cmd、Windows PowerShell 5.1、PowerShell 7、显式 Ubuntu WSL 的固定启动/目录/尺寸/退出样本。 [旧配置四 shell](../experiments/shell/results/2026-10-24-r2-shell-lifecycle/validation.json) · [WSL 提示符同步](research/wsl-size-return.md) | 其他 Qt shell、配置、异常恢复及约定场景的最终包复验仍待完成。旧 hosted 事件的因果按原负例记录 | R2；权限场景 R4 |
| 常用 TUI | **实现中，未验收**：历史固定官方 ConPTY 取得 Vim 和 More 独立协议回放，Vim 的 strict 失败有记录。显式候选与系统 ConPTY 的 Vim 实际窗口证据属于该实验配置。 [Vim 回放](../experiments/shell/results/2026-11-02-tui-vim/README.md) · [More 回放](../experiments/shell/results/2026-11-02-tui-more/README.md) · [候选配置](../experiments/core/CANDIDATE.md) | 正式 Qt 的完整 TUI 矩阵、应用模式、进入/操作/resize/退出与主屏恢复，按 [R2 合同](milestones/R2-local-terminal.md)验收 | R2 |

## 工作区与会话

| 能力 | 当前状态与边界 | 目标差距 | 路标 |
|---|---|---|---|
| 主区/底部多标签与分屏 | **Win32 限定验收**：两个区域独立选中标签，共计最多 8 个真实本地会话；主区可有一个真实左右 split，Alt+- 创建、Alt+Z 隐藏/恢复、Alt+Left/Right 切换、Alt+X 关闭，[两轮独立输出/后台/焦点/回收证据](../experiments/shell/results/2026-10-24-r2-main-split/validation.json) | 仅主区一个 split；任意嵌套、标签排序/拖放、复制会话、标签搜索/颜色未完成；分屏持久化范围见配置行 | R2 / R3 / R4 |
| 基础窗口操作 | **Win32 限定验收**：新建/切换/关闭/重命名、五条分隔线、面板显隐、底部焦点模式 | 完整会话动作/快捷键、命令面板、目标 Focus/SyncInput 行为 | R1 / R4 |
| 工作区外观 | **实现中，未验收**：Qt 目前提供本地 cmd 标签，完整工作区仍待迁移。既有 Win32 原型；两种逻辑尺寸下部分外框/层级依据直接取证；UI 字体来自独立 OFL 来源，私有加载、实际 GDI face/em/行高与缺失资产失败有自动检查 | 不是像素等价；图标、主题/透明度、多语言及所有状态仍缺。Explorer/Filer/Outline 当前为空区域 | R2 / R3 / R4 |
| DPI 与文字缩放 | **Win32 限定验收**：每会话 50%–200%，受控 96→144→192→96；[字体/显示合同](milestones/R2-font-display.md)明确 12×26 基准网格。两台真实 96-DPI 显示器间往返两次保持窗口、rename 焦点、隐藏标签和网格，[证据](../experiments/shell/results/2026-10-24-r2-monitor-font/validation.json)明确 `distinct_dpi=false`；v2 会话恢复另覆盖 100%/150%/75% zoom。[隔离 Windows 11 环境](research/r2-win11-display-environment.md)另有两个实际 96/120-DPI 输出和独立 Win32 窗口两轮往返，仅属环境基础 | 正式 Qt 产品**单显示器 96/144 DPI** 的完整交互与实际呈现仍未验收；真实跨屏迁移已按[决策 4C](adr/0009-r2-scope-decisions.md)移出 R2、推迟 R4，由 [#297](https://github.com/JavaZeroo/termforge/issues/297) 跟踪。旧恢复证据为同一 96-DPI 私有 desktop，目标字形/逐字形 fallback 未等价 | R2 |
| 配置与会话持久化 | **Win32 限定验收**：有界 v2、原子替换、v1 迁移、空会话与损坏/未知版本保护；[多会话恢复](../experiments/shell/results/2026-10-25-workspace-restore-multi/validation.json)覆盖 shell 类型/目录/标题/zoom/split/窗口，[迁移回退](../experiments/shell/results/2026-10-25-workspace-recovery-guards/validation.json)及[写失败与关闭策略](../experiments/shell/results/2026-10-25-workspace-write-failure/validation.json)均有真实验证。[#61 的合法编辑 BS 前缀](../experiments/shell/results/2026-10-26-workspace-restore-bs/validation.json)与 [#72 的空记录 CR 重绘](../experiments/shell/results/2026-10-27-workspace-empty-cr/validation.json)已限定修正；后者同源码三配置共 18 次保存/恢复/钳制通过；目录/版本完成按[独立输出与新提示符合同](research/workspace-restore-transport-oracle.md)核对；只重建白名单 shell，不恢复原进程/历史；保存失败保留窗口，显式放弃保存才跳过 | 仅当前单工作区、正常窗口和白名单本地会话；偏好、分组/搜索、最大化/多窗口与文字标记恢复归后续。真实 DPI、呈现和 #38 等独立回归不因此视为通过，R2 出口未宣告完成 | R2 / R3 / R4 |
| 文本增强与辅助面板 | **未实现** | 高亮/时间戳/折叠/outline、补全与 Fish 增强、命令历史、Sender/QuickBar、Free Type Mode（输入模式，行为待取证）、vim 本地/远程键位、锁屏、Sysmon | R4 |

## 远程、文件与协议

| 能力 | 当前状态与边界 | 目标差距 | 路标 |
|---|---|---|---|
| SSH 连接与安全身份 | **未实现** | 密码/密钥/keyboard-interactive/agent、主机指纹/变更、取消/超时/断线、凭据存储；基础交互中断纳入 R3；GSSAPI、Windows ssh-agent 和 SSH signal 请求纳入高级目标 | R3；高级认证/SSH signal 请求 R4 |
| SSH 连接编排 | **未实现** | 自动执行、ControlMaster、ProxyCommand/ProxyJump、jumpserver、HTTP/SOCKS5 代理、agent forwarding | R3 / R4 |
| SSH 转发与 X11 | **未实现** | 本地/远程/动态转发与生命周期、X11 forwarding、失败和重连行为 | R4 |
| SFTP/SCP 与文件管理 | **未实现** | 上传/下载/队列/完整性/取消/重试/覆盖、远程编辑冲突；本地/远程 Filer 与 WSL 文件访问 | R3 / R4 |
| Telnet、Raw TCP、串口 | **未实现** | 连接/编码/协商、串口配置、异常恢复、平台与协议兼容矩阵 | R4 |
| X/Y/Zmodem | **未实现** | 协议状态、校验/取消/重试、文件命名与传输期间终端响应 | R4 |
| tmux 深度整合与批量输入 | **未实现** | 区别于普通终端运行 tmux；目标 pane/session 整合、Sender/SyncInput 的实际边界与目标可见性 | R4 |
| 日志、备份与迁移 | **未实现** | 日志正确性/隐私、配置备份/迁移、旧格式导入；未知加密凭据重新输入，不承诺复用私有格式 | R3 / R4 |

## 性能、平台与完整复刻

| 能力 | 当前状态与边界 | 目标差距 | 路标 |
|---|---|---|---|
| 组合验收基础 | **限定验收**：[固定源码审计](../experiments/shell/results/2026-10-24-r1-workflow-foundation/validation.json)覆盖基础数据与快照；[受控夹具](../experiments/shell/WORKFLOW.md)分离编号日志、ConPTY传输、原始接收与阶段日志；固定输入前缀保持4096 UTF-16容量，旧IME模式仍要求空前缀 | 该基础证据不单独验收八会话或真实Copy→Paste→IME；组合出口见 #18 独立验收 | R1 |
| 八会话文字工作流 | **限定验收**：[R1 固定源码整体验收](../experiments/shell/results/2026-10-24-r1-system-workflow/validation.json)覆盖同次 8 会话/拒绝第 9 个、后台有限洪流、历史/Find/实际选择与 CF 复制/确认粘贴/真实拼音、切换缩放、淘汰、取消关闭和重复回收；实际字节、目标隔离、动作与 Job/desktop 回收有独立审计 | 限定 Windows 固定拼音、本地 cmd 与有界负载；这是固定 R1 配置的组合验收，不能替代 R2 候选/新后端组合。实际呈现、常用 TUI、长期稳定及 UI/性能等价仍未验收 | R1 |
| 活体资源与滚动端点 | **限定测量**：普通启动默认关闭；[R1 组合审计](../experiments/shell/results/2026-10-24-r1-system-workflow/validation.json)绑定实际接收、state 与同 revision 的 WM_PAINT/BitBlt/GdiFlush 提交，保留跨时钟校准与阶段资源，可离线重算 | launcher-tree 不等于 UI 归因；观察进程成本不等于增量采样成本，短命进程及退出 CPU 可能漏采。短驻留不证明长期无泄漏，`presented_done=null`，不构成性能通过 | R1；扩展 R4 / R5 |
| 自动正确性与构建 | **已有独立回归**：Windows shell/ConPTY/呈现与 Windows/Linux 核心 CI，另有正式 Qt 固定源码构建及会话/剪贴板等测试。 [构建配置](#构建配置与证据边界) · [证据索引](EVIDENCE.md) | 产品打包、干净机器使用、依赖升级、长期回归和更广平台矩阵。可靠性验收与历史原失败见 [R2 合同](milestones/R2-local-terminal.md)和[可靠性总项 #38](https://github.com/JavaZeroo/termforge/issues/38) | R2 / R4 |
| Windows 产品交付 | **实现中，未验收**：旧 Win32 的 R1 Alpha 1 提供未签名 Windows x64 体验 ZIP，附资源许可和构建身份，布局写入 LOCALAPPDATA。正式 Qt 有独立可复现的工程 staging。 [Alpha 发布验证](releases/v0.1.0-alpha.1.md) · [Qt staging](../src/qt/README.md#构建与运行) | R2 最终包、干净机器、Qt 对应源码/替换条件、签名/安装、配置升级及持续运行待验收。产品 Windows 10、Server 2022 等平台支持仍待验证 | R2 / R3 |
| Linux/macOS 与其他架构 | **未实现**客户端；Linux 核心 CI 已有。[官方平台范围台账](../research/windterm-2.7.0-platform-scope.md)已固定 2.7.0 的 Windows x86/x64、macOS x64、Linux x64 资产及声明下限；这不是 TermForge 支持或实测结论 | 最终需真实 Linux/macOS 客户端；平台调查可提前进行，整合及完整验收排在 Windows 11 x64 完整版本之后。其他系统版本、最低版本和架构由用户后续明确；旧系统及 ARM 等保持调查记录，不自动承诺或排除，也不能由核心 CI 推断产品支持 | 持续调查；R5 全平台出口 |
| 吞吐、响应、内存与稳定性 | **限定测量**：有候选成本、连续 CPU 区间和合成回归的固定记录。历史诊断的宿主、观测成本和原始失败各有范围。 [历史成本实验](../experiments/measure/R54-COST.md) · [测量证据](EVIDENCE.md) | 正式 Qt 和 WindTerm 的同负载、同正确性、端点、观测扰动及长期/多会话公平性仍待验证。R2 采用回归型治理，可靠基线和各项容差须经用户确认 | 当前优先；贯穿 R1–R5 |
| 大历史与动态内存压缩 | **未实现**产品能力；WindTerm 压缩属于官方性能声明，内部机制尚未证实 | 1/10/100 万行等明确规模下的内存/滚动/搜索/淘汰实验；根据瓶颈决定独立存储/压缩实现 | R4 |
| 完整目标验收 | **待取证**：已有固定版本和局部观察，没有完整行为/性能对照矩阵 | 先在 Windows 11 x64 完整验收功能/行为/UI/性能及独立构建，再完成 Linux/macOS 全平台出口；重要差异和性能容差须有用户确认，Windows 通过不代表全平台完成；不依赖 WindTerm 二进制或专有资源 | R5 分平台出口 |

官方已知限制属于对照边界，默认以固定原版实际行为为准。例如 tmux 嵌套及其与 Sender/SyncInput 的限制、Windows ZMODEM 非 ASCII 文件名问题，应记录原版证据和差异；明确缺陷的修正或重要独立改进先提交用户确认。项目完整开源不要求内部实现一致，但不能以规范、第三方核心或现有项目策略自动豁免行为差距。
