# Qt 基础 Unicode 文本输入的限定验证

执行项 [#147](https://github.com/JavaZeroo/termforge/issues/147)，父项 [#30](https://github.com/JavaZeroo/termforge/issues/30)。正式 C++ / Qt Widgets / 固定 Microsoft ConPTY 路线将 `QKeyEvent.text()` 的 UTF-16 单元交给本项目既有 `KeyboardEncoder`；每个 widget 独立保存未完成的高代理项。临时编码器处理整条事件，只有整包进入有界输入队列后才提交状态。包过限或队列拒绝保留此前状态；失焦、隐藏、关闭及目标不可用清除未提交的半个标量。

未配对单元使用 U+FFFD，已支持的控制/导航键结束此前未完成标量，焦点变化丢弃未提交单元；这些是**现有 TermForge 编码策略**。本批没有运行 WindTerm，不宣称这些边界已经与原版匹配。

## 公共边界与原始接收

测试通过公开 Qt key/focus/hide/tab 动作进入正式 widget/session，再由固定 ConPTY 启动独立 `shell_input_client.exe`。该现有夹具用 `ReadConsoleW` 读取原始单元，通过 Windows UTF-8 API 导出文件；字面预期不调用被测编码器。只在夹具结束时由 Session 直接发送 EOT(4)，不把它解释为产品已支持 Qt Ctrl-D。输入回显或 `send()` 返回成功均不是接收 oracle。

五组场景：

| 场景 | 实际接收与边界 |
|---|---|
| Unicode 文本 | 中文、NFD `e + U+0301`、整事件与跨事件的 U+1F600、固定 woman-technologist ZWJ/keycap、孤立低代理项、连续高代理项和 Return 边界；58B 精确 UTF-8 |
| 焦点/隐藏与包拒绝 | 显式 Qt 焦点移交、隐藏后恢复均使低代理项独立替换；1024B 接受，1025B 整包拒绝且不污染已持有高代理项；嵌入 NUL；最终 1037B |
| 原有正常模式按键 | ASCII、Return、Tab、Backspace、Ctrl-C 和四方向键的19B字面预期；Qt Ctrl-D 仍拒绝 |
| 标签与关闭 | 两个真实标签各有独立接收文件；隐藏目标拒绝输入，切换丢弃未完成单元；第三个目标带半个代理项关闭后只取消自身，存活目标继续接收 |
| 不可用目标 | 未启动目标拒绝后可以正常启动，孤立低代理项不能复用失败输入；已结束和启动失败目标拒绝，错误及回收可见 |

Qt 层实际拒绝场景使用现有单包上限。16KiB 总队列容量沿既有 `InputQueue` 公共夹具单独验证，没有通过随机洪流制造全队列现场，也没有增加生产测试钩子。不同配置的检查不拼成一条原生运行。

## 已固定的红与针对性绿

- 原始 test-only 源 `8adc256ffe53a7cace75d74bf6ac5d173e8b59fc`，测试 EXE SHA-256 `06d1b14692d08e672517409846fca27821514ca94e3d0163bd3f5b9943ba5dd8`：预期 `4ce4b8ade6968752`，独立接收端只收到 `4c52`；接收子进程正常结束，测试按字节不一致失败，外层 Job active=0/total=4，未超时。[原39文件运行时身份](red-runtime.json)保留。
- 扩展反例源 `39e3c2c` 显示高代理项越过 Return 后与低代理项错误拼接；按既有编码器 finish 策略修复。源 `dca412f` 还固定未聚焦目标接受合成文本的反例。
- 焦点夹具最初仅调用 `activateWindow()`，在 offscreen 平台重获焦点失败；该次失败保留在本地原日志。当前夹具改用公开 `QApplication::setActiveWindow` 并检查实际 Qt 焦点，不能据此声明 Windows 物理输入焦点已验收。
- 针对性源 `2bdcea3161f415f49d96bc0f30e5e72794c8c72f` 的五组场景通过；外层 Job active=0/total=17/terminated=0，测试根正常 exit=0。6个自然结束接收文件和1个取消目标分别记录。可审查的[源/原日志哈希与离线接收重算](focused-validation.json)、[40文件运行时](focused-runtime.json)与最终联合源码验证分开保留。

原等待边界不变：单阶段5秒，监督器主 `communicate` 25秒，CTest35秒，失败清理仍由原监督器/Session处理；这些是诊断期限，不是性能指标或完整进程生命周期承诺。运行使用 Qt 6.10.3、固定 ConPTY 1.25.260930003、VS2022/MSVC19.42 与 app-local CRT14.51；运行时清单绑定所有实际文件。

## 联合源码与一次完整回归

动态尺寸 PR148 合入 main `05f0065f4eb19ce5a76065d18f2247bcc731e285` 后，本分支以 `5d7cb5c` 合并：保留可扩展布局、resize/字体/DPR 事件与独立编码器的焦点、隐藏、关闭和结束清理。执行源码固定为 `2b6d988914b04a2a05520f0b67c08c30b6e2b43b`，tree `46e358272e6f1c99ef5d65ffcb007a3ce41b1ac9`；后续公开文档和 JSON 不算执行源码。[联合验证身份](validation.json)与上述旧红例、旧针对性绿例分别保留。

在 Windows 11 x64 build26200/UBR9711、MSVC19.42.34433.0、Python3.12.10 下使用固定 Qt6.10.3、ConPTY1.25.260930003 与 app-local CRT14.51.36247.0。先运行 Qt 三项针对性检查 **3/3**，随后唯一一次完整 CTest **61/61、exit0、307.06s**。其中正式 Qt 为 `qt_cpp_shell`、`qt_unicode_input`、`qt_cpp_resize` 三项，其余58项属于既有默认/system或独立诊断；**13条内部 unittest skip** 全部保留，包含私有 window station 的 OpenClipboard5，不能写成 CF 验收。时长仅为测试运行记录，不是性能基线。

两次 Unicode 检查各产生六个自然结束接收文件与一个取消目标的空文件，全部逐字核对字面预期。7397个执行输入及运行文件在完整回归后重算零变化：2817源码、4358 SDK、2 ConPTY、12 CRT、38产品目录、40原smoke、40 resize、41 Unicode、49旧诊断文件；这些目录计数包含各自 `runtime-manifest.json`。另外27份旧原件和已有公开红绿证据未变。四份实际运行时清单分别为[产品](final-product-runtime.json)、[原 smoke](final-shell-runtime.json)、[resize](final-resize-runtime.json)、[Unicode](final-input-runtime.json)；其 `files` 字段不包含清单自身。

专用 Qt CI 新增 Unicode 测试目标、完整运行时身份与三项测试选择。当前公开文件由同一批原件离线派生，普通与 `-O` 解释器导出逐字一致；没有重新运行原版、Qt windows-plugin、guest 或第二次完整矩阵。旧 #80/#81/#144/#145 原因仍未知，这次通过不构成因果修复。原生缩放证据仍只绑定其原0ff源码，不转记为本次 Unicode 原生验收。

## 复验与限制

按 [Qt 构建说明](../../../../../src/qt/README.md)配置后：

```powershell
cmake --build build/qt-cpp --config Release --target qt_unicode_input_test
ctest --test-dir build/qt-cpp -C Release -R '^qt_unicode_input$' --no-tests=error --verbose
```

每次产生独立自有临时目录；合成接收文件及 stdout/stderr 留在本地，公开仅源码、输入字面量和脱敏身份/标量摘要。没有新第三方库，也没有修改核心字素、实验后端或开启 `WA_InputMethodEnabled`。Qt offscreen 与人工 `QKeyEvent` 不等于真实 OS IME、物理键盘、跨 DPI、字形/宽度/整形或实际呈现；这些及 WindTerm 对照、应用键盘模式和性能仍未知。本切片不验收完整 #30 或 R2。
