# 随包固定微软 ConPTY

Windows 本地 Shell 的产品路线已按 [ADR-0007](../../docs/adr/0007-bundled-microsoft-conpty.md)确认采用随包固定微软 ConPTY。本目录只保存来源、身份及完整 MIT 许可，DLL/EXE 保留在本地受控目录，不提交到源码仓库。

[manifest.json](manifest.json) 固定现有整合起点 `Microsoft.Windows.Console.ConPTY 1.25.260930003` 的官方来源、源码提交、包及同包 Windows x64 两个文件的 SHA-256。它不是跟踪最新版本的声明，也不代表完整产品验收。包版本与 flags 可依据明确身份和行为证据更新，不由该元数据永久锁定。

首次取得组件时，按 manifest 的官方 URL 获取包并核对包大小及 SHA-256，仅提取列出的 `package_members`，保持 `conpty.dll` / `OpenConsole.exe` 名称。不得混合其他包或系统目录的文件。现有同哈希组件可直接复用，构建不隐式下载或修改 Windows 组件。

Qt 正式入口使用 [source-revision.json](source-revision.json) 固定的源码、补丁和工具链配置。补丁让宿主记录右边界换行以及实际执行的重排，并在输出流中按序交付尺寸版本和单元格迁移。旧官方包仍可用作对照。 [补丁](../../experiments/conpty/upstream/r29-native-map.patch) · [行为对照](../../experiments/shell/results/2026-11-25-bcd-native-depth/same-source-ab.md)

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

根构建将 `TERMFORGE_CONPTY_BACKEND_DIR` 设为准备目录下 `conpty/` 的绝对路径。配置和每次构建都核对双文件、MIT 许可和 NOTICE 的身份，再部署至目标旁的 `conpty-backend/`。运行时以编译时绑定的哈希确认文件对，加载修订版的有序重排接口。使用 `-DTERMFORGE_CONPTY_SOURCE_BUILD=OFF` 可显式构建旧官方包对照。

运行时 `Backend::configured()` 再核对固定双文件，从可执行文件旁的绝对目录加载；create/resize/close 与模块生命周期由同一个 Backend 拥有。当前复用已验证接口的 Console 字符宽度策略 `0x18`，不是旧 passthrough flag。该策略与原版 Unicode、原生控制台和末端行为的完整匹配仍需分别验证。

`experimental`、`default_backend` 与 `supported_client_platforms_verified` 保留了原固定包清单的历史集成状态；当前路线确认见 ADR，新的构建/行为验收须引用实际源码和结果。系统 ConPTY 可用作诊断对照；本目录不会把不同源码或后端的历史成功拼成联合验收。
