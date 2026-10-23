# WindTerm 2.7.0：R1 文字交互契约

**#11 尚未验收完成。** 已直接观察基础导航/选择、系统复制、键盘粘贴、全选/查找菜单及原始 VT 文本投影。复制/粘贴菜单的实际执行、软换行与尾空白的系统剪贴板结果，以及粘贴验收器补强仍未完成。以下记录行为事实，不代表完整等价或性能验收。

目标固定为官方 2.7.0 Windows x64，EXE SHA-256 `7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0`；分发文件白名单摘要为 `e459adf2f12adb6a1b7f55e66a9da1c22794185e66e848057d21e0c60d669203`。公开内容只有原创场景、工具和事实。

## 证据与观察边界

[Hosted 事实摘要](r1-text/hosted-39954191554.json) 来自 [CI 39954191554](https://github.com/JavaZeroo/termforge/actions/runs/39954191554)，源码为 `d150fe44b9fff058bcb5ed6aafe9ac0c34dacade`。在一次性 Windows 2025 runner 中，两个 Shell `text` 场景与两个 loopback `menu-wrap` 场景各使用新配置；每对使用相同 fixture/template。四轮自有 Job 均 `active=0`，两个 VT peer 均关闭，配置检查只含合成会话。两轮 Shell 的总行数不同，不能把相同的核心行为说成全部原始状态相同。

该摘要保留来源 Evidence ID、原 gate 标志和独立复核，三者不混用。artifact `r1-reference-facts` 的 ZIP SHA-256 为 `e8c995357571f8416af6e9f9ff16a1b7ee81cdec75a36d07fe4a4357d969a0cf`。已核对八个 JSON 白名单文件；公开摘要没有用户配置、原始 widgets、句柄、路径、stack、下载令牌或私有提取物。

`r1_fixture.py` 生成编号日志、内空格/尾空格、中文、NFD 组合标记及长行，随后只记录输入，不执行粘贴内容。`r1_vt_fixture.py` 只监听 `127.0.0.1` 的临时端口，接受一个自有 Telnet 连接，按 [NAWS 规范](https://www.rfc-editor.org/rfc/rfc1073) 读取列数后发送原创 VT 字节，不使用账户或外部主机。

`qt_r1_text.js` / `r1_menu_wrap.js` 经 REA 6.7.0 公开 Frida 入口，在自有目标 UI 线程重放 Qt 键盘、鼠标及 context 事件，读取受控文本状态、焦点和 peer 结果。菜单通过实际 popup、选定 action 与排队 Return 执行；不是直接调用 QAction 的业务槽。`selectWord()` / `selectLine()` API 观察单独标明，不代替鼠标选择。本方法不证明物理输入或实际呈现性能。

## 历史、查找与选择

以下行为在两次本地新配置中有逐项相同的记录：[本地一](r1-text/local-01.json)、[本地二](r1-text/local-02.json)。Hosted 两次 `text` 场景也通过相应内容和状态检查。本地记录的系统剪贴板仍为 `verified=false`，不能用后来的 hosted 结果改变原记录的覆盖范围。

| 操作 / 前提 | 直接观察 | 适用边界 |
|---|---|---|
| Ctrl+Shift+Home / End | 滚动到历史顶部 / 底部 | 默认 local Shell；焦点在终端 |
| 历史顶部暂停后外部追加 20 行 | 可保留行数增加 20，视口保持 0 | 无键盘输入 |
| 暂停后输入普通 `x` | 无新增输出，视口回到末尾；peer 收到 `x` | 键入恢复跟随 |
| Shift+PgUp / PgDn | 视口不动，peer 收到 MSVCRT 扩展键码 | 不是已观察到的默认历史快捷键 |
| Ctrl+Shift+F / Escape | 焦点进入查找 ComboBox / 返回终端 | 查找文字未进入 peer |
| 文档末尾输入 `needle` 后等待 | 增量查找自动选第 79 行匹配 | 80 行独立已知 oracle |
| 接着 Enter / F3 | Enter 到第 78 行；F3 到第 79 行 | Enter 朝较小文本位置，F3 朝较大位置 |
| F3 越过最后命中 / Shift+F3 | 循环到首个 / 从首个回到末个 | 当前会话的保留历史 |
| 查找 `中文` | 选中两字，不损坏文本 | 字面量基本样例，不是全 Unicode 规范 |
| 正文双击 `TF-HISTORY-000` | 选中完整词，保留连字符 | 无修饰键鼠标事件 |
| 同点三击 | 选中整行，不包含末尾 LF | 与 `selectLine()` API 不同 |
| `selectLine()` API | 选择相同行，并包含末尾 LF | 不等于三击行为 |
| 正向 / 逆向跨行拖选 | `ALPHA␠␠tail\nBETA 中文 é` | 内空格、中文、组合标记保留 |
| 输入 `ALPHA␠␠tail␠␠␠` | 本地目标文本模型读回不含尾空格 | 不能归因到复制层 |

## 系统复制与键盘粘贴

以下是 hosted 两次 `text` 场景一致的实际系统剪贴板与接收端结果。复制通过 `OpenClipboard` / `GetClipboardData(CF_UNICODETEXT)` 读取，不使用 Qt 文本缓存代替。粘贴输入先写入受控 VM 的系统剪贴板，再触发相应入口。

| 输入 / 操作 | 直接观察 |
|---|---|
| 选中 `ALPHA␠␠tail\nBETA 中文 é`，Ctrl+Shift+C | 系统 CF_UNICODETEXT 精确为 `ALPHA␠␠tail\r\nBETA 中文 é`；硬换行转 CRLF，中文和 NFD 保留 |
| 剪贴板 `TF-SINGLE`，Ctrl+Shift+V | 无确认框；peer 立即收到完整 `TF-SINGLE`，仅一次 |
| 单行粘贴后 Escape | peer 不再增加，但已发送内容不会撤销；终端保有焦点 |
| 剪贴板 `TF-FIRST\r\nTF-SECOND\nTF-THIRD`，Shift+Insert | 出现 `PasteDialog`；peer 在确认前没有增加 |
| 上述多行弹窗中 Escape | 弹窗关闭、焦点回终端、peer 不增加 |
| 同一多行剪贴板，Ctrl+Shift+V | 再次出现 `PasteDialog`；peer 在确认前仍不增加 |
| 点击默认 Paste 按钮 | 弹窗关闭、焦点回终端；peer 仅追加一次 `TF-FIRST\rTF-SECOND\rTF-THIRD` |

脚本阶段名 `paste-single-cancel` 有误导性：该阶段实际是**立即粘贴成功之后的 Escape**，没有可取消的单行确认框。它不能作为单行任务取消的证据。多行取消与确认的入口则有真实弹窗和接收端字节支持。

这一系统复制样例没有尾空白或软换行；不能与下一节的选择模型结果拼接成“系统剪贴板也保留尾空白并合并软换行”的结论。不同传输、alt-screen、逐行发送及更大粘贴任务不在此样例的结论内。

## 原始 VT 换行与实际菜单

Hosted 两次 `menu-wrap` 场景均协商为 63 列，发送字节 SHA-256、选择与搜索结果相同。较早的 [91 列记录一](r1-text/vt-menu-01.json)、[91 列记录二](r1-text/vt-menu-02.json) 也保留为独立的局部证据，不将它们的菜单失败改判为通过。

| 输入 / 操作 | 直接观察 | 系统剪贴板覆盖 |
|---|---|---|
| `LEFT` 写至最后一列，紧接 `RIGHT`，中间无换行字节 | 选择投影为连续 `LEFTRIGHT`；可跨显示折行查找，F3 回到同一唯一匹配 | 未验证 |
| 相同位置显式发送 CRLF | 选择投影为 `LEFT\nRIGHT`，不合并硬换行 | 未验证 |
| `TF-TAIL:two␠␠`、中文、NFD `e` + U+0301 | 选择投影保留两格尾空白、中文和组合标记 | 未验证 |
| 实际上下文菜单 → Select All | 从不同的初始选区变为完整合成 VT 文本 | 不涉及复制 |
| 实际上下文菜单 → Find | 焦点进入 ComboBox，输入 `LEFTRIGHT` 后选中该匹配；输入未泄漏到 peer | 不涉及复制 |

菜单可见标注为 Copy `Shift+Del`、Paste `Shift+Ins`、Find `Ctrl+Shift+F`、Select All `Ctrl+Shift+A`。其中 **Copy/Paste 菜单只是读取条目，尚未激活**；Copy 的 `Shift+Del` 也只是可见标注。实际键盘复制证据来自上一节的 Ctrl+Shift+C，不能自动证明另一键位。

## 验收器限制与未完成项

CI 成功说明 `d150fe4` 当时的脚本 gate 通过，不能直接解锁整个 #11。原 `text` 汇总器有明确漏洞：多行取消比较以弹窗出现后的 peer 内容为基线，可能漏掉打开弹窗前的提前发送；单行效果及取消/接受后弹窗关闭也未严格校验。新增反例已复现这些误通过条件，代码补强仍未完成。

本次对两轮保留的白名单 diagnostic 另做了独立复核：以 `reverse-drag-unicode` 阶段的 peer 内容为粘贴前基线，确认单行只追加 `TF-SINGLE`；多行打开、取消和再次确认前均不再增加，确认后只追加一次精确 CR 分隔文本；取消/接受后 modal 均为 null、焦点回终端。两轮均满足这些更严格的条件。此复核支持本次观察，**并未修复自动验收器**。公开 JSON 将 `original_gate` 与 `independent_recheck` 分开，顶层 `issue_11_complete=false`。

恢复增量工作后，#11 的剩余门槛是：

1. 修复粘贴验收器，使用真实粘贴前基线，按已观察到的单行立即发送策略和多行确认策略分别验证；加入提前泄漏、重复发送、取消后仍开窗等反例。澄清 `paste-single-cancel` 阶段名称或解释。
2. 实际触发 Copy/Paste 菜单，验证正确选区、系统剪贴板、确认/取消和接收端结果。不能以菜单存在或其他动作成功代替。
3. 将原始 VT 软换行、硬换行、尾两空格及 Unicode 样例接到实际 CF_UNICODETEXT 复制验证，保存两次新配置的精确结果。
4. 在补强后的门槛下核对上述基础行为和回收，保留未观察项，完成整个合同后才关闭 #11。

仍仅有静态声明的内容包括默认主屏粘贴提示开关、alt-screen 提示关闭和选择后自动复制关闭；以上运行不能证明这些设置在所有上下文中的语义。更复杂 VT 模式、逻辑行重排、正则/跨会话搜索不由这些基本样例推出。

## 自动复现与隔离

[r1-reference.yml](../../.github/workflows/r1-reference.yml) 仅 `workflow_dispatch`，由代理触发；下载并核验官方 ZIP/EXE、安装锁定 REA，串行运行两次 `text` 与两次 `menu-wrap`。`--require-complete` 当前只覆盖 `text` gate，且有上述待修漏洞；`--require-menu-complete` 只覆盖 Select All/Find 菜单及 VT 选择/搜索。两种标志都不等于整个 R1-01 完成。

本机新建 window station 的实际 `OpenClipboard` 返回 error 5，因此不回退到用户剪贴板。仅显式 `--hosted-ci` 且同时满足 `GITHUB_ACTIONS=true` / `RUNNER_ENVIRONMENT=github-hosted` 时，允许使用一次性 VM 当前 station 的剪贴板，目标窗口仍在自有非输入 desktop。系统剪贴板任务串行执行。

目标与 REA 公开 Node CLI 都先以原生 suspended 状态创建、加入自有 Job 后再恢复；分配失败通过自有句柄终止并等待，超时终止整树。合成进程测试覆盖分配前不执行、分配失败、失败记录写入异常及恢复后立即生子的回收。任何 `launcher-failure.json` 或两层 REA `cleanup_error` 都禁止验收通过，即使已有完整场景标记。

就绪判定检查真实 session tree model、JS engine、peer 数据和活动文本，受总观察时限约束。可能进入模态循环的事件排队发送；`resize`/同步事件保留目标原生异常处理，见 [Frida NativeFunction 文档](https://frida.re/docs/javascript-api/#nativefunction)。这些探针方法已用于上述 hosted 成功观察，仍不代表普通用户输入或性能测量。
