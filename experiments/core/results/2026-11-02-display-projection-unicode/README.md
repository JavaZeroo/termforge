# 有界 Unicode Cell 显示投影与宽字边界回放

`8429935d647a085f63445bca992e85c69c8cffce` 将显式 `HistoryViewport::project` 从单 Cell ASCII 扩展到核心已有的有界 Unicode Cell 表示：保留完整宽字 lead/trail、基本字符及其已有组合标、样式和源映射。测试 / CMake 整合固定于 `de2628641b5fefbda4cc3ccbe17fa752cf4731be`，重新构建后 **core 28/28、1.05 秒；Qt 默认路径 4/4、8.67 秒**。

这些结果不等于完整 Unicode 或 resize 对齐。默认 `Screen::resize` 仍物理裁剪，Qt 仍未接入保留正文 / 新显示投影；新原版结合标后续输入结果尚未由该实现修正。原版字面回放与独立工程检查在下文分别计数。

## 实现合同与工程红绿

[history_view.hpp](../../history_view.hpp)直接复制源 Cell，不从 UTF-8 文本重建格子。宽字两格不能跨显示行或页分裂；末行只剩一格时先发布不属于源正文的显示补白，续游标仍指向同一宽字 lead。投影继续有界：每次最多 200 个源片段，每片段最多 500 个 Cell；一个 lead 最多 68 字节、16 个已有组合标。该表示不是完整字素切分、塑形、emoji/ZWJ 或一般结合标附着算法。

请求从 trail 开始、显示宽度 1 无法容纳宽字、不完整或不一致 lead/trail、非法 UTF-8、超过字节界限及候选 clipped/fragment/spacer 状态均拒绝。失败不发布新布局 revision，也不改变源 Cell、ID/version、epoch/order、历史或协议光标。源窗口不足时仍可回退未完成行，不假装已经推进；返回游标继续绑定源身份。

| 独立记录 | 实际结果 | 计数与身份 |
|---|---|---|
| 原 18 项工程 RED | **11/18 通过**，7 项因仅支持 ASCII 而失败 | 原测试 SHA `cc2a60c4b0148fde55229f726546fd8f480898d9a7ee8070f4554fae5dc74620`；RED EXE 已保存 |
| 同一 18 项首次 GREEN | **18/18 通过** | 保存运行 / 构建日志；该次 EXE 未固定，不用后续二进制代替 |
| 后来补充导入守卫 | **19/19 通过** | 11 种非法导入值在**一个新增 case**内检查，不能写成 29 项或为该新 case 补造 RED |
| 原版宽字字面回放 | **16/16 阶段，240 条断言，零失败** | 1 个原 strict 样本、4 原阶段 × strict/recovery × 整块/逐字节；新增于首次工程 GREEN 之后 |
| 宽字剩一格的分页工程检查 | **4/4 case，64 条断言，零失败** | 四种 feed 的独立工程场景，不计入原版样本或 19 项工程测试 |

[display_projection_unicode_test.cpp](../../display_projection_unicode_test.cpp)的工程范围包含单格非 ASCII、已有 `e + U+0301`、CJK 宽字及样式逐 Cell 保真，完整原子分页、68 字节合法上界、有限源窗口续接、选择 / Find 与旧布局失效。后来加入的 11 种非法导入值覆盖多基本字符、marks 元数据不一致、截断 / 非法 UTF-8、代理项、过长编码、超出 Unicode 范围、孤立零宽字符、控制字符和宽度不一致。既有 ASCII 测试将“所有宽字拒绝”调整为“宽字不能塞进一列显示”，没有将合法宽字继续伪装成 ASCII。

RED 的 `history_view.hpp` SHA 为 `c690f7b5d616875c30d5190eb5bf260bb0345e1cdd77db526d4eb44a9cc000c0`，保存 EXE SHA 为 `ad1a034521404317007796a3eee19d049c409d474cac407aea76aaeb42a38ba9`。原件在 `local/r29-retention/projection-unicode-red-capture/`，7 个归档文件重新核对一致；其 `identity.json` SHA 为 `80731b65e48544f9d21b162936bfc8a339a9ee5f2b700f99ab3e5958ea7c51ee`。这个 RED 的源码头为 `cd946bf`，新增测试 / CMake 仍有工作区身份，不冒称完整已提交构建输入。

首次 GREEN 只有 `projection-unicode-green-build.log` / `projection-unicode-green.log`；随后 19 项用 `projection-unicode-import-build.log` / `projection-unicode-import-test.log`。各轮身份和缺口见 [validation.json](validation.json)。本报告不将初次 GREEN 的缺失 EXE 填成当前 19 项或整合轮的 EXE。

## 原版宽字边界：正文、DSR 与位置单位

[原版两例报告](../../../../research/ui/results/2026-11-02-resize-unicode-reference/README.md)中，本测试只采用 [wide-boundary.json](../../../../research/ui/results/2026-11-02-resize-unicode-reference/wide-boundary.json)，SHA `2b8f8b17bfb95a418c29dbad55a12df13f27df1a92b3c3d2fc01c374087e2b0e`。固定 60B 输入在 `CUP(8,61)` 写 `界Z`，实际网格 `91×20 → 61×20 → 61×20 → 91×20`，第三阶段只写 Q。

| 阶段 | DSR | 精确目标逻辑正文 | 可比较的显示起点，零基 |
|---|---|---|---|
| 初始 | `8;64` | 60 空格 + `界Z` | 界 `(7,60)` |
| 仅缩窄 | `8;61` | 同上；原 Document 另多一个末尾空字符串 | 界 `(8,0)` |
| 仅写 Q | `8;61` | 60 空格 + `Q`，界/Z 消失 | Q `(7,60)` |
| 仅恢复宽度 | `8;61` | 与写 Q 后相同 | Q `(7,60)` |

[resize_unicode_test.cpp](../../resize_unicode_test.cpp)经真实 `resize_columns_preserving` 和 VT 输入回放；每阶段比较实际网格、DSR、全部逻辑 UTF-8 正文及可靠 marker 起点。四个公开完整数组先逐字节和摘要核对，原阶段 1 的末尾空串也保留在字面输入中；与核心比较时仅规范化末尾空字符串容量，不删除中间空行或空格。摘要使用原出口的紧凑 JSON、默认 ASCII escapes，避免把不同 JSON 编码的摘要混同。

界之前全部为 ASCII，字节 / scalar / UTF-16 前缀均为 60，因此其起点可按观测矩形映射到上述显示格；不比较矩形宽 13 与 terminal cell 宽度。Z 的 scalar/UTF-16 前缀为 61，UTF-8 前缀为 63；冻结 API 查询数值为 84，UTF-8 候选解释下位于界的字节范围内，API 一般单位仍未校准。**Z 只参与精确正文比较，不把该 API 矩形当作 Z 的列坐标。** 核心完整两格表示中的 Z 列 62 与这些文本偏移保持区分。

同文件的额外工程场景用 `abc界Z` 与显示宽 4：第一页是 `abc` 加一格显示补白，续游标仍在源列 3；第二页保存完整宽字与 Z。补白命中钳到既有源边界，不创造源 ID 或文字；宽字范围高亮不能染到补白。反向选择 / Find 跨页复制完整 `界Z`，按源 Cell 列 3..6 取范围，不以 UTF-8 字节终点 7 代替。这四次检查不是原版分页 / 选择 / Find 观测。

## 同源码整合与前后身份

整合源码为 `de2628641b5fefbda4cc3ccbe17fa752cf4731be`。执行前于 `2026-11-02T04:07:14.338807+00:00` 固定 63 个选定源码文件及 28 项 core 注册清单；执行后于 `2026-11-02T04:09:37.262144+00:00` 验证选定源码未变化，并固定 32 个实际测试 EXE、1 个仅构建产品入口、同一个输入支持子程序的构建 / 部署两副本、54 个部署依赖、6 份整合日志、6 份构建配置和 1 份注册资料。

本地不可变副本在 `local/r29-retention/unicode-integrated-capture/files/`；`pre.json` SHA 为 `90142276d6d8764c30a0bb06ed8439f5a79a57cf5a68b06feb7f2d3295338e7e`，`post.json` SHA 为 `28bb980b1883e6b335617d28a0e52e6f9b69e0d4ca765dc9b75f31a4f22f64d9`。这是**选定源码 / 注册清单的前置身份**和执行后产物身份；完整工具链、环境和全部构建输入的前置 manifest 仍未保存。

| 当前冻结对象 | SHA-256 |
|---|---|
| `history_view.hpp` | `ccaebb82817e8c7f7f1195225b27d9cb605f4dcfc01cc57dca36a827ec3e4184` |
| 19 项 `display_projection_unicode_test.cpp` | `b1afe46e49e2cf693564e82c321bc22b4938c2d92e394cc6ab4621ac22e99aaf` |
| 当前 19 项测试 EXE | `5957a095bf7e88959c2bbdb3fadaf7c243be1d475679891f9b7bdcf8196d7181` |
| `resize_unicode_test.cpp` | `562bc5d925246372f705a9c07fc35a6bf5b370195d33951fa41b0c5d39738089` |
| 当前宽字回放 EXE | `a60fa11c354ce5e6e5652e361f16b4858a328a7f69048f481175779d41b0d364` |

**core 28/28、1.05 秒**包括新增 `display_projection_unicode_core` 与 `retained_resize_unicode_core`；**Qt 4/4、8.67 秒**仍分别运行默认 shell、Unicode 输入、基础 resize 和五阶段 modes，四项均 `offscreen`，没有额外 `--resize`。两配置独立记录，不合成性能通过率。`termforge_qt_shell.exe` 本轮仅构建；支持子程序的两副本相同，不算第 33 个测试。

两套配置均为 Windows x64 / Visual Studio 17 2022 / MSVC 19.42.34433.0 / SDK 10.0.26100.0 / Release / 单 worker；core 关闭 Qt，Qt 配置单独开启，候选适配器与 resize repro 都关闭。Qt 为 6.10.3，固定 ConPTY 1.25.260930003、CRT 14.51.36247.0；选定部署文件逐项重新核对，不复用旧二进制身份。

整合日志为 `local/r29-retention/unicode-integrated-{core,qt}-{configure,build,ctest}.log`。后来补采的独立运行日志与公开原版引用单列在 `supplemental/identity.json`，SHA `24b3b51f4de8507c4c3b0dada6b381aad9e1ef6d9923ac98bb65b99cfbb15a0f`；该补采没有生成新 native 或原版运行。

## 尚未覆盖的行为

[结合标原版记录](../../../../research/ui/results/2026-11-02-resize-unicode-reference/combining-boundary.json)观察到：缩窄后只输入 U+0302，目标后缀变为 `U+0302 + e + U+0301`，Z 消失。该事实尚需区分 DSR、resize、列夹取与附着状态的影响；本次只读投影没有修改 `Screen` 的结合标输入策略，不能把工程 Cell 保真当成该原版流程已通过。

[历史 ASCII 投影](../2026-11-02-display-projection/README.md)及 [26/4 整合](../2026-11-02-retention-erase-mode-integrated/README.md)保持原身份；当前能力更新不回写旧计数。[默认裁剪失败](../2026-11-02-resize-address/README.md)与 [Qt B/CD resize 失败](../../../shell/results/2026-11-02-qt-terminal-modes/README.md)也未被新核心入口的通过结果豁免。高度变化、完整帧交叉、任意 Unicode/字体/样式、真实不同 DPI、Qt 保留正文接线及用户选择/Find 流程、长期稳定和性能仍不由本记录验收，#27/#29 与完整 R2 继续开放。
