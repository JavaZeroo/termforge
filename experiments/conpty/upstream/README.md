# 固定 ConPTY 的原生 margin 补丁候选

`dc8-native-margin-candidate.patch` 是针对 Microsoft Terminal
`dc8ae365847d41f62d1ebca93815bb00959bc12f` 的独立候选改动，随附
[Microsoft MIT 原许可](MICROSOFT-LICENSE.txt)。原作者版权保留。它目前只通过完整固定文件的
`git apply --unidiff-zero --check`，**未编译、未运行上游测试、未生成后端、未证明修复**。
原问题、逐字段合同与有界运行见 [#56 调查](../../../docs/research/conpty-native-margin-path.md)。

候选在同一 `AdaptDispatch` 中保存隐藏主屏 margin，新建备用屏使用默认 margin，返回主屏恢复保存值；
重复 `1049h` 不覆盖隐藏主屏值，重复 `1049l` 不清空主屏当前值。它没有改源 fixture、运输、API 表、宽度 flag、
TermForge 默认后端或现有官方双 SHA 校验。附加的上游 `AlternateScreenMarginsTest` 也尚未执行。

## 下一次隔离构建的具体入口

在单独的 Microsoft Terminal 源码 checkout 固定上述提交，校验 [validation.json](../results/2026-10-26-native-margin-path/validation.json)
的源文件 SHA 后，先用 `git apply --unidiff-zero --check` 检查、再以同一选项应用本 patch。完整保留上游许可证、依赖锁和构建日志。
按固定 [上游 README](https://github.com/microsoft/terminal/blob/dc8ae365847d41f62d1ebca93815bb00959bc12f/README.md#building-the-code)
和实际构建文件准备工具链；固定提交的
[`x64-windows-static.cmake`](https://github.com/microsoft/terminal/blob/dc8ae365847d41f62d1ebca93815bb00959bc12f/dep/vcpkg-overlay-triplets/x64-windows-static.cmake)
明确要求 **v145**，README 中的 VS2022 概述不能证明仅有 v143 的宿主满足依赖构建。
不要执行会更改个人系统的 winget/Developer Mode 安装步骤。优先使用已有工具或隔离构建宿主。

从上游根目录加载 `tools/OpenConsole.psm1`、调用 `Set-MsBuildDevEnvironment` 后，逐一构建这些真实项目入口，
使用 `Configuration=Release`、`Platform=x64`、正确的根 `SolutionDir`、restore 和单 worker；每一步非零立即停止：

- `src/terminal/adapter/ut_adapter/Adapter.UnitTests.vcxproj`：运行 `ConAdapter.Unit.Tests.dll` 的新增测试、
  已有 scrolling/cursor/reset 测试以及完整 adapter 测试。
- `src/host/exe/Host.EXE.vcxproj`：生成 `OpenConsole.exe`，包含本次 adapter 修订。
- `src/winconpty/dll/winconptydll.vcxproj`：从同一完整提交/依赖/配置生成配套 DLL。

这些入口来自同一提交的 `OpenConsole.slnx`；不是已执行或已验证的构建命令结果。
测试入口可用上游 `Invoke-OpenConsoleTests -Test adapter`，其映射在 `tools/tests.xml`，须限制为无交互 unit 路径。

### 一次性 hosted 构建入口

[专用 workflow](../../../.github/workflows/conpty-upstream.yml)只在本目录/自身的 PR 改动或显式 dispatch 时运行，
独立于默认产品构建。它固定上述源码、上游 manifest 的 vcpkg baseline `927f62e4b8838bd7e441e9c45103a16ffd75007e`、
vcpkg 工具 2026-04-08 的发布 SHA、现有 patch 与关键源码 SHA；在真正的 GitHub-hosted Windows 环境核对 v145/SDK。
临时宿主的 checkout 同时固定 `core.autocrlf=false` 和 `core.eol=lf`，避免 `text=auto` 在 Windows 上改变补丁字节；原 SHA 校验保持严格。
vcpkg 使用完整 Git 历史，HEAD 仍固定在原 baseline；[历史 port 的 tree 对象不能由 shallow checkout 保证](https://learn.microsoft.com/en-us/vcpkg/users/versioning-troubleshooting)。
上游 NuGet 锁定版本、vcpkg overlay 和 terminal feature 均保留。宿主镜像与实际编译器版本/哈希另记，不能据 runner 名称假定工具不变。

[脚本](build_hosted.ps1)先只应用已有测试，编译并要求一次 `AlternateScreenMarginsTest` 的明确 margin 断言失败；
缺包、编译错误、加载错误、零测试、跳过或超时均不算 red。再仅应用两个实现文件的 patch，重建并分别执行一次 focused 和完整 adapter 测试，
通过后才从同源构建 EXE/DLL。TAEF focused/session 期限分别为60/600秒，整个构建 job 最长45分钟，没有自动重试。
[判据](taef_result.py)和[拒绝用例](test_taef_result.py)防止把进程失败、缺失执行或局部测试当作完整结果。
TAEF 参数语义来自 [Microsoft 文档](https://learn.microsoft.com/en-us/windows-hardware/drivers/taef/te-exe-command-line-parameters)，
单失败 red 只接受退出码1；[harness/超时高位](https://learn.microsoft.com/en-us/windows-hardware/drivers/taef/exit-codes-for-taef)不得混作断言失败。

workflow 保存白名单 summary、实际包/工具/双文件 SHA；完整日志仅在 CI 日志中，额外文件留在不上传的 `_private`。
测试通过后的短期实验 artifact 带上游 LICENSE/NOTICE 和补丁，不包含 TAEF、NuGet 工具或 WindTerm 文件。
这些是可执行的验证步骤；其实际结果由对应 PR/#56 记录，入口存在不表示已经编译或通过。
即使 adapter 通过，仍须新 fork 身份、原175B四阶段、原生/运输/候选末端及反例验证；不能加载为现有官方包或据此结项。

构建成功后，为**新的 fork 双文件身份**单独增加显式、默认关闭的测试配置与审查清单；保持当前官方包校验失败即拒绝。
不能把 fork 冒充官方 1.25，也不能用新的 EXE 混配另一个版本的 DLL。create/resize/close 必须来自同模块，
模块存活至 HPCON、读线程和子进程回收之后。记录来源、补丁、工具链、文件 SHA、许可和失败路径。

先用原 175B/四阶段/两尺寸，原 5 秒 child wait、15 秒外部期限、5 秒 cleanup、自有 Job 与 EOF 验证，
同时执行原生 oracle 和实际候选运输回放。再加无备用屏、重复 h/l、默认 margin、样式与光标、
窄/短 resize、历史隔离、查询与 API 写入交叉的反例。只有获得真实原生与末端 red→green，才能提出采用与上游提交。
后续要按上游更新 rebase 小补丁、重建同包双文件并执行同一矩阵；无可维护构建/升级证据则不进入产品配置。
