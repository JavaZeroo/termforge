# C++ / Qt 本地会话迁移入口

按 [ADR-0008](../../docs/adr/0008-cpp-core-qt-widgets.md)采用自主 C++ 核心和 Qt Widgets，Windows 进程由[固定微软 ConPTY](../../third_party/conpty/README.md)承载。当前入口提供固定 80×24 的本地 cmd 标签、基本 ASCII 按键、16 色绘制、启动错误与关闭回收；不是完整 R2 或 WindTerm 界面。

现有 C++ 核心的[能力限制](../../experiments/core/README.md)继续有效。Qt 尚未迁移原 Win32 的完整工作区、分屏、历史浏览、选择/复制、Find、完整键盘/IME、动态 resize 或会话恢复，也不能继承 Alacritty 分支的通过结果。

标签栏的 `+` 创建同类型 cmd，点击标签选择输入目标，关闭按钮只取消对应会话；关闭父窗口取消所有会话。后台标签继续独立排空输出，已关闭标签完成自有进程回收后释放。成功启动的最后会话结束时沿用原最小入口的应用退出路径；有失败启动页时保留该页错误供关闭。当前标签外观、默认动作和多会话退出汇总仅为这批迁移入口的工程合同，未作 WindTerm 完整标签行为对比。

## 构建与运行

按 [Qt 依赖说明](../../third_party/qt/README.md)准备 Qt 6.10.3 / MSVC 2022 x64 qtbase 和固定 ConPTY 文件对：

```powershell
cmake -S . -B build/qt-cpp -G "Visual Studio 17 2022" -A x64 `
  -DTERMFORGE_BUILD_QT_SHELL=ON `
  -DTERMFORGE_BUILD_CANDIDATE_ADAPTER=OFF `
  -DQt6_DIR="<Qt SDK prefix>/lib/cmake/Qt6" `
  -DTERMFORGE_CONPTY_BACKEND_DIR="<absolute fixed ConPTY pair directory>"
cmake --build build/qt-cpp --config Release --target termforge_qt_shell qt_cpp_shell_smoke
build/qt-cpp/qt-bin/Release/termforge_qt_shell.exe
ctest --test-dir build/qt-cpp -C Release -R '^qt_cpp_shell$' --output-on-failure
```

产品入口缺少固定包时明确报错；启动失败保留可关闭的错误窗口。随目录保留 Qt 动态运行库、平台插件、`conpty-backend/` 与 `assets/fonts/`。当前只是本地构建输出，Qt/第三方与运行库的完整分发审计、干净机器验证及产品打包尚未完成。

## 独立验证面

Parser/Screen 仅在 Qt 线程修改；ConPTY reader 向有界输出队列写入，输入由独立有界 worker 发送。自然退出等待子进程、reader EOF、输出排空和 Parser.finish；人工关闭按取消处理。外层监督器在 GO 前把测试根进程纳入自有 Job，超时或残留不能通过。

`qt_cpp_shell` 保留单会话真实 cmd 的 `750` 正文三格新增墨迹、自然退出 37、启动失败可见错误与人工关闭检查。多标签部分实际点击新增、切换及关闭，再核对独立字面量 `750` / `427` 与父窗口中的正文墨迹、输入隔离、关闭后存活页的 `778` 与自然退出 41；失败页关闭后存活页继续得到 `147`，父窗口关闭回收所有会话，单个失败启动页保持可见。外层自有 Job 和原有期限不变。

该测试使用 Qt offscreen 平台和本项目独立取得的 OFL Roboto Mono 字体；没有物理键盘、实际不同 DPI、Windows 原生窗口上屏或用户呈现时间验收，`presented_done=null`。两会话隔离不代表八会话、IME 或完整工作区验收，测试耗时不是终端性能指标。

构建开关默认关闭只是从旧原型向新入口迁移的状态；正式路线已确认，旧构建保留为回归对照。Qt 文字与图形资源均独立取得，不使用 WindTerm 的专有字体、图标或主题。
