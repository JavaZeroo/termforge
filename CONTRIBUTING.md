# 贡献指南

项目当前优先接受基准设计、测量工具、公开协议用例、来源明确的研究和小型架构实验。

开始前阅读 [总体计划](docs/PLAN.md) 和 [M0](docs/milestones/M0-baseline.md)。选取一个明确任务，在 PR 中说明问题、最终行为、验证方法和限制。大型依赖或技术栈变更需要记录 ADR 和实验依据。

## 性能结果

提供具体应用版本/构建、输入 SHA-256、环境、配置、重复样本和正确性检查。说明时间端点是生产完成、消费完成还是画面呈现；不能混为一谈。报告退化和波动，不只挑最佳结果。

## 来源与隐私

遵守 [PROVENANCE](docs/PROVENANCE.md)。使用自生成输入与隔离测试配置，上传前检查用户名、路径、主机名、地址和凭据。第三方实现单独说明许可证；项目 MIT 许可不覆盖未获授权的第三方材料。

## 当前本地检查

当前已有 Python 基准工具和有限 C++ 核心实验，尚无客户端。安装 `benchmarks/requirements.txt` 后执行：

```sh
python -m json.tool benchmarks/run-template.json
python -m unittest discover -s benchmarks/tests -v
python -m unittest discover -s research/tests -v
git diff --check
```

Windows / Visual Studio 2022 C++ 工具链的现有核心检查：

```powershell
cmake -S experiments/receive -B build/receive -G "Visual Studio 17 2022" -A x64
cmake --build build/receive --config Release
python -m unittest experiments.core.test_core experiments.core.test_unicode experiments.receive.test_receive -v
```

自动完整回放与版本限制见[核心说明](experiments/core/README.md)。独立核心构建、可指定二进制路径的测试和 CI 是[当前计划 E1](docs/STATUS-AND-BACKLOG.md)的待办项。后续修改在同一变更中补充必要的构建和测试说明。
