# WindTerm UI 定向运行时探测

原创探测代码，不是 WindTerm 实现或客户端依赖。结论及限制见[首轮 UI 调查](../../docs/research/windterm-ui-2026-10-23.md)与[字体/主标签调查](../../docs/research/windterm-ui-typography-2026-10-23.md)。只支持报告中固定的官方 Windows x64 / Qt 5 包。

## 自动执行入口

以下由代理执行，无需切换桌面或用户点按。先按[发行包校验流程](../../benchmarks/tools/WINDTERM-LAB.md)准备官方发行物；不要指向日常使用的安装目录。

```powershell
.venv/Scripts/python research/ui/launch.py `
  --source local/windterm-official/WindTerm_2.7.0 `
  --out local/ui-reference/new-run --seconds 120
```

输出目录必须不存在且位于仓库 `local/` 内。程序校验 EXE 和复制子集的完整哈希清单（含 Qt DLL、winpty-agent、插件和资源），创建仅含一个合成本地 cmd 的 profile，将进程以 suspended 状态加入自有 Job 后启动在独立、非输入桌面。它不调用 SwitchDesktop，不复制原用户 profile，不修改原安装。预建 `.wind/profiles/` 避开已定位的首启 OpenSSH/PuTTY 导入路径，并隔离 HOME/USERPROFILE/APPDATA/LOCALAPPDATA；隔离桌面/Job 和环境变量不是网络或文件访问沙箱。默认 widgets 探针只看本地初始状态，typography 探针会打开该合成本地 cmd，均不创建远程连接。

注意：首轮旧启动器的新 profile 曾被 WindTerm 自动填入宿主 OpenSSH 元数据，不能将旧配置称为纯合成状态。本版已修正，静态条件、运行时标志与配置检查见第二轮报告。禁用首启导入后，底部 ShellPane 初始为空，这是与旧样本不同的已声明状态。

启动器打印 PID、主线程 ID、目标及脚本哈希，并生成 `probe.js`。等待目标加载 Qt 后，代理调用 REA `instrument_with_frida`：

```json
{"mode":"attach","pid":12345,"source_kind":"file","path":"<absolute-new-run>/probe.js","duration_ms":3000}
```

PID 和绝对路径必须使用本次启动器的输出，不能枚举/附加日常 WindTerm。Frida 必须由已有 REA provider 提供；启动器不安装工具。探测脚本在指定 GUI 线程的 `DispatchMessageW` 入口读取 `QApplication::allWidgets()`，通过 Qt 导出函数获取父子关系、类名、objectName、可见性和 geometry。随后将唯一 MainWindow resize 到 1360×860，在下一次派发采集第二组。无两组数据、出现 error 或截断都不是成功；短观察窗口可能尚未启动完成，应检查本次进程后有限重试。

导出 REA evidence bundle 到本次 `local/` 子目录；启动器本身不收集 Frida 输出。采集后创建 `<out>/STOP` 文件，或等待最多 300 秒的期限；启动器回收自有进程树，将结果写入 `cleanup.json`，要求 active 为 0。`run.json` 和 `files.json` 分别记录进程身份和复制文件哈希。原始证据、profile、复制资源均留在 ignored local/，不提交。

复制清单共 4,047 项，逐项与固定 ZIP 核验。对 `{相对路径: SHA256}` 使用 Python `json.dumps(sort_keys=True, separators=(",", ":"))` 的 UTF-8 字节计算 SHA-256，结果为 `e459adf2f12adb6a1b7f55e66a9da1c22794185e66e848057d21e0c60d669203`。生成的 profiles.config 和合成会话不属于发行物清单。旧版 4,046 项漏了 winpty-agent，不能启动被调查的本地会话路径。复制子集内 DLL/主题/helper 更改、缺失或多出文件均不能启动；这是内容固定，不是上游签名认证。

## 字体与主标签场景

```powershell
.venv/Scripts/python research/ui/launch.py `
  --source local/windterm-official/WindTerm_2.7.0 `
  --out local/ui-reference/new-typography --probe typography --seconds 90
```

同样使用本次 PID 和 probe.js 调用 REA，观察窗口建议 `duration_ms: 8000`。探针在 GUI 线程采集初始状态，resize 到 1360×860，向唯一合成 SessionTree 叶节点发送 End/Return，采集主标签状态，再 resize 到 2048×1120。采样与 resize 分阶段，过渡期间不归属绘制字体；每条字体观测同时记录实际 MainWindow 尺寸，必须匹配对应快照。四个 snapshot、`scenario_complete`、无 error/截断、运行时 first-run 标志为 0、主标签与 TerminalView 存在且无 MessageBar 是取证门槛。实际 shell 存活还需单独的自有进程观察，不能仅由这些控件推定。

增加 `--scale-factor 1.5` 可做 Qt 环境缩放对照。启动器清除继承的六种 Qt 缩放变量，并仅在参数指定时设置 QT_SCALE_FACTOR；这不修改宿主显示设置。两种状态都保留实际 logical DPI、DPR 和 API 返回的 physical DPI。

QWidget 字体与 QPainter 文本绘制字体分别记录，后者仅读取 device/font，不读文字参数。QString/QFontInfo 临时对象析构；QApplication 返回的最多四个 QList 对象保留至自有进程结束。匹配不到 QWidget 的绘制设备不计入，不声称逐字形 fallback 或所有绘制路径覆盖。

导出 evidence.json 后创建 STOP。退出同时检查 profile 仍只有合成项，结果写入 profile-check.json。随后将指定 evidence 转为可审核的精简结果：

```powershell
.venv/Scripts/python research/ui/summarize.py `
  --run-dir local/ui-reference/new-typography `
  --evidence-id <本次证据ID> --out local/ui-reference/new-typography/summary.json
```

摘要需通过脚本/目标身份、阶段完整性、错误/截断、父链、主区域矩形及回收/配置门槛；只输出白名单几何/字体及证据元数据。审核后才可提交摘要，不提交整个证据包、截图、profile 或复制发行物。

## R1 文字动作参考

已有文字导航与实际菜单证据见 [R1 参考合同](../../docs/research/windterm-r1-text-contract.md)。[r1-reference.yml](../../.github/workflows/r1-reference.yml) 串行运行 `text`、`menu-wrap` 和 `action-context`，各使用两次新合成配置；各自的完成门槛和跨轮比较独立保留。

`action-context` 用于调查查找编辑框的 All/Copy/Paste/Find/Escape 目标，以及多行 PasteDialog 的实际 Return 结果。入口必须使用 `r1_launch.py --scenario action-context --hosted-ci`，仅指定的一次性 hosted 环境允许执行；本机在准备文件前拒绝。`r1_summarize.py --require-context-complete` 要求原始动作、编辑文字、焦点、系统剪贴板、精确 peer 字节及回收齐全。Return 只有在弹窗退出后精确无追加或完整追加一次时才分别记为 cancelled/accepted，不由默认按钮样式推断。

新增探针与纯 schema 测试不构成运行观察；真实两轮结果独立审核后才更新参考合同。rename、busy 与关闭中的目标行为仍须单独取证，不能从普通查找框结果外推。

## 解释约束

- `rect` 是父控件坐标系下的 Qt QRect `[x1,y1,x2,y2]`，右下端包含在内。尺寸是 `x2-x1+1`、`y2-y1+1`，允许零尺寸；不要把主窗口桌面坐标当成子控件偏移。
- `isVisible()` 为真不代表进入屏幕视口。辅助控件可能在极远坐标；原始树包含这些对象。QSplitterHandle 的命中范围可能比可见分隔线宽。
- widgets 模式有一次明确的窗口 resize；typography 模式另有两次 resize、本地会话打开和更新请求。这些是 controlled replay，不是完全被动观察。
- Qt 5 / MSVC x64 的 QList、QString、虚表位置是特定版本 ABI 假设。非匹配二进制必须拒绝；这不是通用 Qt SDK。
- 名称只读 objectName，不读窗口标题、编辑器内容或会话文本。仍只允许全新自有配置。
- 两次 QList 返回值的存储保留到本次进程终止；QString 临时对象显式析构。探测会扰动进程，不能拿它测性能。
- typography 模式测得的是 Qt DPI/字体返回值与部分绘制调用。实际物理像素、延迟、逐字形 fallback、真实跨屏 DPI、悬停/拖放/持久化仍需要额外场景。

验证准备守卫（不执行目标）：

```powershell
.venv/Scripts/python -m unittest research.tests.test_ui_probe research.tests.test_ui_typography -v
```
