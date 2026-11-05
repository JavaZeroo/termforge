# 正式 Qt / 固定 ConPTY：保留事务 + 独立显示投影 + 显示光标

本切片把 #27/#29 的核心保留入口接入**正式 Qt 路径**：`SessionModel::resize` 改用 `Screen::resize_preserving`，`TerminalWidget` 按当前宽度绘制独立显示投影页，并把协议光标翻译为显示光标（IME 候选位置、光标下划线、预编辑预览同源）；已有选择/命中继续消费同一份 `ViewMapping`，使用页面布局 revision 拒绝过期页。核心与协议坐标不被显示层修改。

原六阶段 B/CD 失败判据保持：显式 `--resize` 诊断仍以 exit 1 失败，失败信息不变。新证据说明该断言比较的是**原生 ConPTY 重排网格**，而 #27/#29 已确认原版 CUP/DSR/光标寻址跟随保留的缩放前行；两者在缩窄+缩高后不是同一坐标层。内容不再被物理裁剪（B/C/D 保留在保留帧/历史），剩余差异是原生网格与保留行合同的坐标差，需要同时缩宽+缩高后的原版对视口/滚动与后续 CUP 的独立观察才能定案；本切片不据此改写旧失败。

## 配置与身份

- Windows 11 x64、Visual Studio 17 2022 17.12.6、MSVC 19.42.34433.0、Qt 6.10.3 offscreen、随包固定微软 ConPTY 1.25.260930003、app-local CRT 14.51.36247.0；固定后端身份守卫、自有 Job、25 s 监督期限不变。
- 基线 `7fea912beb8e23cb8e1ef783970475d111dab0ed`（含 #308/#309 的 main）。依赖准备摘要 `local/qt-build-preparation.json`：Qt archive `4db84dee…`，输入清单 qt `2796294f…`、conpty `e75e522a…`、msvc_runtime `154176c8…`；本报告不发布依赖包本体。
- 实际运行产物与源码 SHA-256 见 [validation.json](validation.json)；以下是本工作区定向构建的结果，不是完整工作树冻结清单。

## 结果

| 入口 | 实际结果 | 口径 |
|---|---|---|
| 新 CTest `qt_terminal_modes_retained`（`--retained-contract`） | **Passed，12/12 契约检查，0 失败**；child 自然退出 37、Job `active0/total4/terminated0` | 真实 child → 固定 ConPTY → SessionModel/TerminalWidget；公开 Qt resize 到 60×24（只缩宽）后由 child 写 `RESIZED` 与 `S6` |
| Qt 正式路径回归 `qt_cpp_shell` / `qt_unicode_input` / `qt_cpp_resize` / `qt_terminal_modes` / `qt_terminal_modes_retained` | **5/5 通过，9.31 s** | 同一 offscreen 配置；`qt_cpp_resize` 覆盖窗口/核心/transport/真实 child 尺寸与隐藏标签 |
| 核心 CTest（含新 `display_cursor_core`） | **36/36 通过，4.51 s** | 新增显示光标契约测试；原 35 项未改 |
| 旧六阶段诊断 `--resize`（显式、不进 CI） | **exit 1，失败保留**：`stage 6 failed: core resize is not synchronized with the literal native B/CD rows` | 与历史失败同一断言；未弱化、未设 `WILL_FAIL`、未用作通过 |

新入口的可观察事实（来自 `termforge-qt-retained-contract-v1` 记录与 stage 记录）：

- 核心在 60×24 缩宽后保留旧源行：第 7 行第 79 列 `B`、第 9 行第 79 列 `C`、第 10 行第 0 列 `D` 仍在 `visible_lines()`；`history` 为空（只缩宽不裁剪）。
- 协议光标仍为源坐标 `[0,2]`；显示光标为 `[0,2]`（该位置在当前重排下同格）。
- 显示页按 60 列重排：第 8 行 = 19 空格 + `B`（与原生第 8 行逐字节一致）；第 11 行 = 19 空格 + `CD`；第 12 行 = `RESIZED`。
- 命中测试：显示页 `(8,19)` 经 `ViewMapping::hit(..., layout_revision)` 返回保留源行 `B` 的 `(line id, column 79)`，即选择/命中与重排后的显示一致。
- 原生记录分开保留：原生第 8 行同为 19 空格 + `B`；原生第 11 行为 `RESIZED` + 12 空格 + `CD`，而产品按保留行合同把 `RESIZED` 画在显示第 12 行。差别来自 child 的 CUP 指向其**自身重排网格**，不是产品裁剪。

## 与旧六阶段失败的关系

旧断言把“原生重排网格的 B/CD 行”当作产品应同步的目标。加入 #308（宽高组合保留）与 #309（缩窄后 CUP/DSR 跟随保留源行）后，这一目标与已确认的原版坐标层冲突：原生 ConPTY 在 60×12 后把旧正文压缩进可见 12 行，并按新网格解释后续 CUP；产品保留源行、由显示投影重排，`CUP 12;1` 落在保留第 11 行（显示第 11/12 行取决于高度策略）。本次同一次运行因此得到：

- 旧 `--resize`：仍 exit 1（`core resize is not synchronized with the literal native B/CD rows`），旧失败身份保留；
- 内容保留：宽度+高度收缩后 B/C/D 在保留帧/历史中可查，不再因 `line.resize` 丢字；
- 显示链：只缩宽时产品显示与原生重排的 B 行逐字节一致，`CD` 行同样重排，`RESIZED` 的差异可归因于 CUP 坐标层与高度策略。

**下一步的真实前置**（未在本切片完成）：固定原版 WindTerm 2.7.0 在**同时缩宽+缩高**后，视口/滚动如何选择、后续 CUP/DSR 与显示如何对应；有了该证据才能决定六阶段是否应改用保留行合同断言，或把原生网格差异写入已知差异清单。该前置不能靠重跑旧断言或弱化断言取得。

## 复现命令

```powershell
cmake -S . -B build/qt-cpp -G "Visual Studio 17 2022" -A x64 `
  -DTERMFORGE_BUILD_QT_SHELL=ON -DBUILD_TESTING=ON -DTERMFORGE_BUILD_CANDIDATE_ADAPTER=OFF `
  -DQt6_DIR="<prepared>/qt/lib/cmake/Qt6" `
  -DTERMFORGE_CONPTY_BACKEND_DIR="<prepared>/conpty" `
  -DTERMFORGE_MSVC_RUNTIME_DIR="<prepared>/crt"
cmake --build build/qt-cpp --config Release --target qt_terminal_modes_test qt_cpp_resize_smoke qt_cpp_shell_smoke qt_unicode_input_test --parallel 3
ctest --test-dir build/qt-cpp -C Release -R '^(qt_cpp_(shell|resize)|qt_unicode_input|qt_terminal_modes(_retained)?)$' --output-on-failure
# 旧失败保留（显式，不注册 CI）
python src/qt/run_smoke.py build/qt-cpp/qt-modes-bin/Release/qt_terminal_modes_test.exe --resize
# 新契约记录
python src/qt/run_smoke.py build/qt-cpp/qt-modes-bin/Release/qt_terminal_modes_test.exe --retained-contract
```

## 边界

- 单显示器、offscreen、固定 80×24→60×24 缩宽与 60×12 旧诊断；不覆盖真实物理呈现、跨 DPI、历史浏览滚动、系统剪贴板/Find、完整 reflow、备用屏、宽字/结合字在 resize 后的完整交叉或性能。
- 显示投影对不支持的保留 Cell 与无法映射的光标回退到物理视图页（有界、不隐藏输出）；回退路径未做单独原版对照。
- 本切片不关闭 #27/#29，不声明 WindTerm 等价、性能等价比或 R2 完成；合并由用户/调度者决定。

**2026-11-05 后续：** 已补同一次 `SessionModel` 会话的六阶段 trace，首次断言失败定位在 stage 6；resize 后 transport 只有阶段 6 源与 DSR 查询，无 B/C/D 搬移/擦除字节；resize 完成到 stage 6 ACK 之间的原生网格未采。见[同次分歧](../2026-11-05-qt-resize-divergence/README.md)；本报告旧失败、身份和边界不变。


