# 正式 Qt / 固定 ConPTY：八会话文本输入目标隔离与生命周期（#30 切片）

本切片在**正式 C++ / Qt Widgets / 随包固定 Microsoft ConPTY** 构建中，用现有公开 `+` / 标签切换 / 关闭入口创建 **8 个真实 child**，逐个会话用独立 PID、nonce 与接收文件核对：文本与既有无修饰导航键只进入活动目标；可控有界后台输出期间后台 child 继续排空且不夺取输入目标；按非创建顺序切换仍保持目标隔离；关闭非活动/活动标签后存活目标继续接收；替代页不继承已关闭会话的待发 UTF-16 单元；父窗口关闭后所有 child 完成回收。

不修改产品行为，不新增会话上限、Ctrl 字母、应用模式或字素宽度策略；不重复 #313 的 Unicode 呈现/glyph 实验。

## 入口与协议

- 创建：`SessionTabs` 构造创建首个会话，其余 7 个通过公开 `QToolButton`（`newCmdSession`）点击创建；切换/关闭使用公开 `QTabBar` 点击与关闭按钮。没有直接拼装 8 个 model 参与验证。
- 每个 child（`experiments/shell/eight_session_client.cpp`）打开 `CONIN$`/`CONOUT$`，设置 UTF-8 与 VT 输入/输出模式，生成进程内 **nonce**，在 `TF8_READY pid=<pid> nonce=<16hex>` 中打印身份，并创建 `<temp>/<nonce>.bin` 接收文件；测试同时用 `model.resize_state().child_pid` 绑定 PID。
- 接收：child 按批把 `ReadConsoleW` 得到的完整标量转成 UTF-8 追加写接收文件并 flush；测试只比较**硬编码字面预期**（ASCII/CJK/补充平面/结合标 + Home `1b5b48`），不从产品输出生成 expected。
- 受控后台：测试向活动目标发送字面标记 `*BG*`（夹具控制协议，不写入接收文件），child 启动一次**固定 120 行**、每行约 10 ms 的有界后台输出 `TF8_BG pid=... nonce=... seq=NNN`。
- 不提权、不访问个人剪贴板、不使用 OS IME；全部为合成 Qt 事件 + offscreen 平台。

## 实测结果（一次完整运行；连跑两次均通过）

环境：Windows 11 x64 build 26200，MSVC 19.42.34433.0，Qt 6.10.3 offscreen，随包固定 ConPTY 1.25.260930003，app-local CRT 14.51.36247.0；`cpp-core / qt-widgets / reviewed-side-by-side-1.25.260930003 / 80x24`。

创建矩阵（每项独立 PID/nonce/接收文件）：

| creation | PID | nonce | 接收文件 | 结果 |
|---|---|---|---|---|
| 0 | 43348 | `08eaee5cc32f221f` | `08eaee5cc32f221f.bin` | 输入后关闭（pending 目标），READY≈41 ms |
| 1 | 20808 | `08ea16595fcd224e` | `08ea16595fcd224e.bin` | 存活至父窗关闭，READY≈98 ms |
| 2 | 39768 | `08eadc42011f228c` | `08eadc42011f228c.bin` | 非活动标签关闭，READY≈151 ms |
| 3 | 8276 | `08ea677636ff22bb` | `08ea677636ff22bb.bin` | 活动标签关闭，READY≈205 ms |
| 4 | 13248 | `08ea74ea6ead22fa` | `08ea74ea6ead22fa.bin` | 存活，READY≈259 ms |
| 5 | 26452 | `08ea206652522329` | `08ea206652522329.bin` | 存活，READY≈312 ms |
| 6 | 27920 | `08ea2a2aa56b2367` | `08ea2a2aa56b2367.bin` | 存活，READY≈367 ms |
| 7 | 10396 | `08ea6fdec4a22396` | `08ea6fdec4a22396.bin` | 存活，READY≈421 ms |
| 8（替代页） | 19492 | `08ea04e4b4c22d5a` | `08ea04e4b4c22d5a.bin` | 首个输入仅 `NEW`+Home，READY≈2922 ms |

逐会话隔离与字面预期（节选，均为实际 receiver hex＝预期 hex）：

- 创建顺序切换 8 次（首个样本步起始≈434 ms，末个≈845 ms；每步接收校验 2–5 ms）：每次只向当前活动目标发送 `*BG*`+样本+Home；ASCII `TF8-A0 ASCII`、CJK `汉字`、补充平面 `🙂`、结合标 `e`+U+0301、`é`、`终端`、`😀`、`plain` 的接收字节与字面预期逐字节一致。
- 非创建顺序切换 `[4, 0, 7, 2, 5, 1, 6, 3]`：每步追加 `B<n>`+Home，8 个 receiver 均精确匹配，且每次仅目标文件变化（`isolation_checked=true`）。
- 后台有界输出：8 个 child 全部到达 `seq=120`、`queued_bytes=0`（high-water 96–98 B），即后台输出被排空；期间输入只进入活动目标。
- 非活动关闭（creation 2, PID 39768）：43 ms 内标签 8→7、页面释放；存活目标（creation 3）继续收到 `+CLOSE`+Home 且字节精确。
- 活动关闭（creation 3, PID 8276）：30 ms 内标签 7→6、页面释放；新活动目标（creation 4）继续收到 `+ACTIVE`+Home 且字节精确。
- 待发文字隔离：对 creation 0 发送孤立高代理项（未产生字节，文件不变），关闭该标签（38 ms，6→5）后点击 `+` 创建替代页；替代页首个输入接收为 `4e45571b5b48`（`NEW`+Home），**无 U+FFFD 前缀或旧待发单元泄漏**，旧文件保持不变。
- 父窗口关闭：6 个存活会话全部 `finished=true`、`cleanup_verified=true`、`active_processes_after=0`、`output_eof=true`；`app.exec()` 经 `aboutToQuit`/`on_all_exited` 完成，status=0、completions=1，关闭时刻与完成时刻两处清理检查均为 true。外层 OwnedJob supervisor 记录 `job_after={active:0,total:20,terminated:0}`。
- 总运行约 2.93 s（CTest `qt_eight_session` 3.20 s，连跑两次均通过）。

字体/呈现与 glyph 不在本切片范围；`presented_done=null`，测试不抓图、不比较像素。

## 现有 8 会话基础与限制位置

`SessionTabs`（`src/qt/session_tabs.hpp`）当前**没有新增会话计数上限或资源阈值**，公开 `+` 直接调用 `new_session`；因此本切片只验证“现有契约允许的 8 个并发会话”，不把第 9 个拒绝行为、最大会话数或资源政策当作已批准用户决定，也未测试第 9 个。Win32 R1 的 8 会话/拒绝第 9 个是另一配置的历史合同，不转记为 Qt 结论。若后续要定义上限，属新行为，需要单独确认。

## 复验

```powershell
python -m tools.prepare_qt_build
cmake -S . -B build/qt-cpp -G "Visual Studio 17 2022" -A x64 `
  -DTERMFORGE_BUILD_QT_SHELL=ON -DBUILD_TESTING=ON `
  -DTERMFORGE_BUILD_CANDIDATE_ADAPTER=OFF `
  -DQt6_DIR="$PWD/local/qt-build-deps/qt/lib/cmake/Qt6" `
  -DTERMFORGE_CONPTY_BACKEND_DIR="$PWD/local/qt-build-deps/conpty" `
  -DTERMFORGE_MSVC_RUNTIME_DIR="$PWD/local/qt-build-deps/crt"
cmake --build build/qt-cpp --config Release --target qt_eight_session_test --parallel 2
ctest --test-dir build/qt-cpp -C Release -R '^qt_eight_session$' --no-tests=error --output-on-failure
```

同源码回归：Qt 正式路径 CTest 12/12（含新测试，去除需人工剪贴板的 hosted clipboard 两项）；默认/核心 CTest 与 Python Unicode 回归见 [validation.json](validation.json)。完整源码/二进制/运行文件身份见 `validation.json`。

## 边界

- 仅 offscreen + 合成 Qt 事件，不是真实物理键盘、原生 IME、真实系统剪贴板或物理呈现；不宣称完整 Copy→Paste→IME、R2、#30 或 #155 通过。
- 只覆盖固定 8 会话 + 1 替代页、单窗口、单显示器、`80×24`，不含分屏、八会话以上、长期运行、其他 shell、真实 DPI 或性能；后台输出为固定 120 行合成负载，不是吞吐指标。
- 接收 oracle 是每个 child 自己写入的 UTF-8 文件 + READY 身份；测试不使用内部 model 文本自洽替代独立 receiver。`*BG*` 是夹具控制协议，不是产品文本/按键行为。
