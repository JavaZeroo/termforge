# 当前能力与目标矩阵

工作区已有能力的验收基点为 `2dc8d6d`；R1 有界历史的交付基点为 `9b16acc`，2026-10-23。本文件是当前能力与已知差距的唯一矩阵；路标和执行顺序见[总体计划](PLAN.md)，运行结果与来源见[证据索引](EVIDENCE.md)。局部测试、未提交修改和本地提交均不自动成为已交付能力；具体进度与剩余验收只在对应 issue 维护。这里不记录逐批开发过程，也不以测试数量计算完成百分比。

比较目标固定为 **WindTerm 2.7.0**。功能范围依据[固定提交的官方 README](https://github.com/kingToolbox/WindTerm/blob/a8336c1d1e3dd1a981500dae8ef9eb42066de95f/README.md)、[2.7.0 release](https://github.com/kingToolbox/WindTerm/releases/tag/2.7.0)及仓库内的发行物/运行时研究。官方声明说明目标包含什么，不证明已经在本机验证了其行为，更不证明 TermForge 已实现。各平台、版本限制和具体交互仍需逐项形成可重复场景。

状态定义：**已验证（有限）**指表中明确范围已有实现与回归；**实现中，未验收**指模块或窗口接入已有，但整项验收尚未通过；**部分**指有基础但不足以完成目标场景；**未实现**指尚无相应产品能力；**待取证**指目标行为或比较结论尚不明确。每行的“路标”表示主要交付位置，不表示前面的路标已经完成。

## 基础文字交互与本地终端

官方参考的基础文字行为已按 [R1-01 合同](research/windterm-r1-text-contract.md)完成两组各两轮的自动取证；这为独立实现提供行为依据，产品能力仍按下表分别验收。

| 能力 | 当前状态与边界 | 目标差距 | 路标 |
|---|---|---|---|
| 本地命令输入/输出 | **部分；当前有未解决回归**：Windows cmd → ConPTY → 有界队列 → Recovery → HWND；普通字符、中文字符消息、回车、退格、四方向键和退出回收仍有限验证。R1 历史证据覆盖 Ctrl-C，但当前 Windows 11 上 normal/resize 的持续 `cmd for /L` 中断超时由 [#36](https://github.com/JavaZeroo/termforge/issues/36) 保留 | ETX、实际控制事件和实际 Ctrl+C 键记录均未使当前持续输出场景恢复；不能沿用旧证据宣称当前回归通过。中文消息不等于 IME；完整键盘模式与其他 shell 未验收 | R1 / R2 |
| 历史浏览 | **已验证（有限）**：100 行有界物理行历史、只读投影、滚轮/翻页/原生滚动条与每会话阅读锚点；编号内容、隐藏输出、淘汰、缩放与回收已有[重复回归证据](../experiments/shell/results/2026-10-23-r1-foundations/validation.json)，符合 [#12](https://github.com/JavaZeroo/termforge/issues/12) 限定合同 | 基础动作对照见统一文字动作；完整组合见已验收的 #18，reflow、外部核心替换与大规模历史性能未验收 | R1；扩展 R2 / R4 |
| 选择与复制 | **已验证（有限）**：屏幕/100行有界历史的拖选、词/行、全选、自动滚动及每会话高亮；菜单/快捷键和实际 CF_UNICODETEXT 精确往返。[最终系统审计](../experiments/shell/results/2026-10-24-r1-selection-accepted/validation.json)覆盖占用反馈/保持/重试、live覆盖失效、同组标签、原生rename EDIT复制与关闭后目标隔离；[带样式尾空格/软硬行/受控DPI](../experiments/shell/results/2026-10-24-r1-styled-system-copy/validation.json)另有两轮系统证据 | 普通尾空格经 ConPTY 规范化后不保留；显式空白端点策略未对照，词规则与容量为本项目策略。限定动作与参考差异见统一文字动作，完整组合见 #18 的独立验收；矩形选择归 R4，受控 DPI 不等于真实跨显示器 | R1；扩展 R2 / R4 |
| 粘贴 | **已验证（有限）**：有界传输、异步读取、确认与目标绑定；正常系统粘贴、占用/超限错误与重试、读取和发送途中取消/关闭、隐藏目标、洪流及精确接收前缀均有重复验证。[最终系统证据](../experiments/shell/results/2026-10-24-r1-clipboard-accepted/validation.json)保留晚发布原始 1418 和完整回收 | 默认 Return 等限定参考动作已在统一文字动作验收，全流程交叉见 #18 的独立验收；bracketed paste/应用模式归 R2，未宣称全部系统或任意负载验收 | R1；应用模式 R2 |
| 查找与定位 | **已验证（有限）**：有界增量字面量搜索、原生查找框、方向/循环/高亮、中文、追加与淘汰；持续输出时可显式使用有效局部命中，运行中查询替换/取消/关闭均有[托管两轮独立证据](../experiments/shell/results/2026-10-24-r1-find-accepted/validation.json) | 局部结果不表示全局末端/循环；动作与受控 DPI 交叉见统一文字动作，组合与资源见 #18 的独立验收。正则、跨会话、完整 Unicode caseless 和大历史规模归 R4 | R1 / R4 |
| 统一文字动作 | **已验证（有限）**：共用目标/可用状态，实际终端右键/快捷键、原生 Find/rename 编辑、busy/closing/失效选区反馈及受控 DPI/zoom 保持；[最终系统审计](../experiments/shell/results/2026-10-24-r1-actions-accepted/validation.json)与[逐项对照](research/r1-action-comparison.md)覆盖 Find 实际 Ctrl+C/V、中文重选、默认 Return 和会话隔离 | 复杂 busy/closing 参考行为仍未知，采用明确项目策略；完整流程与八会话有限资源已由 #18 另验收，真实跨显示器及完整 WindTerm 等价未验收 | R1 |
| 中文 IME 与键盘 | **已验证（有限）**：基础按键/修饰键/UTF-16 与实际接收字节；[固定 ZWJ/keycap 输入样本](../experiments/shell/results/2026-10-26-unicode-keyboard/validation.json)在默认/候选各两轮精确接收；固定 Microsoft 拼音[五组产品场景](../research/ime/results/2026-10-24-product-ime-contexts/validation.json)覆盖两区域组合/提交/取消、真实候选、输出/resize/zoom/受控 DPI、隐藏/关闭，以及历史阅读、Find/rename 双 Escape 与回收 | 限定 Windows Server 2025 固定拼音及测试范围；同一八会话组合已由 #18 另验收，真实跨屏、所有输入法、复杂字素绘制/选择及应用模式归 R2/后续矩阵 | R1 / R2 |
| VT 核心 | **部分**：自研核心支持基础光标/擦除、CR/LF/BS/TAB、有限 SGR、有界未知序列恢复；[ADR-0003](adr/0003-core-reference-and-reuse.md)已决定 R2 新能力复用候选 Term/Processor；`0.26.1-dev+termforge.2` [固定补丁](../experiments/candidates/alacritty/PROVENANCE.md)已纳入上游资源修复、ED1/ED2 和稳定行元数据，[共同轨迹](../experiments/differential/results/2026-10-25-candidate-common-v5/summary.json)为 95 次一致；[C++ 适配](../experiments/core/CANDIDATE.md)已在显式候选构建接入 ShellWindow，真实 Vim/resize/光标回复经 ConPTY 和有界 InputQueue 验证 | 默认构建保留自研回退；完整 R1 候选回归与 #36 输入缺口未关闭，插删、完整模式和最终呈现仍未验收。备用屏、滚动区与查询回复仅限已记录场景；旧平台支持缺口不因路线决定消失 | R2 / R4 |
| Unicode 与文字显示 | **部分**：UTF-8、固定 Unicode 15.0 宽度策略、有限组合标记、16 色/粗体、Consolas 逐格绘制；实际 DWrite glyph run 已检查 Consolas 身份、中文非 Consolas fallback 与缺字负例 | 完整字素/emoji/整形、通用 fallback、真彩色/更多属性、Powerline；WindTerm 的 Unicode 13 声明与本项目策略差异需明确裁决 | R2 / R4 |
| resize 与 reflow | **已验证（有限）**：默认回退按 ADR-0005 物理行裁剪；候选在固定 ConPTY 重放、逐字节及连续同步中保持唯一行身份、Find 和选区，按 ID 与完整行键合并有界历史，真实淘汰不恢复，显式清空不复活；[合同与验证](../experiments/core/results/2026-10-26-resize-replay/validation.json)。真实文字动作、八会话及 Vim 60×12 resize 有独立场景 | 仅已识别的 resize 事务；候选重排不同于默认裁剪，不等于完整 reflow、备用屏/滚动区全语义或 WindTerm 等价。缩窄默认不可恢复；真实 DPI、呈现、性能及 #36/#38 独立回归未闭合 | R2 |
| 本地 shell 选择 | **部分**：`ShellLaunch` 分离可执行文件、参数、工作目录及 shell 命令差异；cmd、Windows PowerShell 5.1、PowerShell 7、显式 Ubuntu WSL 各有[两轮真实启动/目录/尺寸/正常退出证据](../experiments/shell/results/2026-10-24-r2-shell-lifecycle/validation.json)，无效程序启动错误可见且可关闭后恢复 cmd | 仅声明上述固定入口和隔离目录；任意 profile/参数、通用分屏/排序、全部运行时错误提示及管理员 shell 未完成 | R2；权限场景 R4 |
| 常用 TUI | **部分**：显式候选构建已有两轮真实 Vim 进入、编辑、resize、退出写盘、主屏恢复及光标回复，见[候选产品证据](../experiments/core/CANDIDATE.md)；默认构建保留自研回退，`presented_done=null` | #28 的更多应用、完整输入/输出模式和最终产品验收未完成；headless 核心比较不替代真实场景，当前自有 DIB 不代表实际上屏 | R2 |

## 工作区与会话

| 能力 | 当前状态与边界 | 目标差距 | 路标 |
|---|---|---|---|
| 主区/底部多标签与分屏 | **已验证（有限）**：两个区域独立选中标签，共计最多 8 个真实本地会话；主区可有一个真实左右 split，Alt+- 创建、Alt+Z 隐藏/恢复、Alt+Left/Right 切换、Alt+X 关闭，[两轮独立输出/后台/焦点/回收证据](../experiments/shell/results/2026-10-24-r2-main-split/validation.json) | 仅主区一个 split；任意嵌套、标签排序/拖放、复制会话、标签搜索/颜色未完成；分屏持久化范围见配置行 | R2 / R3 / R4 |
| 基础窗口操作 | **已验证（有限）**：新建/切换/关闭/重命名、五条分隔线、面板显隐、底部焦点模式 | 完整会话动作/快捷键、命令面板、目标 Focus/SyncInput 行为 | R1 / R4 |
| 工作区外观 | **部分**：两种逻辑尺寸下部分外框/层级依据直接取证；UI 字体来自独立 OFL 来源，私有加载、实际 GDI face/em/行高与缺失资产失败有自动检查 | 不是像素等价；图标、主题/透明度、多语言及所有状态仍缺。Explorer/Filer/Outline 当前为空区域 | R2 / R3 / R4 |
| DPI 与文字缩放 | **已验证（有限）**：每会话 50%–200%，受控 96→144→192→96；[字体/显示合同](milestones/R2-font-display.md)明确 12×26 基准网格。两台真实 96-DPI 显示器间往返两次保持窗口、rename 焦点、隐藏标签和网格，[证据](../experiments/shell/results/2026-10-24-r2-monitor-font/validation.json)明确 `distinct_dpi=false`；v2 会话恢复另覆盖 100%/150%/75% zoom | 不同 DPI 的真实跨屏、实际呈现和真实 IME 跨屏仍未验收；恢复证据为同一 96-DPI 私有 desktop，目标字形/逐字形 fallback 未等价 | R2 |
| 配置与会话持久化 | **已验证（有限）**：有界 v2、原子替换、v1 迁移、空会话与损坏/未知版本保护；[多会话恢复](../experiments/shell/results/2026-10-25-workspace-restore-multi/validation.json)覆盖 shell 类型/目录/标题/zoom/split/窗口，[迁移回退](../experiments/shell/results/2026-10-25-workspace-recovery-guards/validation.json)及[写失败与关闭策略](../experiments/shell/results/2026-10-25-workspace-write-failure/validation.json)均有真实验证。只重建白名单 shell，不恢复原进程/历史；保存失败保留窗口，显式放弃保存才跳过 | 仅当前单工作区、正常窗口和白名单本地会话；偏好、分组/搜索、最大化/多窗口与文字标记恢复归后续。真实 DPI、呈现和 #38 等独立回归不因此视为通过，R2 出口未宣告完成 | R2 / R3 / R4 |
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
| 组合验收基础 | **已验证（有限）**：[固定源码审计](../experiments/shell/results/2026-10-24-r1-workflow-foundation/validation.json)覆盖基础数据与快照；[受控夹具](../experiments/shell/WORKFLOW.md)分离编号日志、ConPTY传输、原始接收与阶段日志；固定输入前缀保持4096 UTF-16容量，旧IME模式仍要求空前缀 | 该基础证据不单独验收八会话或真实Copy→Paste→IME；组合出口见 #18 独立验收 | R1 |
| 八会话文字工作流 | **已验证（有限）**：[固定源码整体验收](../experiments/shell/results/2026-10-24-r1-system-workflow/validation.json)覆盖同次8会话/拒绝第9个、五路有限洪流、历史/Find/实际选择与CF复制/确认粘贴/真实拼音/切换缩放/淘汰、取消关闭及5次8→6→8；两fresh各26主检查点、13系统观察点、28动作记录、409/512观测、18实例回收，Copy→Paste→IME段B原件51→57→58字节、其他目标无串入，C大粘贴前缀另核 | 限定Windows固定拼音、本地cmd与有界负载；data-only回归另列，实际呈现、常用TUI、长期稳定、UI/性能等价仍未验收 | R1 |
| 活体资源与滚动端点 | **已验证（有限测量）**：普通启动默认关闭；[同一审计](../experiments/shell/results/2026-10-24-r1-system-workflow/validation.json)核对两次A滚动的实际接收/state/同revision的WM_PAINT BitBlt/GdiFlush提交、8次跨时钟夹测、19阶段窗、5次同六会话驻留及创建窗，公开记录可离线重算 | launcher树不等于UI归因；全Python观察进程成本不等于增量采样成本，短命进程及退出CPU可能漏采。单样本CPU保持unknown，约1秒短驻留不证明长期无泄漏，presented_done=null，不构成性能通过 | R1；扩展 R4 / R5 |
| 自动正确性与构建 | **已验证（有限）**：Windows shell/ConPTY/呈现及 Windows/Linux 核心 CI、差分/恢复/队列/生命周期场景 | 产品打包、干净机器使用、依赖升级、长期回归和更广平台矩阵 | R2 / R4 |
| Windows 产品交付 | **部分**：首个 [v0.1.0-alpha.1 预发布](releases/v0.1.0-alpha.1.md)提供 Windows x64、静态 MSVC 运行库、附资源许可和构建身份的免安装 ZIP；未签名，已验环境限 Windows 11 本机与 Server 2025 CI，布局仍写入 LOCALAPPDATA | 这是 R1 体验包，尚非 R2 日常开发版；干净机器与更广平台矩阵、签名/安装、配置升级及持续运行仍待完成。Windows 10 未验收，Server 2022 已知资源门槛未过 | R2 / R3 |
| Linux/macOS 与其他架构 | **未实现**客户端；Linux 核心 CI 已有。[官方平台范围台账](../research/windterm-2.7.0-platform-scope.md)已固定 2.7.0 的 Windows x86/x64、macOS x64、Linux x64 资产及声明下限；这不是 TermForge 支持或实测结论 | 各平台 PTY/输入/字体/UI/安装与架构覆盖。Windows 7/8/10、macOS 10.13–12、Linux glibc 2.17+ 和未列出的 ARM/旧新系统等按台账分别验收或保留未知，不能由核心 CI 推断产品支持 | R4 / R5 |
| 吞吐、响应、内存与稳定性 | **部分**：机制实验、连续 CPU 区间校准、自有合成回归；**待取证**：与 WindTerm 的实际差距 | 自动同负载/同配置/同正确性对照、采样扰动、实际呈现/交互端点、长时/多会话公平性、可复核预算 | 持续工作；R4 / R5 收敛 |
| 大历史与动态内存压缩 | **未实现**产品能力；WindTerm 压缩属于官方性能声明，内部机制尚未证实 | 1/10/100 万行等明确规模下的内存/滚动/搜索/淘汰实验；根据瓶颈决定独立存储/压缩实现 | R4 |
| 完整目标验收 | **待取证**：已有固定版本和局部观察，没有完整行为/性能对照矩阵 | 全部目标行均有实现、验收与来源；剩余差异有明确裁决，支持平台可从源码复建；不依赖 WindTerm 二进制或专有资源 | R5 |

官方已知限制属于对照边界，不自动变成必须新增的功能或必须重现的缺陷。例如 tmux 嵌套及其与 Sender/SyncInput 的限制、Windows ZMODEM 非 ASCII 文件名问题，应记录实际差异。项目完整开源不要求复制 WindTerm 的私有实现、资源或错误；行为兼容与独立改进须明确标注。
