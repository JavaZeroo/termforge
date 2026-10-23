# WindTerm 2.7.0：R1 文字交互契约

**R1-01 的限定参考行为合同已通过两组各两轮 hosted 验收。** 范围包括基础导航/选择、系统复制、键盘粘贴、实际 Select All/Find/Copy/Paste 菜单，以及 VT 软折行、硬换行、尾空白的系统剪贴板结果。此合同可用于后续独立实现和对照；它不代表 TermForge 功能已经等价，也不是完整 WindTerm 或性能验收。

目标固定为官方 2.7.0 Windows x64，EXE SHA-256 `7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0`；分发文件白名单摘要为 `e459adf2f12adb6a1b7f55e66a9da1c22794185e66e848057d21e0c60d669203`。公开内容只有原创场景、工具和事实。

## 证据与观察边界

[Hosted 事实摘要](r1-text/hosted-39960732305.json) 来自 [CI 39960732305](https://github.com/JavaZeroo/termforge/actions/runs/39960732305)，源码为 `c386494587656faa3d8621f7ddbf43a6aa678255`。在一次性 Windows 2025 runner 中，两个 Shell `text` 场景与两个 loopback `menu-wrap` 场景各使用新配置；每对使用相同 fixture/template、不同 Evidence ID，且第二轮 `repeat.verified=true`。四轮自有 Job 均 `active=0`，两个 VT peer 均关闭，配置检查各只有一个合成会话。两轮 Shell 初始总行数为 90/88，追加后为 110/108；重复门槛比较指定行为，不要求全部原始状态相同。

该摘要保留四个 Evidence ID、原 gate 标志及独立逐阶段复核，三者不混用。artifact `r1-reference-facts`（ID `12505390630`）的 ZIP SHA-256 为 `0ba5db5af77de70ad1c8a346cd1a56030962433fe8cc92bf60c8bc072e43a4ba`。已核对八个 JSON 白名单文件；公开摘要没有用户配置、原始 widgets、句柄、路径、stack、下载令牌或私有提取物。[旧 hosted 摘要](r1-text/hosted-39954191554.json) 保留原先局部门槛及独立复核范围，不回写为本次验收结果。

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

单行阶段为 `paste-single-delivered` / `paste-single-after-escape`，操作前单独记录 `paste-before`。旧证据中的 `paste-single-cancel` 实际也是**立即粘贴成功之后的 Escape**，没有可取消的单行确认框，不能作为单行任务取消证据；旧记录保持原名。多行取消与确认则有真实弹窗和接收端字节支持。

这一 Shell 复制样例只覆盖基本硬换行与 Unicode；软折行和尾空白由下一节同一 VT 样例的实际系统复制独立验证。alt-screen、逐行发送及更大粘贴任务不在此合同内。

## 原始 VT 换行与实际菜单

Hosted 两次 `menu-wrap` 场景均协商为 63 列，发送字节 SHA-256、选择与搜索结果相同。较早的 [91 列记录一](r1-text/vt-menu-01.json)、[91 列记录二](r1-text/vt-menu-02.json) 也保留为独立的局部证据，不将它们的菜单失败改判为通过。

| 输入 / 操作 | 直接观察 | 系统剪贴板覆盖 |
|---|---|---|
| `LEFT` 写至最后一列，紧接 `RIGHT`，中间无换行字节 | 选择投影为连续 `LEFTRIGHT`；可跨显示折行查找，F3 回到同一唯一匹配 | 实际 Copy 菜单所得 CF 中也是连续 `LEFTRIGHT` |
| 相同位置显式发送 CRLF | 选择投影为 `LEFT\nRIGHT`，不合并硬换行 | CF 精确为 `LEFT\r\nRIGHT` |
| `TF-TAIL:two␠␠`、中文、NFD `e` + U+0301 | 选择投影保留两格尾空白、中文和组合标记 | CF 保留 `TF-TAIL:two␠␠\r\n`、中文及 NFD；末行也以 CRLF 结束 |
| 实际上下文菜单 → Select All | 从不同的初始选区变为完整合成 VT 文本 | 不涉及复制 |
| 实际上下文菜单 → Find | 焦点进入 ComboBox，输入 `LEFTRIGHT` 后选中该匹配；输入未泄漏到 peer | 不涉及复制 |
| 实际上下文菜单 → Copy | 事先写入 `TF-COPY-SENTINEL`，激活后系统 CF 替换为整份 VT 文本 | 两轮逐字相同；精确全文见事实 JSON |
| 实际上下文菜单 → Paste，单行输入 | 无确认框，peer 只增加一次 `TF-SINGLE`；随后 Escape 不撤销已发送内容 | 使用 VM 系统剪贴板 |
| 实际上下文菜单 → Paste，多行输入 | 弹出 `PasteDialog`；确认前及 Escape 取消后均不增加字节 | 再次 Paste 并确认后，只增加一次 `TF-FIRST\rTF-SECOND\rTF-THIRD` |

菜单实际激活顺序为 Select All、Find、Copy、Paste、Paste、Paste。粘贴取消/确认后 modal 均为空、焦点返回 `ScintillaEditBase`；稳定阶段与运行结束后的 peer 检查排除重复或晚到字节。菜单可见标注为 Copy `Shift+Del`、Paste `Shift+Ins`、Find `Ctrl+Shift+F`、Select All `Ctrl+Shift+A`；Copy 的 `Shift+Del` 仍只是可见标注，已验证的复制入口是实际 Copy 菜单和 Ctrl+Shift+C。

## 编辑框与实际 Return 的新增观察

[40023107313 的限定独立审计](r1-text/hosted-context-40023107313.json)来自固定源码 `9f1bb8b` 的两个新 `action-context` 运行，目标身份与上述合同相同。以下是两轮保留阶段和精确字节一致的实际观察，不由按钮标注或内部 API 名称推断：

| 前提 / 动作 | 实际结果 |
|---|---|
| Find 查询 `LEFTRIGHT`，Ctrl+A / Ctrl+C | 原生编辑框全选查询；实际系统 CF 为 `LEFTRIGHT`，peer 零字节 |
| Find 编辑框 Ctrl+V 粘贴 `中文` | 查询和终端命中为 `中文`，输入不进入 peer |
| Find 中再次 Ctrl+Shift+F，随后 Escape | 保留 `中文` 查询；Escape 返回 `ScintillaEditBase`，peer 仍为空 |
| 返回后键入 `x`，实际菜单 Paste 多行 | 确认前 peer 精确为 `x`，无粘贴字节 |
| PasteDialog 当前焦点 `QPushButton`，实际发送 Return | 弹窗退出；peer 精确为 `xTF-FIRST\rTF-SECOND\rTF-THIRD`，稳定阶段及最终均不再增加；两轮 outcome 均为 accepted |

该 artifact 只含12个白名单 summary/diagnostic JSON。目标/运行/产物绑定、ZIP摘要、固定模板/fixture、逐阶段值和两轮比较已独立复核；没有原始 REA 事件总序、`probe.js` 或最终 `peer.json`，不能据此声称独立重跑了完整 `summarize_context` 或重算运行时替换后的脚本哈希。后续 `--export-context` 将导出安全有序事件供完整重算；本次局部证据不回写成那种更强的验收。

以上足以明确原产品默认 Cancel/Enter 取消与已观察的参考 Return 不一致，R1-07 应对齐并重新验证；rename、busy/关闭及物理输入行为不由本观察推出。

增强导出的 [40025079604](https://github.com/JavaZeroo/termforge/actions/runs/40025079604) 仍记失败，见[独立失败事实](r1-text/hosted-context-40025079604-failure.json)：两轮原行为门槛通过，但安全导出将菜单操作改变的非 Find 选区误列为额外要求，未产出有序事件。修复仅保留五个 Find 检查点实际需要的选区，省略其他阶段的无关文本；行为及事件顺序门槛不变，新运行通过前不扩大上述验收范围。

## 验收门槛与合同边界

本次两轮 `text.complete=true`、两轮 `menu_verified=true`，两组重复门槛均通过。`menu-wrap` 摘要的 `complete=false` 是有意保留的场景边界：单独菜单场景不含完整文字导航合同，必须与 `text` 合并验收。白名单 diagnostic 的 `validated=false` 表示其为观察导出，不是最终门槛结果；验收读取对应 strict summary。

严格粘贴校验从 `paste-before` 的 peer 内容建立基线，同时检查精确增量、modal、焦点及结束后的 peer。独立复核又从四轮白名单快照重算相同转换，确认单行只发送一次，多行在确认前没有泄漏，取消不发送，确认仅发送一次；四轮结果一致。旧汇总器以弹窗出现后内容作取消基线、可能遗漏提前发送的缺陷已修复；旧运行和旧 JSON 仍只代表其原覆盖范围。

本次支持的验收条件：

1. 默认 Shell 的历史跟随、选择单位、正反向拖选、当前保留历史内查找方向与循环，以及快捷键焦点。
2. 键盘和实际菜单入口的系统复制/粘贴；单行立即发送、多行确认、取消和接受后的焦点与字节。
3. 原始 VT 样例在同一次实际 Copy 中保留软折行合并、硬换行 CRLF、尾两空格、中文和 NFD；精确 CF 字符串两轮一致。
4. 固定目标与工具身份、每轮全新合成配置、两组不同 Evidence ID 的重复比较，以及目标/CLI/peer 回收和失败拒绝门槛。

仍仅有静态声明的内容包括默认主屏粘贴提示开关、alt-screen 提示关闭和选择后自动复制关闭；以上运行不能证明这些设置在所有上下文中的语义。更复杂 VT 模式、逻辑行重排、正则/跨会话搜索不由这些基本样例推出。

## 自动复现与隔离

[r1-reference.yml](../../.github/workflows/r1-reference.yml) 是一次性 VM 自动入口：下载并核验官方 ZIP/EXE、安装锁定 REA，串行运行两次 `text` 与两次 `menu-wrap`。`--require-complete` 覆盖严格 `text` gate；`--require-menu-complete` 现在必须覆盖 Select All/Find/Copy/Paste 菜单、VT 选择/搜索和实际系统剪贴板。旧 `menu-wrap-only` 完成标记只能得到局部 `selection_find_verified`，不能通过新菜单门槛。每组第二轮应以 `--compare-with` 指向第一轮摘要；两种场景须合并核对整个 R1-01 合同。

本机新建 window station 的实际 `OpenClipboard` 返回 error 5，因此不回退到用户剪贴板。仅显式 `--hosted-ci` 且同时满足 `GITHUB_ACTIONS=true` / `RUNNER_ENVIRONMENT=github-hosted` 时，允许使用一次性 VM 当前 station 的剪贴板，目标窗口仍在自有非输入 desktop。系统剪贴板任务串行执行。

目标与 REA 公开 Node CLI 都先以原生 suspended 状态创建、加入自有 Job 后再恢复；分配失败通过自有句柄终止并等待，超时终止整树。合成进程测试覆盖分配前不执行、分配失败、失败记录写入异常及恢复后立即生子的回收。任何 `launcher-failure.json` 或两层 REA `cleanup_error` 都禁止验收通过，即使已有完整场景标记。

就绪判定检查真实 session tree model、JS engine、peer 数据和活动文本，受总观察时限约束。可能进入模态循环的事件排队发送；`resize`/同步事件保留目标原生异常处理，见 [Frida NativeFunction 文档](https://frida.re/docs/javascript-api/#nativefunction)。这些探针方法已用于上述 hosted 成功观察，仍不代表普通用户输入或性能测量。
