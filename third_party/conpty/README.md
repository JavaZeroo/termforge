# 随包固定微软 ConPTY

Windows 本地 Shell 的产品路线已按 [ADR-0007](../../docs/adr/0007-bundled-microsoft-conpty.md)确认采用随包固定微软 ConPTY。本目录只保存来源、身份及完整 MIT 许可，DLL/EXE 保留在本地受控目录，不提交到源码仓库。

## 正式文件对与复建

Qt 正式入口使用 [source-revision.json](source-revision.json) 固定的源码、补丁和工具链配置。补丁让宿主记录右边界换行以及实际执行的重排，并在输出流中按序交付尺寸版本和单元格迁移。旧官方包仍可用作对照。 [补丁](../../experiments/conpty/upstream/r29-native-map.patch) · [行为对照](https://github.com/JavaZeroo/termforge/blob/1f8bf759a051f8c5872359ccb5433873666e2dde/experiments/shell/results/2026-11-25-bcd-native-depth/same-source-ab.md)

Windows x64 复建原生文件对需要 PowerShell 7、Visual Studio 2026 v145 和 Windows SDK 10.0.26100.0。在仓库根目录执行以下命令，新建两个专用 checkout。构建脚本会在 Terminal checkout 应用补丁。

```powershell
git -c core.autocrlf=false clone --no-checkout --filter=blob:none https://github.com/microsoft/terminal.git local/conpty-upstream
git -C local/conpty-upstream -c core.autocrlf=false checkout --detach dc8ae365847d41f62d1ebca93815bb00959bc12f
git -c core.autocrlf=false clone --no-checkout https://github.com/microsoft/vcpkg.git local/conpty-vcpkg
git -C local/conpty-vcpkg -c core.autocrlf=false checkout --detach 927f62e4b8838bd7e441e9c45103a16ffd75007e
pwsh -NoProfile -File experiments/conpty/upstream/build_r29_map.ps1 -Source local/conpty-upstream -Vcpkg local/conpty-vcpkg -Out local/conpty-source-build -CandidateOnly
python -m tools.prepare_qt_build --conpty-pair local/conpty-source-build/candidate-pair
```

准备脚本核对源码、补丁、许可、NOTICE 和双文件实际哈希，并生成这次构建的固定清单。CI 在同一作业构建与验证文件对。原生源码、构建输出和准备目录均保存在忽略的 `local/` 下。本地 Qt SDK、CRT 及应用构建参数见 [Qt 复建说明](../qt/README.md)。

根构建将 `TERMFORGE_CONPTY_BACKEND_DIR` 设为准备目录下 `conpty/` 的绝对路径。配置和每次构建都核对双文件、MIT 许可和 NOTICE，再部署至程序旁的 `conpty-backend/`。运行时 `Backend::configured()` 以编译时绑定的哈希查验文件对，并加载修订版的有序重排接口。后端模块在 create/resize/close 期间保持加载。Console 字符宽度策略沿用 `0x18`；完整 Unicode 与原生控制台匹配仍须验证。

修订版 `termforge.2` 在进入备用屏时将原生光标移至该页起点。备用屏宽度变化时同步限制保存的主屏列，保留隐藏正文直到返回时按实际宽度裁剪。两种返回顺序的固定样本均检查下一字符的写入位置。[WindTerm 对照](https://github.com/JavaZeroo/termforge/blob/1f8bf759a051f8c5872359ccb5433873666e2dde/research/ui/results/2026-11-02-resize-frame-reference/README.md#2026-11-25-补充主屏保存格超出新宽度)

修订版 `termforge.3` 也在备用屏缩短时限制保存行，随后增高仍保留该行。原生子进程在返回主屏后写入，两种返回顺序都用 Win32 单元格读取和实际 DSR 回复核对。[高度对照](https://github.com/JavaZeroo/termforge/blob/1f8bf759a051f8c5872359ccb5433873666e2dde/research/ui/results/2026-11-26-saved-row/README.md)

修订版 `termforge.4` 在备用屏 RIS 后保存主屏光标，并随窗口缩小限制其行列。首次显式返回恢复该位置。[原版与 Win32 写入对照](https://github.com/JavaZeroo/termforge/blob/1f8bf759a051f8c5872359ccb5433873666e2dde/research/ui/results/2026-11-26-ris-resize/README.md)

修订版 `termforge.5` 修复了备用屏 RIS 切回主屏时再次清掉主屏正文的问题。主屏自身执行 RIS 仍清除内容。[原生读回与用户决定](../../docs/research/r334-ris-content.md)

## 官方包对照

`-DTERMFORGE_CONPTY_SOURCE_BUILD=OFF` 启用 `Microsoft.Windows.Console.ConPTY 1.25.260930003` 的旧官方包对照。按 [manifest.json](manifest.json) 的 URL 获取归档并核对大小和 SHA-256，只保留指定的 `conpty.dll` 与 `OpenConsole.exe`。双文件须来自同一归档。清单中的 `experimental`、`default_backend` 和 `supported_client_platforms_verified` 记录该包当时的集成状态。系统 ConPTY 也保留为独立诊断对照。
