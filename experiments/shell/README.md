# 本地 shell 工作区

本说明负责工作区的运行、接口、所有权和验证入口。当前能力统一见[功能矩阵](../../docs/FEATURES.md)，剩余联合验收见 [R2 合同](../../docs/milestones/R2-local-terminal.md)，执行状态见[跟踪索引](../../docs/TRACKING.md)。受控编号日志、洪流、八会话 data-only 场景及资源接缝见 [WORKFLOW](WORKFLOW.md)。

这是可从源码运行的 Windows 本地交互原型：真实 shell → ConPTY → 有界输出队列 → 核心 → 历史视口/只读快照 → DirectWrite/Direct2D → 自有 HWND；窗口输入经有界队列回到 ConPTY。默认构建使用自研 Recovery 核心；显式 `TERMFORGE_BUILD_CANDIDATE_ADAPTER=ON` 构建使用候选 Term/Processor，接口见[候选适配说明](../core/CANDIDATE.md)。两条路径分别验证，不能把候选核心的能力算给默认构建，也不能把默认核心的限制算成候选能力缺失。R1 的固定基础文字验收不代替 R2 日常开发版出口。

## 运行

先按根 CONTRIBUTING 构建。Visual Studio Release 的入口为：

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --target termforge_shell --parallel 2
.\build\bin\Release\termforge_shell.exe
```

程序为 Windows GUI 子系统，不额外打开宿主控制台。默认打开真实可见窗口，在启动目录运行系统 `cmd.exe /d /q`，关闭 AutoRun，设置 UTF-8 code page 与固定 `TF>` 提示符。支持普通字符、中文字符消息、回车、退格和方向键；R1 历史证据包含 Ctrl-C，但当前 Windows 11 持续 `cmd for /L` 输出的中断回归 [#36](https://github.com/JavaZeroo/termforge/issues/36) 尚未解决，不能宣称当前该场景通过。输入 `exit` 或关闭标签会结束本会话并回收其自有子树；关闭主窗口回收全部标签。最后一个标签关闭后工作区保留，可以继续新建。用户启动的进程仍可访问用户自己的文件；这里是本地 shell，不是沙箱。

构建会自动复制独立开源的 Roboto Mono UI 字体及许可到 EXE 旁的 `assets/fonts/`；移动或分发 EXE 时保留此目录及同目录的 `shell_clipboard_reader.exe`。字体从可执行文件目录私有加载，不依赖当前工作目录，不安装到系统。缺失或实际字体不匹配时明确失败。来源、固定哈希及 OFL 许可见[字体说明](../../assets/fonts/README.md)。

`ShellLaunch` 将可执行文件、参数、工作目录和命令差异放在同一启动描述中。默认仍为原 cmd；固定测试另覆盖 Windows PowerShell 5.1、PowerShell 7 和显式 Ubuntu WSL。PowerShell 原生命令使用 `&`，WSL helper 将绝对 Windows 路径转换为 `/mnt/<drive>/...`，不能用同一字符串拼法处理三种 shell。两轮真实启动/目录/尺寸/逐个正常退出及启动失败恢复见[验证](results/2026-10-24-r2-shell-lifecycle/validation.json)；通用分屏/排序、任意 profile/参数和全部运行时错误提示尚未完成。

默认 1360×860 逻辑内容区（以 96 DPI 为基准），底部真实 ShellPane，每会话保留最多 100 行历史；网格按扣除原生滚动条的客户区和实际单元格计算。窗口支持拖动缩放/最大化，网格范围为 1–500 列、1–200 行。默认自研路径采用[物理行 resize 契约](../../docs/adr/0005-physical-row-resize.md)，缩窄会截去右侧内容，增宽不会恢复；不执行备用屏、滚动区域和终端查询，未知序列按 [Recovery 契约](../core/RECOVERY.md)消费。候选路径具有独立的备用屏、滚动区域、真实回复、流式字素和 reflow 切片，完整 resize 合同仍由 [#29](https://github.com/JavaZeroo/termforge/issues/29)验收，不能由上游行为直接替换产品策略。共享[逐格 renderer](../presentation/README.md)的布局缓存至多 1,024 项，满时清空重建；这不是产品级缓存或性能验收。

## 基础文字操作

- 鼠标拖选文字，双击选词、三击选物理行（不含行末换行）；`Ctrl+Shift+C` 或 Copy 菜单复制，`Ctrl+Shift+A` 全选，Escape 清除选区。选区快照最多 8 MiB，复制上限为 4 MiB UTF-8，系统 Unicode 剪贴板另受 1 Mi UTF-16 单元限制；超限明确拒绝。首版词规则将 ASCII 字母/数字/下划线/连字符及非 ASCII 基字归为词，空格成组，其余 ASCII 标点单格；这是本项目策略。
- `Ctrl+Shift+F` 打开当前会话查找窗口；字面量查询默认区分大小写，关闭 Match case 只折叠 ASCII 大小写。静止内容的 Enter/Shift+F3 向前、F3 向后并循环；持续扫描时显式操作可跳到当前已验证的局部命中，提示仍在搜索且不循环，静默末尾初选仍等本轮完成。结果截断明确提示只在有限结果内循环；这是洪流状态的本项目策略。Escape 关闭查找并恢复终端焦点。查询上限 4096 UTF-8 字节，结果最多 1024 项；复杂 Unicode 不做规范化，正则/跨会话属于后续范围。

- 滚轮和滚动条浏览历史，`Ctrl+Shift+Home` / `Ctrl+Shift+End` 到历史首/尾；回看时新输出保持阅读锚点，普通输入和开始粘贴回到底部。历史淘汰时钳制到仍保留的最早行。
- `Ctrl+Shift+V`、`Shift+Insert` 或 Session → Paste 读取剪贴板。读取在独立有时限的自有 helper 中进行；多行文字先确认，默认焦点为 Paste，Enter 确认发送，Escape 取消；焦点移到 Cancel 后 Enter 取消。一次任务固定目标会话，切换标签不重定向剩余字节；Escape 或 Cancel paste 停止后续发送。
- 当前独立粘贴策略：最多 1 Mi 个 UTF-16 单元、4 MiB UTF-8，换行规范化为 CR，拒绝不完整 UTF-16 和不支持的控制字符；不宣称已对齐 WindTerm 默认策略，也不无条件发送 bracketed-paste 包装。
- 键盘处理包含 Tab/Shift+Tab、Home/End/Insert/Delete/Page、修饰键、AltGr 和跨消息 UTF-16。IME 采用每终端独立原生上下文，只发送已提交文字；候选位置使用物理网格。固定 Microsoft 拼音的组合、候选、提交/取消已有五组实际产品验收；其他输入法和真实跨屏仍未验收，见 [IME 工具](../../research/ime/README.md)。

R1 的 [#12–#17 限定合同](../../docs/milestones/R1-local-text.md)和 [#18 八会话数据场景](results/2026-10-24-r1-eight-sessions/validation.json)分别保留历史身份。数据场景覆盖有限并发负载、文字操作及反复回收，不访问系统剪贴板、不调用真实 IME；[实际 Copy→Paste→IME 系统组合](results/2026-10-24-r1-system-workflow/validation.json)有独立验收。资源记录不构成性能通过，后续核心或传输变更需要在相应构建重新验证。

## 工作区操作与已知差异

- `Ctrl+Shift+T`、主区 `+` 或 Session → New main session 新建主区本地 cmd；`Ctrl+T` 或底部 `+` 新建底部 shell。两个区域合计最多 8 个，各自保留选中的标签，可同时显示终端。
- 点击终端、标签或右侧 Sessions 列表改变当前操作区域；状态栏显示 `Focus: Main/Shell`。`Ctrl+Tab` 在该区域切换，`Ctrl+W` 关闭该区域选中的会话，标签 `x` 关闭它所在的会话。主工具栏提供新建、关闭、重命名和只读本地目标说明。
- 主区 `Alt+-` 创建一个真实左右 split，两个独立会话并排显示；`Alt+Left/Right` 切换 pane，`Alt+Z` 隐藏/恢复而不终止进程，`Alt+X` 关闭当前 pane。隐藏标签继续接收输出，关闭只影响目标会话。仅支持一个主区左右 split，[独立操作验证](results/2026-10-24-r2-main-split/validation.json)与下文 v2 工作区恢复分别留证；任意嵌套和拖放不在此范围。
- `Ctrl++` / `Ctrl+-` 或 View 菜单放大/缩小当前会话文字，每次 25%，范围 50%–200%；也支持数字键盘加减。`Ctrl+0` 重置 100%，状态栏显示比例。其他会话保持原比例，重命名时不触发终端缩放；每会话比例随下文 v2 工作区保存与恢复。
- `F2`、标签中键或 Session → Rename shell 重命名；支持 Unicode，Enter 提交、Esc 取消，切换焦点时提交。空白、控制字符或不完整 UTF-16 不接受，最多 64 个 UTF-16 单元；标签与 Sessions 列表同步，标题不写入终端。
- `F11` / View → Focus shell 扩大底部终端；在此模式新建/选择主区会话会先恢复普通布局，再把焦点交给可见的主终端。View → Side panels 显示/隐藏两侧区域。Session 菜单可新建、关闭和退出。
- 拖动左右两列、底部 Shell、左侧与右侧上下区域的五条分隔线调整布局。线宽 1，命中宽 5；拖出窗口仍限于比例边界，Esc 或丢失鼠标捕获取消本次拖动。
- 三列约 1:4:1、上下约 2:1、24 高面板标题、26 高 Shell 标签、27 高底栏、29 高状态栏来自 [直接取证](../../docs/research/windterm-ui-2026-10-23.md)。主区容器空时高 19、有标签时高 26，连接状态工具栏高 34，主终端外区域从 y=93 开始，依据[第二轮测量](../../docs/research/windterm-ui-typography-2026-10-23.md)。主区最后一个标签关闭后恢复空状态，可重新新建。
- Explorer/Filer/Outline 暂为空布局区域，没有文件浏览/大纲功能。当前菜单只暴露已实现动作；底栏是原创新建快捷入口，没有复刻 WindTerm QuickBar 配置。
- UI 使用独立随附的 Roboto Mono，9pt；终端继续使用 Consolas，96 DPI / 100% 下为 12×26 网格与原有调色板。字号、整数物理单元、裁剪和光标共用度量。启动读取窗口 DPI，`WM_DPICHANGED` 更新字体、布局、鼠标命中、编辑器和隐藏终端，取消未提交拖动并保留编辑内容/焦点；先应用最终像素矩形再更新终端网格，避免中间缩窄裁剪。主终端外框已按固定逻辑尺寸对齐，内部网格、标签标题宽度及重命名/关闭策略仍是 TermForge 自身实现。
- 96→144→192→96 受控消息场景已验证；`ui-monitors` 另在两台真实 96-DPI 显示器间往返两次，保持 rename 焦点、隐藏标签和网格，`distinct_dpi=false`，见[R2 合同](../../docs/milestones/R2-font-display.md)与[验证](results/2026-10-24-r2-monitor-font/validation.json)。不同 DPI 的真实跨屏、实际呈现、字体/像素等价、标签排序、SSH/SFTP 仍未验收；配置恢复范围见下文。
- 主窗口是既有 Win32/DirectWrite 实验的有限扩展，不锁定未来产品 GUI 框架；没有复制 WindTerm 主题、图标或反编译实现。

## 工作区保存

配置沿用 `%LOCALAPPDATA%\TermForge\workspace-v1.layout` 这一历史路径，但新文档为 v2；不在另一文件保存副本。保留 v1 严格读取与内存迁移，启动不立即改写旧文件。文件缺失、v1 或损坏配置走现有默认 cmd 回退；未知新版本或读取失败禁止旧程序覆盖。有效空会话与缺失配置分开。

在提交的增删/选择/重命名/缩放/分屏、布局拖动、显隐/焦点模式及窗口尺寸移动结束后保存；启动恢复期间抑制写入，关闭时先抓取最终快照再回收子进程，后续异步删除不再覆盖。保存使用同目录独占临时文件与原子替换；写入失败保留旧文件并显示 `Workspace could not be saved`，后续成功保存才清除。正常关闭若最终保存失败，则保留窗口和活动会话，允许修复后重试；`Session → Exit without saving` / `Ctrl+Shift+W` 才显式放弃未保存状态并回收，不覆盖旧配置，也不表示保存成功。只保存固定 shell 工厂类型、必要 WSL distribution、明确目录、标题、正常窗口位置/逻辑客户区、布局、焦点/split 和 zoom；自定义命令行、秘密、环境、历史和进程身份不进入配置。不承诺断电持久性或多进程冲突合并。

已完成单 cmd 及 **cmd + Windows PowerShell 主区分屏** 的真实双进程恢复：两轮覆盖独立目录、Unicode 标题、100%/150%/75% zoom、split 成员/底部焦点和非默认窗口/布局；新进程实际核对 shell 类型/目录/尺寸，配置逐字一致，旧输出不注入新屏幕。模型与 Windows/Ubuntu 检查见[模型结果](results/2026-10-25-workspace-state-v2/validation.json)，单会话见[核心验证](results/2026-10-25-workspace-restore-core/validation.json)，多会话见[双进程验证](results/2026-10-25-workspace-restore-multi/validation.json)。真实 v1 延迟迁移、有效空会话、损坏默认回退及未知版本不覆盖另见[迁移与回退验证](results/2026-10-25-workspace-recovery-guards/validation.json)；写入失败、成功重试及关闭失败策略见[故障恢复验证](results/2026-10-25-workspace-write-failure/validation.json)。恢复窗口若超出实际工作区，则按可用边界裁剪；测试以独立 Win32 计算和超大保存尺寸区分完整恢复与裁剪恢复，不冒称同尺寸恢复。完整 R2 出口、真实 DPI/呈现和 #38 等回归不因此视为完成。

自动测试仅读写各自结果目录下的 `workspace.layout`，不会读取或修改上述用户配置。恢复场景为 data-only，不生成位图；若等待超时，`persistence-failure.json` 保留有界 PTY 输出和文本屏幕以区分输入与显示问题，内容仅限合成会话。

## 选区丢弃诊断

几何/DPI/缩放变化后若选区被 `TextSelection::reconcile` 丢弃，可用环境变量 `TERMFORGE_SELECTION_DIAG=<jsonl 路径>` 打开一次性诊断：每次丢弃写入一行 `{"schema":"termforge-selection-discard-v1","reason":"..."}`，`reason` 取 `epoch-mismatch`、`anchor-missing-or-shrunk`、`row-identity-replaced`、`row-text-changed`、`endpoint-unavailable` 之一。未设置该变量时不写任何文件、行为不变；`experiments/shell/run.py` 的 `execute()` 会自动把该变量指向本次场景产物目录下的 `selection-discard.jsonl`，因此失败时该文件随 `build/test-artifacts/` 一起保留。`selection_core` 里新增的分组只验证观察器能收到分支名。

## 所有权与调度

- ConPTY 输出线程将最多 64 KiB 的读取块放入 256 KiB 队列；满时等待窗口消费，正常运行不丢弃字节。
- UI 每个 16 ms 定时消息以 8 KiB 小块消费，最多 64 KiB；块间检查 4 ms 预算，随后发布一次快照。单次 feed、快照和绘制仍可能超过预算，它不是硬实时保证。解析和快照发布同属 UI 线程；dirty 范围包含内容与旧/新光标行，多次失效可合并为一次 WM_PAINT。
- 输入写线程独立服务至多 16 KiB 的队列，每条消息不超过 1 KiB。UI 不等待 WriteFile；容量满时拒绝该次输入并发出提示，不宣称输入已成功。关闭期间停止接收新输入。
- resize 由独立 worker 执行，最多一个在途和一个合并请求；UI 先更新核心与快照并继续消费字节。关闭在回收 ConPTY 之前 join 此 worker。字节流没有 resize 世代边界，不保证所有中间帧及历史与 ConPTY 自身重排完全一致。
- 关闭工作在后台执行，UI 继续消费尾部输出；停止输入写入并反复取消该线程的同步 I/O，终止本会话 Job，关闭 ConPTY，等待读取线程退出。队列排空后执行 `finish()`、提交最终画面并销毁窗口。只终止自有 Job 内的进程，包括 shell 留下的后代。
- 正常 `exit 37` 保留实际退出码 37；关闭正在输出的会话通过 Job 取消。外部自动监督器另建拥有范围明确的 Job：宿主收到 `GO` 前不创建 shell，超时后回收整个自有树。

当前同步 I/O 取消与 ConPTY 关闭不提供跨所有 Windows 版本的硬截止时间保证；UI 仍能处理消息，自动测试有外部时限。Server 2022 已知资源门槛问题继续保留，未扩大已验收平台范围。

专用测试可启用UI线程按值runtime快照；worker状态在各自短锁内读取，关闭前及实际join后分别留证，未知容量和未完成查询保持null。资源驱动复用既有sampler，明确按launcher-tree观察并将sampler单列；实际采样开销及调度缺口须随结果保留，短命后代可能漏采。native阶段通过8次夹测校准；19阶段窗与两次实际滚动的state/同revision WM_PAINT提交已按限定场景验证。呈现和长期无泄漏仍未知。普通启动默认关闭这些观测，不自动采样或写文件；字段和限制见[观测合同](WORKFLOW.md#运行快照)。

## 无人值守验证

```powershell
$env:TERMFORGE_BIN_DIR = (Resolve-Path build/bin/Release).Path
python -m unittest experiments.shell.test_shell -v
python experiments/shell/run.py --bin-dir build/bin/Release --out local/experiments/shell-new --repeats 3

# 八会话数据场景：两次 fresh rounds，启用资源采样；目录取新路径。
$env:TERMFORGE_TEST_ARTIFACTS = Join-Path (Get-Location) 'local/workflow-check'
python -m unittest experiments.shell.test_workflow_sessions -v
```

测试启动真实 cmd，不使用假 shell；窗口放在独立非输入 desktop，从不切换用户桌面。输入通过队列化 `PostMessage(WM_CHAR/WM_KEYDOWN)` 进入同一个窗口处理路径，不冒充物理键盘操作。断言匹配完整独立输出行，不能用输入命令中的标记冒充执行结果。

测试需先完成根目录全量构建，包括 `shell_test_launcher`、`shell_size_client`、`shell_input_client`、`shell_text_client`、`shell_queue_probe` 和 `shell_clipboard_reader`。原生 launcher 在收到 `GO` 后才在指定 desktop 创建子进程，并由自有 Job 限定整个进程树。启动、Job 分配失败和超时也保留证据并检查回收。

E2驱动是唯一默认传入`resources=True`的场景驱动，依赖项目Python环境中的psutil；其他普通执行不因导入runner而加载采样依赖。E2 native/launcher/外层时限为70/80/90秒，CTest `workflow_sessions`为300秒。固定源码、实际完成轮数与回归结果见[验证](results/2026-10-24-r1-eight-sessions/validation.json)和[复验](results/2026-10-24-r1-eight-sessions/regression.json)，不能用旧二进制或跳过项补成通过。

resize 两类场景各重复三次，通过真实 `SetWindowPos` 缩放并由子进程查询实际尺寸；包括快速连续请求、洪流中缩放、随后 Ctrl-C 或关闭。正常 resize 验证 60×12、100×28、72×18；关闭场景只查询前两个尺寸。脚本还将自有窗口移至非输入 desktop 右下边缘，验证 retained DIB 不依赖可见更新矩形。每次绘制与全量重绘作 RGB 对比；这些带检查的样本不能作为低开销延迟数据。

每次运行依次验证普通命令、中文、退格、左右方向键编辑，再运行一百万轮的输出命令。至少持续一秒且消费 64 KiB 后发送 Ctrl-C，再执行 `echo TF_AFTER`；确认对应状态与已提交帧后 `exit 37`。[#36](https://github.com/JavaZeroo/termforge/issues/36) 保留历史中断超时与根因缺口；本次 normal/resize 各十轮未复现，不能写成产品修复。独立的[原始 ConPTY 回归](../conpty/README.md#持续-cmd-输出与-ctrlc-独立回归)另保留输入/输出与系统文件身份。另测输出中关闭，正常/关闭各重复三次，检查 Job 活跃进程归零与自有 desktop 消失。监督器用真实存活 shell 验证超时清理。独立队列探针验证容量、背压、顺序、停止唤醒与输入拒绝。短回归不代表长时无泄漏。

产品真实 IME 单独由 [`research.ime.product_runner`](../../research/ime/README.md) 显式托管运行，使用严格受控的真实 `SendInput`，不在这些私有 desktop 场景中合成 IME 消息。五个 `ui-ime-*` 场景有独立输入桌面/焦点/代次检查和内外 Job/GO 门控，开发机拒绝运行；历史/Find 与 rename 双 Escape 已有[固定源码系统审计](../../research/ime/results/2026-10-24-product-ime-contexts/validation.json)。

测试的 `scripted_input_to_state_ms` 从 Ctrl-C 的 WM_CHAR 被处理开始，到后续 `TF_AFTER` 完整输出行进入快照；`scripted_input_to_submit_ms` 到包含该行的 WM_PAINT BitBlt/GdiFlush 完成。两者包含中断、后续命令和调度，不能当作单键延迟。自有 DIB 截图不是合成器呈现，`presented_done=null`。关闭场景没有该标记，相关字段为 null，不参与延迟统计。

默认监督时限为 40 秒，原生基础场景为 25 秒，主区、DPI、选择和查找场景为 30 秒；`ui-clipboard-system` 的监督/原生场景分别为 60/45 秒，watchdog 场景在 3 秒后强制回收。stdout/stderr、失败状态、最终自有 BMP、身份与环境均保留。CTest 产物在 `TERMFORGE_TEST_ARTIFACTS/shell`，默认 `build/test-artifacts/shell`；runner 输出目录必须不存在。只有审核过的合成记录可公开，日常用户会话不会自动录制或上传。

`ui-shells` 要求四种固定入口全部可用，缺少 Ubuntu 不作为跳过。指定一次性 GitHub Windows VM 在运行严格测试前，由 [prepare_hosted_shells](../../tools/prepare_hosted_shells.py) 检查真实启动；仅 Ubuntu 未注册时导入固定 SHA-256 的官方 rootfs。已有 Ubuntu 启动失败仍失败，不替换发行版、不启用系统功能或更改默认项。该入口拒绝普通本机环境；纯 guard/oracle 检查用 `python -m unittest tools.test_prepare_hosted_shells -v`，不执行导入。

四 shell 场景的 `shells-progress.jsonl` 保留阶段、退出及回收事实。提前退出立即失败，最多 8 KiB 的真实输出尾部只写入场景 `_private/local-shells/`；CI 不上传 `_private`。选择场景先把原固定内容锚点滚入可见区再拖动，另以 681/860 两种客户区高度回归；内容、淘汰和复制断言保持相同。

E2产物在`TERMFORGE_TEST_ARTIFACTS/workflow-sessions/run-*`；公开白名单包含合成source/raw/progress、`sessions.json`、脱敏`sample.public.json`、`resource-observations.jsonl`和`resource-summary.json`。原始`sample.json`、controller `stdout.private.json`、`.identity`、`transport.bin`、私有stderr和整个`_resources-private/`均不上传，失败兜底也排除。资源公开记录在CI本地与raw逐条比对，下载后可离线重算脱敏观测；无法从公开artifact重哈希未上传的原始传输和二进制。

## 依据

独立实现，接口语义核对 [Microsoft ConPTY 会话文档](https://learn.microsoft.com/en-us/windows/console/creating-a-pseudoconsole-session)、[CancelSynchronousIo](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelsynchronousio) 和 [VT 输入序列](https://learn.microsoft.com/en-us/windows/console/console-virtual-terminal-sequences#input-sequences)。Windows 窗口退格消息的 U+0008 映射为 VT 的 DEL；首轮将其原样发送导致删词，已由真实 shell 编辑用例检出并修正。

窗口场景按用户行为维护，入口在 `test_shell.py` / `test_selection_bytes.py` / `test_selection_context.py`，不以历史批次命名表示完成程度：

| 场景 | 检查内容 | 独立结果与边界 |
|---|---|---|
| `candidate-tui` / `candidate-input-escape` | 仅显式候选构建：真实 Vim 进入/插入/60×12 resize/退出写盘，主屏恢复、独立 `CSI 6n` 光标回复及独立 ESC 接收 | `test_candidate_tui.py` 各连续两轮；固定 `candidate-vim.vim` 以短 `-S` 命令加载，避免交互式 cmd 拆分长命令；以 Vim 写入的真实 `VimEnter`/`InsertEnter` 事件文件为启动握手，以 `Ctrl-O` 调用命令保存实际 `&columns/&lines`，核对脚本、文件、子进程尺寸及 InputQueue 字节；有界合成输出另存 `candidate-output.bin`，私有 desktop/Job，无截图。不把该路径写成 Vim Escape 或备用屏验收，不替代 #28/#29 或实际呈现验收 |
| `candidate-alt` | 仅显式候选构建：两次 1049h/1049l、主屏文字/样式/光标恢复、历史隔离 | [两轮限定验证](results/2026-10-26-alternate-screen/validation.json)独立核对源 VT、五阶段真实 `CHAR_INFO`、transport 和候选状态；原生恢复快照一致，备用屏文字不进入主历史。夹具按实际控制台状态有界同步，不把切换前快照误记为切换后；origin/wrap、独立 margin、resize 和 #27 整项未验收，`presented_done=null` |
| `candidate-scroll` | 仅显式候选构建：DECSTBM 局部滚动、区域外保持、有限历史语义 | [两轮限定验证](results/2026-10-26-scroll-region/validation.json)独立核对源 VT、三阶段真实 `CHAR_INFO`、候选保留文档及 `pyte==0.8.2` transport 重放；区域外不变、C/D 位于正确区域、A/B 不混入主历史。origin/wrap、独立 margin、resize 和 #27 整项未验收，`presented_done=null` |
| `candidate-margins` | 仅显式候选构建：主屏 `2;3r`、备用屏 `4;5r`、恢复后局部滚动及四阶段真实控制台快照 | [限定验证](results/2026-10-26-margin-state/validation.json)分别核对公开源 VT 的恢复、resize/RIS 与真实 ConPTY 的宿主差异；该批固定系统宿主出现 `REGION_CIXED`，由 [#41](https://github.com/JavaZeroo/termforge/issues/41) 保留。两轮差异观察均为 `margins_per_buffer_verified=false`，不是能力通过；无截图或前台输入，`presented_done=null` |
| `candidate-unicode` | 仅显式候选构建：固定分解组合、单 emoji、ZWJ/keycap 的真实 ConPTY 原文、子控制台 `CHAR_INFO`、整格宽度、实际 DWrite 单 glyph 与产品拖选复制 | v2 按[流式字素合同](../../docs/research/streaming-graphemes.md)两轮核对初始/真实 100×30 resize；保留源/运输/原生映射的区别，缺字、分裂 glyph、半字复制和伪尺寸被拒绝。限定四样本，不声明完整 Unicode/IME 或 WindTerm 等价；`presented_done=null` |
| `ui-workflow-sessions` | 8真实会话/拒绝第9个、五路有限洪流、阅读/Find/输入/缩放/淘汰、合成粘贴取消/关闭、5次8→6→8 | `test_workflow_sessions`独立literal/raw/journal/UI和回收校验；每轮26检查点、18身份、235/256观测，资源scope=launcher-tree。data-only，不代替同次实际CF/IME、native阶段校准或操作提交端点 |
| `ui` / `ui-edit` / `ui-restore` | 多标签、五处分隔线、重命名、捕获/Escape 回滚、新进程恢复布局 | 真实子进程尺寸、保存文件、焦点与回收；只新建会话，不恢复原进程，恢复范围见工作区保存 |
| `ui-main` | 主区/底部隔离、隐藏输出、焦点模式、空区域与关闭目标 | `geometry.json` 按实际客户区/DPI和布局合同检查 HWND/字体/矩形；内部网格由实际终端窗口扣除原生滚动条和物理单元格后计算 |
| `ui-dpi` | 每会话缩放、隐藏标签、受控 DPI、边缘命中/拖动及长行保留 | `dpi.json` 区分受控 DPI 与系统 DPI；实际子进程尺寸及 dirty/full 图像比较；不替代真实跨屏 |
| `ui-monitors` | 至少两台真实显示器间往返，保持 rename 焦点、隐藏标签、网格与 dirty/full 图像 | `monitors.json` 核对真实矩形、实际 DPI 和 `distinct_dpi`；当前两台均 96，不覆盖不同 DPI 或实际呈现 |
| `ui-shells` | cmd、PowerShell 5.1/7、显式 Ubuntu WSL 的启动、参数、隔离目录、实际尺寸和逐个正常退出 | `shells.json` 固定四种身份及 `normal_exit=true`；PowerShell 原生命令前缀与 WSL helper 路径分别处理 |
| `ui-split` | 主区一个真实左右 split 的独立输出、Alt 焦点、隐藏后台、恢复与关闭 | `split.json` 核对真实矩形、可见性和3会话回收；仅一个 split，任意嵌套/拖放/持久化未完成 |
| `ui-launch-failure` | 无效可执行文件的错误显示、失败会话关闭及 cmd 恢复 | `launch-failure.json` 核对 `CreateProcess: 2` 和3会话回收；不声称全部运行时错误对话框 |
| `ui-history` | 编号日志、滚轮/翻页/拖动、阅读锚点、隐藏输出、淘汰、缩放与输入回到底部 | `history.json` 与独立编号 oracle；`native-scroll.jsonl` 记录系统原生非客户区 tracker 的轨迹，使用队列鼠标消息，不移动用户鼠标 |
| `ui-selection` | 正逆拖选、词/行、宽字/组合字符、缩放、双会话、后台追加及淘汰失效 | `selection.json` 与独立逐字预期；检查实际窗口高亮及回收，本场景不操作系统剪贴板 |
| `ui-copy-system` | 选择后实际 Copy 菜单/快捷键系统复制、占用失败反馈/保持CF/释放后重试 | 旧 v3/v4 只算各自覆盖；v5 必须真实 owner 握手、原始错误、sentinel/sequence、strict release/join 和成功重试。[两轮最终系统审计](results/2026-10-24-r1-selection-accepted/validation.json)通过；只在指定一次性 hosted 环境运行，本地跳过不算系统验收 |
| `ui-selection-bytes` / `ui-copy-bytes-system` | 真实 ConPTY 软折行、硬 CRLF、中文/组合字符、显式带样式尾两空格和受控 96→144→192→96 | 独立 producer/transport/capture 文件与逐行 oracle；本地模式不访问剪贴板，system 模式仅 disposable hosted VM。普通尾空格在 ConPTY 中的规范化仍是缺口，不能据此宣称与 WindTerm 该行为等价 |
| `ui-selection-context` / `ui-copy-context-system` | 3会话的同组标签隐藏/返回、live CR覆盖失效、rename EDIT、带选区关闭后的目标隔离 | 16阶段独立literal/journal/transport/raw；local不执行有效CF，system仅disposable hosted并要求真实EDIT Copy与选区CF。实际system两轮已通过[独立审计](results/2026-10-24-r1-selection-accepted/validation.json)；未知显式空白端点策略不据此裁决 |
| `ui-actions` / `ui-actions-system` | 真实终端右键菜单、快捷键、Find/rename 原生编辑、busy/closing/无会话反馈，以及命中和选区跨受控 DPI/zoom | local每轮26阶段且不执行有效CF；system每轮36阶段，覆盖Find原生Ctrl+A/C/V、中文查询重选/Escape、终端Copy/Paste、Shift+Insert、默认Return及目标切换。[固定源码两轮系统审计](results/2026-10-24-r1-actions-accepted/validation.json)已通过；仅一次性hosted运行。范围与策略见[动作对照](../../docs/research/r1-action-comparison.md)，完整CF/IME组合及资源阶段/滚动端点见[#18整体验收](results/2026-10-24-r1-system-workflow/validation.json) |
| `ui-find-stream` | 实际持续 CR 输出中导航、运行中替换查询、取消及关闭目标 | 10 阶段独立 oracle、两个原始接收文件和有限 worker/Job 回收；局部结果绝不冒充全局循环 |
| `ui-find` | 多批搜索、方向/循环、大小写/中文/无结果、蓝黄高亮、后台追加、淘汰与 Escape | `find.json`、逐阶段记录及真实 `ReadConsoleW` 接收字节；查询在原生 EDIT 中操作，独立 oracle 检查稳定位置与焦点 |
| `ui-keyboard` | 修饰键、编辑键、Unicode 与会话目标 | 固定中文、分解组合标记、单个 emoji、`👩‍💻` ZWJ 和 `1️⃣` keycap 经真实 `ReadConsoleW` 接收端与独立预期逐字节核对；只验证消息输入保真，不证明字素绘制、物理键盘或真实 IME |
| `ui-paste` | 默认 Paste/Enter 发送一次、默认 Escape 取消、Tab/Enter、隐藏目标、输出洪流、发送途中取消和关闭 | v2 `paste-workflow.json` 与 `input.bin`/`cancel.bin`/`close.bin`；接收端事件握手确保动作发生在发送途中；本场景不读取用户剪贴板；旧 v1 默认 Cancel 证据保留且不计作新默认验收 |
| `ui-paste-system` | 真实 Unicode 系统剪贴板 → 快捷键 → 确认 → 接收端 | 只在指定一次性 hosted 环境运行；本地未运行/跳过不计系统验收 |
| `ui-clipboard-system` | 经产品 Paste 验证占用/超限错误与重试、读取途中取消/关闭 | 延迟渲染 owner 握手、真实接收端字节、错误画面及 helper/Job 回收；仅一次性 hosted 环境运行；场景存在不等于整项通过，当前结果见 [#14](https://github.com/JavaZeroo/termforge/issues/14) |

`ui-copy-system` 的嵌套 `copy_contention` 另有错误帧几何版本：v1 历史结果继续只接受 1360×860，不得补写现场几何；新 native 生产 v2，在错误发生并绘制时独立查询前后 `GetClientRect` 和 `GetObjectW` 的实际 DIB 尺寸、stride、格式，要求它们与物理 surface 一致。Python 用同一份现场元数据核对完整 BMP 头和精确字节数，接受宿主合法裁剪后的尺寸，同时拒绝缺失或相互矛盾的元数据。纯 oracle 测试和为旧 BMP 构造的模拟 v2 数据只验证校验器，不会把旧结果升级为真实 v2 观测；新的现场合同必须由新源码在指定 hosted VM 实际执行后验收。

系统剪贴板边界另由 `python -m unittest experiments.shell.test_paste -v` 验证。畸形终止符用例保留 `terminator-before/after/reader.utf16le`：先核对发布前确实没有 NUL，再独立捕获系统返回的完整分配。仅接受“原分配未变且读取器拒绝”或“最后一个单元被系统改为 NUL 且读取器精确返回其前缀”两种有字节证据的结果；其他转换仍失败并留存原始内容。跨平台 `paste_test` 直接测试生产读取器共用的有界解析逻辑，包括无终止符、超限、分配末端与字符上限，不能用系统可能规范化了夹具代替解析边界测试。

原生 `layout_state` 测试覆盖严格读取、截断、非法数值、Unicode 路径和替换失败保留。`renderer_metrics` 覆盖 GDI/DirectWrite × 96/120/144/192 DPI × 50/100/125/200% 共 32 组，检查非整数边界、实际绘字与 dirty/full 一致；DWrite glyph run 另断言 ASCII/组合字符的 Consolas 身份、中文非 Consolas fallback 和缺字负例，不把请求字体或 GDI 对照当作实际 fallback 证明。同一测试还验证私有 Roboto Mono 的 GDI face、9pt em 高度和实际行高，并拒绝越界 DPI；其资产依赖与 `termforge_shell` 共用。测试会在构建目录复制一个不含 `assets/` 的独立进程，确认缺失字体以指定错误明确失败，等待期限 10 秒，结束后删除副本。

合成 `frame.bmp` 和 DPI 图片来自自有工作区/终端绘制，不含原生 EDIT 子控件；编辑器由实际 HWND/字体断言验证。这些图片不是用户桌面截图、像素等价或实际呈现完成证据。历史批次的测量与修复依据统一从[证据索引](../../docs/EVIDENCE.md)进入。
