# WindTerm 2.7.0：帧切换、resize 与末列状态

本报告记录 [固定帧协议](../../resize_frame_reference.md)的原版结果，用于 #27/#29。原版观察与 TermForge 的工程表示分开：公开文档 API、DSR 和 marker 矩形不证明私有帧结构，也不能合并成完整 active grid。

本批实际启动原版 **6 次、采集 29 阶段**：5 例原机械审计及严格导出成功，共 21 阶段；`hidden-primary` 的 8 阶段原审计失败保留，随后同一录制的独立离线补充审计通过，新增原版运行次数为 0。不能合称六例原审计全部通过。

## 执行范围与记录状态

每个 case 都使用一个独立新进程、一个合成 loopback Telnet 会话、独立不可重放的计划。固定原版 WindTerm 2.7.0 x64，Windows VM `10.0.26300`，REA `6.7.0`，Python `3.12.10`。窗口为 `1360×749 → 1000×749 → 1360×749`；实际 NAWS 为 `91×20 → 61×20 → 91×20`。中间动作阶段不改变窗口；阶段结束只追加一次 `ESC[6n`，不补 CUP 或重绘正文。

| case | 阶段数 | 本报告材料状态 |
|---|---:|---|
| `alternate-active` | 3 | [严格导出](alternate-active.json)已核验 |
| `hidden-primary` | 8 | 原审计白名单失败保留；[独立离线补充审计](hidden-primary-supplement.json)通过，不计原 strict 成功 |
| `pending-on-shrink` | 4 | [严格导出](pending-on-shrink.json)已核验 |
| `wrap-off-shrink` | 4 | [严格导出](wrap-off-shrink.json)已核验 |
| `pending-on-grow` | 5 | [严格导出](pending-on-grow.json)已核验 |
| `wrap-off-grow` | 5 | [严格导出](wrap-off-grow.json)已核验 |

原始输入和各阶段身份由 JSON 保存。初始公共正文 B 为 78 字节，SHA-256 `c7dd4a7fa283dd0003ca8b2ae13dcb46f143a4717bf5a14eb5f5acb068a9fa1c`；末列例只使用公共前缀 P 加 W/X，具体字节见协议与各 JSON，不把 B 当作全部 case 的输入。

归档前核对精确公开 schema、case/阶段输入、源码与导出器 SHA、原包身份及脱敏字段，再按原字节复制。严格导出本身核验完整 4047 文件原包、加载模块、源码归档、生成 probe、provider evidence ID、一次 attempt、合成 profile、全部 NAWS/DSR/geometry 和回收记录。JSON 不包含本地路径、PID、用户会话、profile、原始资源或文档全文。`job_after.active=0` 只说明受控终止后 Job 空，不证明原版自然退出。

下文 DSR 为一基协议坐标，文档行列为零基，矩形为像素 `(x,y)`。`line_count` 和 `scroll` 是公开文档 API 值，不等同于 20 行 active grid 或历史行数；尤其备用屏记录可以包含当前不可见的主屏文档 marker。

## 活动备用屏：正文重排与恢复

`alternate-active` 在进入备用屏后写入 B，始终保持备用屏活动，再缩窄和恢复。三次 DSR 都为 `11;2`；v/w/x 的文档行列和 position 保持。

| marker | 文档位置 / position | 初始矩形 | 缩窄矩形 | 恢复矩形 |
|---|---|---|---|---|
| ANCHOR | `(3,0)` / `13` | `(147,36)` | `(147,36)` | `(147,36)` |
| v | `(8,90)` / `114` | `(867,126)` | `(379,144)` | `(867,126)` |
| w | `(10,90)` / `207` | `(867,162)` | `(379,198)` | `(867,162)` |
| x | `(10,91)` / `208` | `(147,180)` | `(387,198)` | `(147,180)` |

文档 `line_count=12→13→12`，`scroll=1` 三阶段不变。可见矩形与先前主屏固定正文例相同，但文档行号整体偏移 1；不能据此声称备用屏拥有一行历史、与主屏共用 owner，或已验证离开备用屏时的行为。

## 缩窄前已到末列：ON/OFF 两例结果相同

`pending-on-shrink` 开启 DECAWM，`wrap-off-shrink` 显式关闭 DECAWM；两例分别在初始 91 列的 `CUP(8,91)` 写 W。直到缩窄观察结束，不发送 CUP 或新字符。下表逐阶段 DSR、marker、矩形及文档摘要在两个独立进程中一致。

| 阶段 | 动作 | DSR | W/X 文档与显示 |
|---|---|---|---|
| 0 | 初始前缀、定位并写 W | `8;91` | W `(7,90)`，position `113`，矩形 `(867,126)` |
| 1 | 仅缩窄 | `8;61` | W 文档位置不变，矩形 `(379,144)` |
| 2 | 仅写 X | `8;61` | W 消失；X `(7,60)`，position `83`，矩形 `(627,126)` |
| 3 | 仅恢复宽度 | `8;61` | W 未复活；X 文档位置和矩形不变 |

独立构造固定合成文档数组并重算规范 JSON SHA，两例共八阶段全部与公开摘要相符。阶段 0 的最后一行是 `90 个空格 + W`；阶段 1 保留该文本并增加一条尾部空文档行；阶段 2/3 的最后一行精确为 `60 个空格 + X`。文档行数为 `8→9→8→8`，`scroll=0`。

这证明本例缩窄后的协议光标已在新第 61 列，下一 X 写入新末列并移除旧 W，未产生下一行写入。ON 例把 W 写在开启 DECAWM 的旧末列，但本报告没有直接读取内部 pending-wrap 位；不能把“旧末列后下一字符本应换行”的预期替代 resize 后的实际结果。ON/OFF 在这一轮缩窄和单次 X 上结果不可分，不代表其他动作下两种模式相同。

## 增宽前已到末列：ON/OFF 两例结果相同

`pending-on-grow` 与 `wrap-off-grow` 分别开启/关闭 DECAWM，先写公共前缀 P，再缩窄到 61 列；随后才用 `CUP(8,61), W` 建立窄屏末列状态。增宽后先单独采样，下一阶段才写 X。两例各自五阶段的下列 DSR、marker、矩形和完整文档摘要一致。

| 阶段 | 动作 | DSR | W/X 文档与显示 |
|---|---|---|---|
| 0 | 公共前缀 P | `3;7` | W/X 尚未写入 |
| 1 | 仅缩窄 | `3;7` | W/X 尚未写入 |
| 2 | 窄屏定位末列并写 W | `8;61` | W `(7,60)`，position `83`，矩形 `(627,126)` |
| 3 | 仅增宽 | `8;61` | W 文档位置和矩形不变 |
| 4 | 仅写 X | `8;62` | W 消失；X `(7,60)`，position `83`，矩形 `(627,126)` |

两例共十阶段完整合成文档数组的独立摘要复算均与公开 SHA 相符。阶段 2/3 的末行是 `60 个空格 + W`，阶段 4 为 `60 个空格 + X`，文档行数 `3→3→8→8→8`，`scroll=0`。增宽后 X 覆盖旧 W，然后协议光标递增一列；它既不进入下一行，也不追加到 W 后一列。ON/OFF 仅在本轮增宽与单次 X 上结果不可分，不推广为所有末列行为相同。

## 隐藏主屏：原失败与补充审计

本例先在主屏设置 margin `2..5`、origin ON，再进入备用屏设置 margin `3..7`、origin OFF，缩窄后先仅发 `CSI ?1049l` 采集原样返回，之后才发三组 CUP 控制。

八阶段已经采集，但冻结原审计以 `ValueError: Only fixed frame-source synthetic document lines are allowed` 失败，原失败状态保留。独立 [补充核验器](../../audit_resize_frame_hidden.py)仅允许在阶段 3..7 增加一个已知源派生候选：`61 个空格 + x`，长度 62。它必须先重现原失败，并保持原 `mechanics-check.json` 缺失；不修改执行源或旧计划，不重新启动原版。

离线普通模式与 `-O` 模式各 8 项合成录制测试通过，包括错误计划、私有文本、溢出和重复 marker、晚阶段几何/ack、provider/脚本/源码/原包/profile/attempt 与未完成录制拒绝。随后对同一份原录制执行独立离线核验，exit 0；公开结果采用新 schema `termforge-original-resize-frame-hidden-supplement-v1`，明确记录原失败重现、失败保留、补充审计通过及新增原版运行次数为 0。它不生成或替换原机械审计文件。


补充核验重新验证八阶段、NAWS/DSR、provider/generated script、完整原包、源码归档、profile 与回收。以下为经过该独立补充核验的观察，不冒称原审计成功：

| 阶段 | 动作 / 活动屏 | DSR | 关键文档与矩形观察 |
|---|---|---|---|
| 0 | 主屏 B、margin 2..5、origin ON、MP | `2;12` | MP `(2,9)`；v `(7,90)`；w/x `(9,90)/(9,91)` |
| 1 | 进入备用屏、margin 3..7、origin OFF、AP | `2;12` | AP `(11,9)`，矩形 `(219,18)`；原 MP/v/w/x 仍在文档，但矩形 y 为负 |
| 2 | 仍在备用屏，仅缩窄 | `2;12` | AP 位置不变；主屏 v/w/x 文档坐标保持，矩形仍在屏外 |
| 3 | 仅 `CSI ?1049l`，原样返回主屏 | `3;12` | v/w 消失；x `(9,61)`，position `152`，矩形 `(147,180)`；MP 保留 |
| 4 | 原返回状态下 `CUP(1,1), PS, CUP(99,1), BS` | `20;3` | PS `(0,0)`、BS `(18,0)`；矩形 y 为 `0/342` |
| 5 | 显式 origin ON，同样 CUP 后写 PI/BI | `20;3` | PI/BI 的行列与矩形同阶段 4 |
| 6 | 显式 origin OFF，同样 CUP 后写 PO/BO | `20;3` | PO/BO 的行列与矩形同阶段 4 |
| 7 | 仅恢复宽度，此时已被控制动作设 OFF | `20;3` | v/w 未复活；x 仍 `(9,61)`，矩形 `(635,162)`；BO 矩形 y 为 `324` |

这八阶段的固定合成文档数组均经独立 SHA 复算匹配。原样返回阶段的文档行 7 精确为 61 个空格，行 9 为 `61 个空格 + x`；文档整体摘要为 `031ebdc3f836dd626e360af9ffc15715ce865669b4635fcfdc46a7379092c9a1`。这区别于单纯活动帧显示重排：返回样本的旧 v/w 正文没有保留。它不证明原版具体在 resize、激活或其他内部步骤的哪一处裁剪，也不证明文档 API 的旧副本就是隐藏帧内部存储。

三组上/下控制均落在显示第一行和第 20 行，DSR 都为 `20;3`，不支持此时仍可按原 2..5 区域寻址的假设。**返回样本、显式 ON 和 OFF 控制重合，origin 状态无法由这组控制区分**；不能据此声称私有 origin 位一定关闭，也不能声称区域滚动的全部行为已经验证。阶段 3 的 `3;12` 是未经后续 CUP 改写的单独观察；阶段 7 已受控制动作影响，不是原样返回的第二次验证。

文档行数为 `11→13→14→11→20→20→20→19`，scroll 为 `0→11→12→0→0→0→0→0`。它们不能当成活动备用屏历史、协议网格行号或源 owner 迁移的证据。

## 工程约束与未知

活动备用屏的固定正文有保留和显示重排证据；缩窄前处于旧末列的主屏样本要求调整协议光标及后续写入，不能简单保留旧 column/pending 状态后直接调用现有 `put`。这些是可观察行为约束，不是原版内部机制结论。增宽 ON/OFF 的下一字符同样不换行，而是覆盖原末列 W 后推进光标；隐藏主屏返回还出现旧尾部消失及寻址边界控制重合。不能把活动帧的正文保留合同直接套给隐藏帧，也不能仅根据缩窄样本决定增宽处理。

本组不验证高度改变、宽字/组合字符、样式、单列、任意尺寸、滚动/历史淘汰、完整擦除范围、在途输出或可靠性重复。直接 Telnet 结果不是正式 Qt 与固定 ConPTY 联合验收，也不构成性能等价、完整 WindTerm 行为或 R2 完成声明。

## 源码与结果身份

本批执行源归档基点为 `e1ab10eac5c26dac412bb7ee149d11827d206c3e`，源码 manifest SHA-256 为 `83a0257d886af7de08b7d87e76d483a9c34d5ec88ab208f3521d9e984cc95b04`。基点与实际十个执行源文件摘要分列，不把新工具冒称为基点内已有代码。

| 文件 | SHA-256 |
|---|---|
| [resize_frame_reference.py](../../resize_frame_reference.py) | `cb02eb95ff12caff05accaed5223e816cb081818e5a077d6c562070979f02928` |
| [resize_frame_reference.js](../../resize_frame_reference.js) | `d3269acced0d1400203af9ab16d4998145871fb6520b862124bab6e5c490edf5` |
| [export_resize_frame_reference.py](../../export_resize_frame_reference.py) | `0d76dae1a64741970bf2ec77d053623f5942b4c2ef2919851f24057b3e9f600b` |
| [audit_resize_frame_hidden.py](../../audit_resize_frame_hidden.py) | `e515a61a4b2b8205b56f5fb94c720f74037c67569c5ad6d44d9b62e84e3e3c78` |

原版 EXE SHA-256 为 `7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0`，完整 4047 文件 manifest SHA-256 为 `e459adf2f12adb6a1b7f55e66a9da1c22794185e66e848057d21e0c60d669203`。每份 JSON 另存全部执行源与离线导出源、模块、工具链、provider evidence ID 和原件摘要；不发布原版产物。

| 公开文件 | SHA-256 | 独立计划 SHA-256 |
|---|---|---|
| `alternate-active.json` | `4578bfb765afaf7c891cd0af19aa649fef030b8e2794bbd81ec79f8aaf54bf8e` | `8d168429f782f7175cefbe64172eb2c4bb930880579de768b0cd1e12e683079e` |
| `pending-on-shrink.json` | `0e37a46dccec9c2d0620487c63486d086c205874c62835ce506ba333ebd70bb9` | `b6f9a66488e18d75c6c1db916bd7393868487c21aa408e3c17cfe198f2a4bc38` |
| `wrap-off-shrink.json` | `19b31b4ebe6603f699c203ed7c9b5cb91fa8ff342e5ddecc7951fb5f44a5169a` | `5f91d93f10e7debb2dc9d0a546e5472a99053dea6c223419401893bd38545550` |
| `pending-on-grow.json` | `ee5230fcc35cbab1bf47d09eae675733eebcd79f054fa514974f0abc6df7613c` | `8eb1dc0b09f98d45dcb299cf9938f26984ddf4957dcec867d5d760ba17323d1b` |
| `hidden-primary-supplement.json`（原失败后独立补充） | `1fde75cb4f841177ab868377b8621624afa0383601b994dd511c0f138d2a6974` | `c4650e7e7eb64714a3fbcc7ebe30e45345dd404414676395df0d20bee6efceed` |
| `wrap-off-grow.json` | `c67554247cd10eb20c9f28c8fb117ac48f395782027f7215947189f80c82ff0f` | `e11d15285e71db0b4ea2906fc4c48434bf109efe5282572fe21f0e42129fa13d` |
