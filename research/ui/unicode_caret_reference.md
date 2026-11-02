# WindTerm 2.7.0：U+0302 后的 Q 与位置单位候选

这是 #27/#29 的新独立 `unicode_caret_reference` namespace，承接 [既有 Unicode 原版观察](results/2026-11-02-resize-unicode-reference/README.md)。准备 **3 个全新进程、各 4 阶段，共 12 观察**；尚未运行原版。旧 `resize_unicode_reference` 等已冻结工具、输入计划和结果不修改、不重放。

旧样本在 resize 后输入 U+0302，得到正文 `60空格 + U+0302 + e + U+0301`，DSR 仍为 `8;61`，caretPosition 为 85。下一 Q 在什么位置写入尚未观察；caret API 是否按 UTF-8 字节计量也需要候选查询，不能直接沿用 JS 文本偏移矩形作 glyph/Cell oracle。

## 三例精确协议

各例阶段 0 的输入逐字节相同，都是旧 combining 样本的 **60 bytes**：

```text
1b5b3f366c1b5b3f37681b5b721b5b306d1b5b324a1b5b323b3148303132333435363738391b5b333b3148414e43484f521b5b383b36314865cc815a
```

SHA-256 `c6d08514fb8897ee31059833506de1354f04b69b9721aa1153f34f750e444d49`。它设置 origin OFF、wrap ON、全屏 margin、SGR0、ED2，写数字/ANCHOR，最后在 `CUP(8,61)` 写 `e\u0301Z`。不增加校准 header，不改变起始列。

| case | 阶段 0 | 阶段 1 | 阶段 2 | 阶段 3 | 实际 NAWS 合同 |
|---|---|---|---|---|---|
| `dsr-only-q` | 固定初始输入 | 空正文 | 仅 U+0302 | 仅 Q | 四阶段均 91×20，0 次 resize |
| `cup-position-q` | 固定初始输入 | 仅 `CUP(8,61)` | 仅 U+0302 | 仅 Q | 四阶段均 91×20，0 次 resize |
| `resize-mark-q` | 固定初始输入 | 仅缩到 61 列，空正文 | 仅 U+0302 | 仅 Q | 91×20→61×20→61×20→61×20，仅阶段 1 resize |

阶段 1 的 CUP 为 hex `1b5b383b363148`（7 bytes）；U+0302 为 `cc82`（2 bytes）；Q 为 `51`（1 byte）。每阶段末只追加一次 DSR `1b5b366e`。上一阶段完整回复、稳定正文/窗口、marker/caret 采样和 ack 完成后才放行下一动作；阶段 2 在 Q 之前单独保留。没有 CR/LF、额外 CUP、恢复宽度或补绘。两个窗口仍为 `1360×749` 和 `1000×749`，高度 20 固定。

阶段 0 使用已有原版结果作为初始条件门禁：必须得到 DSR `8;63`，完整文档数组必须为 `["", "0123456789", "ANCHOR", "", "", "", "", 60空格+"e\u0301Z"]`。JS 在写出首个 ack 之前检查，离线审计再检查；不符合则停止、保留失败。**阶段 1–3 不指定成功语义结果**：在有界文本和采集合同内保留实际差异，不根据预期的 mark/Q 行为判通过。

## 文本、候选位置与 caret 分列

[新采样器](unicode_caret_reference.js)对每个源派生 marker 保存原 `lineStart(i)`，然后分别查询六个数值：UTF-16/scalar/UTF-8 三种前缀定义下的起点、末端。每次 `visibleRectFromPosition` 的实际输入和返回矩形单独记录。相同 offset 的不同候选仍保留各自标签，不能把共享矩形当错误。

例如初始 Z 的文本前缀有 62 个 scalar/UTF-16 code unit、63 个 UTF-8 byte；当 `lineStart(7)=23` 时，起点候选分别为 85/86。新工具会同时查询这些候选；不预先指定哪个对应字形。所有允许字符均在 BMP，scalar/UTF-16 相等不能判定原版对非 BMP 字符的单位。

caret 另取原 `caretLine/caretColumn/caretPosition`，**直接调用 `visibleRectFromPosition(caretPosition)`**；不会用 DSR 或文本长度代替 API 输入。同时保存该 caret 行的原 `lineStart`，计算 API 相对值以及它与合成文本各 UTF-8/UTF-16/scalar 边界的候选关系。边界匹配或不匹配只记录，不自动选定单位或设置语义通过。

矩形允许不同字宽、零宽/零高和共享位置；仅做固定窗口/文档安全界限检查（width 0–1360，height 0–749，x −1360..2720，y −200×749..2×749）。这不是产品布局或性能门槛。协议 DSR 仍独立限制为行 1–200、列 1–500，允许回复超出当前 61 列。API 数值、UTF-8 byte、文本偏移、像素与 C++ Cell 列互不替代。

## 有限正文与公开出口

[运行工具](unicode_caret_reference.py)只枚举原 `e/U+0301/Z` 的有序保留/删除子序列；阶段 2 起允许插入一个 U+0302，阶段 3 起允许插入一个 Q。加入明确 NFC 派生，前导空格只取固定 `0/1/59/60/61/62/63` 边界（阶段 0 只取 60），文本切分只取 `60/61/62/63/91`，尾部补空白只取 61/91。目录每阶段数量分别为 **25、218、926、4007**；每行最多 91 个 scalar，最多 200 行。阶段 0 另有上文精确正文门禁。

按 NFD 分解后，每个已发送 token 在整个文档最多一次。Q 在阶段 3 前、U+0302 在阶段 2 前均拒绝；重复源词、任意 Unicode、U+FFFD、dotted circle、私有文本或无界空格拒绝。目录可记录保留前导 mark、覆盖、插入、源字符丢失等候选，但不声明任一候选正确。未知输出仍留原始录制并停止；不放宽旧工具，不自动重跑。需要时另立离线补充审计，不能把原失败改成原 strict 成功。

[新出口](export_unicode_caret_reference.py)严格复核源/计划/完整 4047 文件原包/模块/源码归档/生成 probe/provider/profile/attempt/NAWS/DSR/geometry/逐阶段 ack/回收。经候选校验后才公开可重建的合成正文：每行目录索引、UTF-8 hex、字节/scalar/UTF-16 长度、目录 SHA 与全文 SHA。marker 的六个查询和 caret 原数值查询保留；`cpp_cell_column_mapping`、`document_position_unit_classification`、`position_unit_classification` 均保持 `null`，`semantic_acceptance=false`。

不限原始正文、像素、原版资源、profile、路径与 PID 不公开。重复 JSON 字段、非有限数字、错误查询基准/算术/原 caret 绑定及缺失录制拒绝。旧原版包/provider 全量校验不因公开材料看似合理而跳过。

## 身份、生命周期与离线验证

运行源清单共 10 份：新 `unicode_caret_reference.py/js`，既有 `resize_state_reference.py`、`mode_state_reference.py`、`alternate_reference.py`、`r1_launch.py`、`launch.py`、`qt_r1_text.js`、`r1_vt_fixture.py`、`experiments/conpty/run.py`。出口源共 3 份：新 `export_unicode_caret_reference.py` 与既有 `export_resize_state_reference.py`、`export_mode_state_reference.py`。严格源码归档 manifest 不伪造 Git；Python Unicode 数据版本 `15.0.0` 进入计划。

每例默认离线准备，不授权执行，不启动目标/REA/Node/socket。授权执行使用全新计划与输出，排他 `.attempted` 保留失败且禁止重放。沿既有独立 WindowStation/Desktop、OwnedJob、合成单会话、loopback Telnet 路径。0/1 次 resize 按具体 case 严守；未授权 NAWS、额外回复、过期边界、错几何、高度变化、来源变化、阶段/ack 不一致或超时即停。

网格/正文稳定观察仍为 600/400 ms，REA capture 45 秒、CLI wait 60 秒、JS 场景/定时器 41/42 秒分别记录；`whole_lifecycle_bound_seconds=null`。主动结束后的 Job 空不证明自然退出。

```powershell
python -B -X utf8 -m unittest research.ui.test_unicode_caret_reference research.ui.test_export_unicode_caret_reference -v
python -B -O -X utf8 -m unittest research.ui.test_unicode_caret_reference research.ui.test_export_unicode_caret_reference -v
node --check research/ui/unicode_caret_reference.js
node research/ui/test_unicode_caret_stages.js research/ui/unicode_caret_reference.js

python -B -X utf8 -m research.ui.unicode_caret_reference --prepare --case dsr-only-q --source <pinned-original> --out <source-root>/local/dsr-only-q --rea <rea.cmd> --plan <source-root>/local/dsr-only-q-plan.json
# 根代理已有执行授权时，以 --prepare --authorize-execution 准备全新授权计划后运行：
python -B -X utf8 -m research.ui.unicode_caret_reference --plan <authorized-plan.json>
python -B -X utf8 -m research.ui.export_unicode_caret_reference --directory <capture> --plan <authorized-plan.json> --expected-plan-sha256 <external-plan-sha256> --source-root <source-archive> --reference-source <pinned-original> --destination <new-public.json>
```

本组无连续输入或空白位置控制，不能单独证明 DSR 中性。若CUP与resize不同，或者无resize例自身仍不能解释，应先提出下一最小区分样本；不立即采用“resize一律孤立mark”等政策。离线测试不构成原版观察、完整 Unicode、Qt/fixed-ConPTY 联合验收、性能或 R2 完成结论。
