# WindTerm 2.7.0：组合标记、后续 Q 与位置 API 候选

这批使用固定 Windows x64 原版包，经新的 [`unicode_caret_reference`](../../unicode_caret_reference.md) 协议执行 **3 个独立进程、每个 4 阶段，共 12 阶段**。每个计划只运行一次，原始运行与严格导出均通过，隔离 Job 已核对回收；主动结束不等于自然退出。没有重跑旧 [Unicode resize 样本](../2026-11-02-resize-unicode-reference/README.md)。

确定的结果是：不移动光标时，U+0302 出现在 Z 后，后续 Q 继续追加；显式 CUP 到原 e 的位置后，U+0302 出现在 e 前，Q 替换 e 和它原有的 U+0301；缩窄后也保留 U+0302，Q 替换 e，但原 Z 已在输入 U+0302 时消失。正文与 DSR 可以独立用于字面回放，**API 矩形不能直接作为 glyph、Cell 列或组合归属的 oracle**。

## 来源与执行身份

| 记录 | 公开 JSON SHA-256 | 授权计划 SHA-256 |
|---|---|---|
| [dsr-only-q.json](dsr-only-q.json) | `eb2ec7b8f84ec5a78f3597ef8b2f86947f0a430c9e9b4849680e97d6f8d9544b` | `4cf41bdc73a99f19437bdc99a542dbdcfe7366215f613f9f41cb33400d7a10dd` |
| [cup-position-q.json](cup-position-q.json) | `24a5bad1a5455bb4b95bb9ef20b05b81c79f4237d1310e7fdcb93dd6da98972a` | `7b01cee237dd134281fbb8799247ab663d05f2693218801d75ab7497a71cd9d9` |
| [resize-mark-q.json](resize-mark-q.json) | `1e8e152e659c93f77e96e8b611e5fc9df43d8947912ac804f1f239eb7535f6f2` | `bfe77d25ef654b4b1073b6d47f7a890ac6d289cda16db7a629608ea13b5764cf` |

- 执行源码基点：`0a44d44740a6a27c1a949926fda6fc9947ed58b1`。10 份运行源与 3 份导出源逐文件 SHA 在各 JSON 的 `provenance.execution_source_files_sha256` / `offline_export_source_files_sha256` 中；源码归档与 Git 固定提交字节一致。
- 源码 ZIP SHA：`5d80652680bd5806ff24ed281c35f4fa6994644aa30b319aaa6b54ae63d13712`；严格 manifest SHA：`d850f2d9db01ff5f2cbab36227054265ff5ba0525e8d62e566bb2a5138d0105b`。VM 使用显式源码归档身份，不伪造 Git checkout。
- 原版 EXE SHA：`7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0`；原包 4,047 文件及模块已由 VM 严格导出器核对，文件 manifest SHA：`e459adf2f12adb6a1b7f55e66a9da1c22794185e66e848057d21e0c60d669203`。
- 记录环境：Windows `10.0.26300`、Python `3.12.10`、REA `6.7.0`；运行文件与 provider 身份在公开 JSON 中。运输是隔离合成 loopback Telnet，不是产品 fixed-ConPTY。
- 三例阶段 0 都是协议列出的精确 60-byte UTF-8 输入，SHA `c6d08514fb8897ee31059833506de1354f04b69b9721aa1153f34f750e444d49`。阶段 2 只发 `cc82`，阶段 3 只发 `51`；每阶段单独 DSR、采样并 ack，动作前正文均保留。
- 本机复核公开 schema、归档/提交/源字节、计划与 outer 身份、有限正文目录、完整文字、DSR、六候选的来源与算术、原 caret 查询绑定及脱敏。没有在本机重跑 VM 上的完整原包/provider 审计。

## 12 阶段的完整正文与独立光标值

以下 `S60` 精确表示 60 个 U+0020。所有阶段的前七个文档项均为 `["", "0123456789", "ANCHOR", "", "", "", ""]`；第八项是表中正文。只有 `resize-mark-q` 阶段 1 额外含一个末尾空字符串，文档计数为 9；其余计数均为 8。容量均为 20，scroll 为 0。公开文件逐行保留 UTF-8 hex、字节/scalar/UTF-16 长度、有限目录索引及全文 hash，可完整重建这些空行和空格。

`é` 以下始终指 `e + U+0301`，`◌̂` 仅是说明 U+0302 的记号；实际正文没有 U+25CC dotted circle，也没有做 NFC 替换。

| case / 阶段 | 动作后第八项正文 | 网格 | DSR | 原 caretPosition |
|---|---|---|---|---|
| dsr-only-q / 0 | S60 + `e U+0301 Z` | 91×20 | 8;63 | 87 |
| dsr-only-q / 1 | S60 + `e U+0301 Z` | 91×20 | 8;63 | 87 |
| dsr-only-q / 2 | S60 + `e U+0301 Z U+0302` | 91×20 | 8;63 | 89 |
| dsr-only-q / 3 | S60 + `e U+0301 Z U+0302 Q` | 91×20 | 8;64 | 90 |
| cup-position-q / 0 | S60 + `e U+0301 Z` | 91×20 | 8;63 | 87 |
| cup-position-q / 1 | S60 + `e U+0301 Z` | 91×20 | 8;61 | 83 |
| cup-position-q / 2 | S60 + `U+0302 e U+0301 Z` | 91×20 | 8;61 | 85 |
| cup-position-q / 3 | S60 + `U+0302 Q Z` | 91×20 | 8;62 | 86 |
| resize-mark-q / 0 | S60 + `e U+0301 Z` | 91×20 | 8;63 | 87 |
| resize-mark-q / 1 | S60 + `e U+0301 Z`，另有末尾空项 | 61×20 | 8;61 | 83 |
| resize-mark-q / 2 | S60 + `U+0302 e U+0301` | 61×20 | 8;61 | 85 |
| resize-mark-q / 3 | S60 + `U+0302 Q` | 61×20 | 8;61 | 85 |

表中除初始后缀 `65cc815a` 外，新增后缀依次为：`65cc815acc82`、`65cc815acc8251`、`cc8265cc815a`、`cc82515a`、`cc8265cc81`、`cc8251`。相应第八项 UTF-8 长度分别为 66、67、66、64、65、63 bytes；每一项都包含前导 60 bytes 空格。

不 resize 的两例没有 NAWS 改变；第三例只在阶段 1 从 91×20 变为 61×20，没有恢复宽度、LF、额外 CUP 或补绘。初始 origin OFF、wrap ON、全屏 margin 固定。无连续输入对照，因此本批不能单独证明 DSR 在所有情况下都不影响组合行为。

## 六种候选查询与矩形的不稳定对应

每个 marker 保存原 `lineStart`，分别按 UTF-8 前缀字节、UTF-16 code unit 和 scalar 前缀的起点/末端查询。`lineStart(7)=23`。所有输入都是 BMP，UTF-16 与 scalar 的数值相同，不能据此判断非 BMP API 单位。

例如初始 Z 的 UTF-8 起点候选为 86，UTF-16/scalar 起点候选为 85；初始 caretPosition 为 87，与 `23 + 64-byte 行文本` 一致。无 resize 追加 U+0302、Q 后 caretPosition 为 89、90，也与字节增量一致。这支持这组文本中的字节边界解释，**不证明所有 API、所有字符或私有实现统一使用字节**。CUP 后 caretPosition 85、86 在另外两种候选下也能匹配不同文本边界，故公开分类字段仍为 `null`。

更关键的是，同一阶段、相同数值的 query 可以记录不同矩形。按 `(case, phase, requested_position)` 分组、同时包括直接 caret query：无 resize 例有 5 组差异，CUP 例有 9 组，resize 例有 6 组。**其中 6 组不仅宽度不同，x 也不同；没有观察到同组 y 或 height 不同。**

| case / 阶段 / API 数值 | 记录到的不同 x（px） | 例子 |
|---|---|---|
| cup-position-q / 2 / 85 | 627、635 | acute 的 UTF-16/scalar 起点与 e 的 UTF-8 起点；直接 caret query 为 635 |
| cup-position-q / 2 / 86 | 635、643 | Z 的 UTF-16/scalar 起点与 e 的 UTF-8 末端 |
| cup-position-q / 3 / 85 | 627、635 | Z 的 UTF-16/scalar 起点与 Q 的 UTF-8 起点 |
| cup-position-q / 3 / 86 | 635、643 | Z 的 UTF-8 起点与 Q 的 UTF-8 末端；直接 caret query 为 643 |
| resize-mark-q / 2 / 85 | 627、635 | acute 的 UTF-16/scalar 起点与 e 的 UTF-8 起点；直接 caret query 为 635 |
| resize-mark-q / 2 / 86 | 635、643 | acute 的 UTF-8 起点与 e 的 UTF-8 末端 |

这些组的 y 均为 126、height 均为 18。另如初始 Z 的两次位置 85 查询，矩形宽度分别为 16 与 8。全部原值均保留，未合并相同 offset、选一个“正确”结果或重跑挑结果。这只能说明当前固定调用/采样链没有建立可用于判定 glyph 的单值位置映射；尚未定位原 API 缓存、调用顺序或采样机制等原因，不能宣称原版绘制有错。

直接 `visibleRectFromPosition(caretPosition)` 也只是独立原调用结果：无 resize 四阶段的 x 为 651、651、651、659，CUP 为 651、627、635、643，resize 为 651、627、635、635；各 y 为 126。它们不自动成为产品 caret 绘制或 Cell 列合同。

## 实现输入与尚未确定的边界

正文允许先检验一个较小的自研表示：把 U+0302 附在光标左侧的最后一个空格 Cell，保留下一 Cell 的 e/U+0301；Q 覆盖下一 Cell 后，文本自然成为 S60 + U+0302 + Q。宽度事务后的编辑先按新列宽物化，可以解释缩窄例 Z 的消失。**这是可验证的实现选择，不是已经观察到的原版 Cell 或内部 attachment 机制。** 当前证据没有强迫增加前导零宽 Cell 类型或独立字节插入游标。

独立 [`resize_caret_test.cpp`](../../../../experiments/core/resize_caret_test.cpp) 已逐字核对这 12 个原始输入/正文/DSR，定义 strict/recovery × whole/byte 四种 feed，共 **48 阶段**。该回放只比较完整逻辑正文、协议网格和 DSR；只忽略末尾空字符串容量，不 trim 空格或省略中间空行；API offset/矩形、私有组合状态均不作为 Cell oracle。此目录记录原版证据，不继承后续 C++ 编译、RED/GREEN 或全量回归结果。

尚未覆盖的边界包括首列无前驱、跨行/软换行、宽字前驱、pending-wrap、多个连续 mark、剪切后的选区/Find 原子范围、实际 glyph/字体与 caret 显示、Qt/fixed-ConPTY 联合流程、任意脚本/非 BMP/完整字素、性能和 R2 出口。新正文回放通过也不自动关闭这些任务。

## 离线门禁与公开范围

本工具冻结前 Python normal 25/25、`-O` 25/25，Node controller/sampler 9/9；根代理注册的三个 CTest 项通过 3/3（32.21 秒，`caret-registered-ctest.log`）。这些是工具离线验证，与三次真实原版观察分列，不是产品性能或完整兼容性结论。

公开材料只包含有限源派生正文与标量观察、输入/源码/计划/文件哈希。不限正文、像素、原包资源、profile、路径、进程标识及原始 provider 日志保留私有；`semantic_acceptance=false`、`presented_done=null`、单位/策略分类为 `null`。这批没有恢复宽度、重复可靠性测量或性能评价，也不宣称完整 Unicode、#27/#29 或 R2 已完成。
