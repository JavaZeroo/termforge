# R1 键盘与系统 IME 验证

本目录提供 R1-06 的自动验证入口。原生 HWND/HIMC 生命周期和键盘字节合同已可独立运行；**真实 Microsoft Pinyin 组合、候选 UI、提交及取消尚待受控 hosted 环境实测**。`real_ime_workflow_verified: false` 永远不是通过。探针通过后还需完成真实终端会话、切换、rename、输出、DPI/zoom 的交叉验收，不能单独关闭 #16。

## 可复现入口

仓库配置并构建后执行；实际构建目录可替换 `build/bin/Release`：

```powershell
cmake --build build --config Release --target keyboard_test ime_context_test termforge_ime_probe
ctest --test-dir build -C Release -R '^(keyboard|ime_context)$' --output-on-failure
python research/ime/run_probe.py --exe build/bin/Release/termforge_ime_probe.exe --mode desktop --out local/ime-desktop
python research/ime/run_probe.py --exe build/bin/Release/termforge_ime_probe.exe --mode station --out local/ime-station
```

`run_probe.py` 记录 EXE 哈希、UTC 时间、时限、退出码、原始 stdout/stderr 和解析后的 `result.json`。进程或 JSON 失败保留失败，超时返回 124；hosted 模式必须所有真实 IME 条件为真才返回 0。`desktop` 成功只表示原生上下文/请求坐标可用，`station` 可返回 2 表示环境不可用。

## 键盘和 IME 接口

- `KeyboardEncoder::key_down` 编码普通模式的方向键、Home/End/Insert/Delete/PageUp/PageDown 及修饰键；普通 Tab、Return、Ctrl-C 等由 `WM_CHAR` 生成一次。Shift-Tab/Ctrl-Space 的 `skip_translate` 要在消息循环跳过 `TranslateMessage`，避免重复字符。
- `character` 保留 NUL，跨消息收齐 UTF-16 代理对；孤立代理用 U+FFFD；失焦 `reset` 丢弃尚未提交的半个代理。Ctrl+Alt 保留给 AltGr，工作区快捷键也须检查 Alt 状态。
- 每个终端 HWND 拥有自己的 `ImeContext`，只在 UI 线程使用。窗口创建后 `attach`；原生消息先交给 `handle`；只向 PTY 发送 `committed`，不发送 `preedit`。失焦取消旧上下文，晚到提交不转给新会话。
- `set_caret` 接收物理客户区坐标；移动、resize、DPI、zoom 或输出改变光标后刷新。`detach` 返回 HIMC 销毁结果；应在窗口销毁前调用。单次结果/预编辑上限 256 UTF-16 单元，非法结果显式失败。
- 工作区动作和普通按键不能抢走正在进行的真实组合；应用光标/键盘模式、第三方 IME 全矩阵仍属后续范围。以上为 TermForge 原创合同，不能冒充 WindTerm 已观察行为。

## 桌面生命周期与耗时结论

本机 Windows 10.0.26200 上，同进程私有 UI 线程退出后，曾观测到起始模块分别为 `msctfmonitor.dll`、`MSCTF.dll` 的 Windows 线程仍附着私有 desktop。自有 HWND/HIMC 已销毁，`CloseDesktop` 仍报 170；这不等于 HIMC 泄漏。官方说明也明确：调用进程仍有线程使用该 desktop 时不能关闭。[CloseDesktop](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-closedesktop)

测试采用父进程持有 desktop，子进程通过 `STARTUPINFO.lpDesktop=station\desktop` 从启动附着，先纳入 kill-on-close Job 再恢复运行。子进程有界退出、Job 活跃进程数归零后，父进程关闭 desktop 并要求同名 `OpenDesktop` 返回 `ERROR_FILE_NOT_FOUND`；自有 station 同样严格关闭并确认消失。无用户 desktop 回退，不调用 `SwitchDesktop`。[STARTUPINFO](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/ns-processthreadsapi-startupinfow)

20 轮两个顶层窗口反复创建/切焦点/销毁的原始场景曾用时 36.55–36.67 秒；逐阶段计时显示第 3–20 轮首次 `SetActiveWindow` 各等待约 2 秒。测试保留自有应用 frame，并在销毁两个终端 HWND 前激活 frame 后，相同 20 轮断言三次为 0.508、0.562、0.416 秒，严格清理均通过。这里只修复测试的窗口生命周期，不声称终端性能提升或已解释 Windows 内部等待实现。最终测试还覆盖非零 child exit 和强制 deadline 后的桌面删除。

原始失败、阶段时间戳和成功样本保存在开发机 `local/r1-ime-evidence/`，不随源码发布。以上为局部诊断，不是跨机器性能基准。

## 一次性 hosted Windows 门槛

只在 `JavaZeroo/termforge` 的 GitHub-hosted Windows job 中运行（`win22`/`win25`）；设置脚本在任何修改之前检查环境、管理员身份和工作区内输出路径。本机守卫已实测拒绝且不创建输出目录。禁止在开发机伪造 CI 环境变量。

```powershell
./research/ime/prepare_hosted.ps1 -Out "$env:GITHUB_WORKSPACE/local/hosted-ime/environment"
python research/ime/run_probe.py --exe build/bin/Release/termforge_ime_probe.exe --mode hosted --out local/hosted-ime/probe
```

准备脚本查询/安装 `Language.Basic~~~zh-CN~0.0.1.0`，需要重启即失败；只修改该一次性 VM 的用户语言，固定官方 Microsoft Pinyin CLSID/profile，并记录 OS、镜像版本及 `ChsIME.exe` 版本/哈希。`prepared-not-verified` 不是输入法可用证明。[语言能力安装](https://learn.microsoft.com/en-us/powershell/module/dism/add-windowscapability)、[Microsoft Pinyin 标识](https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/default-input-locales-for-windows-language-packs)

探针再次验证 hosted 环境和真实 input desktop，在每次输入前确认自有窗口仍为 foreground/focus。实际按键 `NIHAO` 必须产生原生组合及可见候选窗口；同一候选 HWND 在光标移动时须产生匹配位移，Space 恰好提交一次“你好”，无额外 `WM_CHAR`；再输入 `NI` 并用 Escape 取消。只激活本进程 profile，不使用 `TF_IPPMF_ENABLEPROFILE` 隐式修改用户注册配置。[ActivateProfile](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfinputprocessorprofilemgr-activateprofile)

候选窗口类名、前后矩形、预编辑文字、提交内容和事件数均保存在 JSON。候选类无法识别、语言包缺失、没有 input desktop、焦点丢失或任何步骤未验证，门槛失败。[独立 workflow](../../.github/workflows/r1-ime.yml) 仅由 `workflow_dispatch` 触发，设置总时限，无论成功失败都上传自有环境 JSON、结果 JSON 与原始探针 stdout/stderr；不上传安装文件、系统配置或 DISM 日志。当前尚未验证 hosted 镜像的语言包和交互会话可用性；本地两个隔离模式中 profile 枚举为空，不能据此宣称实际系统 IME 已支持。
