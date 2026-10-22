# 无界面定向研究

入口仅分析固定官方 WindTerm 2.7.0 Windows x64，不执行目标。原始索引、数据库、反编译和汇编输出全部保留在 gitignored `local/`。公开结论见[首轮报告](../docs/research/windterm-headless-2026-10-22.md)。这不是客户端启动器，也不测量运行时性能。

## 固定环境

本轮使用 Python 3.12.10、Ghidra 12.1.4、Temurin JDK 21.0.12.1+1。以下下载校验值已实测核对；先校验再解压到 `local/toolchains/`，无需安装到系统 PATH。

| 来源 | 文件 | SHA-256 |
|---|---|---|
| [Ghidra 官方发行](https://github.com/NationalSecurityAgency/ghidra/releases/tag/Ghidra_12.1.4_build) | `ghidra_12.1.4_PUBLIC_20260921.zip` | `ddac49f903da9d5bac833e5cc79395098b9c33cfd3279be5f31bd00387d2d4db` |
| [Temurin 官方发行](https://github.com/adoptium/temurin21-binaries/releases/tag/jdk-21.0.12.1%2B1) | `OpenJDK21U-jdk_x64_windows_hotspot_21.0.12.1_1.zip` | `f9d6e191ab098c0d416e7d588a24420a8621cd2f4720dab2459b8b7b2d2d8b4e` |

官方目标的获取和身份见[隔离实验环境](../benchmarks/tools/WINDTERM-LAB.md)。从仓库根目录执行，输出目录必须不存在：

```powershell
.venv/Scripts/python research/run_headless.py `
  --target local/windterm-official/WindTerm_2.7.0/WindTerm.exe `
  --ghidra local/toolchains/ghidra_12.1.4_PUBLIC `
  --java-home local/toolchains/jdk-21.0.12.1+1 `
  --out local/analysis/windterm-new-run
```

该入口验证目标哈希、Ghidra 版本和输出位置，为子进程设置 Java，限制分析 CPU 数为 2、等待上限为 1800 秒。失败保留日志，超时只终止自有分析进程树。`run.json` 记录目标/脚本/运行器哈希、工具版本、退出状态和目标是否保持原样。索引脚本逐项导出函数与候选引用；每个候选最多保留 256 条引用，并记录总数和截断标志。

## 复用数据库，定向检查

已导入后使用 `-process -noanalysis -readOnly`，避免反复全量分析。下列命令的 Java 环境仅用于当前 PowerShell 进程；推荐由代理在临时子进程执行。替换运行目录和明确的函数入口，输出目录必须不存在：

```powershell
$env:JAVA_HOME=(Resolve-Path local/toolchains/jdk-21.0.12.1+1).Path
$env:PATH="$env:JAVA_HOME\bin;"+$env:PATH
& local/toolchains/ghidra_12.1.4_PUBLIC/support/analyzeHeadless.bat `
  "$PWD/local/analysis/windterm-new-run/projects" WindTerm `
  -process WindTerm.exe -noanalysis -readOnly `
  -scriptPath "$PWD/research/ghidra" `
  -postScript InspectSelectedFunctions.java `
  "$PWD/local/analysis/windterm-new-run/selected" 1402e4cc0
```

定向脚本输出每个函数的本地 `.asm`、`.c` 和 `.json`，单函数反编译超时 60 秒。应检查 JSON 的完成状态与日志；工具进程成功退出不等于每个函数都成功反编译。直接调用 Java 脚本没有 Python 入口的目录保护，调用者须保持输出在 `local/`。不要提交这些输出或从中复制私有实现。

```powershell
.venv/Scripts/python -m unittest discover -s research/tests -v
```

守卫测试不运行目标或 Ghidra。真实导入与只读复查的验证记录见报告。Ghidra 的推断类型、间接调用和未定义数据可能不完整；重要结论需回查指令和独立运行实验。
