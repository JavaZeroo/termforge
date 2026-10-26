# 贡献指南

R1 基础文字交互已按限定合同验收，当前推进 R2 的联合配置、兼容性、输入与交付验收，并行处理性能和环境缺口。

开始前阅读[总体计划](docs/PLAN.md)、[当前能力](docs/FEATURES.md)、[R2 交付路线与验收](docs/milestones/R2-local-terminal.md)和[任务跟踪](docs/TRACKING.md)。已验基础合同见 [R1](docs/milestones/R1-local-text.md)；先核对 issue 的真实阻塞、目标构建配置和工作树，再选取范围明确的任务。PR 说明问题、最终行为、验证方法和限制，并更新对应能力行。按 [ADR-0006](docs/adr/0006-confirmed-project-direction.md)，目标范围、主要技术路线、验收门槛和重要差异需用户确认；ADR 记录提议、证据和确认依据。研究、候选实验及已确认方向内的实现和修复可自主推进。

规划文件各维护一种事实：能力在 FEATURES、路标范围在 PLAN、交付顺序和验收在当前路标、实时进度和依赖在 GitHub。TRACKING 和 EVIDENCE 分别提供任务与来源导航。源码注释说明机制与不变量，TODO 指向责任 issue；不要把“这一批已完成”“下一波”等动态进度写进实现。固定实验、协议和历史结果的身份应保留，不机械改名。

## 性能结果

提供具体应用版本/构建、输入 SHA-256、环境、配置、重复样本和正确性检查。说明时间端点是生产完成、消费完成还是画面呈现；不能混为一谈。报告退化和波动，不只挑最佳结果。

关键指标逐项对标 WindTerm 2.7.0，首个完整版本环境为 Windows 11 x64。先测可靠基线，再提出容差并与用户明确；未经确认的数值只作提案，不用于宣布性能验收通过。终端行为默认原版优先；与规范、第三方核心或项目策略冲突时保留原版证据和差异，明确缺陷的修正由用户确认。

## 来源与隐私

遵守 [PROVENANCE](docs/PROVENANCE.md)。使用自生成输入与隔离测试配置，上传前检查用户名、路径、主机名、地址和凭据。第三方实现单独说明许可证；项目 MIT 许可不覆盖未获授权的第三方材料。

## 干净构建与自动检查

当前交付是 Python 工具、有限 C++17 核心实验与支持物理行缩放的 Windows shell 原型，尚无日常开发版。需要 CMake 3.20+、Python 3.10+ 和 C++17 编译器；主 CI 使用 Python 3.12、Windows Server 2025 runner 的 Visual Studio 与 Ubuntu 24.04 / GCC，本机另验证 Windows 11 / Visual Studio 2022。正常构建使用仓库内已生成的 Unicode 表，不访问 WindTerm 安装目录，也不下载私有程序。

从仓库根目录运行，推荐使用 Python 虚拟环境：

```sh
python -m pip install -r benchmarks/requirements.txt
python -m pip install --require-hashes -r experiments/differential/requirements.txt
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
git diff --check
```

CTest 自动把当前构建的二进制路径交给测试，不依赖开发机旧构建。`replay`、`terminal_trace`、`recovery_probe`、`resize_test` 与核心计量在 Windows/Linux 构建；匿名管道、ConPTY、呈现探针和 `termforge_shell` 仅在 Windows 构建。恢复、计时校准、自有窗口与真实 shell 回归均纳入 CTest。Linux 跳过明确依赖 Windows 的用例，不把缺失的 Windows 二进制当作已通过。只构建源码时可加 `-DBUILD_TESTING=OFF`，此时不要求 Python 测试依赖。

默认可执行文件位于 `build/bin/Release`（Visual Studio）或 `build/bin`（Make/Ninja）。单独运行测试或完整回放时，支持 `TERMFORGE_BIN_DIR` 环境变量；回放脚本也接受 `--bin-dir`。显式指定的目录缺文件会报错，不回退到旧构建。例如：

```powershell
# Windows / Visual Studio；Linux / Make 或 Ninja 使用 build/bin。
$env:TERMFORGE_BIN_DIR = (Resolve-Path build/bin/Release).Path
python -m unittest experiments.core.test_core experiments.core.test_unicode experiments.receive.test_receive -v
python -m experiments.core.run --out local/experiments/core-new
python -m experiments.core.run --fixture unicode --out local/experiments/core-unicode-new
```

完整回放的输出目录必须不存在；默认在 Windows 验证文件与管道两条路径，在 Linux 只验证文件回放，也可显式使用 `--route replay`。自动回放与核心支持边界见[核心说明](experiments/core/README.md)。

[GitHub Actions](.github/workflows/build.yml) 在干净检出上构建并执行相同检查及两套完整回放。Windows 剪贴板/窗口合成场景保留成功或失败产物；其余失败产物限于本次 CTest/CMake 日志和 `build/test-artifacts` 内的合成用例结果。主构建不上传仓库 `local/`、WindTerm、个人终端会话或完整工作目录。[R1 参考探针](.github/workflows/r1-reference.yml)在相关探针文件进入 main 后自动运行，也保留手动入口；[IME 工作流](.github/workflows/r1-ime.yml)的自动入口只做有界只读盘点，语言准备与真实输入须显式选择 `verify`。两者仅上传白名单事实和自有探针记录。测试可通过 `TERMFORGE_TEST_ARTIFACTS` 指定合成记录目录。CI 的运行时间用于回归诊断，不是受控性能基准。

Server 2022 的 ConPTY 稳态句柄增长尚未解决，未列入已验收平台。它保留为[按需兼容性探针](.github/workflows/conpty-compat.yml)，运行同样严格的断言，失败仍报告失败；[原因与证据](docs/research/conpty-platform-check-2026-10-22.md)独立保留，不能把现代 Windows 主 CI 通过解释为旧系统也通过。

## Windows Alpha 构建与打包

首个 Alpha 是 R1 原型的 Windows x64 免安装体验包，不代表 R2 开发版验收。正式打包使用独立构建目录、Release 和静态 MSVC 运行库 `/MT`，不要求用户安装 Visual C++ Redistributable。Python、CMake 和编译器仅用于构建/验证，不是运行包依赖。

以下命令从仓库根目录运行；先安装上文测试依赖，`local/alpha-smoke-new` 必须不存在：

```powershell
cmake -S . -B build-alpha -A x64 -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded
cmake --build build-alpha --config Release --parallel 2
ctest --test-dir build-alpha -C Release --output-on-failure
python tools/package_alpha.py --bin-dir build-alpha/bin/Release --out-dir dist --version 0.1.0-alpha.1
Expand-Archive -LiteralPath dist/TermForge-v0.1.0-alpha.1-windows-x64.zip -DestinationPath local/alpha-unpacked-new
python tools/alpha_smoke.py --bundle local/alpha-unpacked-new/TermForge-v0.1.0-alpha.1-windows-x64 --test-bin build-alpha/bin/Release --out local/alpha-smoke-new
```

ZIP 包含 `termforge_shell.exe`、`shell_clipboard_reader.exe`、三个 `assets/fonts` 文件、`LICENSE`、`README.txt` 和 `build-info.json`；`SHA256SUMS.txt` 作为单独发布资产。保留资源许可和原始字体字节；打包清单不包括测试二进制或个人配置。包内身份与哈希用于核对构建输入和文件，不能单独证明可复现的逐字节二进制构建。

[Alpha 工作流](.github/workflows/alpha-release.yml)从干净源码构建、验证并保留待发布产物；上传 Actions artifact 不等于发布 GitHub release。审核产物和验证证据后，再以 `v0.1.0-alpha.1` 创建 GitHub prerelease。此包未签名，已验环境限 Windows 11 本机和 Windows Server 2025 CI；最低系统版本与其他 Windows 版本仍须单独验收。布局写入 `%LOCALAPPDATA%\TermForge\workspace-v1.layout`，所以免安装不代表所有数据都在解压目录。
