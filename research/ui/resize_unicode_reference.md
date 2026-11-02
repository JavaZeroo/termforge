# WindTerm 2.7.0：Unicode 缩宽边界与后续输入协议

这是 #27/#29 的独立 `resize_unicode_reference` namespace，依据 [ADR-0010](../../docs/adr/0010-bounded-resize-retention.md)准备 **2 个 case、各 4 阶段**。每例一个全新原版进程、独立源码归档/计划/输出，最多执行一次；尚未运行原版，没有行为验收结论。既有 resize/mode/frame/edit/erase 工具与失败计划保持不变。

复用既有固定 4047 文件原包、独立 WindowStation/Desktop、OwnedJob、合成单会话及 loopback Telnet 机制，不新增私有结构读取。只准备计划默认 `execution_authorized=false`，不启动原版、REA、Node 或 socket；原版由根代理在冻结身份后串行执行。

## 固定输入与动作前观察

两例窗口均为 `1360×749 → 1000×749 → 1360×749`，要求实际 NAWS `91×20 → 61×20 → 91×20`。只在阶段 1/3 请求 resize，高度固定 20，不探测 GUI 最小列数。每阶段末尾仅追加 `ESC[6n`；恰好一条完整 DSR 回复、稳定文档与窗口观察及 ack 全部完成后，才放行下一动作。阶段 1 在修改正文之前单独观察，不能被阶段 2 的结果替代。没有 CR/LF。

共同前缀明确设置 origin OFF、wrap ON、全屏 margin、SGR0、ED2，然后在 `(2,1)` 写 `0123456789`、`(3,1)` 写 `ANCHOR`。初始 Unicode 输入固定在 `CUP(8,61)`。所有位置按发送的协议记为一基，尚不推断原版之后的物理单元格状态。

| 阶段 | 网格 | `wide-boundary` | `combining-boundary` |
|---|---|---|---|
| 0 | 91×20 | 公共前缀、`CUP(8,61)`、`界Z` | 公共前缀、`CUP(8,61)`、`e\u0301Z`（NFD） |
| 1 | 61×20 | 仅缩宽、DSR 和完整观察 | 仅缩宽、DSR 和完整观察 |
| 2 | 61×20 | 仅输入 `Q`，UTF-8 hex `51` | 仅输入 U+0302 COMBINING CIRCUMFLEX ACCENT，hex `cc82` |
| 3 | 91×20 | 仅恢复宽度并观察 | 仅恢复宽度并观察 |

两例初始输入各 60 字节。`wide-boundary` 的完整初始 hex 为：

```text
1b5b3f366c1b5b3f37681b5b721b5b306d1b5b324a1b5b323b3148303132333435363738391b5b333b3148414e43484f521b5b383b363148e7958c5a
```

SHA-256：`ef608ca9c74bf7f46eeaffa2b920da7d23a4095498f9a3011034189979c14d30`。末尾 `界Z` 为 4 个 UTF-8 字节、2 个 Unicode scalar、2 个 UTF-16 code unit。

`combining-boundary` 仅将上述末尾 `e7958c5a` 换成 `65cc815a`，完整初始 SHA-256 为 `c6d08514fb8897ee31059833506de1354f04b69b9721aa1153f34f750e444d49`。末尾 `e\u0301Z` 同为 4 个 UTF-8 字节，但有 3 个 scalar、3 个 UTF-16 code unit。后续 U+0302 为 2 个 UTF-8 字节。字节、字符计数均不是单元格列数或显示宽度。

宽字例观察新右界附近的保留/显示分段及 Q 写入；结合标例观察 resize 后单独输入 U+0302 的附着、保留或丢失。阶段 3 已受阶段 2 影响，不能冒称未经修改的 resize 往返。

## 位置与计量单位

采样分别保留以下事实：

- 源输入：精确 UTF-8 bytes、hex 和 SHA；不规范化发送的 NFD 输入。
- 文档正文：原 `lineText(i)` 返回的字符串及文档 API 行索引。公开出口仅在精确有限候选校验后保存合成文本表示。
- 文本内偏移：JS `indexOf` 给出的 UTF-16 code-unit offset。全体允许字符均在 BMP 内，因此此样本 scalar/UTF-16 偏移相等；**不能据此判定原版 API 对任意 Unicode 的计量单位**。
- API 查询：单独记录 `lineStart(i)` 返回值、实际传给 `visibleRectFromPosition` 的 `lineStart(i) + JS offset` 数值及返回矩形。这是明确记录的查询构造，不宣称矩形必然代表相同 C++ Cell 列，也不把原版位置值当 UTF-8 字节偏移。
- DSR：保留原一基回复，行 1–200、列 1–500 的独立安全界限不依赖当前网格宽度。允许 61 列窗口下回复列 91。
- 显示矩形：保存实际像素值，允许宽字矩形宽于 ASCII、结合标与基础字符共享坐标、宽度或高度为零。不会要求 Unicode 字形宽度等于 8 像素，也不会把重复坐标当错误。

ASCII 数字与 ANCHOR 可用作本次固定几何参照，不能证明 Unicode 宽度。矩形安全界限只限制为宽 0–1360、高 0–749、x 在 −1360..2720、y 在 −200×749..2×749；这是采集界限，不是产品性能或行为验收标准。内部 cursor、宽字 continuation cell、结合标附着位和文档存储都不直接读取。

## 有限文本与严格公开出口

[运行工具](resize_unicode_reference.py)只生成固定源的有限候选：已知锚点、已发送字符的有序保留/删除组合、阶段 2 后插入一个 Q 或 U+0302，以及明确的 NFC 派生。宽字例最多额外加入一个已声明的邻接空格。阶段 0 的源词前导位置固定 60 个空格；缩宽后候选前导空格数仅为 `0/1/59/60/61/62/63`，文本切分边界仅为 `60/61/62/63/91`，补齐空白长度仅为 61/91。这里枚举文本字符串，**不将这些字符串索引定义为物理 Cell 列**。

每阶段候选数量固定如下；Python Unicode 数据版本 `15.0.0` 进入计划身份。所有行最长 91 个 scalar、最多 200 行。将候选按 NFD 分解后，全体正文中的每个已发送源 token 最多出现一次；未来动作、重复源词、未声明 Unicode、替换字符 U+FFFD、dotted circle 和任意私有文本均拒绝。缺少源字符、拆分的 mark、共享矩形或不可区分结果可作为观察保留；集合之外的输出必须停止并保留原件，不能自动扩白名单或重跑旧计划。

| case | 阶段 0 | 阶段 1 | 阶段 2 | 阶段 3 |
|---|---:|---:|---:|---:|
| `wide-boundary` | 25 | 148 | 656 | 656 |
| `combining-boundary` | 25 | 218 | 926 | 926 |

[严格出口](export_resize_unicode_reference.py)复用已冻结的完整原包/模块/源码归档/计划/生成 probe/provider/attempt/profile/NAWS/DSR/geometry/ack/Job 核验，并再次检查每一行的有限源派生。公开 `synthetic_text` 保存每行的候选索引、UTF-8 hex、字节数、scalar 数和 UTF-16 code-unit 数，以及候选目录 SHA。目录由公开运行源码 `sorted(synthetic_lines(case, phase))` 重建；完整正文数组 SHA 也保留。因此可以在不发布不限原始录制的前提下，逐字节重建已经批准的合成文本，检查 NFC 与原 NFD 的差别。

marker 公共字段明确命名为 `line_text_offset_utf16`、`line_text_offset_scalar`、`line_text_prefix_utf8_bytes`、`line_start_api_value`、`requested_position_api_value` 和 `rect`。不输出模糊的 `column`，`cpp_cell_column_mapping` 与 `document_position_unit_classification` 均为 `null`。原 profile、绝对路径、PID、资源、未审计正文、像素与 provider 原件留在私有输出；重复 JSON 字段、非有限数字和缺失录制拒绝。

## 身份、停止条件与离线验证

运行源码仍为 10 份：新 `resize_unicode_reference.py/js`，以及既有 `resize_state_reference.py`、`mode_state_reference.py`、`alternate_reference.py`、`r1_launch.py`、`launch.py`、`qt_r1_text.js`、`r1_vt_fixture.py`、`experiments/conpty/run.py`。导出器为新 `export_resize_unicode_reference.py` 加既有 `export_resize_state_reference.py`、`export_mode_state_reference.py`，共 3 份。源码归档严格使用 `reference-source.json`；有 manifest 时不调用或伪造 Git。

每计划排他创建 `.attempted`，失败后不可重放。未授权、source/runtime/REA 身份变化、错误几何/高度、额外或过期 NAWS、源/回复/ack/阶段不一致及界限/期限失败均停止。网格/正文稳定观察分别为 600/400 ms；REA capture 45 秒、CLI wait 60 秒、JS 场景/定时器 41/42 秒分别记录，`whole_lifecycle_bound_seconds=null`，不拼接成生命周期保证。主动终止原版后 Job 空不等于自然退出。

```powershell
python -B -X utf8 -m unittest research.ui.test_resize_unicode_reference research.ui.test_export_resize_unicode_reference -v
python -B -O -X utf8 -m unittest research.ui.test_resize_unicode_reference research.ui.test_export_resize_unicode_reference -v
node --check research/ui/resize_unicode_reference.js
node research/ui/test_resize_unicode_stages.js research/ui/resize_unicode_reference.js

# 离线准备单个 case，不启动目标；每例必须使用全新 plan/out。
python -B -X utf8 -m research.ui.resize_unicode_reference --prepare --case wide-boundary --source <pinned-original> --out <source-root>/local/wide-boundary --rea <rea.cmd> --plan <source-root>/local/wide-boundary-plan.json
# 根代理在已有授权下使用 --prepare --authorize-execution 创建独立授权计划，再运行：
python -B -X utf8 -m research.ui.resize_unicode_reference --plan <authorized-plan.json>
python -B -X utf8 -m research.ui.export_resize_unicode_reference --directory <capture> --plan <authorized-plan.json> --expected-plan-sha256 <external-plan-sha256> --source-root <source-archive> --reference-source <pinned-original> --destination <new-public.json>
```

离线测试覆盖精确 UTF-8 源字节、分块 DSR、每阶段握手、NAWS 边界、身份和失败不重放；纯 V8 执行实际控制器及嵌套文档采样代码。出口测试验证文本重建、单位分列、宽字宽矩形、mark 共享矩形和拒绝路径。它们不启动原版、VM 或 native，不建立 WindTerm 对齐、完整 grid、ConPTY 联合路径、性能或 R2 验收结论。
