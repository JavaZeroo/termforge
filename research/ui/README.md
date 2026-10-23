# WindTerm UI 定向运行时探测

原创探测代码，不是 WindTerm 实现或客户端依赖。完整结论及限制见[UI 调查](../../docs/research/windterm-ui-2026-10-23.md)。只支持报告中固定的官方 Windows x64 / Qt 5 包。

## 自动执行入口

以下由代理执行，无需切换桌面或用户点按。先按[发行包校验流程](../../benchmarks/tools/WINDTERM-LAB.md)准备官方发行物；不要指向日常使用的安装目录。

```powershell
.venv/Scripts/python research/ui/launch.py `
  --source local/windterm-official/WindTerm_2.7.0 `
  --out local/ui-reference/new-run --seconds 120
```

输出目录必须不存在且位于仓库 `local/` 内。程序校验 EXE 和复制子集的完整哈希清单（含 Qt DLL、插件和资源），创建全新 profile，将进程以 suspended 状态加入自有 Job 后启动在独立、非输入桌面。它不调用 SwitchDesktop，不读取原用户 profile，不修改原安装；隔离桌面/Job 不是网络或安全沙箱，运行的是原程序。只打开本地初始状态，不创建远程连接。

启动器打印 PID、主线程 ID、目标及脚本哈希，并生成 `probe.js`。等待目标加载 Qt 后，代理调用 REA `instrument_with_frida`：

```json
{"mode":"attach","pid":12345,"source_kind":"file","path":"<absolute-new-run>/probe.js","duration_ms":3000}
```

PID 和绝对路径必须使用本次启动器的输出，不能枚举/附加日常 WindTerm。Frida 必须由已有 REA provider 提供；启动器不安装工具。探测脚本在指定 GUI 线程的 `DispatchMessageW` 入口读取 `QApplication::allWidgets()`，通过 Qt 导出函数获取父子关系、类名、objectName、可见性和 geometry。随后将唯一 MainWindow resize 到 1360×860，在下一次派发采集第二组。无两组数据、出现 error 或截断都不是成功；短观察窗口可能尚未启动完成，应检查本次进程后有限重试。

导出 REA evidence bundle 到本次 `local/` 子目录；启动器本身不收集 Frida 输出。采集后创建 `<out>/STOP` 文件，或等待最多 300 秒的期限；启动器回收自有进程树，将结果写入 `cleanup.json`，要求 active 为 0。`run.json` 和 `files.json` 分别记录进程身份和复制文件哈希。原始证据、profile、复制资源均留在 ignored local/，不提交。

复制清单共 4,046 项，逐项与固定 ZIP 核验。对 `{相对路径: SHA256}` 使用 Python `json.dumps(sort_keys=True, separators=(",", ":"))` 的 UTF-8 字节计算 SHA-256，结果为 `b5b18ab1ca7f9843d58a6496e5ce191f1cec48d16f5c697e62a4dcaf5f5fe3e8`。生成的新 profiles.config 不属于发行物清单。复制子集内 DLL/主题更改、缺失或多出文件均不能启动；这是内容固定，不是上游签名认证。

## 解释约束

- `rect` 是父控件坐标系下的 Qt QRect `[x1,y1,x2,y2]`，右下端包含在内。尺寸是 `x2-x1+1`、`y2-y1+1`，允许零尺寸；不要把主窗口桌面坐标当成子控件偏移。
- `isVisible()` 为真不代表进入屏幕视口。辅助控件可能在极远坐标；原始树包含这些对象。QSplitterHandle 的命中范围可能比可见分隔线宽。
- 只读几何采集之外，有一次明确的窗口 resize；这是 controlled replay，不是完全被动观察。
- Qt 5 / MSVC x64 的 QList、QString、虚表位置是特定版本 ABI 假设。非匹配二进制必须拒绝；这不是通用 Qt SDK。
- 名称只读 objectName，不读窗口标题、编辑器内容或会话文本。仍只允许全新自有配置。
- 两次 QList 返回值的存储保留到本次进程终止；QString 临时对象显式析构。探测会扰动进程，不能拿它测性能。
- 物理 DPI、实际字体选择、绘制像素、延迟、悬停/拖放/持久化状态都需要额外场景。脚本不做这些结论。

验证准备守卫（不执行目标）：

```powershell
.venv/Scripts/python -m unittest research.tests.test_ui_probe -v
```
