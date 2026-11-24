# Qt 单会话原生拼音观察

在专用 Windows 11 VM 的交互会话中，对基于 `main` 的正式 Qt 控件和固定 ConPTY 运行独立 `ReadConsoleW` 接收端。观察器在自有 Job 验证后启动，使用系统 Windows QPA。外部驱动逐键核对前台窗口、所属线程和焦点，通过 OS 虚拟键触发输入法。 [#155](https://github.com/JavaZeroo/termforge/issues/155) · [冻结构建 45400268011](https://github.com/JavaZeroo/termforge/actions/runs/45400268011)

- 目标 GUI 线程的 `GetActiveProfile` 返回固定微软拼音 profile，语言 `0x0804`、HKL `8040804`，HIMC 已打开且处于 native 转换模式。组词 `ni'hao` 可观察。
- 组合期间实际改变窗口大小，显示终端网格由 `84×21` 变为 `94×23`，模型报告 resize 请求已结束。首次组合按 Esc 取消，第二次按空格确认。Qt 日志只有一次 `e4bda0e5a5bd` 提交，独立接收文件也正好是这 6 字节。
- 顶层窗口候选枚举在组合前、组合中、resize 后和取消后均返回同一个无归属关联的全屏 `CoreWindow`。候选身份、关联和移动保持未知。Qt 的窗口内光标查询几何单独保留。
- 第一次请求在缺少 `System.Drawing` 引用时停在 PowerShell 编译阶段。修正该前置后实际运行一次，收到字节后观察器根进程退出 1，终态文件未写出。内外 Job 活动进程均已回到 0。该轮记录保留失败状态。

接收端正常写文件后，唯一会话完成会触发 `SessionTabs::on_all_exited`。原观察器给该通知绑定了 `app.exit(1)`，使下一次观察器轮询无法验证接收文件。已移除这一绑定，终态由观察器在收到会话完成、EOF 和 Job 回收后写入再退出。 [初次原生记录](validation.json) · [观察器源码](../../../../src/qt/ime_native_single_test.cpp)

读取到的合成事件、接收字节及尺寸为本轮固定样本证据。候选窗身份、实际呈现、144 DPI、字体动作和多会话组合需后续独立验证。

## 退出顺序修正后的复验

用冻结源码 `5fe10a5` 和同一台 VM、同一输入法配置运行一次。驱动在组合期间缩放窗口，网格从 `84×21` 变为 `94×23`，resize 请求完成。首次按 Esc 取消，没有提交事件。第二次按空格确认，只记录一次 Qt 提交。独立 `ReadConsoleW` 接收文件正好是 `e4bda0e5a5bd`（6 字节）。观察器写出终态后退出 0，内外两层 Job 均完成回收，活动进程数为 0。 [冻结构建 45401506639](https://github.com/JavaZeroo/termforge/actions/runs/45401506639) · [复验记录](validation-finalization.json)

本次验证覆盖 Windows 11 `10.0.26300.0`、Qt `6.10.3`、96 DPI 下的单会话输入与退出。顶层窗口采样仍无法识别候选窗，候选关联和物理位置继续待查。144 DPI、字体动作和多会话组合仍待验证。

## 候选面板屏幕坐标与 UI Automation

仍使用冻结 `5fe10a5` 构建和同一 96 DPI 隔离桌面。每轮先抓基线，再输入 `NIHAO`，依次采样组合、原点固定 resize、延后 resize、纯移动和 Esc 取消。第一次驱动的 UIA 坐标构造失败，保留像素与输入结果。修正构造并补充 `FromHandle` 后重采一轮，按原样核对 6 字节与 Job 回收。 [两次观测及哈希](validation-candidate-uia.json)

- 组合后出现两条新增亮带，合并边界约 `[133,168,772,202]`。固定左上角把窗口由 `780×540` 增为 `876×588` 后，两个采集点的亮带坐标相同，较窄的固定内部区域逐字节相同。同时，Qt 的窗口内光标查询保持原值。该 resize 改变了网格大小，光标锚点没有在本样本中移动。
- 只移动窗口 `(-48,+64)` 后，延后采样的下方亮带保持原坐标，上方亮带与窗口亮区合并。取消后两条候选相关亮带消失。纯移动样本的固定区域像素发生变化，保留坐标和哈希各自的观测。
- UIA 从宿主窗口句柄识别出属于 `TextInputHost` 的 `CoreWindow`，其可枚举子树未返回「你好」候选元素。亮带中心点命中的 UIA 元素均属于 Qt 窗口。当前证据确认候选相关像素及这条 UIA 路径的覆盖边界，候选元素到窗口句柄的关联仍未知。 [Microsoft UIA 元素查询接口](https://learn.microsoft.com/en-us/windows/win32/winauto/uiauto-obtainingelements)

以上 resize 固定原点、纯移动条件与历史 N11 对照轨迹不同。保持输出、字体、候选关联以及该纯移动动作的 WindTerm 对照为独立待验项。

## 目标线程 TSF 和 IMM 候选观测

冻结 `daf4520` 后在同一 VM 用相同按键、resize 和移动重新观测一次。Qt 线程已存在 TSF 线程管理器和有焦点的文档管理器。基线、组合、resize 后、移动后和 Esc 后，`EnumUIElements` 都返回成功，第一次 `Next` 即 `S_FALSE`，没有候选列表可用于核对文档关联。 [冻结构建](https://github.com/JavaZeroo/termforge/actions/runs/45404974749) · [复验结果](validation-candidate-context.json) · [TSF 接口](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfuielementmgr-enumuielements)

IMM 在组合期间可读到请求的候选框 `CFS_EXCLUDE` 矩形 `[10,55,19,77]` 和位置 `(10,77)`，同时候选列表容量查询返回 0。64 像素纵向窗口移动后，Qt 窗口内查询值仍相同。这个矩形描述输入上下文的请求，屏幕物理区域和候选关联仍需独立证据。观察进程仅查询已有对象，没有激活 provider、创建线程管理器或改写输入上下文。

同轮屏幕亮带、一次「你好」提交、独立接收端 6 字节和两级 Job 回收均已保存。候选窗候选元素到文档或实际窗口的直接关联保持未知。
