# WindTerm 2.7.0 合成会话：UIA 可见性与可驱动入口

本页记录驱动原版 2.7.0 做合成会话观察时的一条死胡同与可用入口，供 [#155](https://github.com/JavaZeroo/termforge/issues/155) 的 harness 与后续 [#123](https://github.com/JavaZeroo/termforge/issues/123)/[#126](https://github.com/JavaZeroo/termforge/issues/126) 观察复用。全部结论来自一处隔离 Windows 11 VM 上的固定 2.7.0 发行包（EXE SHA 与仓库其它记录一致），没有修改产品代码、没有 Frida 注入。

## 合成 profile 的加载状态

固定布局是 profiles.config 指向唯一 profile 目录，profile 下放 .wind/profiles/default.v10/terminal/user.sessions，其中一条合成会话（group/label/protocol/system/target/uuid/process.arguments/process.workingDirectory）。

| 检查 | 结果 |
|---|---|
| profile 是否被读取 | 是：原版在同一目录写出 user.config、global/reopen.history、terminal/reopen.history，且不改写我们的 user.sessions |
| 组名是否进入界面 | 是：主窗口 UIA 树与“新建会话”对话框的分组下拉里都出现 TermForge Lab |
| 会话叶是否进入 UIA 树 | **否**：整棵树里只有一个 TreeItem（组），该节点 raw_children=0、无 ExpandCollapse/SelectionItem |
| 是否与会话参数有关 | 否：把 process.arguments 换成已验证的 /d /q 后结果完全相同 |
| 是否与累积状态有关 | 否：删掉 user.config 与两份 reopen.history、只留 user.sessions 后仍只有组 |
| 是否有命令行开会话入口 | 未发现：-s / --session / -n / --new-session / -h 都会普通启动，且不拉起会话目标进程 |

## UIA 可用的部分

- 菜单栏可展开：会话 / 编辑 / 搜索 / 选择 / 转到 / 查看 / 模式 / 工具 / 窗口 / 帮助；会话菜单含 新建会话、新建窗口、断开会话、重新连接会话、打开会话...、最近打开、保存会话、日志、关闭视图、退出。
- “新建会话”对话框可完整枚举：主机、端口、OneKey、标签、分组（下拉已含合成组）、终端、系统（linux/cmd/dumb）、字符集、描述，以及完整设置树（会话/终端/Bell/模式/键盘/安全/日志/窗口/选择/SSH/连接/代理/验证/SFTP/X11/X-Y-Z Modem）。
- 文本字段可用 ValuePattern 写入；列表项可用 SelectionItemPattern 选中。
- 该对话框没有“命令行/参数”字段；受控命令只能来自会话的 process.arguments。

## 已知的 UIA 限制

- QComboBox 的“系统”没有暴露 ExpandCollapsePattern，选中列表项后 ValuePattern 仍显示旧值（linux），未验证为真正生效；
- 对菜单项调用 InvokePattern（如新建会话）会进入模态事件循环，脚本需改用快捷键或异步方式，否则阻塞到超时；
- 截图可用（VM 侧 screenshot 产出 1024×768 PNG），但本会话的记录者无法直接读图；用 Windows.Media.Ocr 可读到大字标题（如 TermForge Lab），不足以判定叶节点是否存在。

## 后续修正：会话其实是折叠的，而且能打开

初版记录“叶不出现”，后续对照实验推翻了这一判断的一半：

- 组节点**默认折叠**，UIA 不暴露折叠子项、也没有 ExpandCollapse 模式；对该组 UIA 矩形的中心做**一次鼠标双击**后，`TF_UI_CMD` 叶立即出现在树里；
- 该叶支持 Invoke/Value/GridItem/TableItem（没有 SelectionItem）；**单击叶 + Enter** 可以把会话打开——用长驻命令 `/d /q /k` 验证：打开后 `cmd` 进程存活（1 个），原版标题变为 `TF_UI_CMD - WindTerm`，工作目录为合成 host-home；
- 之前“打开后没有子进程”的真实原因是**会话命令立刻失败**：原合成 profile 里的命令是 `qt_ime_input_test.exe --ime-output-child ...`，该测试二进制在会话里直接退出（exit 2，stderr 为 `Qt IME test requires its owned Job supervisor`），它只能由自有 Job + stdin GO 门的 supervisor 启动。

因此 harness 的可用路径是：**双击组展开 → 单击叶 + Enter 打开 → 会话里必须是一个可在 WindTerm 内长驻的进程**（真实 cmd/PowerShell，或一个由会话启动的 controller）。这也意味着不需要 Frida 注入即可驱动原版会话。

## 边界

- 单 VM、单发行包、单一合成 profile 配置；未覆盖 WinPTY/ConPTY 本地 shell、真实输入法、多显示器或性能。
- 本页不改变任何产品行为、优先级或 #155/#165 的状态；它只把“UIA 不是可靠入口”这一事实固定下来，避免后续重复探索。