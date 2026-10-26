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
准备工具链；不要执行会更改个人系统的 winget/Developer Mode 安装步骤。优先使用已有工具或隔离构建宿主。

从上游根目录加载 `tools/OpenConsole.psm1`、调用 `Set-MsBuildDevEnvironment` 后，逐一构建这些真实项目入口，
使用 `Configuration=Release`、`Platform=x64`、正确的根 `SolutionDir`、restore 和单 worker；每一步非零立即停止：

- `src/terminal/adapter/ut_adapter/Adapter.UnitTests.vcxproj`：运行 `ConAdapter.Unit.Tests.dll` 的新增测试、
  已有 scrolling/cursor/reset 测试以及完整 adapter 测试。
- `src/host/exe/Host.EXE.vcxproj`：生成 `OpenConsole.exe`，包含本次 adapter 修订。
- `src/winconpty/dll/winconptydll.vcxproj`：从同一完整提交/依赖/配置生成配套 DLL。

这些入口来自同一提交的 `OpenConsole.slnx`；不是已执行或已验证的构建命令结果。
测试入口可用上游 `Invoke-OpenConsoleTests -Test adapter`，其映射在 `tools/tests.xml`，须限制为无交互 unit 路径。

构建成功后，为**新的 fork 双文件身份**单独增加显式、默认关闭的测试配置与审查清单；保持当前官方包校验失败即拒绝。
不能把 fork 冒充官方 1.25，也不能用新的 EXE 混配另一个版本的 DLL。create/resize/close 必须来自同模块，
模块存活至 HPCON、读线程和子进程回收之后。记录来源、补丁、工具链、文件 SHA、许可和失败路径。

先用原 175B/四阶段/两尺寸，原 5 秒 child wait、15 秒外部期限、5 秒 cleanup、自有 Job 与 EOF 验证，
同时执行原生 oracle 和实际候选运输回放。再加无备用屏、重复 h/l、默认 margin、样式与光标、
窄/短 resize、历史隔离、查询与 API 写入交叉的反例。只有获得真实原生与末端 red→green，才能提出采用与上游提交。
后续要按上游更新 rebase 小补丁、重建同包双文件并执行同一矩阵；无可维护构建/升级证据则不进入产品配置。
