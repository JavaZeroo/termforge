# C++ / Qt 本地会话迁移入口

按 [ADR-0008](../../docs/adr/0008-cpp-core-qt-widgets.md)采用自主 C++ 核心和 Qt Widgets，Windows 进程由[固定微软 ConPTY](../../third_party/conpty/README.md)承载。当前入口提供默认 80×24、随窗口可用区域变化的本地 cmd 标签、基本 ASCII 按键、16 色绘制、启动错误与关闭回收；不是完整 R2 或 WindTerm 界面。

现有 C++ 核心的[能力限制](../../experiments/core/README.md)继续有效。Qt 尚未迁移原 Win32 的完整工作区、分屏、历史浏览、选择/复制、Find、完整键盘/IME 或会话恢复；完整 resize/reflow 仍需按目标调查和实现，也不能继承 Alacritty 分支的通过结果。

网格沿当前 Roboto Mono 字体度量和终端可用区域计算，保留八像素边距与一行状态区，并沿现有 Screen 资源上限限制为 1..500 列、1..200 行。字体变化重新计算度量；隐藏标签在显示/布局后同步。GUI 线程负责物理 Screen resize，现有生命周期 worker 用单个最新请求串行执行 ConPTY resize、wait、cancel 和 close；GUI 不等待 native resize。重复同尺寸不重新初始化屏幕或发起新尺寸操作；关闭、错误与退出停止接受并清除待处理请求，已选取的 native 操作由同一 worker 收尾，不重新激活会话。原物理裁剪不回填历史或恢复被裁掉的正文，目标 reflow 和 WindTerm 内容恢复策略未验收。

标签栏的 `+` 创建同类型 cmd，点击标签选择输入目标，关闭按钮只取消对应会话；关闭父窗口取消所有会话。后台标签继续独立排空输出，已关闭标签完成自有进程回收后释放。成功启动的最后会话结束时沿用原最小入口的应用退出路径；有失败启动页时保留该页错误供关闭。当前标签外观、默认动作和多会话退出汇总仅为这批迁移入口的工程合同，未作 WindTerm 完整标签行为对比。

产品入口禁用 Qt 的最后窗口自动退出。关闭父窗口或最后标签后，事件循环继续排空可见及已移除会话，等模型结束并核对回收后统一退出；用户关闭的成功状态为 0，已结束但回收无法验证时为 1，不等待一个不会再变化的验证标志。自然结束继续返回最后会话状态；启动失败页在用户关闭前保持可见。

## 构建与运行

按 [Qt 依赖说明](../../third_party/qt/README.md)准备 Qt 6.10.3 / MSVC 2022 x64 qtbase、固定 ConPTY 文件对和 [app-local 微软 CRT](../../third_party/msvc_runtime/README.md)：

```powershell
cmake -S . -B build/qt-cpp -G "Visual Studio 17 2022" -A x64 `
  -DTERMFORGE_BUILD_QT_SHELL=ON `
  -DTERMFORGE_BUILD_CANDIDATE_ADAPTER=OFF `
  -DQt6_DIR="<Qt SDK prefix>/lib/cmake/Qt6" `
  -DTERMFORGE_CONPTY_BACKEND_DIR="<absolute fixed ConPTY pair directory>" `
  -DTERMFORGE_MSVC_RUNTIME_DIR="<absolute prepared CRT directory>"
cmake --build build/qt-cpp --config Release --target termforge_qt_shell qt_cpp_shell_smoke qt_cpp_resize_smoke
build/qt-cpp/qt-bin/Release/termforge_qt_shell.exe
ctest --test-dir build/qt-cpp -C Release -R '^qt_cpp_(shell|resize)$' --output-on-failure
```

固定 ConPTY/CRT 输入缺失或篡改时构建明确失败；实际工具集更新到固定 CRT 之外时须重新审查依赖。产品中的 shell 启动失败保留可关闭的错误窗口。随目录保留 Qt 动态运行库、平台插件、`conpty-backend/`、`assets/fonts/`、十个固定 CRT DLL 及其原许可/NOTICE；每次构建重新校验并部署 CRT，不运行安装器、不修改 System32。最终目录用 `tools/msvc_runtime.py manifest` 记录全部文件。当前只是本地构建输出，Qt/第三方的完整分发审计及产品打包尚未完成。

## 独立验证面

Parser/Screen 仅在 Qt 线程修改；ConPTY reader 向有界输出队列写入，输入由独立有界 worker 发送。自然退出等待子进程、reader EOF、输出排空和 Parser.finish；人工关闭按取消处理。外层监督器在 GO 前把测试根进程纳入自有 Job，超时或残留不能通过。

`qt_cpp_shell` 保留单会话真实 cmd 的 `750` 正文三格新增墨迹、自然退出 37、启动失败可见错误与人工关闭检查。多标签部分实际点击新增、切换及关闭，再核对独立字面量 `750` / `427` 与父窗口中的正文墨迹、输入隔离、关闭后存活页的 `778` 与自然退出 41；失败页关闭后存活页继续得到 `147`，父窗口关闭回收所有会话，单个失败启动页保持可见。外层自有 Job 和原有期限不变。

退出路径另在真实 `app.exec()` 中覆盖双会话父窗口关闭、最后活动标签关闭、单个失败启动页关闭，以及已自然结束标签关闭。在统一完成回调、`aboutToQuit` 和 `exec` 返回时分别核对模型已结束、回收可验证和各自 Job active=0；已移除页的记录在其销毁前取得，不用析构后的外层 active=0 代替退出前证明。

独立的 `qt_cpp_resize` 在同一 25 秒 OwnedJob 监督合同内验证 80×24→60×12→100×30→80×24、极窄/极短/1×1、重复、资源上限、字体变化、隐藏页、关闭隔离及尺寸请求与退出交叉。测试 child 用各次 nonce 通过独立命名管道返回真实 `GetConsoleScreenBufferInfo` 的 buffer/window 值，避免极小屏幕容不下回复；保留每条旧值与新值。`resize_state()` 的 requested/core 与 transport 完成不是 child ACK；正常退出可丢弃尚未应用的请求。有限 2 MiB ASCII 输出同时经过有界队列，核对 GUI 事件、实际读取、排空及回收。[固定结果与原失败](../../experiments/shell/results/2026-10-29-qt-resize/README.md)不外推为原版 reflow、真实不同 DPI、物理呈现或性能验收。

字面网格测试使用可见父窗口内的终端子区域；独立顶层测试保留一列请求并根据原生事件处理后的实际可用区域核对 core 和 child，记录平台调整差异。大 child 的资源上限验证保留 owner 裁剪范围。旧 678 离屏通过后的原生极窄失败及新夹具离屏验证分别保留，新源码原生结果须独立接续。

该测试使用 Qt offscreen 平台和本项目独立取得的 OFL Roboto Mono 字体；没有物理键盘、实际不同 DPI、Windows 原生窗口上屏或用户呈现时间验收，`presented_done=null`。两会话隔离不代表八会话、IME 或完整工作区验收，测试耗时不是终端性能指标。

构建开关默认关闭只是从旧原型向新入口迁移的状态；正式路线已确认，旧构建保留为回归对照。Qt 文字与图形资源均独立取得，不使用 WindTerm 的专有字体、图标或主题。
