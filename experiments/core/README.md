# 最小终端状态回放与有界恢复

本说明描述**默认自研 C++17 核心**：把分块字节流变成可检查的屏幕、光标和有界历史状态，供离线回放、接收实验和[本地 shell 工作区](../shell/README.md)使用，不依赖 WindTerm、Qt 或私有实现。显式候选构建的 Term/Processor 能力与 [C ABI / C++ 拥有值适配](CANDIDATE.md)另列，不受下表的自研支持范围约束；产品核心路线见 [ADR-0003](../../docs/adr/0003-core-reference-and-reuse.md)。

解析器提供默认 `strict` 和显式 `ParseMode::recovery` 两种模式。replay/receive、trace 默认值和共同子集差分使用严格模式，本地 shell 选择恢复模式；测试 trace 可用第七个参数 `--recovery` 比较恢复行为，或 `--resize=RxC` 在输入结束点 resize 并追加检查点。[恢复策略](RECOVERY.md)定义未知控制序列、非法 UTF-8、超长输入、取消及 EOF 的有界处理；下表未支持输入的失败规则适用于严格模式。

`Screen::resize` 采用[物理行裁剪契约](../../docs/adr/0005-physical-row-resize.md)。`resize_test` 通过根 CTest 在 Windows/Linux 检查内容、历史、宽字符与字节流边界；本地 shell 将它与窗口/ConPTY 尺寸同步，replay CLI 不执行隐式 resize。

## 支持范围和约定

控制序列的名称与基本行为参考 [XTerm Control Sequences](https://invisible-island.net/xterm/ctlseqs/ctlseqs.html)。本实验只实现下表子集；限制和失败策略是本项目的实验约定，不是对 xterm 全部行为的声明。

| 输入 | 当前行为 |
|---|---|
| ASCII `0x20..0x7e` | 单列写入和覆盖；写到最右列时置待换行标志，下一个可打印字符才换行 |
| CR / LF | CR 回到本行第 0 列；LF 下移并保持列号，底部时全屏向上滚动；清除待换行 |
| BS / TAB | BS 左移并限制到第 0 列；TAB 前进到下个 8 列制表位，限制到最右列；清除待换行 |
| `CSI A/B/C/D` | 相对移动，省略或 0 表示 1，限制到屏幕边界 |
| `CSI H/f`、`G`、`d` | 行列定位、列定位、行定位；输入为 1 起始坐标，省略或 0 表示 1 |
| `CSI J/K` 的 0、1、2 | 向后、向前或全部擦除，包含当前位置；填充空格，不移动光标，不清空历史 |
| `CSI n X` | 擦除当前位置起 n 格；省略或 0 为 1，裁剪到本行右边界，不移动光标，清除待换行；[来源与 More 回归](../../docs/research/more-capture-ech.md) |
| UTF-8 | 跨块解码 1–4 字节；严格拒绝过长编码、代理区、越界、孤立续字节及截断输入 |
| `CSI m` | 0 重置、1/22 粗体开关；30–37/40–47 和 90–97/100–107 前景/背景色；39/49 恢复默认色 |

状态 JSON 使用 0 起始 `row/column`，每行保留尾部空格，记录 `wrap_pending`、完整可见行、保留的历史行和已淘汰行数。滚动按物理行保存历史，默认 24×80、最多 100 行历史；没有逻辑行重排。搜索由独立只读投影模块执行，解析器不持有查询。清除显示不是清除历史。

解析器在 `feed` 调用之间保留 ESC/CSI 和 UTF-8 状态。SGR 参数最多 16 个，其他命令按语义限制为 1 或 2 个；数值最多 99,999、CSI 参数到终止字符最多 32 字节。尺寸限制为 1–200 行、1–500 列，历史容量 0–10,000；容量为零时只累计淘汰数。

**明确未支持：** 完整字素簇和文字整形、ZWJ/变体选择符/旗帜/肤色修饰序列、256 色/真彩色及其他 SGR 属性、OSC/DCS 的功能、备用屏幕、滚动区域、插入删除、逻辑行 reflow、模式切换、回复查询及其他控制字节。严格模式下，未支持输入、非法参数或 EOF 时截断的序列使运行失败；不静默忽略，也不输出成功快照。严格模式不支持控制序列内部的 C0 中断/取消；错误后不得继续复用该解析器对象。Recovery 可消费这些未实现功能，但不会执行它们。

## 字符宽度与单元格契约

宽度数据固定为 Python 3.12.10 的 Unicode 15.0.0 数据库，不追随宿主系统变化。[UAX #11](https://www.unicode.org/reports/tr11/) 给出 East Asian Width 属性，但它本身不是完整终端布局算法；以下是本实验的明确策略：

- 已分配的 L/N/P/S 类字符及 Zs：W/F 为 2 列，其余（包括 Ambiguous）为 1 列。
- Mn/Me 附着于最近直接写入的基字符，不占新列，每格最多 16 个标记。移动、擦除和显式换行会断开附着；没有基字符或超过上限则失败。SGR 不断开附着，标记保留基字符样式。
- 拒绝控制/格式/私用/未分配字符、Mc、变体选择符、regional indicator 和肤色修饰符。不能据此声称支持全部文字系统或 emoji 字素；单个已分配 emoji 标量按上述属性处理。
- 宽字符占主格（width=2）与空文本续格（width=0）。覆盖或擦除任何一半会清除完整旧字符，即使另一半在擦除区间外；写入位置仍由光标决定。
- 行尾只剩一列时，宽字符先换到下一行，原行剩余格保持原内容；屏幕仅一列时拒绝宽字符。写满行后结合标记仍附着原基字符，下一个占列字符才触发换行。
- 每个单元格保存前景、背景、粗体；擦除使用当前样式，滚动新行使用默认样式，历史保留原样式。没有字体、调色板或实际绘制。

`lines/history` 是 UTF-8 行文本，续格不额外输出字符，因此字符串长度不等于列数。非普通 ASCII 或非默认样式单元格通过稀疏 `cell_details/history_cell_details` 输出坐标、文本、width 和 style；空缺字段表示没有例外，普通 ASCII 快照格式保持兼容。`current_style` 仅在非默认时输出。fg/bg=-1 是默认，0–15 是色表索引，不是 RGB。

`unicode_width.hpp` 含 1,246 个压缩区间，来自原创 `generate_width.py`；脚本要求 `unicodedata.unidata_version == 15.0.0`，生成文件随源码提交，正常构建不下载数据。数据来源及许可见 [UNICODE-LICENSE.txt](UNICODE-LICENSE.txt)，不是项目 MIT 许可的替代。重新生成命令：`.venv/Scripts/python experiments/core/generate_width.py`。

## 模块边界

- `screen.hpp` 中的 `Screen` 管理单元格网格、光标、待换行、样式和历史；当前按单元格行搬移，单元格使用字符串，尚未优化内存布局。
- `Parser` 消费分块字节并调用屏幕操作，与 Windows 管道和线程无关。
- `replay` 从文件读入，在计时前完成输入准备，计时覆盖分块 feed 和 EOF 检查；JSON 序列化不计入。
- `receive` 可选文件输入：消费者校验字节后 feed；解析失败仍排空传输并 join，最终失败且不输出 screen，避免线程异常导致挂起。屏幕只由消费者修改，join 后读取。

## 无交互构建与验证

根目录 CMake 已在 Windows / Visual Studio 2022 和 Linux / GCC 构建验证；核心不包含 Windows API，管道与 ConPTY 条件构建。完整依赖及 CI 说明见 [贡献指南](../../CONTRIBUTING.md)。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
$env:TERMFORGE_BIN_DIR = (Resolve-Path build/bin/Release).Path
.venv/Scripts/python -m unittest experiments.core.test_core experiments.core.test_unicode experiments.receive.test_receive -v
.venv/Scripts/python experiments/core/run.py --out local/experiments/core-new
.venv/Scripts/python experiments/core/run.py --fixture unicode --out local/experiments/core-unicode-new
```

输出目录必须不存在，每次子进程有超时，失败记录会保留。单独回放命令：

```powershell
build/bin/Release/replay.exe input.bin 24 80 100 512
# 读取上限、通知策略、调度模式、输入文件字节数、队列容量、文件
build/bin/Release/receive.exe 512 edge stream 580000 512 input.bin
```

`test_core.py` 使用手写期望画面，每个有效短案例遍历块长 1 到输入长度+1；同时覆盖不支持字节、截断序列、参数/尺寸上限和管道中的失败退出。不是用同一个解析器生成自己的正确答案。

`run.py` 生成 20,000 条带覆盖、擦除、CR/LF 的编号记录，共 580,000 字节。期望屏幕与历史直接由编号范围推导。分别通过 5 种分块的文件回放，以及 3 种读取上限 × 2 种通知策略 × 2 种调度的真实管道传输，逐项比较完整状态后保存快照摘要。管道容量等于读取上限，专门制造背压。

`--fixture unicode` 使用 800,000 字节的中文、组合标记、红色前景和编号记录；期望格位及样式同样直接构造，不调用待测解析器。

## 文字投影与选择

`text_document.hpp` 提供同线程、短生命周期的只读适配；返回行文本与跨度均按值持有，以行身份/版本和物理半开列范围表达位置，不向消费方暴露可变行引用。`text_selection.hpp` 为每个会话独立持有选区，合并真实软折行、以 CRLF 复制硬行边界并保留显式空格；不拆开宽字和有限组合标记。

选择快照最多 8 MiB，调用者另设复制字节上限；超限全拒绝、不返回截断 UTF-8，纯未写 padding 返回空选择结果。首版词规则将 ASCII 字母/数字/下划线/连字符及非 ASCII 基字归为词，空格成组，其他 ASCII 标点单独选择；这是项目策略，不是完整 Unicode 分词或 WindTerm 所有词边界的证明。非破坏性尺寸变化仅在投影、跨度和换行完全相同后刷新版本；覆盖、裁剪和淘汰使选区失效。

复验入口为 `ctest --test-dir build -C Release -R '^selection_core$' --output-on-failure`。纯模块通过不代替 [#13](https://github.com/JavaZeroo/termforge/issues/13) 的鼠标、窗口、系统剪贴板和跨会话验收。

## 有界增量查找

`text_search.hpp` 在同一文本投影上执行 UTF-8 字面量匹配，软折行直接相连，硬行边界为 LF。大小写选项仅折叠 ASCII，不做 Unicode 规范化；匹配不重叠，命中组合字符的一部分时高亮整个单元格。这些是首版明确策略，不能外推为完整 WindTerm 或正则语义。

查询最多 4096 UTF-8 字节，模块结果硬上限 4096，窗口使用 1024。调用者以 `step` 分批推进，在屏幕改变后调用 `notify_change`；模块保留稳定前缀进度，分批检查内容版本、相邻行和淘汰，查询替换或关闭会使旧 generation 失效。结果快照必须经 `match(document, index)` 对当前内容重新验证。 `navigate_no_wrap` 只导航当前可验证命中，不补全未知范围、不报告全局循环；窗口在仍运行的扫描中仅对用户显式请求采用此策略，并明确提示局部结果。`work_used` 约束扫描/元数据工作单元，单行投影、结果过滤和窗口绘制仍有成本，不是整个 UI tick 的硬截止保证。

复验入口为 `ctest --test-dir build -C Release -R '^search_core$' --output-on-failure`。窗口方向、循环、焦点与持续输出的完整验收由 [#15](https://github.com/JavaZeroo/termforge/issues/15) 承接。

## 历史回放结果与限制

2026-10-22 的 v0.2 ASCII/Unicode 回放满足当时的字节、画面、历史、样式及背压检查。结果按版本保留，不代表当前提交重新执行了同一批测量：

- v0.2 ASCII：[身份](results/2026-10-22-v02-ascii/manifest.json)、[记录](results/2026-10-22-v02-ascii/samples.jsonl)、[期望](results/2026-10-22-v02-ascii/expected.json)
- v0.2 Unicode：[身份](results/2026-10-22-v02-unicode/manifest.json)、[记录](results/2026-10-22-v02-unicode/samples.jsonl)、[期望](results/2026-10-22-v02-unicode/expected.json)

- v0.1（`d15a1fc`）：[输入及源码身份](results/2026-10-22/manifest.json)、[完整预期屏幕/历史](results/2026-10-22/expected.json)、[运行记录](results/2026-10-22/samples.jsonl)

每个配置只跑一次，是正确性与计量端点烟测，没有预热和统计对照。`parse_ms` 在文件回放中是整个 feed 循环，在管道中是各块 feed 的墙钟耗时总和；后者包含计时开销及可能的线程抢占，排除队列等待和字节比较。它已经包含于整体运行时间，不能重复相加。解析、屏幕更新和历史维护尚未各自计量。管道原有的队列到校验延迟字段在解析模式下结束于 feed 之后，不能与旧的纯字节校验样本直接等同比较。

以上 v0.2 历史样本未测显示或输入响应，也没有覆盖完整终端协议。单元格模型改变了成本，不能用历史单次计时声称性能改善。[pyte 参考差分与逐项裁决](../differential/README.md)保留各自输入和边界；[长批次计量](../measure/README.md)尚不足以完整归因或排名。窗口集成及其验证见[本地 shell 工作区](../shell/README.md)；剩余能力以 [R2 合同](../../docs/milestones/R2-local-terminal.md)及[功能矩阵](../../docs/FEATURES.md)为准。
