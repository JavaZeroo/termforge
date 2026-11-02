# WindTerm 2.7.0：固定纯高度缩小与恢复

一次新进程完成 `91×20 → 91×14 → 91×20`。三阶段的完整合成文档保持相同；缩高时 DSR 从 `19;7` 变为 `14;7`，所有 marker 的查询矩形上移 90 px。恢复高度后，DSR 和 marker 矩形都保持缩高后的值，**本样本没有回到初始正文显示位置或初始协议光标行**。

这是直接 Telnet 的有限行为观察，`status=observation`、`semantic_acceptance=false`。原版内部 history 存储、完整活动网格、Qt/ConPTY 整合及 R2 验收均未由此证明。

## 来源与一次运行边界

[strict-export.json](strict-export.json) 保留 VM 严格导出的原字节，SHA256 `32d3f6cf2d1c6746ff517b684ecd8c631c86ecc87874209ba5f831ea244730e0`；独立本地核对见 [audit.json](audit.json)。

- 执行源码：`50a560739e45d00ee31177d84b7c54a144f5273e`；[新协议](../../resize_height_measured_reference.md) case `height-retention-measured`，1 次原版运行、3 个阶段。
- 源 ZIP SHA256：`a1171426f142a9fac23e7f163b8ad68d4f2871aa5fedc17dd232f9495bdd5684`；执行源 manifest SHA256：`14cf4a37472bd90abd30dfd1b1dc916cd1b904f60e4502e10763f7b71b4bfe38`。
- 单次计划 SHA256：`2c58a7de70724ab61f9606c836cdde2095405f91c9d77e114733d3e054184f9c`。初始发送固定 75 B，再按阶段发送 DSR 共 12 B，应用回复共 21 B；之后没有 CUP、CR/LF、补写或重绘。
- 请求和实际窗口都是 `1360×749 → 1360×569 → 1360×749`，无自动调参或重试。缩高端来自[独立几何调查](../2026-11-02-resize-geometry-reference/README.md)；恢复端在本次首次单独检验。
- 本地核对 10 个执行源和 3 个出口源：源 ZIP、当前冻结字节、指定提交与公开哈希全部一致；逐项重建 3 份完整文档及 21 个 marker。完整原版包、私有 raw/provider 和计划核验由 VM 严格出口完成，本地没有重开那些原件。

[旧 10 行预测失败](../2026-11-02-resize-height-reference/README.md)仍保留原源码和原计划；本次使用独立 namespace 与身份，不将其失败改成 14 行通过。旧 `width-then-height` 仍为 0 次原版运行。

## 三阶段的直接结果

| 记录 | 初始 | 缩高后 | 恢复高度后 |
| --- | --- | --- | --- |
| NAWS，列×行 | 91×20 | 91×14 | 91×20 |
| terminal capacity | 20 | 14 | 20 |
| DSR，1-based | 19;7 | 14;7 | 14;7 |
| API 文档行数 | 19 | 19 | 19 |
| API scroll | 0 | 5 | 5 |
| API top | null | null | null |
| caret API `(line,column,position)` | (17,0,131) | (17,0,131) | (17,0,131) |

NAWS 记录仅这三组，冻结事件数为 `1,2,3`；两次 resize 都有各自的前后 NAWS 边界和准确窗口检查。caret API 数值与 DSR 分开记录，不将 `caretColumn=0` 解释成协议第 0 列，也不将 `top=null` 补成 5。

三阶段完整逻辑文档数组均为 19 个字符串，零基下仅以下内容非空：

| API 文档行 | 完整内容 |
| ---: | --- |
| 1 | `HTOP` |
| 5 | `HARD_A` |
| 6 | `HARD_B` |
| 11 | 89 个空格，随后 `uvw`，共 92 个 ASCII 字符 |
| 17 | `LOWEND` |

其余 14 行都是空字符串，包括末尾第 18 行。完整数组按 `json.dumps(lines,separators=(",", ":"))` 编码的 SHA256 为 `ac5cc0c46768d14945e8c63103f3d7d56f9a5fc2f5631dd4fe8f485e9ba19efc`，三阶段逐项相等。这里验证的是实际 API 文档数组，不把 19 个逻辑字符串当成 19 条活动物理行；`uvw` 的逻辑行在 91 列显示中跨行。

## 源位置与显示矩形

所有 marker 各一次；源行、行内文本 offset、API position 在三阶段保持相同。以下行/offset 为零基，仅此 ASCII 源的 UTF-8 字节、Unicode 标量与 UTF-16 单元数量相同；不推广为一般 API 位置单位。

| marker | 源行 / 行内 offset | API position | x，px | 初始 y | 缩高 y | 恢复 y |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| HTOP | 1 / 0 | 1 | 147 | 18 | -72 | -72 |
| HARD_A | 5 / 0 | 9 | 147 | 90 | 0 | 0 |
| HARD_B | 6 / 0 | 16 | 147 | 108 | 18 | 18 |
| u | 11 / 89 | 116 | 859 | 198 | 108 | 108 |
| v | 11 / 90 | 117 | 867 | 198 | 108 | 108 |
| w | 11 / 91 | 118 | 147 | 216 | 126 | 126 |
| LOWEND | 17 / 0 | 125 | 147 | 324 | 234 | 234 |

查询矩形宽、高均为 `8×18 px`，只描述查询起点所在单格，不是整个 marker 的包围框。`HTOP` 后两阶段矩形位于负 y；这不能写成它仍在可见区域内。其他正坐标也不是实际 glyph 呈现验收。本次没有采集 viewport 裁切矩形或 raster，实际可见像素范围仍不由这些坐标代填。

## 对下一步实现的约束与未知

可将这条精确轨迹用作核心字面回放：正文保留、软行连续、DSR `19;7→14;7→14;7`，并保留恢复后显示位置。它支持本样本增高时不回拉先前内容；**`scroll=5` 加统一位移不证明原版内部恰好搬移了 5 条 history 物理行**，实现可在现有 owner 内保持可观测等效。

当前 C++ `Screen::resize` 的源码策略是靠底光标缩高时移走顶部行、增高只补空行；这是实现对照，报告没有运行或验收 C++。原版文档没有增加尾部空字符串，也不能据此规定核心内部容量。后续回放须分开保留正文、末尾空容量、协议坐标与显示投影。

未知仍包括：先缩宽再缩高的正文右尾、其他光标位置或底部非空内容、非默认 margin/origin、备用帧、高度后写入、历史容量/淘汰和连续尺寸。纯高度成功不允许调用裁剪式高度路径时悄悄丢失先前宽度保留的正文；宽高交叉需要另一个独立样本。

拥有进程的请求 exit 0、RootReaped/CleanupVerified 为 true、JobAfterActive 为 0；目标 Job 也已无活动进程。这仍是受控终止，不证明自然 GUI 退出。完整活动网格、样式、Unicode、本地 ConPTY、实际呈现、性能和 R2 均未验收。
