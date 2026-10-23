# 贡献指南

项目当前优先接受基准设计、测量工具、公开协议用例、来源明确的研究和小型架构实验。

开始前阅读 [总体计划](docs/PLAN.md) 和 [M0](docs/milestones/M0-baseline.md)。选取一个明确任务，在 PR 中说明问题、最终行为、验证方法和限制。大型依赖或技术栈变更需要记录 ADR 和实验依据。

## 性能结果

提供具体应用版本/构建、输入 SHA-256、环境、配置、重复样本和正确性检查。说明时间端点是生产完成、消费完成还是画面呈现；不能混为一谈。报告退化和波动，不只挑最佳结果。

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

[GitHub Actions](.github/workflows/build.yml) 在干净检出上构建并执行相同检查及两套完整回放。失败时仅上传本次生成的 CTest/CMake 日志和 `build/test-artifacts` 内的合成用例结果；不会上传仓库 `local/`、WindTerm、个人终端会话或完整工作目录。测试可通过 `TERMFORGE_TEST_ARTIFACTS` 指定合成失败记录目录。CI 的运行时间用于回归诊断，不是受控性能基准。

Server 2022 的 ConPTY 稳态句柄增长尚未解决，未列入已验收平台。它保留为[按需兼容性探针](.github/workflows/conpty-compat.yml)，运行同样严格的断言，失败仍报告失败；[原因与证据](docs/research/conpty-platform-check-2026-10-22.md)独立保留，不能把现代 Windows 主 CI 通过解释为旧系统也通过。
