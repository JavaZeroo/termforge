# 参考实现差分（C1）

此实验把 TermForge 的 C++ 核心与 **pyte 0.8.2 + wcwidth 0.2.13** 放到同一组原始字节、屏幕尺寸和分块输入下，逐检查点比较。参考只用于语义验证；本实验没有计时，不把 Python 的速度用作 C++ 性能目标，也不把 pyte 选为产品依赖。

## 固定来源

| 组件 | 固定版本 / 许可 | 一手来源 |
|---|---|---|
| pyte | 0.8.2 / LGPLv3，随包 LICENSE | [release 源码](https://github.com/selectel/pyte/tree/0.8.2)、[LICENSE](https://github.com/selectel/pyte/blob/0.8.2/LICENSE)、[PyPI 元数据与 wheel SHA256](https://pypi.org/pypi/pyte/0.8.2/json) |
| wcwidth | 0.2.13 / MIT，随包 LICENSE | [release 源码](https://github.com/jquast/wcwidth/tree/0.2.13)、[LICENSE](https://github.com/jquast/wcwidth/blob/0.2.13/LICENSE)、[PyPI 元数据与 wheel SHA256](https://pypi.org/pypi/wcwidth/0.2.13/json) |

[requirements.txt](requirements.txt) 固定两项版本和通用 wheel 的 SHA256，安装时使用 `--require-hashes`。不跟随 latest，也不根据版本号推断当前维护活跃度。测试代码、输入和适配代码为本项目原创；没有把参考实现源码改标 MIT。依赖只安装在 Python 环境，保留原包许可；`UNICODE_VERSION=15.0.0` 在导入前固定，参考包源码哈希也写入运行摘要。

## 自动运行

从仓库根目录运行，先按根目录构建说明生成 `terminal_trace`：

```powershell
python -m pip install --require-hashes -r experiments/differential/requirements.txt
cmake -S . -B build -A x64
cmake --build build --config Release --target terminal_trace
python experiments/differential/run.py --trace build/bin/Release/terminal_trace.exe --out local/differential/new-run
$env:TERMFORGE_TRACE=(Resolve-Path build/bin/Release/terminal_trace.exe).Path
python -m unittest discover -s experiments/differential -p test_*.py -v
```

Linux 使用对应生成器构建，二进制为 `build/bin/terminal_trace`。也可用 `TERMFORGE_TRACE` 或 `TERMFORGE_BIN_DIR` 选择二进制。`--out` 必须是新目录，已有结果不会覆盖。单例可加 `--case wide_lead_overwrite --plan byte`。每次子进程限时 20 秒，核心 trace 限制输入 1 MiB、累计输出 100 万格，避免诊断序列无限膨胀；trace 不适合测量吞吐。

输出按 `case/plan/` 保留 `input.bin`、`chunks.txt`、双方 JSONL、stderr、`report.json`。报告包含输入哈希、尺寸、分块计划、最早差异的字节偏移/字段/双方值与重放参数。即使核心退出失败也保留已有输出；异常退出不会被已知差异规则接受。直接重放一个失败目录：

```text
terminal_trace input.bin 3 12 3 chunks.txt
```

具体尺寸以该目录 `report.json` 为准。测试产物默认保留在 `build/test-artifacts/differential/run-*`，CI 可通过 `TERMFORGE_TEST_ARTIFACTS` 改变位置；只含自有合成输入。

## 比较内容与边界

19 个场景各运行 5 个计划：逐字节、整块，以及种子 `42`、`20261022`、`8675309` 的 1–17 字节非均匀拆包。每个计划都比较初始状态、每次 feed 后的状态和最终状态。逐字节计划能在 UTF-8 和 CSI 内部打断；随机计划另外与双方各自的逐字节轨迹在同一字节偏移比较，检查分块不变性。只在随机计划中出现的错误会单独报告。

投影状态包括：光标行列、待换行状态、每一屏幕格和保留历史格的文字/宽度/前景/背景/粗体，以及当前样式。未比较解析器内部状态、历史丢弃计数、字符附着的内部索引、未支持属性、回复、绘制或性能。

只有两项显式表示转换：

1. pyte 以 `cursor.x == columns` 表示待换行；映射为最后一列加 `wrap_pending=true`。
2. pyte 会将组合文字存为 NFC，TermForge 保留输入标量；比较双方 NFC 后的文字。该转换只承认规范等价文字，不修补宽度、光标、擦除或样式差异。

共同子集的 13 场景覆盖 ASCII/滚动、有界历史、光标移动/默认参数/边界、CR/LF/BS/TAB、J/K、基本 SGR、Unicode 拆包、有限组合文字、宽字符历史和固定种子的控制流。它们不是 VT 兼容性认证，更不能代表所有 Unicode 或所有应用轨迹。

## 已知差异独立保留

`expected` 始终指 pyte，`actual` 指 TermForge，并不表示 pyte 必然正确。六个最小例子的规范、产品文字策略和第二参考裁决见[逐项决定](decisions.md)，不以迎合单个参考为修改核心的依据。

| 场景 | 最早字节偏移 | 观察到的差异 |
|---|---:|---|
| `wide_at_right_margin` | 14 | 仅剩一列时写入“中”：TermForge 先换行，pyte 将宽字首格留在当前行末 |
| `wide_lead_overwrite` | 5 | 覆盖宽字首格：TermForge 清空其尾格，pyte 保留空字符串尾格 |
| `wide_trailer_overwrite` | 8 | 覆盖宽字尾格：TermForge 清空原首格，pyte 保留原“中” |
| `combining_at_pending_wrap` | 14 | 行末基字后的组合标记：TermForge 留在原行，pyte 先进入下一行 |
| `vertical_move_at_pending_wrap` | 15 | 待换行时向上移动：TermForge 取消待换行，pyte 保留 |

[known-differences.json](known-differences.json) 按案例名、输入 SHA256、尺寸、首次偏移、字段、差异方向以及双方最终完整投影状态 SHA256 精确匹配。其余差异、意外变成相同、执行异常都会失败。不同拆包的检查点编号可能不同，但同一最早字节偏移与差异必须一致。五个保留例子均在造成首次差异的操作处结束；不会用一个已知差异放行后续任意行为。原第六项 `styled_scroll_blank` 已按裁决修正为滚动新行默认样式，不再列为参考差异。

生成的共同流限定为无样式滚动，样式滚动仍由单独案例持续检查。回归测试保留“中途不同、最终相同”的光标例子：最早偏移为 16，只检查最终屏幕会漏掉它。

## 原生候选共同轨迹（#25）

`compare_candidate.py` 将同一批 19 场景、5 种分块、输入文件和检查点同时交给自研 `terminal_trace` 与 `termforge-alacritty-trace`，保留双方原始 JSONL、stderr、最早差异和双二进制重放命令：

```powershell
python experiments/differential/compare_candidate.py --out local/candidate-common-new
python -m unittest discover -s experiments/differential -p test_candidate_compare.py -v
```

共同投影只额外统一 NFC 与空白/空单元格表示；显式空格、`mode_bits` 和回复不在这一投影裁决，样式、宽度、光标、屏幕和历史仍逐格比较。`expected` 仅是自研核心字段位置、`actual` 是候选，二者都不是真值。`execution_passed=true` 只表示执行与分块检查通过，`semantic_acceptance` 固定为 false，差异不得自动视为兼容通过或任一实现错误。

2026-10-25 [候选 Windows 摘要](results/2026-10-25-candidate-common-v3/summary.json)为 95 次、1011 检查点，90 次一致、5 次已裁决参考差异；[pyte 摘要](results/2026-10-25/summary.json)为 70 次一致、25 次保留的已知差异。`erase_display` 的最早差异随分块检查点位置而不同，[ED1/ED2 已分开裁决](decisions.md#erase_display-的-ed1ed2-分离裁决)，当时的 `candidate-known-differences.json` 按五种计划固定输入、最早差异和最终状态；新输入不能复用旧裁决。新增 4 个自动门槛与原有 8 个差分测试共同通过；候选 workflow 在 Windows/Ubuntu 构建两个原生 trace 并运行同一矩阵。未构建可选 Rust 候选的通用主回归会明确跳过这 4 项，candidate workflow 不以该跳过代替执行。六类 pyte 差异与 `erase_display` 均已有[逐项裁决](decisions.md)，`differences_adjudicated=true` 只覆盖当前差异，`semantic_acceptance=false` 不把它们算作兼容一致。真实应用和 fuzz 仍由 #25 后续处理，本切片不完成核心路线决定。

## 扩展能力轨迹（#25）

`capability_matrix.py` 另跑备用屏、滚动区、光标查询、异常恢复和 resize 各 5 种分块，共 25 次。严格模式未支持的自研功能记为 `core_unsupported`，必须同时满足候选执行成功，不能当作兼容通过或静默跳过。Recovery 由测试 trace 的显式 `--recovery` 启用，非法 UTF-8、未知 CSI 和 OSC 流与候选逐检查点一致。

[2026-10-25 摘要](results/2026-10-25-capabilities-v2/summary.json)为 15 次 `core_unsupported`、5 次 `match`、5 次 `resize_difference`。自动断言包括备用屏恢复后的 `A/BZ` 主屏、滚动区结果 `B/D/X`、真实回复 `ESC[1;2R`、Recovery 的 `ABC U+FFFD D` 及双方分块不变性。新增 4 个门槛后，差分与能力测试合计 16 项通过；未构建候选时主回归明确跳过这些可选项，candidate workflow 强制执行。

两个测试 trace 在同一输入结束点执行 `--resize=2x6` 并追加同偏移检查点。双方实际尺寸均为 2×6，候选重排为 `123456`/`7890`、光标列 4，自研物理裁剪保留列 5，故记为 `resize_difference` 而非一致。`resize_covered=true`，但 `real_application_covered=false`、`semantic_acceptance=false`；重排合同由 #29 决定，本矩阵不提前实现产品功能。

## 真实应用核心轨迹（#25）

`real_app_capture` 通过 ConPTY 启动真实 Windows cmd、`more.com` 和 Vim，分别验证正常退出、`q` 退出及无配置/无交换文件/无 viminfo 的编辑 `:wq` 与文件内容。Vim 握手要求 `VimEnter` 写入 `READY`、`InsertEnter` 写入 `INSERT`，并观察到 `[New]` 和实际插入文本，再发送 Escape 与 `:wq`，不以一个 ESC 或固定延时代替状态；等待真实事件文件，不依赖消息是否被重绘覆盖。manifest 记录真实编辑器路径和事件验证结果，事件文件一并保留。失败时保存阶段、错误、退出码、合成输出和 Job 清理结果，真实负例验证这些证据可用。`real_app_matrix.py` 将捕获字节原样交给双方核心，自研使用产品同源的 Recovery 模式；它不是截图或 GUI 验收。

[2026-10-25 摘要](results/2026-10-25-real-applications/summary.json)记录应用版本、二进制/源码哈希和退出/文件结果：分页器和 Vim 为 `match`，shell 仅有已裁决的 ED2 历史策略 `reference_difference`。新增 3 个门槛后，差分、能力和真实应用测试合计 19 项通过；Windows candidate workflow 强制构建并执行，其他平台或缺少 Vim/捕获依赖时明确跳过可选测试。

[握手与失败诊断切片](results/2026-10-25-real-applications-v4/validation.json)在本地 Windows 验证三应用 3 次一致、真实编辑器路径和负例回收；真实应用 Python 为 4 项，differential CTest 通过。#37 仍需 Windows Server 2025 CI 的真实应用、fuzz 和计量自然结束，不能据此提前关闭。

[真实事件文件切片](results/2026-10-25-real-applications-v5/validation.json)改用 Vim 写入的 `VimEnter`/`InsertEnter` 文件握手，保留 `READY`、`INSERT` 和实际 60×12 证据。原始输出证明 `echon` 插入消息可能被后续重绘覆盖，故不依赖消息显示时机。候选聚焦 4 项、连续 3 轮场景、真实应用 Python 4 项和 differential 均通过；完整 CI 未通过前 #37 保持开放。

`real_application_covered=true` 仅指这三条真实核心轨迹，`presented_done=null`、`semantic_acceptance=false`，不代表产品 TUI 已交付、所有应用已兼容或 #26 已决定。持续 fuzz 与最小失败复现仍由 #25 后续完成。

## 有界 fuzz 与固定回归（#25）

`fuzz_matrix.py` 在 Recovery 下按固定 token 池和种子生成 1–12 项、最长数十字节的输入，执行 whole/byte 对照并核对最终分块状态。它保留首个未登记失败的种子、计划、最早差异、原始输入和双二进制重放；按逆序单字节删除，只有同一字段差异仍存在才接受删除，不以更短但不同质的输入冒充最小化。`--seeds` 可扩大调查范围，未登记新失败始终使命令失败。

首个 seed 3 是行首组合标记后接宽字：自研按 Recovery 合同替换无相邻基字标记，候选附着到后续宽字，属于参考行为差异而非通过。逆序删除得到最小例 `81 e4 b8 ad 1b 5b 48`，保留同一 `.screen.cells[0][0].text` 差异。`fuzz-known-failures.json` 固定原输入和最小例的输入哈希、最早差异与双方最终状态；改变输入不能复用旧裁决。

[2026-10-25 摘要](results/2026-10-25-fuzz/summary.json)覆盖 seed 0–3 的 8 次 whole/byte：2 次已知失败、2 次一致、4 次已裁决参考差异；[首次失败与最小化](results/2026-10-25-fuzz/first-failure.json)独立保留。新增 4 个门槛后，差分、能力、真实应用和 fuzz 测试合计 23 项通过，candidate workflow 持续运行同一小矩阵。该种子范围不是广泛兼容证明，扩大范围发现的新差异仍需逐项裁决；#26 才评估成本与路线。

## 2026-10-22 实际验证

[历史运行摘要](results/2026-10-22/summary.json)记录共同子集、精确已知差异和分块不变性检查结果；已知差异不算兼容通过。摘要保存输入/源码/二进制/参考包源码哈希和每次最早差异；完整 trace 留在本地输出目录，可按上面的命令重建。

自动测试覆盖实际运行整个矩阵、故意制造额外差异、截断序列执行失败、错误分块计划，以及中间状态恢复的漏检回归。变更已知差异前须核查规范和用例，再审查预期；测试不会自动更新预期。

## termforge.1 兼容补丁结果（#27）

当前固定 `0.26.1-dev+termforge.1` 的[来源与补丁](../../experiments/candidates/alacritty/PROVENANCE.md)纳入上游资源修复、ED1/ED2 和稳定行元数据。[v4 共同矩阵](results/2026-10-25-candidate-common-v4/summary.json)为 95 次、1011 检查点全部一致，旧 ED 差异不再放行；[真实应用 v2](results/2026-10-25-real-applications-v2/summary.json)3 次全部一致；[fuzz v2](results/2026-10-25-fuzz-v2/summary.json)为 2 次固定已知差异、6 次一致。[扩展能力 v3](results/2026-10-25-capabilities-v3/summary.json)仍是 15 次自研未支持、5 次一致、5 次 resize 差异，未把 resize 合同提前关闭。

全部 vendor 文件纳入结果哈希，23 个差分 Python 测试、140 个固定 crate 单元测试和 doc test 通过。上述旧 0.26.0 结果继续作为原始基线，不能改写版本或拼接性能统计；新结果仍不是完整语义接受或产品 TUI。

## termforge.2 拥有值适配结果（#27）

`0.26.1-dev+termforge.2` 增加滚动复用/初始 ID 顺序、历史淘汰和实际 C++/C ABI 适配。[v5 共同矩阵](results/2026-10-25-candidate-common-v5/summary.json)继续 95 次全部一致，[真实应用 v3](results/2026-10-25-real-applications-v3/summary.json)3 次一致，[fuzz v3](results/2026-10-25-fuzz-v3/summary.json)为 2 已知差异+6 一致，[扩展能力 v4](results/2026-10-25-capabilities-v4/summary.json)分类不变。142 个固定 crate 单元测试、doc test、23 个差分 Python、15 个计量与 C++ candidate_core 均通过；[适配验证](../../experiments/core/results/2026-10-25-candidate-adapter/validation.json)另行记录。默认 shell 和产品 TUI 仍未验收。

## termforge.3 每缓冲区 margin 与宿主差异（#27/#41）

`0.26.1-dev+termforge.3` 随主/备用网格保存独立滚动区域，并在 resize/RIS 同时重置。[v6 共同矩阵](results/2026-10-26-candidate-common-v6/summary.json)为 95 次一致，[扩展能力 v5](results/2026-10-26-capabilities-v5/summary.json)分类不变，[fuzz v4](results/2026-10-26-fuzz-v4/summary.json)为 2 已知差异+6 一致，[真实应用 v6](results/2026-10-26-real-applications-v6/summary.json)的固定新鲜捕获为 3 次一致。141 个固定 crate 单元测试、1 个 doc test 与 C++ `candidate_core` 均通过。

[margin 验证](../../experiments/shell/results/2026-10-26-margin-state/validation.json)分别保留源 VT 的整段/逐字节恢复、resize/RIS 和真实 ConPTY 的宿主差异。真实宿主未恢复主屏 margin 由 #41 保留，差异观察不作为能力通过。另一 366 字节 More 捕获的尾部 `CR + CSI 79X` 暴露 Recovery 擦除差异与捕获同步问题，旧、新候选均与 pyte 一致，由 #42 保留，不自动改判 match 或归因于 margin 修复。

路线判断见 [ADR-0003](../../docs/adr/0003-core-reference-and-reuse.md)。
