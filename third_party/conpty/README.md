# 随包固定微软 ConPTY

Windows 本地 Shell 的产品路线已按 [ADR-0007](../../docs/adr/0007-bundled-microsoft-conpty.md)确认采用随包固定微软 ConPTY。本目录只保存来源、身份及完整 MIT 许可，DLL/EXE 保留在本地受控目录，不提交到源码仓库。

[manifest.json](manifest.json) 固定现有整合起点 `Microsoft.Windows.Console.ConPTY 1.25.260930003` 的官方来源、源码提交、包及同包 Windows x64 两个文件的 SHA-256。它不是跟踪最新版本的声明，也不代表完整产品验收。包版本与 flags 可依据明确身份和行为证据更新，不由该元数据永久锁定。

首次取得组件时，按 manifest 的官方 URL 获取包并核对包大小及 SHA-256，仅提取列出的 `package_members`，保持 `conpty.dll` / `OpenConsole.exe` 名称。不得混合其他包或系统目录的文件。现有同哈希组件可直接复用，构建不隐式下载或修改 Windows 组件。

Qt 正式入口的本地／CI 依赖准备可使用 `python -m tools.prepare_qt_build`，详见 [Qt 复建说明](../qt/README.md)。该独立准备命令先验证完整 nupkg 身份，再提取并验证文件对；CMake 构建本身仍不下载、不修改 Windows 组件。归档与文件对都只放在被忽略的本地目录，CI 不把它们发布为产品包。

根构建将 `TERMFORGE_CONPTY_BACKEND_DIR` 设为该文件对的绝对目录，对正式入口调用 `termforge_use_bundled_conpty(target)`。该函数不依赖 Rust 候选开关；配置和每次构建均核对双文件及本目录许可的身份，并把两文件、manifest 和 LICENSE 复制到目标旁的 `conpty-backend/`。缺包或身份错误直接失败，不退回系统版本。

运行时 `Backend::configured()` 再核对固定双文件，从可执行文件旁的绝对目录加载；create/resize/close 与模块生命周期由同一个 Backend 拥有。当前复用已验证接口的 Console 字符宽度策略 `0x18`，不是旧 passthrough flag。该策略与原版 Unicode、原生控制台和末端行为的完整匹配仍需分别验证。

`experimental`、`default_backend` 与 `supported_client_platforms_verified` 保留了原固定包清单的历史集成状态；当前路线确认见 ADR，新的构建/行为验收须引用实际源码和结果。系统 ConPTY 可用作诊断对照；本目录不会把不同源码或后端的历史成功拼成联合验收。
