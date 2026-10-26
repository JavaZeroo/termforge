# 固定 ConPTY 实验包

本目录只保存来源、身份和 MIT 许可；不提交 DLL/EXE，不下载或安装系统组件。[manifest.json](manifest.json) 固定 Microsoft 官方 `Microsoft.Windows.Console.ConPTY 1.25.260930003`、对应 release/source commit、完整包及同包 Windows x64 两个文件的 SHA-256。`LICENSE` 来自该固定源码提交。

从 manifest 的固定官方 URL 下载包，先核对包大小及 SHA-256，再仅将列出的两个 `package_members` 提取到一个实验目录，保持名称 `conpty.dll` / `OpenConsole.exe`。不要从其他包混合复制文件。Microsoft 包声明的最低系统版本不等于本项目的验证范围；当前实验结果仅来自本机 Windows 11 build 26200。

```powershell
cmake -S . -B build/candidate-conpty -A x64 `
  -DTERMFORGE_BUILD_CANDIDATE_ADAPTER=ON `
  -DTERMFORGE_CONPTY_BACKEND_DIR=<absolute-reviewed-directory>
cmake --build build/candidate-conpty --config Release
```

构建阶段核对固定双哈希，并把匹配的 DLL/EXE、manifest 和许可复制到可执行文件旁的 `conpty-backend/`。运行阶段从自身可执行文件位置取得绝对目录，再次核对两文件；使用限制 DLL 搜索目录的 `LoadLibraryExW`，三项 ConPTY API 和模块生命期由同一个 `Backend` 拥有。缺失、身份不符或导出不存在即失败，不回退到系统 API。

这是显式实验构建。默认未设置目录时继续使用系统 API，不查找机器上的 Windows Terminal 安装，也不锁定产品后端或更改 Alpha 包路线。实验 `configured()` 使用固定官方头文件的 `PSEUDOCONSOLE_GLYPH_WIDTH_CONSOLE=0x18`，维持现有 Win32 readback 合同；系统始终 flags 0，通用 `reviewed_side_by_side()` / 独立 margin A/B 也默认 flags 0。这是宽度策略，**不是已删除的 passthrough mode**。末端核心的复杂字素合同仍由 #39 跟踪。

当前明确缺口：实际多次 resize 的文字/位置仍有差异；原粘贴并发夹具在新宿主已接收全部 3MB 后才看到 receiver checkpoint，不能当成“未完成传输时取消”通过；源控制台的每屏 margin 仍未恢复。详细结果见 [实验整合记录](../../docs/research/experimental-conpty-backend.md)。#47、#41 和 R2 保持开放。
