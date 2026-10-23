# R1 键盘与系统 IME 验证

本目录提供 R1-06 的自动验证入口。原生 HWND/HIMC 生命周期和键盘字节合同已可独立运行；**固定 Microsoft Pinyin 的独立探针及四组产品窗口交叉场景均已通过限定 hosted 验收**，见[独立审计](results/2026-10-24-product-ime-accepted/validation.json)。`real_ime_workflow_verified: false` 永远不是通过。本次四场景覆盖真实终端会话、切换、rename、输出和受控 DPI/zoom；其他输入法、真实跨屏及完整动作矩阵不据此扩大。

## 可复现入口

仓库配置并构建后执行；实际构建目录可替换 `build/bin/Release`：

```powershell
cmake --build build --config Release --target keyboard_test ime_context_test termforge_ime_probe
ctest --test-dir build -C Release -R '^(keyboard|ime_context)$' --output-on-failure
python research/ime/run_probe.py --exe build/bin/Release/termforge_ime_probe.exe --mode desktop --out local/ime-desktop
python research/ime/run_probe.py --exe build/bin/Release/termforge_ime_probe.exe --mode station --out local/ime-station
```

`run_probe.py` 记录 EXE 哈希、UTC 时间、时限、退出码、原始 stdout/stderr 字节和解析后的 `result.json`。`--out` 必须是新的目录：任何已有目录（含空目录）均在启动 probe 前拒绝，并发运行最多一个取得该目录；重跑须另选目录，例如 `local/ime-desktop-02`，不会覆盖旧失败。进程或 JSON 失败保留失败，超时返回 124 并保留已捕获的部分输出；hosted 模式必须所有真实 IME 条件为真才返回 0。`desktop` 成功只表示原生上下文/请求坐标可用，`station` 可返回 2 表示环境不可用。

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

只在 `JavaZeroo/termforge` 的 GitHub-hosted Windows job 中运行（`win22`/`win25`/`win25-vs2026`）。[独立 workflow](../../.github/workflows/r1-ime.yml) 对相关文件的 push 自动执行只读盘点；手动 dispatch 接受 `mode=inventory|verify`，默认 `inventory`。自动运行不会安装语言、修改用户语言或运行真实输入动作；只有显式 dispatch `mode=verify` 才进入准备和真实输入门槛。也可独立手动触发盘点：

```sh
gh workflow run r1-ime.yml --repo JavaZeroo/termforge -f mode=inventory
```

`inventory_hosted.py` 在创建目录之前检查 hosted 身份、管理员权限及工作区内输出路径；已有目录一律拒绝，包括空目录。禁止在开发机伪造 CI 环境变量。它为 OS、PowerShell、DISM 参数元数据、拼音二进制、固定 TIP 和目标能力分别建立独占子目录；每项记录开始/结束、耗时、错误及子进程回收结果，子进程关键步骤另写原子 `progress.json`。实际镜像版本、InstallationType、模块版本、文件哈希及 TIP 注册/用户列表信息均来自本轮盘点。

每项使用固定 Windows PowerShell、`-NoProfile -NonInteractive`，脚本收到 `GO` 前不执行环境查询；父进程先纳入自有 kill-on-close Job，再放行。非能力项上限 20 秒，能力查询默认 60 秒，另给进程与 Job 各最多 5 秒回收。单项查询失败或超时后，只有已确认自有进程退出且 Job 清空才继续其他独立盘点；回收失败或状态未知就停止。只要有一项失败，汇总仍是 `inventory-failed`。回收只覆盖自有进程树，不终止系统 servicing 服务；`active=0` 不代表系统全局空闲。GO 门控保证脚本业务探测在 Job 分配后开始，不声称 PowerShell 主机初始化从第一条指令即已纳管。

能力查询先检查真实参数集是否同时包含 `Online/Name/LimitAccess/LogPath`；缺参即失败，不去掉参数重试。查询固定为 `Language.Basic~~~zh-CN~0.0.1.0`，使用 `-LimitAccess` 阻止联系 Windows Update/WSUS 获取能力来源，并写本项专用日志。能力不存在或 `NotPresent` 属于成功观察；`inventory-complete-ime-unverified`、文件存在、TIP 注册都不等于能实际输入。[查询参数](https://learn.microsoft.com/en-us/powershell/module/dism/get-windowscapability?view=windowsserver2025-ps)

上传仅限白名单 `inventory.json`、各项 `phase.json/progress.json` 及明确的真实探针结果文件。DISM 只公开最多 64 KiB 日志尾部提取的 HRESULT、错误/警告计数与截断标记；不上传原始日志、私有 stdout/stderr、系统配置或安装文件。单项输出/摘要写入失败仍保留失败状态，不能把查询完成转换为整项通过。

本地仅运行以下测试；它们检查守卫、原子报告、纯 PowerShell 参数合同/故障路径，以及真实合成子孙进程的超时回收，不执行系统能力或语言操作：

```sh
python -m unittest research.ime.test_hosted_inventory research.ime.test_prepare_hosted -v
```

以下为早期独立探针的证据范围；产品交叉验收以末节的完整结果为准。显式 [verify 40008180684](https://github.com/JavaZeroo/termforge/actions/runs/40008180684) 的[脱敏事实](results/2026-10-24-real-ime-probe/validation.json) 固定 `68ad8d9`、Server 2025 Datacenter 26100、`win25-vs2026/20261021.278.1` 及拼音程序版本/哈希；17 个白名单文件与 ZIP 摘要一致，原始 stdout 与结果相同、stderr 为空。六项查询/六个准备阶段完成，固定 TIP 在 `zh-Hans-CN` 用户条目中，程序哈希前后不变；Basic 前后仍为 NotPresent，没有安装语言能力。真实预编辑 `ni'hao`、实际候选位移（+144,+96）、提交“你好”恰好一次及 Escape 取消均通过。probe 进程已回收、HIMC/自有窗口销毁；旧 probe collector 没有自有 Job，这一结果不证明产品终端、PTY、跨会话或资源矩阵通过。

当前 `verify` 先盘点，再用 Windows PowerShell 执行现有 provider 用户激活；逐阶段保存失败，明确枚举 Generic.List 中每个语言值，只接受 `zh-CN`/实际返回的 `zh-Hans-CN` 与精确固定 TIP，完成前后身份校验后才进入真实探针。`prepared-not-verified` 只代表准备完成。更早的[盘点哈希失败](results/2026-10-24-hosted-inventory/validation.json)、[空读回](results/2026-10-24-hosted-activation-readback/validation.json)、[标签拒绝](results/2026-10-24-hosted-language-tag/validation.json) 保留原结论；[旧准备超时](https://github.com/JavaZeroo/termforge/actions/runs/39950720638) 也不改判成功。依据：[用户语言列表](https://learn.microsoft.com/en-us/powershell/module/international/get-winuserlanguagelist?view=windowsserver2025-ps)、[设置输入方法](https://learn.microsoft.com/en-us/powershell/module/international/set-winuserlanguagelist)、[Pinyin 标识](https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/default-input-locales-for-windows-language-packs)。

探针再次验证 hosted 环境和真实 input desktop，在每次输入前确认自有窗口仍为 foreground/focus。实际按键 `NIHAO` 必须产生原生组合及可见候选窗口；同一候选 HWND 在光标移动时须产生匹配位移，Space 恰好提交一次“你好”，无额外 `WM_CHAR`；再输入 `NI` 并用 Escape 取消。只激活本进程 profile，不使用 `TF_IPPMF_ENABLEPROFILE` 隐式修改用户注册配置。[ActivateProfile](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfinputprocessorprofilemgr-activateprofile)

候选窗口类名、前后矩形、预编辑文字、提交内容和事件数均保存在 JSON。候选类无法识别、语言包缺失、没有 input desktop、焦点丢失或任何步骤未验证，门槛失败。上述固定镜像的独立探针已通过，仍不能外推其他镜像/版本或产品交叉场景。本地两个隔离模式的 profile 枚举为空也只代表其原隔离范围。

## 产品真实输入场景

`r1-ime.yml` 的显式 `mode=verify` 先完成固定环境准备与独立探针，再运行 `python -m research.ime.product_runner --launcher build/bin/Release/shell_test_launcher.exe --timeout 60 --out local/hosted-ime/product`。普通 push 仅盘点。开发机在创建证据目录、窗口或改变输入法前拒绝；无需用户逐轮点击。

| 场景 | 独立验收 |
|---|---|
| `ui-ime-basic` | 两区域真实预编辑不发送、“你好”恰好一次、Escape 取消、候选随工作区移动 |
| `ui-ime-geometry` | 预编辑中 20 行真实输出、resize、125% zoom、受控 120 DPI；候选和实际物理 caret、保留行逐字检查 |
| `ui-ime-focus` | 隐藏标签、失焦/重入及原生 rename EDIT 的真实组合；PTY 接收端不串会话。第一次 Escape 取消组合、第二次关闭 rename，已按固定源码托管验收 |
| `ui-ime-close` | 组合中关闭目标、候选消失、接收进程退出，另一会话仍可提交/取消 |
| `ui-ime-reading` | 已按固定源码托管验收：历史阅读→终端中文组合恢复可见光标→Find EDIT 中文组合/提交；第一次 Escape 只取消新组合，第二次关闭 Find；两区域独立接收、9组上下文与实际候选消失 |

原生 launcher/产品分别等待外层与内层 GO；两个 kill-on-close Job 都必须归零。只查询现有 input desktop，不创建/切换/删除系统输入桌面。原始 stdout/stderr、阶段进度、独立接收文件和结果分别保存；已有目录拒绝覆盖，超时仍保留原始失败。候选既检查同一原生实例的相对移动，也按实测单元格/OS DPI/显示器工作区检查绝对锚定，允许向上排布和右边界夹紧。

本地 schema/清理测试不代替真实输入。[6a165c8 / verify 40029638833](results/2026-10-24-product-ime-contexts/validation.json) 已独立重算五模式28检查点、9个 reading 上下文、精确接收字节/原始进度与内外 Job0，并核对 Find/rename 候选消失和双 Escape；[自有帧复核](results/2026-10-24-product-ime-contexts/visual-validation.json)仅代表最终 retained DIB，不是原生候选过程截图或实际上屏计时。该批不代替 #18 完整流程与八会话资源验收。[154dc48 的显式 run 40017974845](results/2026-10-24-product-ime-accepted/validation.json)已独立复核四场景22检查点、字节/候选/画面和回收；8个正常receiver exit0，组合中关闭的目标由Job取消exit91且空接收。受控DPI不代表实际跨显示器，后续行为变更仍需重验。

历史失败与修复保留：[c206324 四场景 stop 失败](results/2026-10-24-product-stop-failure/validation.json)缺少最终manifest/frame，不能追认为通过；[data-only固定源码红→绿](results/2026-10-24-console-receiver-stop-fix/validation.json)接受显式停止的成功零字符读，保留正常零读拒绝、原始错误和3秒join。后续hosted产品成功有自己独立的完整证据，不改变旧失败。
