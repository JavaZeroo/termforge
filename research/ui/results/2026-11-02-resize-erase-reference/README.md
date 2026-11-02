# WindTerm 2.7.0：预置宽屏 L 后的 EL0 / EL2

本报告记录 [独立擦除协议](../../resize_erase_reference.md)的两次原版运行，用于 #27/#29。**两例均采集完五阶段，但原机械审计均因有限文本白名单漏项而失败；随后两次独立离线补充审计通过。** 合计 2 次原版、10 个阶段，新增原版运行次数为 0。没有“两例原 strict 成功”的结论。

| case | 原版动作 | 原版记录 | 独立离线公开结果 |
|---|---|---|---|
| `erase-right` | EL0，`1b5b304b` | 1 次新进程、5 阶段；原审计失败保留 | [erase-right-supplement.json](erase-right-supplement.json) |
| `erase-whole-line` | EL2，`1b5b324b` | 1 次新进程、5 阶段；原审计失败保留 | [erase-whole-line-supplement.json](erase-whole-line-supplement.json) |

## 新输入与独立前置控制

本批初始正文是 **85 字节**，不是原样的 78 字节 B：在旧 `DECAWM OFF + CUP(8,91)` 前插入 7 字节 `CUP(8,1), L`（hex `1b5b383b31484c`），其余 `uv/wx` 顺序不变，最后可打印字节仍为原 `wx`。新正文 SHA-256 为 `a301c656926ae72c07d2c91b67e1734286ce50b7cb38c5b4f6b64f06db0563b3`；旧 B 为 `c7dd4a7fa283dd0003ca8b2ae13dcb46f143a4717bf5a14eb5f5acb068a9fa1c`。每份公开 JSON 的 `initial_body_derivation` 保留原源、插入位置、插入段与新源的完整合成字节及摘要。

L 在初始 91 列时已经写入；缩窄后只用 CUP 定位。两例都明确保留阶段 1 的缩窄后基线和阶段 2 的 CUP 后基线，两组完整文档数组逐字节相同，阶段 2 的 DSR 为 `8;61`，随后才释放擦除字节。这与 [旧 edit 失败](../2026-11-02-resize-edit-reference/README.md)不同：旧例在窄屏打印 L 的准备本身改变了尾部，未发送 EL0；旧 EL2 当时未运行。新批次的源码、输入、计划和结果独立，不覆盖或重试旧失败。

两例各自使用 fresh 原版进程、合成 loopback Telnet 会话和最多一次 attempt 的计划。固定 WindTerm 2.7.0 x64，Windows VM `10.0.26300`、REA `6.7.0`、Python `3.12.10`。窗口为 `1360×749 → 1000×749 → 1360×749`，实际 NAWS `91×20 → 61×20 → 91×20`；每阶段结束只追加一次 `ESC[6n`。

## 直接观察

DSR 使用一基协议坐标；文档行列为零基；矩形为像素 `(x,y)`。文档长度和空白范围不是完整 active grid，也不表示原版内部显式空格或未写 padding 的类型。

| 阶段 | 输入本体 | 两例 DSR | 文档观察 |
|---|---|---|---|
| 0 | 新 85 B 正文 | `11;2` | L `(7,0)`、v `(7,90)`；w/x `(9,90)/(9,91)` |
| 1 | 空，仅缩窄 | `11;2` | L/v/w/x 文档坐标保持；v/w/x 显示重排 |
| 2 | `CUP(8,61)`，hex `1b5b383b363148` | `8;61` | 完整文档与阶段 1 相同；L/v 均仍在 |
| 3 | 各自 EL0 或 EL2 | `8;61` | EL0 留 L、去 v；EL2 去 L/v；w/x 保留 |
| 4 | 空，仅恢复宽度 | `8;61` | 消失的 L/v 未复活；其余正文文档位置保持 |

| 观测 | EL0 | EL2 |
|---|---|---|
| 阶段 3/4 文档行 7 | `L + 59 个空格`，总长 **60** | **60 个空格** |
| L | 保留于 `(7,0)`、position `23`、矩形 `(147,126)` | 擦除后消失 |
| v | 擦除后消失，恢复未复活 | 相同 |
| w/x 文档坐标 | 仍为 `(9,90)/(9,91)` | 相同 |
| w/x 全局 position | `206/207 → 175/176`，减少 31 | 相同 |
| 擦除后窄屏 w/x 矩形 | `(379,180)/(387,180)` | 相同 |
| 恢复后 w/x 矩形 | `(867,162)/(147,180)` | 相同 |
| 文档 `line_count` | `11→12→12→11→11` | 相同 |
| 文档 `scroll` | 五阶段均 0 | 相同 |

两例全部阶段的 `ANCHOR` 保持文档 `(2,0)`、position `12`、矩形 `(147,36)`。初始 v 矩形为 `(867,126)`，缩窄及 CUP 后为 `(379,144)`；擦除前 L 始终为 `(147,126)`。w/x 擦除前缩窄矩形为 `(379,198)/(387,198)`，擦除目标行的旧尾部消失后，后续显示位置上移一行。

这不是只凭 marker 缺失或 position 差值推算空格：按新 85 B 输入独立构造全部合成文档数组，再对规范 JSON 计算 SHA，10 个阶段都匹配公开摘要。阶段 3/4 的完整文档 SHA 分别为：

| case | 阶段 3/4 整体文档 SHA-256 | 目标行 UTF-8 SHA-256 |
|---|---|---|
| EL0 | `39054c2e569c4c96f22307129b573ce9773cddd420172e9dd81b3b1d1af0c503` | `aa449f548dcc543a1313ccfb05de53c806c3bc72eee324b8043dea2dd2b60c01` |
| EL2 | `a41d2e151b704b4c2c965827680060d9ad8c38d3a15c59d6854eb112e44c5721` | `6a9c3911d69fdb86d4b5a6b93dbdd9d5d3b1125551416b7af129b29936ae48e2` |

## 原失败保留与有限补充核验

两份原录制都以 `ValueError: Only fixed erase-source synthetic document lines are allowed` 未通过冻结机械审计；原外层 exit 1。原集合覆盖有限区间填空格及 61/91 列片段，但遗漏上述长度 60 的两个源派生形式。阶段 0..2 没有漏项；CUP 前置通过，EL 已实际执行，不能把这次失败写成“未发送擦除”。

[独立补充核验器](../../audit_resize_erase_right.py)每次只处理一份授权计划与录制，先校验外部给定的 plan SHA，绑定该 case 的 85 B 输入和 EL 动作，重现原审计的精确错误，要求原 `mechanics-check.json` 仍不存在。新命名空间只在阶段 3/4 增加一个候选：EL0 为 `L + 59 空格`，EL2 为 `60 空格`；两个新增候选不能跨 case 使用。其余原机械与严格导出规则继续执行。

补充核验逐项复查五阶段、NAWS/DSR/实际窗口、CUP 前置、生成 probe 与 provider evidence ID、源码归档、完整 4047 文件原包、执行模块、合成 profile、attempt 与回收。两次独立离线调用均 exit 0，未启动原版、未补造原机械审计文件、未修改冻结执行源或原件。输出采用独立 schema `termforge-original-resize-erase-extent-supplement-v1`，明确记录 `original-audit-failed-offline-supplement-passed`、失败保留及新增原版运行次数 0。

审计器的普通/`-O` 离线测试各 12 项通过，覆盖两个完整合成包导出、85 B 身份、跨 case 候选与动作拒绝、私有文本/溢出/重复 marker、原错误必须复现、准备内容变化、晚阶段错误、provider/脚本/源码/包/profile/attempt、旧计划变动和重复 JSON 字段。测试夹具使用无可执行内容的合成模块，不计入原版运行次数，也不替代原版观察。

公开前再次核对精确 schema、脱敏、源码与导出器摘要、原包身份、固定输入和全部阶段，然后按原字节复制。公开材料只有合成输入、DSR、marker/几何、正文摘要及标量身份，不带原文全文、路径、PID、用户会话、profile 或原版资源。两份结果均为 `job_after.active=0`，受控主动终止后 Job 空不证明自然 GUI 退出。

## 工程含义与边界

本样本要求保留不同的内容有效范围与网格容量：实际网格仍是 61 列，而擦除后的文档目标行只有 60 个字符。若 TermForge 为已测操作保留内容范围元数据，必须能表达 EL0 的 L 与尾随空白、EL2 的 60 个空白，而不是把擦除空白冒充用户写入或无条件恢复 91 列旧尾部。该元数据是独立工程表示，不声称原版采用同一种成员或存储结构。

本次只在 `CUP(8,61)` 后执行 EL0/EL2，不能判定长度 60 普遍来自光标位置、新宽度减一或其他内部文档策略；也没有证明完整第 61 列网格 Cell 的所有属性。其他光标列、尺寸、连续擦除、擦除后下一字符、宽字/组合字符、样式与带背景空白、软续接、主/备用交叉、高度变化和滚动仍需各自证据。w/x 未受影响是当前固定源的观察，不是任意行擦除范围已经匹配。

本报告不合并旧 EL1、旧 edit、帧/末列或核心测试的成功计数；不构成正式 Qt / 固定 ConPTY 联合验收、可靠性重复、性能等价或 R2 完成声明。

## 来源身份

本批执行源归档基点为 `905c819f9dab73f1076f06ab93f2f3a1078b19c8`，manifest SHA-256 为 `33c2643c3df7b8dc1dfcb8ba89df995022d9bd268c10c56bbed2837be1244bbb`。基点与十个实际执行源文件摘要分列，不把新工具说成已存在于该基点。

| 文件 | SHA-256 |
|---|---|
| [resize_erase_reference.py](../../resize_erase_reference.py) | `3f88214a4019e170b43add07ccbc8c5d4064908102ce1e0ce3a7f60fca6302e6` |
| [resize_erase_reference.js](../../resize_erase_reference.js) | `8d74f8f64bdb1c8102d4e5167f68e58a0971abb7b1cebcc44a59ac65f0765c36` |
| [export_resize_erase_reference.py](../../export_resize_erase_reference.py) | `e6d3c06790dc062e7614c60043639f34d620e48b19a829f81caaac5ce2c12745` |
| [audit_resize_erase_right.py](../../audit_resize_erase_right.py) | `3cca4c0e291fa85f5d64d90500c00cee8d8c26993aa88ac228bb881ca4ac0ec1` |

原版 EXE SHA-256 为 `7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0`；完整 4047 文件 manifest SHA-256 为 `e459adf2f12adb6a1b7f55e66a9da1c22794185e66e848057d21e0c60d669203`。两份 JSON 另列全部执行源、四个离线审计源、已加载模块、工具链、provider evidence ID 和原件摘要。

| 公开文件 | SHA-256 | 原独立计划 SHA-256 |
|---|---|---|
| `erase-right-supplement.json` | `2db5a322d88e843f5dbb1be718d12db9710dcc6080a5f57d6b74dc560f43d57e` | `0aab539343af44672ce9a90a48f2e4d9cdcd46e71faa3dc95dae5b84bfc2ae0c` |
| `erase-whole-line-supplement.json` | `60d064d279bfa44afdea005c46f966db661c0a0a7cd502c59a7dcc9952a28f56` | `7b230e351792de6e0be68ee66fd2f70db91fa1e33f848360970100c133112dd1` |
