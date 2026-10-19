# TermForge

**以性能证据驱动的完整开源终端项目。**

目标是在核心日常场景中达到 WindTerm 的速度、响应和内存效率，同时提供可构建、可测试、可长期维护的完整源码。

An independent, fully open-source terminal project driven by reproducible performance evidence. WindTerm is a comparison target, not an upstream product or affiliation.

> 当前阶段：研究与基准设计。尚无可用客户端、已完成的性能测量或性能等价声明。`TermForge` 是项目暂名。

## 项目原则

- 先定义正确性与性能目标，再通过原型选择技术栈；目前不锁定语言、GUI 框架或渲染后端。
- 性能同时包含吞吐、交互延迟、CPU、内存、稳定性与多会话公平性。
- 不以丢数据、关闭必要功能、无界缓冲或延迟呈现换取漂亮跑分。
- 优先复用维护中的开源协议与基础库，独立实现产品整合。
- 所有产品源码、测试、资源和构建流程都应可获得；不依赖 WindTerm 二进制或专有资源。
- 先验证性能核心，再做到日常可用，最后扩展高级功能。

## 从这里开始

| 文档 | 内容 |
|---|---|
| [总体计划](docs/PLAN.md) | 分阶段交付与决策关卡 |
| [首个里程碑](docs/milestones/M0-baseline.md) | WindTerm 基线工具、实验及原型规格 |
| [性能测量规范](benchmarks/README.md) | 场景、指标、环境控制与正确性 |
| [运行记录模板](benchmarks/run-template.json) | 不包含虚构结果的记录格式草案 |
| [功能矩阵](docs/FEATURES.md) | 首版范围与高级能力 |
| [WindTerm 调查](docs/research/windterm-audit.md) | 已公开/未公开内容和静态证据边界 |
| [架构决策 ADR-0001](docs/adr/0001-evidence-before-stack.md) | 性能基线先于技术栈 |
| [贡献指南](CONTRIBUTING.md) | 如何提交可验证的工作 |

## 路线

1. **M0 — 基线与原型规格**：可复现输入、测量工具、WindTerm 结果、瓶颈假设。
2. **M1 — 最小终端核心**：完整输入输出路径，模块独立计量，候选架构实验。
3. **M2 — 核心性能达标**：正确性与性能共同通过，明确选型。
4. **M3 — 日常可用**：本地终端、SSH/SFTP、会话、多标签分屏、配置与安装包。
5. **M4 — 高级功能与发布**：兼容性扩展、跨平台验证和持续回归。

阶段 0 优先在 Windows 上建立基线；其他平台的支持范围由测量和工程验证决定。各阶段的名称不代表当前已经实现相关能力。

## 许可证与来源

原创代码和文档采用 [MIT](LICENSE)。第三方组件保留其自身许可证与归属。

本项目与 WindTerm/KingToolbox 无隶属关系。研究中的公开协议、功能观察、源码事实和推断须区分；不将专有代码、主题、协议定义资源或反编译结果自动视为可复用源码。详见 [来源规则](docs/PROVENANCE.md)。
