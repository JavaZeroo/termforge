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

## 对 harness 的含义

1. **不能把“UIA 暴露会话叶”当作驱动原版的前提**：组能进模型，叶不保证出现。
2. 仍可继续不注入的路线：用对话框新建并保存会话，让原版自己写出规范 user.sessions，再与合成条目逐字段比对；或用“打开会话...”配合预制文件。
3. 若要直接读 widget 树/触发动作，已验收的 [research/ui/launch.py](../../research/ui/launch.py) 走的是 Frida + Qt 探针，而不是 UIA；切换过去需要显式记录注入身份与回收证据（当前 bootstrap 计划把 frida_deploys 固定为 0）。

## 边界

- 单 VM、单发行包、单一合成 profile 配置；未覆盖 WinPTY/ConPTY 本地 shell、真实输入法、多显示器或性能。
- 本页不改变任何产品行为、优先级或 #155/#165 的状态；它只把“UIA 不是可靠入口”这一事实固定下来，避免后续重复探索。
