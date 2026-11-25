# C++ / Qt 本地终端

Windows x64 的正式配置使用自主 C++ 核心、Qt Widgets 和随包固定源码修订的微软 ConPTY。`TERMFORGE_BUILD_QT_SHELL=ON` 启用 Qt 构建，产品入口为 `termforge_qt_shell.exe`。R2 交付范围为本地终端基础与已知差异清单，最终包仍待验收。 [产品配置与差距](../../docs/FEATURES.md) · [R2 验收](../../docs/milestones/R2-local-terminal.md)

| 功能 | 当前入口和固定验证 |
|---|---|
| cmd 标签 | `+` 新建标签，可切换和独立关闭。初始网格为 80×24，按可用区域调整。收到关闭操作后等待自有会话回收，无法验证回收时报告失败。有限八会话的接收隔离、关闭及回收采用真实子进程与离屏 Qt 事件。 [会话证据](../../experiments/shell/results/2026-11-08-qt-eight-session-isolation/README.md) |
| 历史 | 滚轮、`Ctrl+Shift+Home/End` 和 `QScrollBar` 按源行锚点浏览。滚轮使用 Windows 的行数或按页设置，查询失败时回退 3 行。 [阅读锚点](../../experiments/shell/results/2026-11-05-qt-history-anchor/README.md) · [系统滚轮](../../experiments/shell/results/2026-11-05-qt-wheel-native/README.md) · [滚动条](../../experiments/shell/results/2026-11-07-qt-history-scrollbar/README.md) |
| 选择与复制 | 左键正反拖选、跨历史有界自动滚动，`Ctrl+Shift+C` 或上下文菜单 Copy 有界写入 `CF_UNICODETEXT`。选区与显示页通过 `ViewMapping` 对应到源行。 [选区迁移](../../experiments/shell/results/2026-11-06-qt-selection-migration/README.md) · [自动滚动](../../experiments/shell/results/2026-11-07-qt-selection-autoscroll/README.md) · [系统复制](../../experiments/shell/results/2026-11-06-qt-system-copy/README.md) |
| 粘贴 | `Ctrl+Shift+V`、`Shift+Insert` 或上下文菜单 Paste 读取系统剪贴板并绑定发起会话。传输有界，多行先确认。固定 hosted Windows QPA 样本核对了占用/重试及多行取消/确认的真实接收字节，历史失败按原源码保留。 [系统粘贴](../../experiments/shell/results/2026-11-06-qt-system-paste/README.md) · [多行对照](../../experiments/shell/results/2026-11-06-qt-system-paste/attempt-ledger.md) |
| Find | `Ctrl+Shift+F` 打开有界字面量查询，支持上一项、下一项、关闭及主屏/历史页的定位高亮。限制为 4096 字节、1024 个结果，忽略大小写只折叠 ASCII。 [Find 回归](../../experiments/shell/results/2026-11-06-qt-find/README.md) |
| 键盘、文字与 IME | 支持基础导航键、F1–F12、限定 Unicode 文本传输与 16 色/部分样式绘制。IME 预编辑只在界面显示，提交后才发送 UTF-8，取消或失焦清理预编辑。 [键盘/文本](../../experiments/shell/results/2026-10-29-qt-unicode-input/README.md) · [离屏绘制](../../experiments/shell/results/2026-11-04-qt-unicode-presentation/README.md) · [固定微软拼音](../../research/ui/results/2026-10-30-qt-ime-fixed-pinyin/README.md) |

`PageUp/PageDown` 仍按应用导航键处理。历史翻页键映射、粘贴回底、选词/选行、其他 shell、分屏及工作区恢复仍待迁移。Qt 的各项定向测试和历史 IME 观察按运行身份记录。整合交叉、实际呈现、单屏 96/144 DPI、完整输入法和可靠性按 R2 合同逐项验证。

## 构建与运行

先构建[固定 ConPTY 文件对](../../third_party/conpty/README.md)，按[Qt 依赖说明](../../third_party/qt/README.md)准备 Qt 6.10.3 / MSVC 2022 x64 qtbase 和固定 app-local CRT。以下从仓库根目录执行。

```powershell
cmake -S . -B build/qt-cpp -G "Visual Studio 17 2022" -A x64 `
  -DTERMFORGE_BUILD_QT_SHELL=ON -DBUILD_TESTING=ON `
  -DTERMFORGE_BUILD_CANDIDATE_ADAPTER=OFF `
  "-DQt6_DIR=$PWD/local/qt-build-deps/qt/lib/cmake/Qt6" `
  "-DTERMFORGE_CONPTY_BACKEND_DIR=$PWD/local/qt-build-deps/conpty" `
  "-DTERMFORGE_MSVC_RUNTIME_DIR=$PWD/local/qt-build-deps/crt"
cmake --build build/qt-cpp --config Release --target termforge_qt_shell qt_cpp_shell_smoke qt_cpp_resize_smoke
./build/qt-cpp/qt-bin/Release/termforge_qt_shell.exe
ctest --test-dir build/qt-cpp -C Release -R '^qt_cpp_(shell|resize)$' --no-tests=error --output-on-failure
```

启用 Qt 时默认使用固定源码 ConPTY。配置和构建都会核对文件对、许可及 NOTICE，运行时从程序目录旁的 `conpty-backend/` 校验和加载。`TERMFORGE_CONPTY_SOURCE_BUILD=OFF` 供显式旧官方包对照。固定依赖路径缺失或校验失败时构建报错。 [固定源码与产物清单](../../third_party/conpty/source-revision.json) · [专用 CI](../../.github/workflows/qt-cpp.yml)

生成工程 staging 并核对文件身份。

```powershell
python -m tools.stage_qt_product stage `
  --bin-dir build/qt-cpp/qt-bin/Release `
  --dependencies local/qt-build-deps `
  --dependency-summary local/qt-build-preparation.json `
  --out local/qt-product-staging `
  --summary local/qt-product-staging.json
python -m tools.stage_qt_product verify --staging local/qt-product-staging
```

staging 核对固定 Qt SDK、ConPTY、CRT、字体、逐文件哈希及许可证据，按清单排除测试夹具和系统 DLL 副本。对应源码与替换条件审查、干净机器运行和 R2 最终包验收仍须完成。 [文件清单](../../packaging/qt-product/manifest.json) · [Qt 部署与许可](../../third_party/qt/README.md#产品-staging-与部署边界)

## 独立验证面

会话模型在 Qt 线程处理 Parser/Screen，ConPTY 读写通过有界队列。自然退出等待进程、输出排空和解析结束，关闭路径独立核对 Job 回收。基础 smoke 覆盖真实 cmd 输入输出、标签新增/切换/关闭、启动错误与可用区域引发的 resize。窗口尺寸、核心尺寸、transport 完成与真实 child 确认分别记录。 [基础会话](../../experiments/shell/results/2026-10-28-qt-tabs/README.md) · [基础 resize](../../experiments/shell/results/2026-10-29-qt-resize/README.md)

固定源码 ConPTY 按原输出流报告尺寸版本、实际原生网格搬移及软换行。Qt 应用实际重排后继续解析，另以保留文档投影生成显示、光标和命中位置。固定修订已在原样六阶段、真实 Win32 读写及与视口同缓冲深度的 WindTerm 本地 Shell 四阶段完成核对。早期官方包的失败、不同缓冲深度及直接 Telnet 的差异各按样本记录。更广尺寸、持续输出交叉、应用兼容与完整 reflow 仍待验证。 [固定对照](../../experiments/shell/results/2026-11-25-bcd-native-depth/same-source-ab.md) · [历史早期失败](../../experiments/shell/results/2026-11-05-qt-resize-divergence/README.md) · [保留策略](../../docs/adr/0010-bounded-resize-retention.md)

专用 CI 的固定源码构建、Qt 离屏回归和 hosted Windows 剪贴板用例独立记录。原生 IME、实际呈现和受控合成事件各用自己的验证身份。旧冻结候选窗几何对照的面板身份和字体阶段仍未知。 [CI 当前入口](../../.github/workflows/qt-cpp.yml) · [IME 历史几何](../../docs/research/qt-ime-candidate-panel-reference-2026-11-01.md) · [证据索引](../../docs/EVIDENCE.md)
