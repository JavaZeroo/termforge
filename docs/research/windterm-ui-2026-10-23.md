# WindTerm 2.7.0 UI：资源、二进制与运行时交叉验证

## 结论与范围

UI 对齐改为直接调查发行物：声明式资源给出样式/动作，二进制给出构造关系，隔离运行时给出实际控件树和尺寸。截图只用于最终视觉复查。已完成第一批主窗口结构取证，TermForge 已据此独立实现工作区与本地多标签，并继续补齐 UI-B 交互；尚未恢复或验证 WindTerm 的完整 UI 行为。

后续修正：本轮“全新 profile”仍被原程序自动导入过宿主 OpenSSH 元数据，不能称为纯合成配置。第二轮已定位首启导入条件并修正启动器；主连接标签、字体与 Qt 1.5 倍缩放的新证据及状态差异见[第二轮报告](windterm-ui-typography-2026-10-23.md)。下文几何保留为首轮特定状态的历史记录。

目标：官方 WindTerm 2.7.0 Windows x64，EXE 13,534,208 字节，SHA-256 `7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0`。原安装与官方 EXE 身份不同，不能混用。本次只比较原安装的指定全局资源；不读取个人 profile、会话、凭据或 crash dump。发行包来源和既有身份校验见[实验环境](../../benchmarks/tools/WINDTERM-LAB.md)。

TermForge 的既有可缩放 shell 保持可运行。本报告不发布 UI 等价或性能结论。已知规格与独立实现策略的边界见下文 UI-A/B/C。

## 证据分层

| 证据 | 得到的事实 | 不能证明 |
|---|---|---|
| 发行包 `global/themes/dige-black`、配置/动作文件 | 样式声明、控件选择器、动作树/快捷键上下文 | 最终字体、实际布局、所有动作可用 |
| 保存的 Ghidra 12.1.4 数据库，只读/noanalysis，定向检查 10 个函数 | Qt 类构造、splitter 组合、初始化路径 | 当前显隐、持久化恢复结果、完整交互 |
| 原创 Frida 探针，在自有非输入桌面运行固定 EXE | 可见 QWidget 父子关系、类名/对象名、两种尺寸下几何 | 完整像素、实际呈现、性能、其他 DPI |

REA 资源证据 `ev_234bbe52ca6967cc401df23fe87f1f1d623788b5f1a3870eda2817da53bd0c17`：PE 资源树含图标、图标组和 manifest，没有 Win32 dialog 模板。这不等于没有嵌入 Qt UI 数据。

运行时证据：首次控件树 `ev_646e0491938a0eb3222ba7a571093c73a832e54a3058ec152c73063cb858caca`；补充名称与 resize `ev_70124b85caec62b4eb2aa5e4a2cdd1b23a04c26772ca70350f90e0da9089cd90`；公开探针新 profile 重跑 `ev_5d0336a36fd91d890e15be5bd10440b726fd3369fe70926f1fdf505b0f6021e4`。这些是本地 evidence ID，不是公开托管链接；目标哈希来自启动器的独立校验，不能冒称 Frida provider 自带身份验证。

## 静态构造事实

地址按官方 EXE 首选 image base `0x140000000`：

| 入口/地址 | 观察 |
|---|---|
| `MainWindow` 构造 `0x1403ad490`，vtable `0x140a36ca8` | QMainWindow 子类 |
| 主窗口组合 `0x1403ae5c0` | 单独的 KitBar 与 QStackedWidget；该构造路径先隐藏 KitBar |
| `KitWindow` 构造 `0x140395f50`，初始化 `0x140396270` | 菜单、状态栏和多层 splitter 由另一层窗口抽象组织 |
| splitter 构造 `0x140399ec0` | 七个 splitter；构造期 setSizes 请求含 1000/4000/1000 和 4000/2000，不能直接当像素 |
| splitter 状态保存入口 `0x1403966b0` | 保存的调用引用索引记录四处 `QSplitter::saveState` 调用：`0x140396b95`、`0x140396d47`、`0x140396f25`、`0x140397110` |
| splitter 状态恢复入口 `0x140397420`、`0x140398140` | 同一索引记录两入口内的 `QSplitter::restoreState` 调用；存在恢复路径，不代表已验证重启恢复结果 |
| 菜单 `0x1403993f0` | 遍历 ActionItem 树构造菜单/动作，非静态 Win32 菜单模板 |
| `TabBar` 构造 `0x14052aeb0` | 自定义 QTabBar，连接 currentChanged/tabMoved；完整拖放语义未知 |
| `QuickBar` 组合 `0x1404e25a0` | Quick → 可伸展内容 → Config → Close；外层 margin/spacing 为零 |

这里的 KitDock 是观察到的自定义类名；不能据此把所有面板当成 QDockWidget。原始反编译/汇编保存在 ignored local/，此表只记录独立描述的事实，不复制实现。

UI-B 的保存/恢复补充来自既有 `binary-index/candidates.jsonl` 的导入函数调用引用，没有扩大反编译范围。`saveState`/`restoreState` 入口不能证明持久化文件名、数据格式、保存时机、失败回退或 profile 合并规则；这些仍需定向运行时验证。TermForge 的布局文件与保存策略是独立设计。

## 运行时主窗口结构

固定包的全新隔离 profile，没有创建主区域连接；底部本地 ShellPane 自动出现。两个早期样本共 377 个 QWidget；公开启动器重跑得到 383 个，其中 127 个 `isVisible()` 为真。内部辅助控件数量随初始化状态变化，不能将固定数量作为一致性断言。三次自有 Job 回收后 active 均为 0。

```text
MainWindow
└─ QStackedWidget → TerminalWindow
   ├─ QMenuBar
   ├─ 主 splitter
   │  ├─ 工作区 splitter
   │  │  ├─ 上部三列
   │  │  │  ├─ 左列：ExplorerPane / FilerPane（各在 KitDock 内）
   │  │  │  ├─ 中列：KitGroup → ViewGroup / toolbar+AddressBar / 内容 stack
   │  │  │  └─ 右列：SessionPane / OutlinePane（各在 KitDock 内）
   │  │  └─ 底部 KitDock → ShellPane → 标签组 / terminal stack
   │  └─ QuickBar
   └─ StatusBar
```

下面是公开探针重跑的 Qt 逻辑坐标测量。窗口尺寸指内容尺寸，不含原生外框；组件坐标统一换算到 TerminalWindow 原点。仅代表此次新 profile、当前环境；物理 DPI 与实际字体尚未测量。

| 组件 | 2048×1120：x,y,w,h | resize 后 1360×860：x,y,w,h |
|---|---|---|
| 菜单 | 0,0,2048,33 | 0,0,1360,33 |
| 中列 KitGroup | 342,33,1364,686 | 227,33,906,513 |
| ExplorerPane 内容 | 0,57,341,319 | 0,57,226,232 |
| FilerPane 内容 | 0,401,341,318 | 0,314,226,232 |
| SessionPane 内容 | 1707,57,341,319 | 1134,57,226,232 |
| OutlinePane 内容 | 1707,401,341,318 | 1134,314,226,232 |
| ShellPane 内容 | 0,744,2048,319 | 0,571,1360,232 |
| ShellPane 内 TerminalWidget | 0,770,2048,293 | 0,597,1360,206 |
| QuickBar | 0,1064,2048,27 | 0,804,1360,27 |
| StatusBar | 0,1091,2048,29 | 0,831,1360,29 |

两种尺寸下，PaneGroup 高 24，ShellPane 内标签组高 26，空中央 ViewGroup 高 19、其 TabBar 高 0；中央 toolbar 高 34，AddressBar 高 26。**不能把空标签状态的 19 当成已有主会话标签的高度。** 左中右宽度约 1:4:1，与构造期请求一致，但这只支持该初始状态下的推断，不能覆盖用户拖动和恢复后的布局。

主题分隔线声明 1px，但 runtime splitter handle 的命中范围可达 5 个逻辑单位，两者并不矛盾。终端 SciScrollbar 宽 16；其高度在重跑中受其他子控件影响而改变，因此不将其高度写成固定常量。

## 样式和动作的声明事实

下列行号相对于官方包资源，不是 TermForge 源码；只记录参数和行为摘要。

| 来源 | 可用于独立实现的规格事实 |
|---|---|
| `gui.theme:9–14` | 背景 `#1c1c1c`、UI 文字 `#bbbbbb`、9pt；字体优先 Roboto Mono/Fira Code/系统字体 |
| `gui.theme:393–410,899–905` | 通用 tab 内容高 24px，水平 padding 8px；ViewGroup 宽度范围 80–200px，选中顶边 2px `#007acc` |
| `gui.theme:697–699,731–736` | PaneGroup tab 最大宽 160px；ShellPane 选中底边 2px 蓝色 |
| `gui.theme:598–611,850–867` | KitBar 图标 20px、padding 6px；普通终端滚动条 16px，tmux 8px |
| `scheme.theme:320–326` | 终端文字 `#F8F8F2`，背景同 UI；字体首选 RobotoMono Nerd Font，fontSize 数值 10（未验证其运行时单位/实际选择） |
| `wind.config:33–40` | 声明双击关闭、中键重命名、关闭后选择 previous、前九 tab 序号、中部省略；hoverToSelect 数值 0.2，单位/运行效果未验证 |
| `global/wind.actions:2367–2369`；`terminal/configs/menu.config:138,382` | 声明 `Window.RenameView`，目标为当前主 view，并出现在 Session/ViewTab 菜单；不能据此证明底部 ShellPane 的重命名路径 |
| `terminal/configs/menu.config:2–13` | 顶层顺序 Session/Edit/Search/Selection/Goto/View/Mode/Tool/Window/Help |
| `terminal/configs/toolbar.config:5–11` | 新建/最近会话、断开、重连、地址栏、更多菜单 |
| `global/wind.keymaps:511–533,1265–1271,1360–1363,1635–1638` | 切换/关闭快捷键依赖 normal/local/remote 等上下文；不能当全局同一套键映射 |

样式源码还包含字面 `1x` 边框宽度，不能擅自修正为 `1px` 并声称已恢复其效果。声明数值、实际 QWidget 几何和最终像素必须分开保存。

`tabbar.*` 是配置声明，不是 ShellPane 交互已执行的证据。`SelectPreviousTab` 的具体历史选择规则、双击/中键在不同标签组的适用范围、重命名编辑器的确认与取消方式均未运行验证。F2 入口也未由本次 WindTerm 证据建立。

指定资源比较：官方与原安装的 gui.theme、scheme.theme、wind.config、menu/toolbar/statusbar 配置字节相同；wind.actions、wind.keymaps 不同。原安装 About 文本含 Prerelease 3，且有额外 remote Ctrl+V/条件 Ctrl+C 绑定；不能从 About 字符串推定 EXE 身份，也不能把额外绑定当成官方默认。

## UI 实现进展与验收边界

执行优先级调为 UI-A → UI-B → UI-C；C3/C4 兼容性、U1 输入和性能证据继续保留，不能被外观对齐替代。没有选定新的产品 GUI 框架。

1. **UI-A 布局与样式**：以实测层级组织可缩放 workspace，集中管理颜色/间距/面板尺寸；先补测“已有主标签”和面板显隐后的布局，避免将空主区域常量带进实际 shell。原创绘制和资产，保持现有 parser/transport/renderer 边界。
2. **UI-B 可操作工作流**：真实多标签本地 shell、切换/关闭/重命名、session 列表、面板显隐与 splitter 拖动；逐个核对动作上下文、焦点与关闭后的选择。未实现的 SSH/SFTP、文件传输不做可点的假功能。
3. **UI-C 自动对齐回归**：同窗口逻辑尺寸、同 DPI/字体、同初始状态比较层级/矩形/动作结果；再用自有窗口截图检查绘制误差。覆盖缩放、显隐、两个会话隔离、后台输出、退出回收，保留 C5 的裁剪重绘回归。

当前独立实现覆盖五条可拖动分隔线（左右列宽、上下主区域、左右列内部上下比例），以及 F2/中键/菜单重命名、布局比例与显隐状态持久化。分隔线约束、拖动取消、重命名提交/取消、关闭后的选择、配置格式与保存时机均为 TermForge 策略，不宣称与 WindTerm 精确一致；产品实现不读取 WindTerm 配置。当前终端在底部 ShellPane 对应区域，不能用它替代“已有主连接标签”的对齐验证。

尚未回答：WindTerm 主连接标签样式实际应用、DPI/字体 fallback、菜单运行时启用条件、悬停/拖放精确语义、布局保存/恢复格式与时机、分屏/全屏、弹窗、sender/transfer 等其他页面。这些需要定向状态场景，不以反编译整个程序为开工前置条件。TermForge 的交互与配置回归只验证本项目行为，不会自动补足这些 WindTerm 证据缺口。

## 复现与来源

[探测入口与约束](../../research/ui/README.md)。公开内容为原创探测器和精简事实说明；WindTerm EXE/DLL/主题/图标/动作脚本及原始反编译和 widget dump 均不进入仓库。Qt runtime graph 不等于获得源码或资产许可。研究只用于独立实现规格，产品构建不依赖 WindTerm。

本地证据位于 `local/ui-reference/`：`resource-findings.json`、`binary-facts.md`/哈希索引、`runtime-evidence.json`、`repro-01/run.json`/`files.json`/`cleanup.json`。首次两次 probe 使用本地原型启动器；第三次为本文公开入口的 fresh-profile 重跑。未宣称视觉完全等价，也未使用插桩运行给出性能排名。

首轮取证时的验证：本机原有 12 套 CTest 全部通过（71.69 秒）；新增准备守卫覆盖错误 EXE、输出范围/重用、fresh profile 与私有目录排除，以及 EXE 不变但 Qt DLL/主题更改时拒绝。审查后补上复制子集整体哈希，4,046 个文件逐项与固定 ZIP 核验。这些是当次取证工具与既有产品的验证记录，不代表后续 UI-B 的测试结果或 WindTerm 完整 UI 等价。
