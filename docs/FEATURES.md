# 当前能力与目标矩阵

本文件维护当前 main 的能力与已知差距。R1 已按限定合同验收；R2 仍在实现与整合，尚未达到日常开发版出口。路标见[总体计划](PLAN.md)，验收合同见 [R2 出口检查表](milestones/R2-local-terminal.md)，执行状态见 [GitHub 任务入口](TRACKING.md)，结果与来源见[证据索引](EVIDENCE.md)。每份证据只证明其固定源码、构建配置和场景；局部测试、未合并分支及历史成功不自动代表当前产品整体验收。

比较目标固定为 **WindTerm 2.7.0**。功能范围依据[固定提交的官方 README](https://github.com/kingToolbox/WindTerm/blob/a8336c1d1e3dd1a981500dae8ef9eb42066de95f/README.md)、[2.7.0 release](https://github.com/kingToolbox/WindTerm/releases/tag/2.7.0)及仓库内的发行物/运行时研究。官方声明说明目标包含什么，不证明已经在本机验证了其行为，更不证明 TermForge 已实现。各平台、版本限制和具体交互仍需逐项形成可重复场景。

状态定义：**限定验收**指表中明确范围已有实现与独立回归；**实现中，未验收**指已有实现，但整项目标场景或整合验收未完成；**未实现**指尚无相应产品能力；**待取证**指目标行为或比较结论未知；**限定测量**仅说明已有指定范围的观测，不代表性能预算通过。每行的“路标”表示主要交付位置，不表示之前的路标已完成。

## 构建配置与证据边界

| 配置 | 当前定位 | 可以引用的证据与限制 |
|---|---|---|
| 默认 main：自研核心 + 系统 ConPTY | 默认构建与已发布 Alpha 的基础路线，不需要 Rust | 已有 R1 限定验收；VT/Unicode 能力仍为有限子集，不能引用候选核心的字素或 TUI 结果 |
| 显式候选 main：`termforge.4` + 系统 ConPTY | `TERMFORGE_BUILD_CANDIDATE_ADAPTER=ON`；按 [ADR-0003](adr/0003-core-reference-and-reuse.md)推进 R2 新终端能力 | [固定来源](../experiments/candidates/alacritty/PROVENANCE.md)、[适配边界](../experiments/core/CANDIDATE.md)与 [#39 字素合同](research/streaming-graphemes.md)；候选正确性通过不关闭系统 ConPTY 或 R2 父项 |
| 实验分支：`termforge.4` + 固定官方 ConPTY | [PR #50（Draft）](https://github.com/JavaZeroo/termforge/pull/50)，联合源码 `3e5b96d` 已重建并[冻结真实基线](research/joint-conpty-baseline.md)，尚未合并或通过产品整体验收 | [#47](https://github.com/JavaZeroo/termforge/issues/47) 跟踪后端整合；终端 margin 与原生状态分列。[resize 残尾 #51](https://github.com/JavaZeroo/termforge/issues/51)、[Console-width Vim #52](https://github.com/JavaZeroo/termforge/issues/52)、[未完成粘贴的确定性取消夹具 #53](https://github.com/JavaZeroo/termforge/issues/53)仍开放，现有 completed 观察不能证明部分发送取消通过或产品 Cancel 故障；worker gate [#62](https://github.com/JavaZeroo/termforge/issues/62) 已有[独立夹具修正与双后端实际 worker 验证](research/sender-fence.md)，默认产品路径不启用 fence，也不替代 #53 的 UI 取消/关闭；工作区 [#63](https://github.com/JavaZeroo/termforge/issues/63) 回归仍开放 |

以下能力默认指已合并实现；某个配置的通过结果不能转用于另一个配置。R1 的真实系统 CF/拼音验收属于固定自研核心配置，不外推到候选 `.4` 或实验后端联合配置。

## 基础文字交互与本地终端

官方参考的基础文字行为已按 [R1-01 合同](research/windterm-r1-text-contract.md)完成两组各两轮的自动取证；这为独立实现提供行为依据，产品能力仍按下表分别验收。

| 能力 | 当前状态与边界 | 目标差距 | 路标 |
|---|---|---|---|
| 本地命令输入/输出 | **实现中，未验收**：Windows shell → ConPTY → 有界队列 → 终端核心 → HWND；普通字符、中文字符消息、基础编辑和回收有固定场景。[持续 cmd 输出/Ctrl-C 回归](../experiments/conpty/README.md#持续-cmd-输出与-ctrlc-独立回归)保留完整负载、单次 ETX、原始字节和清理 | [#36](https://github.com/JavaZeroo/termforge/issues/36) 的历史 normal/resize 超时原因未知；旧、新探针的后续成功不是因果修复。原生控制处理函数、完整键盘模式及四 shell 交叉仍需验收；中文字符消息不等于 IME | R1 / R2 |
| 历史浏览 | **限定验收**：100 行有界物理行历史、只读投影、滚轮/翻页/原生滚动条与每会话阅读锚点；编号内容、隐藏输出、淘汰、缩放与回收有[固定回归](../experiments/shell/results/2026-10-23-r1-foundations/validation.json)，组合出口见八会话工作流 | 候选历史/resize 合同另列；完整 reflow 与大历史规模的内存、搜索和滚动性能未验收 | R1；扩展 R2 / R4 |
| 选择与复制 | **限定验收**：屏幕/100行有界历史的拖选、词/行、全选、自动滚动及每会话高亮；菜单/快捷键和实际 CF_UNICODETEXT 精确往返。[最终系统审计](../experiments/shell/results/2026-10-24-r1-selection-accepted/validation.json)覆盖占用反馈/保持/重试、live覆盖失效、同组标签、原生rename EDIT复制与关闭后目标隔离；[带样式尾空格/软硬行/受控DPI](../experiments/shell/results/2026-10-24-r1-styled-system-copy/validation.json)另有两轮系统证据 | [#66](https://github.com/JavaZeroo/termforge/issues/66) 已定位为场景只等待单会话，改为[双方各三 tick 的阶段门槛](research/selection-session-tick-gate.md)；本地数据两轮与[同实现 hosted CF 两轮](../experiments/shell/results/2026-10-26-selection-tick-gate/hosted-validation.json)已分配置复核，#66 限定闭合；普通尾空格经 ConPTY 规范化后不保留；显式空白端点策略未对照，词规则与容量为本项目策略。限定动作与参考差异见统一文字动作，完整组合见 #18 的独立验收；矩形选择归 R4，受控 DPI 不等于真实跨显示器 | R1；扩展 R2 / R4 |
| 粘贴 | **限定验收**：有界传输、异步读取、确认与目标绑定；正常系统粘贴、占用/超限错误与重试、读取和发送途中取消/关闭、隐藏目标、洪流及精确接收前缀均有重复验证。[最终系统证据](../experiments/shell/results/2026-10-24-r1-clipboard-accepted/validation.json)保留晚发布原始 1418 和完整回收 | 默认 Return 等限定参考动作已在统一文字动作验收，全流程交叉见 #18 的独立验收；bracketed paste/应用模式归 R2，未宣称全部系统或任意负载验收 | R1；应用模式 R2 |
| 查找与定位 | **限定验收**：有界增量字面量搜索、原生查找框、方向/循环/高亮、中文、追加与淘汰；持续输出时可显式使用有效局部命中，运行中查询替换/取消/关闭均有[托管两轮独立证据](../experiments/shell/results/2026-10-24-r1-find-accepted/validation.json) | 局部结果不表示全局末端/循环；动作与受控 DPI 交叉见统一文字动作，组合与资源见 #18 的独立验收。正则、跨会话、完整 Unicode caseless 和大历史规模归 R4 | R1 / R4 |
| 统一文字动作 | **限定验收**：共用目标/可用状态，实际终端右键/快捷键、原生 Find/rename 编辑、busy/closing/失效选区反馈及受控 DPI/zoom 保持；[最终系统审计](../experiments/shell/results/2026-10-24-r1-actions-accepted/validation.json)与[逐项对照](research/r1-action-comparison.md)覆盖 Find 实际 Ctrl+C/V、中文重选、默认 Return 和会话隔离 | 复杂 busy/closing 参考行为仍未知，采用明确项目策略；完整流程与八会话有限资源已由 #18 另验收，真实跨显示器及完整 WindTerm 等价未验收 | R1 |
| 中文 IME 与键盘 | **限定验收**：基础按键/修饰键/UTF-16 与实际接收字节；[固定 ZWJ/keycap 输入](../experiments/shell/results/2026-10-26-unicode-keyboard/validation.json)在默认/候选各两轮精确接收。固定 Microsoft 拼音[产品场景](../research/ime/results/2026-10-24-product-ime-contexts/validation.json)覆盖两区域组合/提交/取消、候选、输出/resize/zoom/受控 DPI、隐藏/关闭、历史阅读和原生控件 | 限定 Windows Server 2025 固定拼音及已有场景；同次组合由八会话工作流另验收。固定字素绘制/选择已由 #39 限定完成，但所有输入法、应用键盘模式、完整 IME 和真实不同 DPI 跨屏仍未完成 | R1 / R2 |
| VT 核心 | **实现中，未验收**：默认自研支持基础光标/擦除（含 ECH）、CR/LF/BS/TAB、有限 SGR 和有界恢复。显式候选 `.4` 接入 `Term/Processor`、C++ 拥有值适配、稳定行元数据、每缓冲区 margin 与流式字素；[共同轨迹](../experiments/differential/results/2026-10-26-candidate-common-v6/summary.json)及[源 margin 合同](../experiments/shell/results/2026-10-26-margin-state/validation.json)为各自固定版本结果 | [#27](https://github.com/JavaZeroo/termforge/issues/27) 的 origin/wrap、插删、模式/resize 交叉未闭合；[#41](https://github.com/JavaZeroo/termforge/issues/41) 系统 ConPTY 已丢失的 margin 不能由末端补回。ECH pending-wrap 差异见[限定合同](research/more-capture-ech.md)；#36/#38、完整呈现和平台支持独立验收 | R2 / R4 |
| Unicode 与文字显示 | **实现中，未验收**：默认自研保留 Unicode 15。显式候选 `.4` 的 [#39 Unicode 17 流式字素合同](research/streaming-graphemes.md)已限定验收：固定 ZWJ/keycap 的单 lead/两列、光标、SGR/查询、原子选择复制、单列 resize 与有界无损分片；实际 ConPTY/DWrite 核对固定样本的单 glyph、字体和网格 | [#30](https://github.com/JavaZeroo/termforge/issues/30) 仍需完整输入/IME 交叉；所有脚本/整形、通用 fallback、所有原生重放、真彩色/更多属性、Powerline 和真实跨屏未验收。分段 helper 的规范数据通过不代表完整终端 Unicode 正确性；[#54](https://github.com/JavaZeroo/termforge/issues/54) 保留已观察的性能成本 | R2 / R4 |
| resize 与 reflow | **限定验收**：默认按 ADR-0005 物理行裁剪；候选在[固定 ConPTY 重放](../experiments/core/results/2026-10-26-resize-replay/validation.json)与连续同步中保持行身份、Find/选区和有界历史，淘汰/清空不复活；候选 `.4` 另覆盖主/备用屏缩到单列 | [#29](https://github.com/JavaZeroo/termforge/issues/29) 的完整 resize 交叉未完成；固定事务不等于完整 reflow 或 WindTerm 等价。系统/实验 ConPTY、备用屏/滚动区、真实 DPI、呈现、性能及 #36/#38 分别验收；默认缩窄不可恢复 | R2 |
| 本地 shell 选择 | **实现中，未验收**：`ShellLaunch` 分离可执行文件、参数、工作目录及 shell 命令差异；cmd、Windows PowerShell 5.1、PowerShell 7、显式 Ubuntu WSL 各有[两轮真实启动/目录/尺寸/正常退出证据](../experiments/shell/results/2026-10-24-r2-shell-lifecycle/validation.json)，无效程序启动错误可见且可关闭后恢复 cmd | 仅声明上述固定入口和隔离目录；任意 profile/参数、通用分屏/排序、全部运行时错误提示及管理员 shell 未完成 | R2；权限场景 R4 |
| 常用 TUI | **实现中，未验收**：显式候选 + 系统 ConPTY 有固定 Vim 进入、编辑、60×12 resize、退出写盘、主屏恢复及光标回复的[产品证据](../experiments/core/CANDIDATE.md)；More 尾部 ECH 与捕获端点有[限定修复](research/more-capture-ech.md) | [#28](https://github.com/JavaZeroo/termforge/issues/28) 的更多应用及完整输入/输出模式未完成；PR #50 实验后端的 Vim 失败不能由系统后端结果豁免。headless 比较与自有 DIB 不等于真实呈现，`presented_done=null` | R2 |

## 工作区与会话

| 能力 | 当前状态与边界 | 目标差距 | 路标 |
|---|---|---|---|
| 主区/底部多标签与分屏 | **限定验收**：两个区域独立选中标签，共计最多 8 个真实本地会话；主区可有一个真实左右 split，Alt+- 创建、Alt+Z 隐藏/恢复、Alt+Left/Right 切换、Alt+X 关闭，[两轮独立输出/后台/焦点/回收证据](../experiments/shell/results/2026-10-24-r2-main-split/validation.json) | 仅主区一个 split；任意嵌套、标签排序/拖放、复制会话、标签搜索/颜色未完成；分屏持久化范围见配置行 | R2 / R3 / R4 |
| 基础窗口操作 | **限定验收**：新建/切换/关闭/重命名、五条分隔线、面板显隐、底部焦点模式 | 完整会话动作/快捷键、命令面板、目标 Focus/SyncInput 行为 | R1 / R4 |
| 工作区外观 | **实现中，未验收**：两种逻辑尺寸下部分外框/层级依据直接取证；UI 字体来自独立 OFL 来源，私有加载、实际 GDI face/em/行高与缺失资产失败有自动检查 | 不是像素等价；图标、主题/透明度、多语言及所有状态仍缺。Explorer/Filer/Outline 当前为空区域 | R2 / R3 / R4 |
| DPI 与文字缩放 | **限定验收**：每会话 50%–200%，受控 96→144→192→96；[字体/显示合同](milestones/R2-font-display.md)明确 12×26 基准网格。两台真实 96-DPI 显示器间往返两次保持窗口、rename 焦点、隐藏标签和网格，[证据](../experiments/shell/results/2026-10-24-r2-monitor-font/validation.json)明确 `distinct_dpi=false`；v2 会话恢复另覆盖 100%/150%/75% zoom | 不同 DPI 的真实跨屏、实际呈现和真实 IME 跨屏仍未验收；恢复证据为同一 96-DPI 私有 desktop，目标字形/逐字形 fallback 未等价 | R2 |
| 配置与会话持久化 | **限定验收**：有界 v2、原子替换、v1 迁移、空会话与损坏/未知版本保护；[多会话恢复](../experiments/shell/results/2026-10-25-workspace-restore-multi/validation.json)覆盖 shell 类型/目录/标题/zoom/split/窗口，[迁移回退](../experiments/shell/results/2026-10-25-workspace-recovery-guards/validation.json)及[写失败与关闭策略](../experiments/shell/results/2026-10-25-workspace-write-failure/validation.json)均有真实验证。[#61 的合法编辑 BS 前缀](../experiments/shell/results/2026-10-26-workspace-restore-bs/validation.json)已在默认/候选 system 限定修正；目录/版本完成按[独立输出与新提示符合同](research/workspace-restore-transport-oracle.md)核对；只重建白名单 shell，不恢复原进程/历史；保存失败保留窗口，显式放弃保存才跳过 | 仅当前单工作区、正常窗口和白名单本地会话；偏好、分组/搜索、最大化/多窗口与文字标记恢复归后续。真实 DPI、呈现和 #38 等独立回归不因此视为通过，R2 出口未宣告完成 | R2 / R3 / R4 |
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
| 自动正确性与构建 | **限定验收**：Windows shell/ConPTY/呈现及 Windows/Linux 核心 CI、差分/恢复/队列/生命周期场景 | 产品打包、干净机器使用、依赖升级、长期回归和更广平台矩阵；#40 的 CI 纯夹具超时根因与持续复验仍未闭合 | R2 / R4 |
| Windows 产品交付 | **实现中，未验收**：首个 [v0.1.0-alpha.1 预发布](releases/v0.1.0-alpha.1.md)提供 Windows x64、静态 MSVC 运行库、附资源许可和构建身份的免安装 ZIP；未签名，已验环境限 Windows 11 本机与 Server 2025 CI，布局仍写入 LOCALAPPDATA | 这是 R1 体验包，尚非 R2 日常开发版；干净机器与更广平台矩阵、签名/安装、配置升级及持续运行仍待完成。Windows 10 未验收，Server 2022 已知资源门槛未过 | R2 / R3 |
| Linux/macOS 与其他架构 | **未实现**客户端；Linux 核心 CI 已有。[官方平台范围台账](../research/windterm-2.7.0-platform-scope.md)已固定 2.7.0 的 Windows x86/x64、macOS x64、Linux x64 资产及声明下限；这不是 TermForge 支持或实测结论 | 各平台 PTY/输入/字体/UI/安装与架构覆盖。Windows 7/8/10、macOS 10.13–12、Linux glibc 2.17+ 和未列出的 ARM/旧新系统等按台账分别验收或保留未知，不能由核心 CI 推断产品支持 | R4 / R5 |
| 吞吐、响应、内存与稳定性 | **实现中，未验收**：已有机制实验、连续 CPU 区间校准与合成回归；候选字素改动的受限诊断中 project/interactive 吞吐中位数约下降 16%/9%，由 [#54](https://github.com/JavaZeroo/termforge/issues/54) 跟踪 | 共享宿主、allocator 包装与缺少真实呈现/输入时延端点使该诊断不构成正式预算。仍需同负载/同正确性、无插桩对照、观测开销、长期/多会话公平性和可复核预算；与 WindTerm 的实际差距**待取证** | 持续工作；R4 / R5 收敛 |
| 大历史与动态内存压缩 | **未实现**产品能力；WindTerm 压缩属于官方性能声明，内部机制尚未证实 | 1/10/100 万行等明确规模下的内存/滚动/搜索/淘汰实验；根据瓶颈决定独立存储/压缩实现 | R4 |
| 完整目标验收 | **待取证**：已有固定版本和局部观察，没有完整行为/性能对照矩阵 | 全部目标行均有实现、验收与来源；剩余差异有明确裁决，支持平台可从源码复建；不依赖 WindTerm 二进制或专有资源 | R5 |

官方已知限制属于对照边界，不自动变成必须新增的功能或必须重现的缺陷。例如 tmux 嵌套及其与 Sender/SyncInput 的限制、Windows ZMODEM 非 ASCII 文件名问题，应记录实际差异。项目完整开源不要求复制 WindTerm 的私有实现、资源或错误；行为兼容与独立改进须明确标注。
