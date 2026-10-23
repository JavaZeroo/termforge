# WindTerm UI 第二轮：字体、缩放与主连接标签

## 结论

本轮在官方 2.7.0 的自有测试副本中，自动选择并打开唯一的合成本地 cmd 会话，测量主区域从空状态到有标签的变化，并观察文字绘制调用使用的字体。默认缩放完成两次独立运行；另完成一次 `QT_SCALE_FACTOR=1.5` 的受控运行。所有实例均在非输入 desktop 的自有 Job 内结束，active 归零，profile 最终只含合成 Shell 项。

这为下一批独立 UI 实现提供尺寸与字体依据，不是 WindTerm 完整行为、像素或性能等价验收。1.5 倍 Qt 环境缩放也不等于真实 Windows 150% 显示设置或跨屏切换。

## 先修正取证环境

旧的“全新 profile”条件不够严格：检查此前自有 `repro-01/02` 的结果时，发现 WindTerm 自动导入了宿主 OpenSSH 会话元数据。没有启动这些远程会话，原始数据仍仅在 ignored local/。旧样本不能被描述为只包含合成会话；这里修正第一轮报告的隔离表述。

定向只读 Ghidra 调查确认了具体条件，地址基于官方 EXE 首选基址 `0x140000000`：

| 地址 | 静态事实 |
|---|---|
| `0x140382b97` | CGlobalConfig 构造将 singleton `0x140c29370` 的 `+0x24` 字节清零 |
| `0x140020597` / `0x14002059f` / `0x1400205a1` | 检查所选 `.wind/profiles/` 是否存在；不存在才将该标志置 1 |
| `0x14044ce35` / `0x14044ce39` | 初始化检查此标志，零则跳过首启自动导入 |
| `0x14044ced3` / `0x14044e936` | 非零时分派 openssh/putty/shell 导入，OpenSSH 分支调用 `0x14044f530` |

启动器现在预建自有 `.wind/profiles/`，写入一个原创 `TF_UI_CMD` Shell 配置，同时把 HOME/USERPROFILE/APPDATA/LOCALAPPDATA 指向自有目录。运行时标志为 0，结束后配置检查只剩预置的单项；这验证了所调查导入路径被避开，不是所有文件访问的隔离审计，也不是安全沙箱。

另发现旧复制子集漏了根目录 `winpty-agent.exe`。无该文件时，主标签虽然出现，但存在 MessageBar，Job 中没有 cmd，不能当作成功会话。补齐后可观察到本次实例下的 `winpty-agent.exe → cmd.exe` 及 conhost，错误条消失。静态错误字符串 `0x140a2fc88` 及引用 `0x1402e7635` 与此一致。没有据此推定其他环境下必然选择 WinPTY。

新增 helper 从原固定 ZIP 独立核验，SHA-256 为 `9add1a61155ec47cf6f347faf776b746eebbde1dc9360d81b8a909da34650642`。复制清单从 4,046 改为 4,047 项，新清单 SHA-256 为 `e459adf2f12adb6a1b7f55e66a9da1c22794185e66e848057d21e0c60d669203`。EXE 身份保持 `7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0`。

## 主区域标签的实际布局

SessionPane/TreeView 的 End 选中唯一合成叶节点，再经 Qt 按键事件发送 Return。资源声明 `wind.keymaps:258–261` 与 `wind.actions:701–704` 将此上下文映射至 SessionPane.Open；运行后确认主区域出现一个 TerminalView 和一个主标签。它不是底部 ShellPane 标签。

下表为默认缩放时的 Qt 逻辑坐标，原点是 TerminalWindow，不含桌面位置和原生外框。当前合成配置的底部 ShellPane 为空；不能把本状态直接说成首次安装的完整默认布局。

| 组件 | 1360×860：x,y,w,h | 2048×1120：x,y,w,h |
|---|---|---|
| 主 ViewGroup | 227,33,906,26 | 342,33,1364,26 |
| 主 TabBar 可用区域 | 227,33,844,26 | 342,33,1302,26 |
| 主工具栏 | 227,59,906,34 | 342,59,1364,34 |
| AddressBar | 330,63,773,26 | 445,63,1231,26 |
| TerminalView | 227,93,906,453 | 342,93,1364,626 |
| ScintillaEditBase | 227,93,906,429 | 342,93,1364,602 |
| 文本 viewport | 228,94,888,427 | 343,94,1346,600 |
| SciScrollbar | 1116,94,16,427 | 1689,94,16,600 |

主 ViewGroup 空状态高 19、内部 TabBar 高 0；有一个主标签后两者均高 26。合成标题对应的单个标签 rect 为 `[0,0,116,25]`，即 117×26；不是所有标题都固定 117 宽。1.5 倍缩放样本中，同一标题变为 120×26，TabBar 可用宽少 1 个逻辑单位。在 2048×1120 阶段，ScintillaEditBase、viewport 和滚动条高度也各少 1，分别为 601、599、599；其他表列矩形一致。单次缩放样本尚不足以把这些差异全部归因于缩放；至少不能假设所有逻辑尺寸恒定，或简单乘以缩放系数预测文字测量和舍入。

## 控件字体与绘制字体必须分开

| 观察边界 | 本次结果 | 解释 |
|---|---|---|
| QWidget::font 与 QFontInfo | UI/TerminalView 的控件字体是 Roboto Mono，9pt，解析 pixelSize=12 | 控件继承属性；不能当作自绘终端字体 |
| 终端 viewport 的 QPainter 文本绘制调用 | QFont.family 为 Consolas，pointSizeF=10；QFontInfo.resolved_family 为 RobotoMono Nerd Font，pointSizeF=9.75、pixelSize=13 | 记录的是该次文本绘制使用的 painter 字体与 Qt 解析结果；不能只看输入 family 判断最终字体 |
| 默认 QWidget/QScreen | logical DPI=96，DPR=1；屏幕 physical DPI 约 108.918 | API 报告值，不是独立物理屏幕测量 |
| `QT_SCALE_FACTOR=1.5` | logical DPI 仍为 96，DPR=1.5；屏幕 physical DPI 返回约 72.626 | 受控 Qt 变换下的返回值，不应解释为显示器物理密度发生变化 |

两种缩放下，上述 QFontInfo 返回的 pixelSize 相同。这里不将该数值直接等同于屏幕上的物理字形像素，也未测量中文、emoji 的逐字形 fallback、字素整形或实际终端网格 advance。`exactMatch=false` 是匹配状态，不代表显示错误。

探针使用 DLL 导出和本地指令核实的 MSVC x64 ABI：QWidget::font 返回借用引用；QString/QFontInfo 的成员返回对象使用 `this, result`，临时对象正确析构；QWidget 到 QPaintDevice 的第二基类偏移为 `+0x10`。QPainter::drawText 等入口只读取设备和字体，不读取字符串参数。未匹配到 QWidget 的 backing-store 绘制不纳入此观察，因此不能声称覆盖全部绘制路径。

## 复现、数据与剩余问题

入口和自动步骤见 [research/ui](../../research/ui/README.md)。发布的 JSON 只保留白名单角色的矩形、DPI、字体与证据身份；不含进程指针、PID、宿主路径、会话文本或原始反编译。几何表中的 SciScrollbar 来自本地原始树，未加入精简摘要的角色白名单。

成功样本：

- `typography-06`：[默认缩放摘要 1](ui-typography/normal-01.json)，`ev_b2f19933ee03075a736c9882d7798d12ab4312cdc247d5a1bb2d77b2a6da5379`。
- `typography-07`：[默认缩放摘要 2](ui-typography/normal-02.json)，`ev_d8c3c7453675c7764e61bfff4ed31fc02cd1d2d73550e6c4b67b1800f4b022cc`。
- `typography-08-scale150`：[Qt 1.5 倍摘要](ui-typography/scale-150.json)，`ev_028565f0a019225dc48ca7a2e23da68e9301910d15624c605832b47accec32d0`。

这些 ID 是本地 REA 证据标识，不是公共托管链接。原始证据与静态指令保存在 `local/ui-reference/`。06/07 的四阶段精简数据完全一致；三份最终样本均有自有 cmd 进程链、Job active=0，profile 合成项检查通过。

历史探索也保留：01 因探针布尔参数 ABI 错误中止；02 因缺 winpty-agent 仅打开错误状态的主标签；03/04/05 在审查中发现采样后立即 resize，可能把后续绘制字体归到前一阶段。因此它们均不作为最终字体证据。修正版将采样、重绘等待与 resize 分阶段，过渡期间停止归属，每次字体观测记录实际窗口尺寸；摘要器拒绝缺失、类型错误或与对应快照不一致的尺寸。06/07/08 全部重新运行并通过该门槛。插桩、重绘和字体解析均会扰动程序，不用于性能排名。

下一批实现可据此调整主区域标签/工具栏层级，并选择许可证明确、可独立获取的字体来源。尚未解决：真实显示设置与跨屏 DPI、逐字形 fallback、文本网格与光标度量、多个主连接标签的拖放/关闭历史、完整菜单状态和长期配置恢复。原程序字体文件、图标、主题与私有实现均未复制进开源项目。
