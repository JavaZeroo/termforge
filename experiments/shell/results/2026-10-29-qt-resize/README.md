# Qt 基础动态网格与真实 child 尺寸

本结果仅覆盖 [#143](https://github.com/JavaZeroo/termforge/issues/143) 的基础窗口/核心/固定 ConPTY 尺寸同步，父项 [#29](https://github.com/JavaZeroo/termforge/issues/29) 继续负责完整 resize、历史与内容策略。实际实现源码为 **`678299d54454e26ac04ef6d1c46237ab6b33752b`**，基于 `730404de` 开工并合入 main **`4d7bd8a`** 的 CRT/1049 基础后冻结；后续报告修改另作 docs 提交。[validation.json](validation.json)保留完整查询、各阶段源码/二进制及私有原始记录身份。

## 实现边界

窗口和标签页解除固定大小，使用原有 Roboto Mono 字体度量、八像素边距及一行状态区计算网格，沿现有 Screen 上限限制为 1..500 列、1..200 行。当前字体或像素比例事件会重新计算度量；本轮实际字体测试是同一字体 11pt→12pt，没有验证真实跨 DPI。

GUI 线程继续拥有 Parser/Screen。单个 latest-size 槽限制待处理控制请求，现有生命周期 worker 独占启动之后的 resize/wait/cancel/close；native 调用期间不持有 GUI 使用的控制锁，reader、输入 worker 和有界输出队列继续工作。同尺寸跳过重新初始化；关闭、错误和退出清除待处理请求，已选取的 native 操作由该 worker 收尾，状态不会重新接受请求。底层 Session API、旧 Win32/system 路线和实验后端未改成新的产品路线。

请求/core、transport 完成与 child ACK 是三个观测边界。测试 child 只收到 `Q nonce`，逐次调用真实 `GetConsoleScreenBufferInfo` 并返回 buffer、完整 window RECT、错误码及 nonce，不收到期待尺寸。独立命名管道绑定 server/client PID、session 和本 Session child，响应不受 1×1 屏幕容量限制；所有旧/新回复按序保留。调用成功、请求值或 CSI 窗口重绘均不作 child ACK。

## 红绿与已知失败

| 切片 | 真实红例 | 修订后的限定结果 |
|---|---|---|
| public Qt view → Core | `cd1b0b0` 请求 60×12，窗口仍 736×574、core 80×24 | `542d988` 窗口 556×310、core 60×12；该阶段尚未查询真实 child |
| Core → real child | `be1d393` 的 128 次独立查询全为 80×24，尽管 core 已是 60×12 | `e3abef5` 保留 Q101 的旧 80×24，以及 Q102 的新 60×12，buffer/window 同时匹配 |
| 非法尺寸与 pending | `038ff28` 拒绝零列后仍接受控制并保留 pending | `1c4deaa` 进入关闭，accepting/pending 均为 false，并完成回收 |
| 当前字体度量 | `b803319` 改为 12pt 后，请求 60 列却得到 66 列 | `82d97d1` 重读度量，core 60×12，并保留 child 旧值到新值的独立回复 |

前三组增量发生在 CRT 合并前的本机配置，字体组及最终配置使用合并后的固定 app-local CRT，分别绑定源码，未把不同配置拼成一组比较。前三组中的 native/error 二进制及字体二进制已私有冻结；最早 GUI-only 红绿 EXE 在覆盖前没有冻结，身份明确为 null，没有补造。一次新增测试编译失败来自字符串字面量的 `send` 重载歧义，改成显式 `std::string`；它不计产品行为红例。

## 最终一次完整矩阵

环境为 Windows 11 Pro 10.0.26200 x64、VS 2022 / VC Tools 14.42.34433（compiler self 19.42.34433.0）、CMake 3.31.0、Python 3.12.10、Release。Qt 6.10.3 官方 MSVC 2022 x64 SDK、固定 ConPTY 1.25.260930003 与 CRT 14.51.36247.0 均在本工作树全新离线准备；candidate adapter 关闭，Qt 使用 **offscreen**。

build-all 成功后，干净的实现 commit/tree 与运行时一起冻结，再执行唯一完整 CTest **57/57、exit 0**；7324 个冻结文件在结束后逐一复核，零变化：source 2781、Qt SDK 4358、ConPTY 输入 2、CRT 输入 12、原始归档/许可 4、产品运行时 38、原 Qt smoke 40、resize smoke 40、既有诊断运行时 49。三个 Qt 数量包含其自身 manifest；[产品清单](product-runtime.json)、[原 smoke 清单](shell-smoke-runtime.json)、[resize 清单](resize-smoke-runtime.json)分别绑定除清单自身外的 37/39/39 文件。

Qt 两项与其余 55 项既有/独立诊断分列：

- 原 `qt_cpp_shell` 的 `750/427/778/147`、退出 37/41、错误墨迹、标签输入/关闭隔离及真实 `app.exec()` 退出前回收原样通过；外层 Job active 0 / total 26 / terminated 0。
- `qt_cpp_resize` 覆盖初始 80×24→60×12→100×30→80×24，以及 1×24、80×1、实际 1×1、回到 80×24、重复；每个稳定尺寸点都保留独立 child window/buffer ACK。另验证可容 600×240 格的区域受既有上限约束，实际 core/初始 child 为 500×200。
- 主序列实际派发 8 个 Qt 事件；隐藏页保持自己的旧尺寸值，显示/layout 后同步。关闭该页后 survivor 可继续查询并调整到 100×30，最终独立回收。
- 覆盖启动后立即关闭、已观察到 pending 时关闭、pending 中自然退出、启动失败与非法尺寸。自然退出竞态中特意保留 **core 请求 60×12、transport/child 仍 80×24** 的结果；它验证退出会清掉未应用请求，不计作尺寸同步通过。
- 有限 2 MiB ASCII 输出与尺寸请求交叉，实际 transport 读取 2,097,195 字节，输出队列 high-water **262,144** 达到既有容量后仍可排空，自然退出 37。重复同尺寸后真实 cmd 算术正文 `918` 保留。resize 根 exit 0，Job active 0 / total 25 / terminated 0，无超时/强制回收。

完整矩阵内部 **13 条 skip** 原因逐项保留，包括参考组件未构建、须一次性环境的真实 CF/剪贴板、opt-in 失败注入，以及 **OpenClipboard denied (5)**。其私有诊断原件身份保留，只删除公开文本中的绝对路径。跳过项不算系统剪贴板或新 Qt 能力通过。CTest 总耗时 301.08s、resize 8.94s 只是原始诊断记录，不是性能指标或批准的门槛。

两个 Qt 测试仍由原监督器在 `GO\n` 前纳入 OwnedJob，整体期限 25 秒、CTest 35 秒；新尺寸阶段 5 秒/最多 128 条回复及 10ms 非匹配查询节奏在执行前固定，没有修改旧算术/退出 oracle 或延长期限取绿。全部原始回复、失败样本及回收状态分列。

## 保持开放的范围

Screen 继续采用现有物理行裁剪，不回填历史、恢复被裁掉的内容或宣称目标 reflow 已实现。完整 WindTerm 尺寸/样式对照、真实 TUI、Unicode/IME/选择/Find、实际不同 DPI、物理键盘与呈现、性能、完整 #29/#31/R2 均未由本项验收，`presented_done=null`。本机既有诊断通过也不解释或修复 hosted system 路线的 [#80](https://github.com/JavaZeroo/termforge/issues/80)、[#144](https://github.com/JavaZeroo/termforge/issues/144)、[#145](https://github.com/JavaZeroo/termforge/issues/145) 原失败。

源码 678 的 resize 独立运行时已按冻结的 40 个文件逐项核对并打成私有 ZIP，包含自身 manifest，供后续 `windows` 插件原生确认；本轮没有执行该包或操作 guest。那份补充结果、CI 和双轴审查由 PR 接续，不回填为本次 offscreen 57 项的成功。
