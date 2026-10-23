# 本地 shell 工作区：A1 / C5 / UI-A / UI-B / 主区会话 / DPI

这是可从源码运行的 Windows 本地交互原型：真实 `cmd.exe` → ConPTY → 有界输出队列 → Recovery 核心 → 只读快照 → DirectWrite/Direct2D → 自有 HWND；字符/方向键从窗口回到 ConPTY。C5 新增核心、窗口与 ConPTY 尺寸同步，仍不是日常开发版。

## 运行

先按根 CONTRIBUTING 构建。Visual Studio Release 的入口为：

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --target termforge_shell --parallel 2
.\build\bin\Release\termforge_shell.exe
```

程序为 Windows GUI 子系统，不额外打开宿主控制台。默认打开真实可见窗口，在启动目录运行系统 `cmd.exe /d /q`，关闭 AutoRun，设置 UTF-8 code page 与固定 `TF>` 提示符。支持普通字符、中文字符消息、回车、退格、方向键和 Ctrl-C。输入 `exit` 或关闭标签会结束本会话并回收其自有子树；关闭主窗口回收全部标签。最后一个标签关闭后工作区保留，可以继续新建。用户启动的进程仍可访问用户自己的文件；这里是本地 shell，不是沙箱。

构建会自动复制独立开源的 Roboto Mono UI 字体及许可到 EXE 旁的 `assets/fonts/`；移动或分发 EXE 时保留此目录。字体从可执行文件目录私有加载，不依赖当前工作目录，不安装到系统。缺失或实际字体不匹配时明确失败。来源、固定哈希及 OFL 许可见[字体说明](../../assets/fonts/README.md)。

默认 1360×860 逻辑内容区（以 96 DPI 为基准），底部真实 ShellPane，100% 文字缩放时初始约 113×7；每会话 100 行内部历史。窗口支持拖动缩放/最大化，网格范围为 1–500 列、1–200 行。采用[物理行 resize 契约](../../docs/adr/0005-physical-row-resize.md)，缩窄会截去右侧内容，增宽不会恢复，也没有逻辑行重排。没有历史浏览、选择/复制/粘贴、IME 支持声明、SSH、备用屏或完整 TUI 兼容性。不实现终端查询回复，未知序列按 [Recovery 契约](../core/RECOVERY.md)消费。逐格布局来自 [V1](../presentation/README.md)，布局缓存至多 1,024 项，满时清空重建；尚未做产品级缓存策略或性能对比。

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
- UI 每个 16 ms 定时消息以 8 KiB 小块消费，最多 64 KiB；块间检查 4 ms 预算，随后发布一次快照。单次 feed、快照和绘制仍可能超过预算，它不是硬实时保证。解析和快照发布同属 UI 线程；dirty 范围包含内容与旧/新光标行，多次失效可合并为一次 WM_PAINT。最初每 tick 仅取 8 KiB 导致合成洪流积压，修正依据见批次记录。
- 输入写线程独立服务至多 16 KiB 的队列，每条消息不超过 1 KiB。UI 不等待 WriteFile；容量满时拒绝该次输入并发出提示，不宣称输入已成功。关闭期间停止接收新输入。
- resize 由独立 worker 执行，最多一个在途和一个合并请求；UI 先更新核心与快照并继续消费字节。关闭在回收 ConPTY 之前 join 此 worker。字节流没有 resize 世代边界，不保证所有中间帧及历史与 ConPTY 自身重排完全一致。
- 关闭工作在后台执行，UI 继续消费尾部输出；停止输入写入并反复取消该线程的同步 I/O，终止本会话 Job，关闭 ConPTY，等待读取线程退出。队列排空后执行 `finish()`、提交最终画面并销毁窗口。只终止自有 Job 内的进程，包括 shell 留下的后代。
- 正常 `exit 37` 保留实际退出码 37；关闭正在输出的会话通过 Job 取消。外部自动监督器另建拥有范围明确的 Job：宿主收到 `GO` 前不创建 shell，超时后回收整个自有树。

当前同步 I/O 取消与 ConPTY 关闭不提供跨所有 Windows 版本的硬截止时间保证；UI 仍能处理消息，自动测试有外部时限。Server 2022 已知资源门槛问题继续保留，未扩大已验收平台范围。

## 无人值守验证

```powershell
$env:TERMFORGE_BIN_DIR = (Resolve-Path build/bin/Release).Path
python -m unittest experiments.shell.test_shell -v
python experiments/shell/run.py --bin-dir build/bin/Release --out local/experiments/shell-new --repeats 3
```

测试启动真实 cmd，不使用假 shell；窗口放在独立非输入 desktop，从不切换用户桌面。输入通过队列化 `PostMessage(WM_CHAR/WM_KEYDOWN)` 进入同一个窗口处理路径，不冒充物理键盘操作。断言匹配完整独立输出行，不能用输入命令中的标记冒充执行结果。

测试需先完成根目录全量构建（包括 `shell_size_client`、`shell_queue_probe`）。resize 两类场景各重复三次，通过真实 `SetWindowPos` 缩放并由子进程查询实际尺寸；包括快速连续请求、洪流中缩放、随后 Ctrl-C 或关闭。正常 resize 验证 60×12、100×28、72×18；关闭场景只查询前两个尺寸，不声称已验证关闭中的最终子进程尺寸。每次绘制与全量重绘进行 RGB 对比，这些带检查的样本不能作为低开销延迟数据。

resize 脚本会将自有窗口移至私有 desktop 右下边缘，让大部分行列不可见，验证 retained DIB 不依赖可见更新矩形也保持完整。正常用户窗口不做该测试移动。初版 CI 曾检出尺寸钳制和残留画面，失败与修复记录见第四批报告。

每次运行依次验证普通命令、中文、退格、左右方向键编辑，再运行一百万轮的输出命令。至少持续一秒且消费 64 KiB 后发送 Ctrl-C，再执行 `echo TF_AFTER`；确认对应状态与已提交帧后 `exit 37`。另测输出中关闭，正常/关闭各重复三次，检查 Job 活跃进程归零与自有 desktop 消失。监督器用真实存活 shell 验证超时清理。独立队列探针验证容量、背压、顺序、停止唤醒与输入拒绝。短回归不代表长时无泄漏。

测试的 `scripted_input_to_state_ms` 从 Ctrl-C 的 WM_CHAR 被处理开始，到后续 `TF_AFTER` 完整输出行进入快照；`scripted_input_to_submit_ms` 到包含该行的 WM_PAINT BitBlt/GdiFlush 完成。两者包含中断、后续命令和调度，不能当作单键延迟。自有 DIB 截图不是合成器呈现，`presented_done=null`。关闭场景没有该标记，相关字段为 null，不参与延迟统计。

每个子进程限时 40 秒，原生基础场景限时 25 秒（DPI 为 30 秒），watchdog 场景 3 秒强制回收；stdout/stderr、失败状态、最终自有 BMP、身份与环境均保留。CTest 产物在 `TERMFORGE_TEST_ARTIFACTS/shell`，默认 `build/test-artifacts/shell`；runner 输出目录必须不存在。只有审核过的合成记录可公开，日常用户会话不会自动录制或上传。

## 依据

独立实现，接口语义核对 [Microsoft ConPTY 会话文档](https://learn.microsoft.com/en-us/windows/console/creating-a-pseudoconsole-session)、[CancelSynchronousIo](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelsynchronousio) 和 [VT 输入序列](https://learn.microsoft.com/en-us/windows/console/console-virtual-terminal-sequences#input-sequences)。Windows 窗口退格消息的 U+0008 映射为 VT 的 DEL；首轮将其原样发送导致删词，已由真实 shell 编辑用例检出并修正。

UI 自动场景另测两个真实 cmd 的独立输出、隐藏标签继续消费新输出、Sessions 列表点击、通过窗口命令新建/关闭和鼠标消息切换、F11 布局往返、2048×1120 放大及侧栏显隐。子进程独立查询 113×27、113×7、170×11；关闭最后标签后重新创建第三个会话并执行命令，再关闭主窗口，全部回收归零。合成 frame.bmp 由同一工作区绘制函数与终端 DIB 合成，核验参考坐标的分隔线/标题/活动标签颜色；它不是用户桌面截图，也不是像素等价或实际呈现完成证据。

UI-B 新增五条分隔线的两端越界、capture 丢失/Esc 回滚、Unicode 输入重命名及空白/取消、后台标签退出期间编辑焦点与身份保持、不可见 Sessions 行不可点击。拖动后由子进程独立查询 113×12；另启新进程加载保存文件，检查比例/显隐与实际网格恢复，确认不恢复标题。三个 UI 场景各重复两次；独立跨平台 `layout_state` 测试覆盖严格读取、所有截断前缀、非法数值、Unicode 路径和替换失败保留。完整 runner 同样运行 UI 与新进程恢复场景。

`ui-main` 场景每次创建并回收五个真实会话，重复两次：主区两个标签与底部输出隔离，隐藏标签继续消费；点击两区切换焦点，重命名失焦不抢焦点，后台退出不打断另一区域编辑；验证焦点模式下的主区隐藏/恢复、空底部时焦点落到父窗口、点击底部关闭按钮不误关主区，以及两个区域均有活会话时整体关闭。快捷键场景调用与真实消息循环共用的派发函数，鼠标/编辑器消息仍走窗口过程；不是物理键鼠输入证明。

`geometry.json` 保留空状态及两种连接窗口尺寸的矩形、实际 HWND 位置与字体 face。Python 按独立测量常量比较，并对改错矩形/遗漏阶段/伪造字体等负例拒绝。子进程另查询主区 75×17 / 113×24、底部 113×7 / 170×11；这些网格只验证当前终端 resize，不是 WindTerm 文本网格等价。

`ui-dpi` 每次创建三个真实会话，重复两次，验证独立缩放、上下限、重置、隐藏标签传播、长行在等比 DPI 切换时保留、编辑器字体/焦点、150% 边缘命中和 200% 拖动；每个阶段由子进程重新查询尺寸，并比较保留位图与全量重绘。`dpi.json` 分开记录受控 DPI 与系统窗口 DPI，以及逻辑矩形、实际 HWND 矩形、单元物理尺寸和行列数；Python 根据固定参考几何独立验算并拒绝篡改数据。`frame-144.bmp` / `frame-192.bmp` 是工作区与终端的合成检查图，不含原生 EDIT 子控件；编辑器由实际 HWND/字体断言验证。

同一场景另外检查不足一个字符格的 resize 仍产生子窗口更新区域，以及 168 DPI / 2001×1406 的实际右/下边缘覆盖、贴边子窗口和鼠标端点。布局始终以实际客户区端点收尾，避免逻辑尺寸来回取整漏掉一像素；此项不冒充真实跨屏或四阶段外的子进程尺寸验收。

独立 `renderer_metrics` 原生测试覆盖 GDI/DirectWrite × 96/120/144/192 DPI × 50/100/125/200% 的 32 组组合，核验非整数缩放边界、实际绘字、光标、背景覆盖及 dirty/full 一致。规格见 [UI-dpi-zoom](../../docs/milestones/UI-dpi-zoom.md)。
