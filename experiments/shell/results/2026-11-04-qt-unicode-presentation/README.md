# 正式 Qt / 固定 ConPTY：固定 Unicode 样本贯通 Qt 呈现与独立 child 接收（#30 切片）

本切片在**同一个正式 C++ / Qt Widgets / 随包固定 Microsoft ConPTY 构建**里，把一组有界固定 Unicode 样本从 Qt 输入一路贯通到中文：真实 child 的独立字节接收、child 回显、核心解析、`TerminalWidget` 实际绘制，以及离屏抓图后的字形/fallback/落格测量。它补的是当前**未覆盖**的呈现合同：既有 Qt 测试只核对 `750`/`427` 等 ASCII 墨迹或核心文字，`2026-10-26-unicode-renderer` 是历史 Win32/DirectWrite 路径，`qt_unicode_input` 只核对输入字节，均不核对本正式路径的 Unicode 绘制。

新增 CTest `qt_unicode_render`；不关闭 #30/#155/#7，不确认新的字素/宽度策略。

## 链路与固定样本

```
公开 Qt 文本/焦点事件 → 既有 KeyboardEncoder → 固定 ConPTY →
child `ReadConsoleW` 精确接收（receiver 文件 = 独立字节 oracle）→
child 以 CR→CRLF 回显同一字节（echo 文件 = 呈现源 oracle）→
SessionModel parser → Screen cells → TerminalWidget 绘制 → 离屏 QImage 抓图
```

- 样本集固定 5 条（字面 UTF-8，逐字节核对）：ASCII `TermForge ASCII`；CJK `汉字终端`；补充平面 `🙂😀`（U+1F642/U+1F600）；结合标 `e`+U+0301；仅记录项 `👩‍💻1️⃣`（U+1F469 U+200D U+1F4BB U+0031 U+FE0F U+20E3）。
- 前 4 条是断言合同；ZWJ/keycap 只记录实际结果，不批准聚类或宽度策略（#39 未决）。
- 第二条独立会话不接收任何样本，用于同时观察目标隔离：其 receiver 为 0 字节，屏幕不出现样本文字，child pid 与主会话不同。

## 实际结果（本次运行）

环境：Windows 11 x64 build 26200，MSVC 19.42.34433.0，Qt 6.10.3 offscreen，随包固定 ConPTY 1.25.260930003，app-local CRT 14.51.36247.0，Python 3.12.10。`TerminalWidget` 80×24、单元 9×22 px；产品字体 Roboto Mono（SHA-256 `af0bff75…`）。

| 样本 | 独立 child 接收 / 回显 | 核心解析结果（实测） | 实际字形与 fallback | 绘制/落格 |
|---|---|---|---|---|
| ASCII | 61B 精确；receiver `32a5695a…`、echo `96ae6f16…` | 15 个 width=1 cell | `Roboto Mono`，advance 9 = 单元 9 | 每格有墨迹，墨迹落在各自 cell 内 |
| CJK | 同上一条链路，逐字节一致 | 4 个 width=2 cell + 4 个 trail | `Alibaba PuHuiTi`（宿主 fallback），advance 15 ≤ span 18 | 每格有墨迹（0..14 px），未越格 |
| 补充平面 | 逐字节一致 | 2 个 width=2 cell + 2 个 trail | `Segoe UI Emoji`，advance 21 > span 18 | 实测墨迹 3..17 px；同字体无裁剪参考渲染墨迹同为 3..17，**本样本未被可见裁切**；advance 超出仅登记 |
| 结合标 | 逐字节一致 | 1 个 cell，text=`e`+U+0301，width=1，marks=1 | `Roboto Mono`，advance 9 = 单元 9 | 墨迹 1..7 px，未越格 |
| ZWJ/keycap（仅记录） | 逐字节一致（含 U+200D/U+FE0F） | **U+200D、U+FE0F 被 recovery parser 替换为 U+FFFD**；解析为 `👩`+FFFD+`💻`+`1`+FFFD+U+20E3，5 个 lead cell | `Segoe UI Emoji`（两个 emoji）、`Roboto Mono`、U+FFFD+U+20E3 处为 `Roboto Mono`+`Microsoft Sans Serif` | 所有可用字形有墨迹；聚类/宽度保持未批准 |

要点：

- 全部 5 条样本经**真实 child** 接收到的 UTF-8 与源字节完全一致，回显字节也与文档化的 CR→CRLF 回显完全一致；因此“呈现源”与“接收 oracle”同源且可独立重算。
- 核心对 ASCII/CJK/补充平面/结合标保持标量、`Cell.text` 与既有 width（含结合标 `marks=1`），与既有 15 项解析测试的表示一致；Qt 绘制按 lead cell 的 `width` 分配格宽。
- 字体 fallback 不是猜测：`QTextLayout::glyphRuns()` 逐 cell 记录实际 `RawFont` family，`missing_glyphs=0`；CJK/emoji 确实用了宿主 fallback 字体。
- `Segoe UI Emoji` 的 advance（21 px）大于两格 span（18 px），但同一字体无裁剪参考渲染的墨迹为 3..17 px，实际绘制墨迹也是 3..17 px：该样本没有可见裁切。该 `advance_over_span=true` 作为实测记录保留，不推广为“所有 emoji 都不会被裁”。
- **ZWJ/keycap 实测缺口**：当前 Unicode 15 宽度表不含 U+200D/U+FE0F，recovery parser 按既有策略逐标量替换为 U+FFFD，因此产品呈现为 FFFD 而不是组合 emoji/keycap。该缺口只记录，不新增依赖、聚类或宽度策略；未来若支持，测试接受两种结果（精确或本文记录的 recovery 结果）。
- 离屏抓图与字形测量都是**局部**证据：`presented_done=null`，不是物理上屏、不是原生 IME、不是真实 DPI 或 WindTerm 等价。

## 开发中出现并保留的失败（未改写为通过）

1. 最初 fixture 用 `ReadFile(CONIN$)`（VT 输入）接收：BMP 文本正确，但补充平面标量被拆成 U+FFFD，receiver 与源字节不一致。改用**既有已验证的 `ReadConsoleW`+严格 UTF-8 转换**接收路径后一致；该失败说明“VT 输入 + ReadFile”不能作为非 BMP 接收 oracle，未用于通过。
2. 离屏平台默认只装载应用内字体（`font_database_families=1`），CJK/emoji 无 fallback 面、无墨迹。测试改为在创建 `QApplication` 前把 `QT_QPA_FONTDIR` 指向宿主字体目录（`%WINDIR%\Fonts`），并用 `host_fallback_rendered` 字段显式区分“宿主有 fallback 面”与“环境限制”；本机实测为有 fallback。
3. 曾把 emoji 的“advance ≤ span”当作失败断言；实测证明墨迹不越格而 advance 含右侧 bearing，改为“无裁剪参考墨迹必须落在 span 内 + advance 只记录”，不是放宽产品断言。

## 复验

```powershell
python -m tools.prepare_qt_build
cmake -S . -B build/qt-cpp -G "Visual Studio 17 2022" -A x64 `
  -DTERMFORGE_BUILD_QT_SHELL=ON -DBUILD_TESTING=ON `
  -DTERMFORGE_BUILD_CANDIDATE_ADAPTER=OFF `
  -DQt6_DIR="$PWD/local/qt-build-deps/qt/lib/cmake/Qt6" `
  -DTERMFORGE_CONPTY_BACKEND_DIR="$PWD/local/qt-build-deps/conpty" `
  -DTERMFORGE_MSVC_RUNTIME_DIR="$PWD/local/qt-build-deps/crt"
cmake --build build/qt-cpp --config Release --target qt_unicode_render_test --parallel 2
ctest --test-dir build/qt-cpp -C Release -R '^qt_unicode_render$' --no-tests=error --output-on-failure
```

本次同源码结果：Qt 正式路径 CTest **6/6**（`qt_cpp_shell`、`qt_unicode_input`、`qt_unicode_render`、`qt_cpp_resize`、`qt_terminal_modes`、`qt_terminal_modes_retained`，合计 9.92 s）；新测试连跑两次均通过，receiver/echo SHA-256 相同，除 child pid 与一个混合 cell 的 fallback 面列表顺序外逐字段一致。默认/核心配置 CTest **41/41**（27.61 s，含 `core` 的 16 项核心与 15 项 Unicode Python 测试、`keyboard`、`recovery`、`resize_replay_contract` 及全部 `*_core`）。完整源码/二进制/运行文件身份见 [validation.json](validation.json)，不发布 SDK、部署目录或原始日志。

## 边界

- 只验证固定 5 样本、单显示器、离屏 offscreen、80×24、当前宿主字体；不验证物理呈现、真实 96/144 DPI、原生 IME、完整字素/整形/全部 emoji、选择/复制/Find、TUI、resize 交叉、多种字体配置或性能。字体 fallback 依赖宿主是否安装对应面，测试会记录 `host_fallback_rendered=false` 而不是把它当作产品通过。
- 不重复既有 15 项核心解析测试、`qt_unicode_input` 的输入字节合同、旧 `f72c5c4` 联合输入回归或历史 Win32/DWrite 渲染证据；只补正式 Qt 路径未覆盖的“解析后实际绘制 + 独立 child 接收同源”合同。
- 不关闭 #30/#155/#7，不宣称 R2 完成、WindTerm 等价或性能改善。
