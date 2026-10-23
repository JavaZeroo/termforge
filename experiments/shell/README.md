# 本地 shell 工作区

受控编号日志、洪流、八会话data-only场景及资源接缝见 [WORKFLOW](WORKFLOW.md)。E2在真实工作区验证有限并发负载、文字状态及反复回收；同次实际Copy→Paste→IME完整工作流、资源阶段校准和操作提交端点仍待验收，实时状态见 [#18](https://github.com/JavaZeroo/termforge/issues/18)。

这是可从源码运行的 Windows 本地交互原型：真实 `cmd.exe` → ConPTY → 有界输出队列 → Recovery 核心 → 历史视口/只读快照 → DirectWrite/Direct2D → 自有 HWND；窗口输入经有界队列回到 ConPTY。核心、窗口与子进程尺寸同步，当前按 [R1 合同](../../docs/milestones/R1-local-text.md)推进基础文字交互，仍未达到日常开发版验收。

## 运行

先按根 CONTRIBUTING 构建。Visual Studio Release 的入口为：

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --target termforge_shell --parallel 2
.\build\bin\Release\termforge_shell.exe
```

程序为 Windows GUI 子系统，不额外打开宿主控制台。默认打开真实可见窗口，在启动目录运行系统 `cmd.exe /d /q`，关闭 AutoRun，设置 UTF-8 code page 与固定 `TF>` 提示符。支持普通字符、中文字符消息、回车、退格、方向键和 Ctrl-C。输入 `exit` 或关闭标签会结束本会话并回收其自有子树；关闭主窗口回收全部标签。最后一个标签关闭后工作区保留，可以继续新建。用户启动的进程仍可访问用户自己的文件；这里是本地 shell，不是沙箱。

构建会自动复制独立开源的 Roboto Mono UI 字体及许可到 EXE 旁的 `assets/fonts/`；移动或分发 EXE 时保留此目录及同目录的 `shell_clipboard_reader.exe`。字体从可执行文件目录私有加载，不依赖当前工作目录，不安装到系统。缺失或实际字体不匹配时明确失败。来源、固定哈希及 OFL 许可见[字体说明](../../assets/fonts/README.md)。

默认 1360×860 逻辑内容区（以 96 DPI 为基准），底部真实 ShellPane，每会话保留最多 100 行历史；网格按扣除原生滚动条的客户区和实际单元格计算。窗口支持拖动缩放/最大化，网格范围为 1–500 列、1–200 行。采用[物理行 resize 契约](../../docs/adr/0005-physical-row-resize.md)，缩窄会截去右侧内容，增宽不会恢复，也没有逻辑行重排。历史、选择复制、系统粘贴、查找、固定拼音输入和统一文字动作均已有 R1 限定验收；八会话data-only验证与完整真实CF/IME组合验收分别记录，后者仍未完成，SSH、备用屏和完整 TUI 尚未实现。不实现终端查询回复，未知序列按 [Recovery 契约](../core/RECOVERY.md)消费。逐格布局来自 [V1](../presentation/README.md)，布局缓存至多 1,024 项，满时清空重建；尚未做产品级缓存策略或性能对比。

## 基础文字操作

- 鼠标拖选文字，双击选词、三击选物理行（不含行末换行）；`Ctrl+Shift+C` 或 Copy 菜单复制，`Ctrl+Shift+A` 全选，Escape 清除选区。选区快照最多 8 MiB，复制上限为 4 MiB UTF-8，系统 Unicode 剪贴板另受 1 Mi UTF-16 单元限制；超限明确拒绝。首版词规则将 ASCII 字母/数字/下划线/连字符及非 ASCII 基字归为词，空格成组，其余 ASCII 标点单格；这是本项目策略。
- `Ctrl+Shift+F` 打开当前会话查找窗口；字面量查询默认区分大小写，关闭 Match case 只折叠 ASCII 大小写。静止内容的 Enter/Shift+F3 向前、F3 向后并循环；持续扫描时显式操作可跳到当前已验证的局部命中，提示仍在搜索且不循环，静默末尾初选仍等本轮完成。结果截断明确提示只在有限结果内循环；这是洪流状态的本项目策略。Escape 关闭查找并恢复终端焦点。查询上限 4096 UTF-8 字节，结果最多 1024 项；复杂 Unicode 不做规范化，正则/跨会话属于后续范围。

- 滚轮和滚动条浏览历史，`Ctrl+Shift+Home` / `Ctrl+Shift+End` 到历史首/尾；回看时新输出保持阅读锚点，普通输入和开始粘贴回到底部。历史淘汰时钳制到仍保留的最早行。
- `Ctrl+Shift+V`、`Shift+Insert` 或 Session → Paste 读取剪贴板。读取在独立有时限的自有 helper 中进行；多行文字先确认，默认焦点为 Paste，Enter 确认发送，Escape 取消；焦点移到 Cancel 后 Enter 取消。一次任务固定目标会话，切换标签不重定向剩余字节；Escape 或 Cancel paste 停止后续发送。
- 当前独立粘贴策略：最多 1 Mi 个 UTF-16 单元、4 MiB UTF-8，换行规范化为 CR，拒绝不完整 UTF-16 和不支持的控制字符；不宣称已对齐 WindTerm 默认策略，也不无条件发送 bracketed-paste 包装。
- 键盘处理包含 Tab/Shift+Tab、Home/End/Insert/Delete/Page、修饰键、AltGr 和跨消息 UTF-16。IME 采用每终端独立原生上下文，只发送已提交文字；候选位置使用物理网格。固定 Microsoft 拼音的组合、候选、提交/取消已有五组实际产品验收；其他输入法和真实跨屏仍未验收，见 [IME 工具](../../research/ime/README.md)。

基础能力与统一文字动作已按 [#12–#17 的各自限定合同](../../docs/milestones/R1-local-text.md)验收；当前能力与边界统一见[功能矩阵](../../docs/FEATURES.md)。[#18](https://github.com/JavaZeroo/termforge/issues/18)的E2受控场景覆盖实际8会话/拒绝第9个、五路有限洪流、前台文字操作、合成粘贴取消/关闭和5次8→6→8，本地及同源码hosted各两fresh已通过限定复验。该场景不访问系统剪贴板、不调用真实IME；资源记录也不构成性能通过或R1整体验收。

## 工作区操作与已知差异

- `Ctrl+Shift+T`、主区 `+` 或 Session → New main session 新建主区本地 cmd；`Ctrl+T` 或底部 `+` 新建底部 shell。两个区域合计最多 8 个，各自保留选中的标签，可同时显示终端。
- 点击终端、标签或右侧 Sessions 列表改变当前操作区域；状态栏显示 `Focus: Main/Shell`。`Ctrl+Tab` 在该区域切换，`Ctrl+W` 关闭该区域选中的会话，标签 `x` 关闭它所在的会话。主工具栏提供新建、关闭、重命名和只读本地目标说明。
- `Ctrl++` / `Ctrl+-` 或 View 菜单放大/缩小当前会话文字，每次 25%，范围 50%–200%；也支持数字键盘加减。`Ctrl+0` 重置 100%，状态栏显示比例。其他会话保持原比例，重命名时不触发终端缩放。比例暂不跨进程保存。
- `F2`、标签中键或 Session → Rename shell 重命名；支持 Unicode，Enter 提交、Esc 取消，切换焦点时提交。空白、控制字符或不完整 UTF-16 不接受，最多 64 个 UTF-16 单元；标签与 Sessions 列表同步，标题不写入终端。
- `F11` / View → Focus shell 扩大底部终端；在此模式新建/选择主区会话会先恢复普通布局，再把焦点交给可见的主终端。View → Side panels 显示/隐藏两侧区域。Session 菜单可新建、关闭和退出。
- 拖动左右两列、底部 Shell、左侧与右侧上下区域的五条分隔线调整布局。线宽 1，命中宽 5；拖出窗口仍限于比例边界，Esc 或丢失鼠标捕获取消本次拖动。
- 三列约 1:4:1、上下约 2:1、24 高面板标题、26 高 Shell 标签、27 高底栏、29 高状态栏来自 [直接取证](../../docs/research/windterm-ui-2026-10-23.md)。主区容器空时高 19、有标签时高 26，连接状态工具栏高 34，主终端外区域从 y=93 开始，依据[第二轮测量](../../docs/research/windterm-ui-typography-2026-10-23.md)。主区最后一个标签关闭后恢复空状态，可重新新建。
- Explorer/Filer/Outline 暂为空布局区域，没有文件浏览/大纲功能。当前菜单只暴露已实现动作；底栏是原创新建快捷入口，没有复刻 WindTerm QuickBar 配置。
- UI 使用独立随附的 Roboto Mono，9pt；终端继续使用 Consolas，96 DPI / 100% 下为 12×26 网格与原有调色板。字号、整数物理单元、裁剪和光标共用度量。启动读取窗口 DPI，`WM_DPICHANGED` 更新字体、布局、鼠标命中、编辑器和隐藏终端，取消未提交拖动并保留编辑内容/焦点；先应用最终像素矩形再更新终端网格，避免中间缩窄裁剪。主终端外框已按固定逻辑尺寸对齐，内部网格、标签标题宽度及重命名/关闭策略仍是 TermForge 自身实现。
- 96→144→192→96 受控消息场景已验证，真实跨显示器 DPI、字体/像素等价、标签排序/会话持久化、SSH/SFTP 均未验收。受控消息不修改系统显示设置，不能当作真实跨屏结果。
- 主窗口是既有 Win32/DirectWrite 实验的有限扩展，不锁定未来产品 GUI 框架；没有复制 WindTerm 主题、图标或反编译实现。

## 布局保存

拖动提交、显隐/焦点模式切换及 View → Reset layout 后，自动保存到 `%LOCALAPPDATA%\TermForge\workspace-v1.layout`。下一次启动恢复五个分割比例及侧栏/焦点模式；焦点模式保留原分割比例。仅保存布局，不保存窗口位置/尺寸、标签标题、命令或会话。

左右列宽各限于工作区的 10%–35%，上部限于 25%–80%，左右列内部上半部限于 20%–80%，另保留标题所需最小空间。这些是独立实现的可用性边界。配置为版本 1 的严格文本格式，读取最多 1 KiB；文件缺失、损坏、截断、版本未知或数值越界时整体使用默认布局。保存使用同目录独占临时文件与原子替换，失败保留旧文件，状态栏显示 `Layout could not be saved`。不承诺断电持久性；多进程同时操作采用最后成功保存的布局。

自动测试仅读写各自结果目录下的 `workspace.layout`，不会读取或修改上述用户配置。

## 所有权与调度

- ConPTY 输出线程将最多 64 KiB 的读取块放入 256 KiB 队列；满时等待窗口消费，正常运行不丢弃字节。
- UI 每个 16 ms 定时消息以 8 KiB 小块消费，最多 64 KiB；块间检查 4 ms 预算，随后发布一次快照。单次 feed、快照和绘制仍可能超过预算，它不是硬实时保证。解析和快照发布同属 UI 线程；dirty 范围包含内容与旧/新光标行，多次失效可合并为一次 WM_PAINT。
- 输入写线程独立服务至多 16 KiB 的队列，每条消息不超过 1 KiB。UI 不等待 WriteFile；容量满时拒绝该次输入并发出提示，不宣称输入已成功。关闭期间停止接收新输入。
- resize 由独立 worker 执行，最多一个在途和一个合并请求；UI 先更新核心与快照并继续消费字节。关闭在回收 ConPTY 之前 join 此 worker。字节流没有 resize 世代边界，不保证所有中间帧及历史与 ConPTY 自身重排完全一致。
- 关闭工作在后台执行，UI 继续消费尾部输出；停止输入写入并反复取消该线程的同步 I/O，终止本会话 Job，关闭 ConPTY，等待读取线程退出。队列排空后执行 `finish()`、提交最终画面并销毁窗口。只终止自有 Job 内的进程，包括 shell 留下的后代。
- 正常 `exit 37` 保留实际退出码 37；关闭正在输出的会话通过 Job 取消。外部自动监督器另建拥有范围明确的 Job：宿主收到 `GO` 前不创建 shell，超时后回收整个自有树。

当前同步 I/O 取消与 ConPTY 关闭不提供跨所有 Windows 版本的硬截止时间保证；UI 仍能处理消息，自动测试有外部时限。Server 2022 已知资源门槛问题继续保留，未扩大已验收平台范围。

专用测试可启用UI线程按值runtime快照；worker状态在各自短锁内读取，关闭前及实际join后分别留证，未知容量和未完成查询保持null。资源驱动复用既有sampler，明确按launcher-tree观察并将sampler单列；实际采样开销及调度缺口须随结果保留，短命后代可能漏采。native阶段时钟未校准、phase_windows=null，操作到state/实际WM_PAINT提交端点仍未接入。普通启动默认关闭这些观测，不自动采样或写文件；字段和限制见[观测合同](WORKFLOW.md#运行快照)。

## 无人值守验证

```powershell
$env:TERMFORGE_BIN_DIR = (Resolve-Path build/bin/Release).Path
python -m unittest experiments.shell.test_shell -v
python experiments/shell/run.py --bin-dir build/bin/Release --out local/experiments/shell-new --repeats 3

# E2专用驱动：两个fresh rounds，启用资源采样；目录取新路径。
$env:TERMFORGE_TEST_ARTIFACTS = Join-Path (Get-Location) 'local/workflow-check'
python -m unittest experiments.shell.test_workflow_sessions -v
```

测试启动真实 cmd，不使用假 shell；窗口放在独立非输入 desktop，从不切换用户桌面。输入通过队列化 `PostMessage(WM_CHAR/WM_KEYDOWN)` 进入同一个窗口处理路径，不冒充物理键盘操作。断言匹配完整独立输出行，不能用输入命令中的标记冒充执行结果。

测试需先完成根目录全量构建，包括 `shell_test_launcher`、`shell_size_client`、`shell_input_client`、`shell_text_client`、`shell_queue_probe` 和 `shell_clipboard_reader`。原生 launcher 在收到 `GO` 后才在指定 desktop 创建子进程，并由自有 Job 限定整个进程树。启动、Job 分配失败和超时也保留证据并检查回收。

E2驱动是唯一默认传入`resources=True`的场景驱动，依赖项目Python环境中的psutil；其他普通执行不因导入runner而加载采样依赖。E2 native/launcher/外层时限为70/80/90秒，CTest `workflow_sessions`为300秒。固定源码、实际完成轮数与回归结果见[验证](results/2026-10-24-r1-eight-sessions/validation.json)和[复验](results/2026-10-24-r1-eight-sessions/regression.json)，不能用旧二进制或跳过项补成通过。

resize 两类场景各重复三次，通过真实 `SetWindowPos` 缩放并由子进程查询实际尺寸；包括快速连续请求、洪流中缩放、随后 Ctrl-C 或关闭。正常 resize 验证 60×12、100×28、72×18；关闭场景只查询前两个尺寸。脚本还将自有窗口移至非输入 desktop 右下边缘，验证 retained DIB 不依赖可见更新矩形。每次绘制与全量重绘作 RGB 对比；这些带检查的样本不能作为低开销延迟数据。

每次运行依次验证普通命令、中文、退格、左右方向键编辑，再运行一百万轮的输出命令。至少持续一秒且消费 64 KiB 后发送 Ctrl-C，再执行 `echo TF_AFTER`；确认对应状态与已提交帧后 `exit 37`。另测输出中关闭，正常/关闭各重复三次，检查 Job 活跃进程归零与自有 desktop 消失。监督器用真实存活 shell 验证超时清理。独立队列探针验证容量、背压、顺序、停止唤醒与输入拒绝。短回归不代表长时无泄漏。

产品真实 IME 单独由 [`research.ime.product_runner`](../../research/ime/README.md) 显式托管运行，使用严格受控的真实 `SendInput`，不在这些私有 desktop 场景中合成 IME 消息。五个 `ui-ime-*` 场景有独立输入桌面/焦点/代次检查和内外 Job/GO 门控，开发机拒绝运行；历史/Find 与 rename 双 Escape 已有[固定源码系统审计](../../research/ime/results/2026-10-24-product-ime-contexts/validation.json)。

测试的 `scripted_input_to_state_ms` 从 Ctrl-C 的 WM_CHAR 被处理开始，到后续 `TF_AFTER` 完整输出行进入快照；`scripted_input_to_submit_ms` 到包含该行的 WM_PAINT BitBlt/GdiFlush 完成。两者包含中断、后续命令和调度，不能当作单键延迟。自有 DIB 截图不是合成器呈现，`presented_done=null`。关闭场景没有该标记，相关字段为 null，不参与延迟统计。

默认监督时限为 40 秒，原生基础场景为 25 秒，主区、DPI、选择和查找场景为 30 秒；`ui-clipboard-system` 的监督/原生场景分别为 60/45 秒，watchdog 场景在 3 秒后强制回收。stdout/stderr、失败状态、最终自有 BMP、身份与环境均保留。CTest 产物在 `TERMFORGE_TEST_ARTIFACTS/shell`，默认 `build/test-artifacts/shell`；runner 输出目录必须不存在。只有审核过的合成记录可公开，日常用户会话不会自动录制或上传。

E2产物在`TERMFORGE_TEST_ARTIFACTS/workflow-sessions/run-*`；公开白名单包含合成source/raw/progress、`sessions.json`、脱敏`sample.public.json`、`resource-observations.jsonl`和`resource-summary.json`。原始`sample.json`、controller `stdout.private.json`、`.identity`、`transport.bin`、私有stderr和整个`_resources-private/`均不上传，失败兜底也排除。资源公开记录在CI本地与raw逐条比对，下载后可离线重算脱敏观测；无法从公开artifact重哈希未上传的原始传输和二进制。

## 依据

独立实现，接口语义核对 [Microsoft ConPTY 会话文档](https://learn.microsoft.com/en-us/windows/console/creating-a-pseudoconsole-session)、[CancelSynchronousIo](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelsynchronousio) 和 [VT 输入序列](https://learn.microsoft.com/en-us/windows/console/console-virtual-terminal-sequences#input-sequences)。Windows 窗口退格消息的 U+0008 映射为 VT 的 DEL；首轮将其原样发送导致删词，已由真实 shell 编辑用例检出并修正。

窗口场景按用户行为维护，入口在 `test_shell.py` / `test_selection_bytes.py` / `test_selection_context.py`，不以历史批次命名表示完成程度：

| 场景 | 检查内容 | 独立结果与边界 |
|---|---|---|
| `ui-workflow-sessions` | 8真实会话/拒绝第9个、五路有限洪流、阅读/Find/输入/缩放/淘汰、合成粘贴取消/关闭、5次8→6→8 | `test_workflow_sessions`独立literal/raw/journal/UI和回收校验；每轮26检查点、18身份、235/256观测，资源scope=launcher-tree。data-only，不代替同次实际CF/IME、native阶段校准或操作提交端点 |
| `ui` / `ui-edit` / `ui-restore` | 多标签、五处分隔线、重命名、捕获/Escape 回滚、新进程恢复布局 | 真实子进程尺寸、保存文件、焦点与回收；不恢复会话进程或标题 |
| `ui-main` | 主区/底部隔离、隐藏输出、焦点模式、空区域与关闭目标 | `geometry.json` 按固定参考常量检查 HWND/字体/矩形；内部网格由实际客户区扣除系统滚动条后计算 |
| `ui-dpi` | 每会话缩放、隐藏标签、受控 DPI、边缘命中/拖动及长行保留 | `dpi.json` 区分受控 DPI 与系统 DPI；实际子进程尺寸及 dirty/full 图像比较；不替代真实跨屏 |
| `ui-history` | 编号日志、滚轮/翻页/拖动、阅读锚点、隐藏输出、淘汰、缩放与输入回到底部 | `history.json` 与独立编号 oracle；`native-scroll.jsonl` 记录系统原生非客户区 tracker 的轨迹，使用队列鼠标消息，不移动用户鼠标 |
| `ui-selection` | 正逆拖选、词/行、宽字/组合字符、缩放、双会话、后台追加及淘汰失效 | `selection.json` 与独立逐字预期；检查实际窗口高亮及回收，本场景不操作系统剪贴板 |
| `ui-copy-system` | 选择后实际 Copy 菜单/快捷键系统复制、占用失败反馈/保持CF/释放后重试 | 旧 v3/v4 只算各自覆盖；v5 必须真实 owner 握手、原始错误、sentinel/sequence、strict release/join 和成功重试。[两轮最终系统审计](results/2026-10-24-r1-selection-accepted/validation.json)通过；只在指定一次性 hosted 环境运行，本地跳过不算系统验收 |
| `ui-selection-bytes` / `ui-copy-bytes-system` | 真实 ConPTY 软折行、硬 CRLF、中文/组合字符、显式带样式尾两空格和受控 96→144→192→96 | 独立 producer/transport/capture 文件与逐行 oracle；本地模式不访问剪贴板，system 模式仅 disposable hosted VM。普通尾空格在 ConPTY 中的规范化仍是缺口，不能据此宣称与 WindTerm 该行为等价 |
| `ui-selection-context` / `ui-copy-context-system` | 3会话的同组标签隐藏/返回、live CR覆盖失效、rename EDIT、带选区关闭后的目标隔离 | 16阶段独立literal/journal/transport/raw；local不执行有效CF，system仅disposable hosted并要求真实EDIT Copy与选区CF。实际system两轮已通过[独立审计](results/2026-10-24-r1-selection-accepted/validation.json)；未知显式空白端点策略不据此裁决 |
| `ui-actions` / `ui-actions-system` | 真实终端右键菜单、快捷键、Find/rename 原生编辑、busy/closing/无会话反馈，以及命中和选区跨受控 DPI/zoom | local每轮26阶段且不执行有效CF；system每轮36阶段，覆盖Find原生Ctrl+A/C/V、中文查询重选/Escape、终端Copy/Paste、Shift+Insert、默认Return及目标切换。[固定源码两轮系统审计](results/2026-10-24-r1-actions-accepted/validation.json)已通过；仅一次性hosted运行。范围与策略见[动作对照](../../docs/research/r1-action-comparison.md)，完整CF/IME组合及资源阶段/操作端点仍待#18 |
| `ui-find-stream` | 实际持续 CR 输出中导航、运行中替换查询、取消及关闭目标 | 10 阶段独立 oracle、两个原始接收文件和有限 worker/Job 回收；局部结果绝不冒充全局循环 |
| `ui-find` | 多批搜索、方向/循环、大小写/中文/无结果、蓝黄高亮、后台追加、淘汰与 Escape | `find.json`、逐阶段记录及真实 `ReadConsoleW` 接收字节；查询在原生 EDIT 中操作，独立 oracle 检查稳定位置与焦点 |
| `ui-keyboard` | 修饰键、编辑键、Unicode 与会话目标 | 真实 `ReadConsoleW` 接收端与独立字节预期；不替代真实 IME |
| `ui-paste` | 默认 Paste/Enter 发送一次、默认 Escape 取消、Tab/Enter、隐藏目标、输出洪流、发送途中取消和关闭 | v2 `paste-workflow.json` 与 `input.bin`/`cancel.bin`/`close.bin`；接收端事件握手确保动作发生在发送途中；本场景不读取用户剪贴板；旧 v1 默认 Cancel 证据保留且不计作新默认验收 |
| `ui-paste-system` | 真实 Unicode 系统剪贴板 → 快捷键 → 确认 → 接收端 | 只在指定一次性 hosted 环境运行；本地未运行/跳过不计系统验收 |
| `ui-clipboard-system` | 经产品 Paste 验证占用/超限错误与重试、读取途中取消/关闭 | 延迟渲染 owner 握手、真实接收端字节、错误画面及 helper/Job 回收；仅一次性 hosted 环境运行；场景存在不等于整项通过，当前结果见 [#14](https://github.com/JavaZeroo/termforge/issues/14) |

系统剪贴板边界另由 `python -m unittest experiments.shell.test_paste -v` 验证。畸形终止符用例保留 `terminator-before/after/reader.utf16le`：先核对发布前确实没有 NUL，再独立捕获系统返回的完整分配。仅接受“原分配未变且读取器拒绝”或“最后一个单元被系统改为 NUL 且读取器精确返回其前缀”两种有字节证据的结果；其他转换仍失败并留存原始内容。跨平台 `paste_test` 直接测试生产读取器共用的有界解析逻辑，包括无终止符、超限、分配末端与字符上限，不能用系统可能规范化了夹具代替解析边界测试。

原生 `layout_state` 测试覆盖严格读取、截断、非法数值、Unicode 路径和替换失败保留。`renderer_metrics` 覆盖 GDI/DirectWrite × 96/120/144/192 DPI × 50/100/125/200% 共 32 组，检查非整数边界、实际绘字与 dirty/full 一致。

合成 `frame.bmp` 和 DPI 图片来自自有工作区/终端绘制，不含原生 EDIT 子控件；编辑器由实际 HWND/字体断言验证。这些图片不是用户桌面截图、像素等价或实际呈现完成证据。历史批次的测量与修复依据统一从[证据索引](../../docs/EVIDENCE.md)进入。
