# Qt 基础动态网格与真实 child 尺寸

本结果仅覆盖 [#143](https://github.com/JavaZeroo/termforge/issues/143) 的基础窗口/核心/固定 ConPTY 尺寸同步，父项 [#29](https://github.com/JavaZeroo/termforge/issues/29) 继续负责完整 resize、历史与内容策略。初次冻结源码为 **`678299d54454e26ac04ef6d1c46237ab6b33752b`**，基于 `730404de` 开工并合入 main **`4d7bd8a`** 的 CRT/1049 基础；其离屏完整矩阵与随后原生失败均保留。夹具修订源码为 **`0ff783cb14f23dfa641b6b6f45894c038cca048c`**，生产实现没有变化。[原验证身份](validation.json)与[平台可用区域补充记录](platform-area-validation.json)分列，不拼接成一次验收。

## 原生反例与可用区域夹具修订

678 的 offscreen 完整 57 项通过后，同一 40 文件包在隔离 Win11 **26300.9711、Session 1、windows 插件**实际执行失败：root exit 1，非超时、无强制结束，v2 Job active 0 / total 8、cleanup true。80×24→60×12→100×30→80×24 正常；请求 1×24 时，事件派发前记录 widget 25×574、core 1×24，随后实际 HWND 外框宽 136px，Q107..234 共 128 次 child window/buffer 均为 **11×24**，最终独立尺寸 oracle 失败。135 条完整 child 回复及原生窗口记录均保留。此时不能把调用成功或初始 widget 请求当作稳定可用区域，也不能据此断言 transport 损坏；原记录没有最后的 core 观测，平台调整的因果仍待新原生记录确认。此环境与 CRT 原测试的 26300.9457 / Session 2 分列。

本地旧包曾在现有桌面上用 OwnedJob 复现 exit 1、Job 0/4/0，但遗漏独立 WindowStation/Desktop；这份记录明确带隔离缺口，不计隔离原生验证，后续本机只执行 offscreen。原包、原 full、失败 raw 均未覆盖。

0ff 仅修订测试：字面 80×24→60×12→100×30→80×24、1×24、80×1、实际 1px×1px 对应 1×1、回到 80×24 和重复，放入可见 owned parent 的子区域，避免顶层标题栏的最小宽度影响夹具输入。原字面预期、独立 nonce/CSBI、5 秒/128 条/10ms 查询预算及 25 秒外层监督均未改。另保留原独立顶层的一列请求，记录 requested pixels → 事件派发后的 actual widget area → 当前字体独立推导的 effective grid → core/transport/child；不把预期硬改成 11。超界 600×240 区域仍核对实际 core/child 500×200，改为可见 owner 内的大 child；5416×5326 的 child 只在 1200×900 owner 内可见，明确属于裁剪的网格上限验证，不计完整上屏。

修订后 targeted offscreen 1/1 通过；固定 0ff source/tree 后，**新的唯一完整矩阵 57/57、exit 0，13 条内部 skip**，与 678 的完整矩阵分开。7329 个冻结文件 post-run 零变化（source 2786，其他输入/运行时数量与旧记录一致）；原 Qt smoke Job 0/26/0、新 resize Job 0/27/0，均无超时/强制回收。主序列保留全部 12 条旧/新回复及 8 个 Qt 事件；2 MiB 输出实际读取 2,097,195 字节、队列 high-water 262,144、自然退出 37。CTest 302.43s、resize 11.01s 为诊断记录。新 [40 文件运行时清单](resize-child-region-runtime.json)及私有 ZIP 另绑 0ff。[平台区域补充记录](platform-area-validation.json)本身不含新源码 guest 运行；后续原生结果另列如下，不将离屏通过转记成原生成功。

0ff 完整矩阵结束、审计完成后再合入 main `ae985443` 的独立 SGR1 研究工具与离线测试；该合并没有 Qt/core/runtime 实现变更，未为它重跑产品完整矩阵。完整 57 项和新包身份均继续只属于 0ff；后续合并配置的 CI 和原生复验另列。

## 修订源码 0ff 的隔离原生验证

主代理在上述隔离 Win11 **26300.9711、Session 1**，对冻结的 0ff 40 文件 ZIP 只执行一次 `windows` 插件测试；[原生补充结果](native-validation.json)绑定包 SHA-256 `1b1ac682efbc32f3a48b57eef927f95ab293c2533b9444ea6b69d99273f93bde`，与完整离屏矩阵及发布合并配置分别记录。直接启动前核对保留 root handle 的 PID 和继承 Job，然后写入固定 `GO\n`；native root 预算 25 秒、共享 cleanup 5 秒、外层请求执行预算 40 秒，均未因原失败扩展。native 和外层请求 root 均 **exit 0、未超时、未强制结束**；外层 v2 Job active 0 / total 31 / after active 0，root reaped、cleanup verified。显式生命周期用例仍包含预期的 child 取消，不将其描述为所有子进程自然退出。

全部 **83 条有序结构化观察、40 条完整 child CSBI 回复、15 条可见 HWND 的尺寸/DPI 观察**经去除 PID 和 HWND 标识后公开。sample 的 14 条 ACK 只统计主序列，不是全部用例总数：

- 字面 owned child 区域的普通序列、1×24、80×1、实际 1px×1px、恢复及重复均通过，主序列派发 8 个 Qt 事件。
- 独立顶层仍保留请求 25×574px、初始 core 1×24；原生事件处理后实际区域 **120×574px**，当前字体独立推导为 **11×24**，随后 core、transport 完成及 nonce 1701 的真实 child window/buffer 同为 11×24。生产策略未改；新记录直接支持“顶层平台最小尺寸改变可用区域”的解释，旧 678 缺少最终 core 观测的事实仍保留。
- 上限用例实际 child/core 为 500×200；5416×5326px 的大 child 在本次原生 owner 中只可见 **1200×749px**，与离屏 1200×900 的观察分列，不称完整上屏。
- 隐藏页、survivor、字体变化、启动错误和 pending/关闭/自然退出交叉完成；自然退出竞态保留 core 请求 60×12、transport/child 旧 80×24，仅计生命周期。
- 2 MiB 用例实际读取 2,097,195 字节、队列 high-water 262,144，排空后自然退出 37；同尺寸 cmd 正文保留。夹具不逐字节核对完整 2 MiB payload，此结果不作完整负载正确性或性能验收。

执行前后 System32 所查五个 CRT DLL 均不存在，使用包内固定 app-local CRT。结束后的只读 guest 审计重新核对旧/新各 40 文件、原失败与新成功、请求 metadata 与实际 staged 脚本哈希，以及 pending 请求为零且无队列 quarantine；本地离线发布另核对 ZIP 全部成员、19 项 source blob 和 39 项原始/既有公开文件未变。原始脚本、请求、窗口标识和宿主路径留在私有目录，只公开合成数值及原件哈希。

本次观察到的窗口 **仅 DPI 96**。这是该冻结源码的有限原生窗口/真实 child 验证，不是完整不同 DPI、IME/Find/rename、物理输入、实际上屏、性能、产品发布或完整 #29/#31/R2 验收；`presented_done=null`。旧原生失败及本机未隔离桌面复现的缺口均未抹去。

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

## 初次源码 678 的唯一完整矩阵

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

源码 678 的 resize 独立运行时按冻结的 40 个文件逐项核对并打成私有 ZIP，包含自身 manifest；随后 `windows` 插件执行的失败已在上方及补充记录保留，不回填为本次 offscreen 57 项的成功。0ff 新包与其一次原生验证、合并配置 CI 和双轴审查继续使用各自身份。
