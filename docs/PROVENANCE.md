# 来源与公开边界

原创代码/文档采用 MIT。所有第三方代码和资产须记录来源、版本、许可证和修改情况；不能给第三方材料重新套用项目许可证。

可进入仓库的材料：原创实现、公开规范链接、简洁事实性研究、授权明确的第三方代码、自生成测试输入和审查后的测量结果。

不进入仓库的材料：WindTerm 可执行文件/DLL、专有终端定义/主题/资源、个人连接配置和凭据、崩溃转储、大批反编译或字符串提取内容。二进制研究的功能推断不等于源码复用授权。

来源判断不得只凭“GitHub 上能看见”或“文件是文本”。对于 WindTerm，官方已发布自有源码、第三方依赖及发行包资源具有不同的许可边界。

研究报告分别标记观察、推断和未知；公开结果应删除个人路径、账户、主机信息。测试优先采用隔离配置及合成数据。

## 核心代码来源的记录口径

按 [ADR-0006](adr/0006-confirmed-project-direction.md) 认真研究、借鉴原版公开代码及关键机制。模块的实现来源与行为依据分开记录：直接复用 WindTerm 公开源码须注明固定版本、许可和修改；根据原版运行观察或逆向研究形成的实现须链接具体行为/机制证据；按功能合同独立开发须注明合同；Alacritty 等其他开源实现及其修改单独记录上游来源，不能归为 WindTerm 代码。

“逆向研究指导的原创实现”和“直接复制反编译输出”是不同来源，不用同一个代码占比混算。无法确定来源时标为待核实，不能自动归入自研。后续核心代码统计须固定提交、构建配置、代码范围及计数方法，并将第三方代码、生成数据与测试/研究工具分列；模块受到原版行为启发不等于其中每行都是原版源码。

## 已使用的第三方数据

- `assets/fonts/RobotoMono-Regular.ttf`：从 Google Fonts 的 RobotoMono 官方仓库固定提交独立取得，未修改；适用 SIL Open Font License 1.1，完整版权与许可证随字体分发。字体源码、固定版本、原始链接、字节数和 SHA-256 见[字体来源](../assets/fonts/README.md)。只在 TermForge 进程内作为工作区 UI 字体加载；不是从 WindTerm 提取的资源，不受项目 MIT 许可证覆盖。

- 差分测试依赖 `pyte 0.8.2`（LGPLv3）和 `wcwidth 0.2.13`（MIT），固定 wheel 哈希并保留原包许可；没有复制其源码或将其作为产品组件。版本、官方来源与适配边界见[差分说明](../experiments/differential/README.md)。原创项目许可证不替代这些依赖的许可证。

- `experiments/core/unicode_width.hpp`：通过原创生成器从 Python 3.12.10 `unicodedata` 所带 Unicode 15.0.0 属性数据生成，含项目自己的宽度/拒绝策略。未复制 CPython 实现源码。Unicode 数据保留[官方许可](../experiments/core/UNICODE-LICENSE.txt)与归属，来源为 [Unicode](https://www.unicode.org/)；正常构建使用固定生成文件。此数据不归项目 MIT 许可覆盖。

- 外部核心候选当前使用 `alacritty_terminal 0.26.1-dev+termforge.5`（Apache-2.0），固定上游 commit `542147067dd016fc5f49120ef0ba605a98a4e3a7` 的 manifest/changelog/许可证与 `src/`，并保留 ED、稳定行身份/淘汰、安全 swap、每缓冲区滚动区域、流式字素和显式历史清空代次补丁；精确传递依赖及校验值由 `Cargo.lock` 固定。未复制上游 46 MiB 行为录制，候选不进入当前产品包；默认 CMake 不依赖 Rust，显式适配器构建使用 staticlib。原始 0.26.0 身份、当前来源、许可证和补丁清单见[候选来源台账](../experiments/candidates/alacritty/PROVENANCE.md)。

- 候选新增精确锁定 `unicode-segmentation 1.13.3`（Unicode 17，MIT OR Apache-2.0），官方来源为 [unicode-rs](https://github.com/unicode-rs/unicode-segmentation) 与 [crates.io 固定版本](https://crates.io/crates/unicode-segmentation/1.13.3)。保留原依赖许可证；与既有 `unicode-width 0.2.2` 同使用 Unicode 17，默认自研核心仍使用 Unicode 15。版本、包哈希、MSRV、依赖成本和修改边界见[候选来源台账](../experiments/candidates/alacritty/PROVENANCE.md)。

- 候选规范测试数据 `GraphemeBreakTest-17.0.0.txt` 从 [Unicode 17 官方数据](https://www.unicode.org/Public/17.0.0/ucd/auxiliary/GraphemeBreakTest.txt)原样取得（766 例、126570 字节，SHA-256 `e2d134d2c52919bace503ebb6a551c1855fe1a1faec18478c78fff254a1793ec`），随附[Unicode License V3](../experiments/candidates/alacritty/vendor/alacritty_terminal/tests/data/UNICODE-LICENSE.txt)，不由项目 MIT 许可证覆盖。它验证分段 helper 的规范边界，不代表完整 Term/VT 状态正确性；原始执行字节身份与 Git blob 换行身份分列保存在[验证记录](../experiments/shell/results/2026-10-26-streaming-graphemes/validation.json)。
