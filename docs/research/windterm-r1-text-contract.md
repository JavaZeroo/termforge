# WindTerm 2.7.0：R1 文字交互契约

这是受控本地 Shell 的行为取证，不是完整等价或性能验收。目标是官方 2.7.0 Windows x64，EXE SHA-256 `7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0`；分发文件白名单摘要为 `e459adf2f12adb6a1b7f55e66a9da1c22794185e66e848057d21e0c60d669203`。公开内容只包含原创场景、工具和事实。

## 观察方法

`research/ui/r1_fixture.py` 生成 80 行编号日志、内空格/尾空格、中文、NFD 组合标记及长行；随后只记录键盘输入，不执行粘贴命令。`r1_launch.py` 复用已有官方包哈希、fresh profile 和防止初次导入用户配置的检查；只启动新的副本，Job 在恢复主线程之前分配。即使 Job 分配失败，已创建的 suspended 进程仍通过自有句柄终止并等待。

原创 `qt_r1_text.js` 通过 REA 6.7.0 的公开 Frida 入口，在目标 UI 线程重放 Qt 事件，读出受控文本状态、焦点及 peer 接收结果。双击/三击使用鼠标事件；`selectWord()` / `selectLine()` API 的观察单独命名。所有调用只针对新建的合成配置，不附着用户程序。此方法不能代表物理输入或呈现性能。

## 已直接观察

同一最终脚本的两次本地 fresh profile 运行 `r1-local-05/06` 均通过导航/选择严格验证，Job `active=0`，配置检查只含一个合成会话。公开白名单记录：[第一次](r1-text/local-01.json)、[第二次](r1-text/local-02.json)。两次导航/选择记录逐项一致，系统剪贴板均明确为 `verified=false`、完整状态为 `complete=false`。原始记录保留在忽略的 `local/ui-reference/`。此前探索运行只帮助修正探针，不作为完整 R1-01 验收。

| 操作 / 前提 | 观察结果 | 适用边界 |
|---|---|---|
| Ctrl+Shift+Home / End | 滚动到历史顶部 / 底部 | 默认 local Shell；焦点在终端 |
| 历史顶部暂停后外部追加 20 行 | 可保留行数增加 20，视口保持 0 | 无键盘输入 |
| 暂停后输入普通 `x` | 无新增输出，视口回到末尾；peer 收到 `x` | 键入恢复跟随 |
| Shift+PgUp / PgDn | 视口不动，peer 收到 MSVCRT 扩展键码 | 不能列作默认历史快捷键 |
| Ctrl+Shift+F / Escape | 焦点进入查找 ComboBox / 返回终端 | 查找文字未进入 peer |
| 文档末尾输入 `needle` 后等待 | 增量查找自动选第 79 行匹配 | 80 行独立已知 oracle |
| 接着 Enter / F3 | Enter 到第 78 行；F3 到第 79 行 | Enter 默认向前，F3 向后 |
| F3 越过最后命中 / Shift+F3 | 循环到首个 / 从首个回到末个 | 单个会话保留历史 |
| 查找 `中文` | 选中两字，不损坏文本 | 字面量基本样例；不是全 Unicode 规范 |
| 正文双击 `TF-HISTORY-000` | 选中完整词，保留连字符 | 无修饰键鼠标事件 |
| 同点三击 | 选中整行，不包含末尾 LF | 与 `selectLine()` API 不同 |
| `selectLine()` API | 选择相同行，并包含末尾 LF | 不能用此 API 冒充三击 |
| 正向 / 逆向跨行拖选 | `ALPHA␠␠tail\nBETA 中文 é` | 内空格、中文、组合标记保留；逆向事件分帧发送 |
| 输入 `ALPHA␠␠tail␠␠␠` | 本地目标文本模型读回不含尾空格 | 尚不能归因到复制层 |

长行在参考本地 Windows 传输中变为物理行。本样例不能回答远端 VT 的逻辑软换行复制策略。

## 静态声明与未完成项

已读官方分发声明指向 Ctrl+Shift+C 复制、Ctrl+Shift+V / Shift+Insert 粘贴、右键显示菜单；默认主屏粘贴提示开启、alt-screen 提示关闭、选择后自动复制关闭。它们是静态线索，实际入口、确认以及系统 CF_UNICODETEXT 必须单独观察。

本机新建 window station 的实际 `OpenClipboard` 返回 error 5；Qt 缓存中的文字不能代替系统剪贴板。只有 `--hosted-ci` 且同时为 `GITHUB_ACTIONS=true` / `RUNNER_ENVIRONMENT=github-hosted` 时，允许使用一次性 GitHub-hosted VM 当前 station 的剪贴板，窗口仍在自有非输入 desktop。不提供普通本机当前 station 回退。

当前缺口：实际 CF_UNICODETEXT 的 CR/LF、粘贴两个键盘入口与取消/确认、完整两次重复运行待 hosted 执行。菜单入口和真正 VT 软换行未在本轮声称通过。保持 #11 开放直到基础核心语义获得证据。

## 自动复现与判定

`.github/workflows/r1-reference.yml` 仅手动 dispatch，由代理触发即可。Windows 2025 disposable job 下载并核验官方 ZIP/EXE，安装锁定 REA，串行执行两次新的合成配置。系统剪贴板不可与产品剪贴板测试并行。`r1_summarize.py --require-complete` 检查身份、阶段、独立内容预期、实际系统剪贴板、peer 字节及回收；任何缺口保持失败。

`python -m unittest research.tests.test_ui_r1 -v` 验证证据拒绝与 hosted 安全边界。设置 `TERMFORGE_R1_FAILURE_SOURCE` 为已校验的官方目录后，还会运行真实 suspended 进程的 Job 分配失败注入；本机 5 项测试全部通过，目标句柄已终止，空 Job 的 `active=0`。公开 artifact 仅 `local/r1-public/*.json` 白名单：原始 widgets、句柄、路径、stack、分发文件、配置和私有提取物都不上传。
