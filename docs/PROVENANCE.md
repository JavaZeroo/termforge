# 来源与公开边界

原创代码/文档采用 MIT。所有第三方代码和资产须记录来源、版本、许可证和修改情况；不能给第三方材料重新套用项目许可证。

可进入仓库的材料：原创实现、公开规范链接、简洁事实性研究、授权明确的第三方代码、自生成测试输入和审查后的测量结果。

不进入仓库的材料：WindTerm 可执行文件/DLL、专有终端定义/主题/资源、个人连接配置和凭据、崩溃转储、大批反编译或字符串提取内容。二进制研究的功能推断不等于源码复用授权。

来源判断不得只凭“GitHub 上能看见”或“文件是文本”。对于 WindTerm，官方已发布自有源码、第三方依赖及发行包资源具有不同的许可边界。

研究报告分别标记观察、推断和未知；公开结果应删除个人路径、账户、主机信息。测试优先采用隔离配置及合成数据。

## 已使用的第三方数据

- `assets/fonts/RobotoMono-Regular.ttf`：从 Google Fonts 的 RobotoMono 官方仓库固定提交独立取得，未修改；适用 SIL Open Font License 1.1，完整版权与许可证随字体分发。字体源码、固定版本、原始链接、字节数和 SHA-256 见[字体来源](../assets/fonts/README.md)。只在 TermForge 进程内作为工作区 UI 字体加载；不是从 WindTerm 提取的资源，不受项目 MIT 许可证覆盖。

- 差分测试依赖 `pyte 0.8.2`（LGPLv3）和 `wcwidth 0.2.13`（MIT），固定 wheel 哈希并保留原包许可；没有复制其源码或将其作为产品组件。版本、官方来源与适配边界见[差分说明](../experiments/differential/README.md)。原创项目许可证不替代这些依赖的许可证。

- `experiments/core/unicode_width.hpp`：通过原创生成器从 Python 3.12.10 `unicodedata` 所带 Unicode 15.0.0 属性数据生成，含项目自己的宽度/拒绝策略。未复制 CPython 实现源码。Unicode 数据保留[官方许可](../experiments/core/UNICODE-LICENSE.txt)与归属，来源为 [Unicode](https://www.unicode.org/)；正常构建使用固定生成文件。此数据不归项目 MIT 许可覆盖。

- 外部核心候选实验使用 `alacritty_terminal 0.26.0`（Apache-2.0），crate SHA-256 为 `bda177466b9524d59f1b12f0dd30b68696788e9992a7e959021c4a0ed96fcf59`；精确传递依赖及校验值由 `Cargo.lock` 固定。未 vendoring 上游源码，候选不进入当前产品包或默认 CMake 依赖。官方来源、许可边界和复现入口见[候选说明](../experiments/candidates/alacritty/README.md)。
