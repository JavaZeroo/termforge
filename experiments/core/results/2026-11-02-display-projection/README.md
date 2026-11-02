# 有界 ASCII 显示投影：首个核心切片

[ADR-0010](../../../../docs/adr/0010-bounded-resize-retention.md)确认方案后的首个独立切片，在现有 `HistoryViewport` / `ViewMapping` 上增加显式、只读的 `project`。它能将尚未丢失的源单元格按指定宽度组合成显示页，并把命中/高亮映射回原源位置。**没有改变 `Screen::resize` 的物理裁剪，也没有接入默认 Qt 显示路径或改变协议光标。** 本记录不代表正文保留方案已经交付。

实现提交 `ec766f27d767257f542280f5bb9ca403c82dd683`，随后合入主线 `68eeb936a32e3a527a3c114274df02f63512ed93` 得到 `ad45b50aa018a354da948f218f81c7e972c0c7a3`。核心 **19/19，0.95 秒**，Qt / 固定 ConPTY 回归 **4/4，9.22 秒**。后续 `f6b16992c12193f3b4c57b6e4df57a4291834f59` 只增加独立离线探针及 CMake 注册；产品源码一致，但 native 构建早于该注册，不能称为整棵源码树相同的一次联合验收。秒数仅为 CTest 执行记录，不是性能测量。

选定源码、二进制、日志和原版历史摘要的精确 SHA-256，以及构建配置、分轮结果见 [validation.json](validation.json)。这些哈希在运行后采集，不冒充运行前冻结的完整输入清单；RED 和中间 GREEN 未保存的精确源码/二进制身份保持 `null`。原日志含本地构建路径，只发布哈希和经核对的合成摘要。

## 固定源与实际断言

[历史原版三阶段样本](../../../../research/ui/results/2026-11-02-resize-state-reference/README.md)提供 78 字节合成正文，SHA-256 `c7dd4a7fa283dd0003ca8b2ae13dcb46f143a4717bf5a14eb5f5acb068a9fa1c`。本切片没有运行原版；strict/recovery × 整块/逐字节共 **4 次核心 feed**，不是新增 4 个原版样本。

Parser 始终保持 **91×20**，只将显示投影宽度依次设为 **91 → 61 → 91**；没有调用 `Screen::resize`。下表为测试中的零基显示行列，源于原版 marker 矩形的固定网格换算，不表示当前 Qt 已画出该画面。

| marker | 91 列 | 61 列 | 恢复 91 列 |
|---|---|---|---|
| `v` | `(7,90)` | `(8,29)` | `(7,90)` |
| `w` | `(9,90)` | `(11,29)` | `(9,90)` |
| `x` | `(10,0)` | `(11,30)` | `(10,0)` |

四种 feed 均检查 marker 无丢失/重复、ANCHOR 不移动、源 ID/版本/epoch/顺序不变、缩窄命中回到原 `v` 坐标、`wx` 查找范围在显示页合为一段，以及选择复制仍为 `wx`。缩窄投影后的独立 DSR 查询仍精确返回 `ESC[11;2R`；恢复宽度也不能复用旧窄页的布局 stamp。这验证只读投影不会改动协议状态，不能替代真实 resize 后寻址合同。

另有 **3 个工程场景**，不作为原版匹配声明：

- 有限源窗口与分页：软行未完成时不发布伪硬行；扩大源窗口后续接；页满返回准确后缀；源覆盖、文档重排、缓冲区 epoch 改变均拒绝旧 continuation，失败准备不推进布局 revision。
- 历史尾部与 Cell 保真：在 250 次合成行输出后只读目标尾行，保留文字、显式空格、颜色和 underline 状态，合成 padding 不伪造源身份；拒绝超过 200 源行的请求及宽字输入。样式断言是核心 Cell 状态，不是像素或字体验收。
- 空软续行：恰好填满显示行后，已擦空的软后继不能再制造一个空显示行；整页、分页和源窗口恢复分别检查。此追加修正只有后续 GREEN，没有对应的独立行为 RED 记录。

## 有界性及使用限制

一次显式请求最多扫描 **200 个源片段，每片段最多 500 个 Cell**；显示宽度为 1–500 列，页预算为 1–200 行。`source_rows_read` 计算实际检查过内容的源行，包括回滚的部分显示行，不把有限身份/续接检查冒称 Cell 扫描。不会自动展开历史前缀；显示行 ID 为 0，源身份保存在映射中。既有物理行 `snapshot()` 路径保持不变，显式投影的旧物理 viewport totals 留空，使用 `next/stop` 遍历。

`page_full`、`source_end` 和 `source_limit` 含义不同。源窗口结束不是硬换行；不足以构成完整显示行时，`source_limit` 可返回未推进的 `next`。调用者必须在上限内扩大有限源窗口，无法完成时保留未完成状态，不能反复提交同一窗口而期待自动前进。

每次成功发布页增加布局 revision；消费者须提供当前 stamp 才能命中/投影，不能仅凭源文字未变继续使用旧宽度页面。continuation 另绑定源 ID/版本、epoch、顺序 revision。单 Cell ASCII 之外的内容明确拒绝：源码检查宽字、marks、非 ASCII、多字符、裁剪/字素片段及 leading spacer；本测试有宽字拒绝用例，未为其余每种拒绝类型新增独立动态用例，不声称完整 Unicode 覆盖。

## 红绿与回归边界

| 阶段 | 实际结果 | 可以证明什么 |
|---|---|---|
| 首次编译 | `projection-red-build.log`：`take_replies()` 的 `vector<string>` 被误与字符串比较，编译失败 | 测试类型笔误，不能计为功能 RED |
| 修正笔误后编译及行为 RED | `projection-red-build-1.log` 构建成功；CTest 0/1，输出 `explicit display projection is not implemented`，总计 0.04 秒 | 未实现 stub 被调用后失败，独立于编译错误 |
| 首次实现及边界补充 | `projection-green-ctest.log`、`projection-bounds-ctest.log` 各 1/1，总计各 0.04 秒 | 各自保存的中间 GREEN；不追认后来新增用例当时已经存在 |
| 空软后继补充 | `projection-soft-edge-ctest.log` 1/1，总计 0.03 秒 | 后续补充 GREEN，没有保存该补充的先行 RED |
| 核心回归 | `core-ctest.log` 19/19，总计 0.95 秒 | 包含显示投影与既有 resize、历史、选择、查找、模式/区域等核心用例 |
| Qt 回归 | `qt-ctest.log` 4/4，总计 9.22 秒 | 启动/会话、Unicode 输入、基础尺寸和五阶段模式回归；不执行显式投影或第六阶段正文 resize 诊断 |

Windows x64，SDK 10.0.26100.0，MSVC 19.42.34433.0，Visual Studio 17 2022，Release，构建并行度 1；核心构建关闭 Qt 和候选适配器。Qt 构建使用 Qt 6.10.3、固定微软 ConPTY 1.25.260930003、CRT 14.51.36247.0，`offscreen` 运行，候选适配器及 `TERMFORGE_ENABLE_RESIZE_REPRO` 均关闭。Qt supervisor 四项均未超时，退出码 0，自有 Job 活动数 0；实际物理呈现没有观测端点。

已有构建目录可复验本核心切片：

```powershell
cmake --build <core-build> --config Release --target display_projection_test --parallel 1
ctest --test-dir <core-build> -C Release -R '^display_projection_core$' --output-on-failure
```

19 项与 Qt 4 项的具体选择、结果及哈希在 JSON 中分开记录。Qt 构建准备仍按[正式入口说明](../../../../src/qt/README.md)，不将核心 Qt-OFF 结果作为 GUI 验收。

实际正文裁剪、resize 后写入/光标、备用屏交叉、完整 Unicode、Qt 投影接线及完整历史/选择/Find 工作流仍未完成；[既有 B/CD 失败诊断](../../../shell/results/2026-11-02-qt-terminal-modes/README.md)未被本切片修复或重新运行。新原版动作调查不在本报告内。#27/#29 保持开放，无新性能门槛，也不声明 R2 完成。
