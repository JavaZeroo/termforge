# C2：有界输入恢复策略

本实验在原有严格解析之外提供显式 `ParseMode::recovery`。它让未实现或损坏的输入不再终止消费，但不会扩充成完整 VT 模拟器；不实现 OSC 副作用、剪贴板、查询回复、备用屏幕或滚动区域。所有规则是 TermForge 当前有限核心的契约。

```cpp
termforge::Parser parser(24, 80, 100, termforge::ParseMode::recovery);
parser.feed(bytes);
parser.finish();
const auto& diagnostics = parser.diagnostics();
```

默认构造仍选择 `strict`。已有 replay/receive/trace 不自动改为 Recovery；共同子集差分仍使用严格模式。严格模式保持支持范围、成功快照与错误退出合同，抛错后对象不得继续使用。`finish()` 现在是非 const，因为 Recovery 的 EOF 处理会更新状态。

## 消费和同步规则

| 输入 | Recovery 行为 |
|---|---|
| 已支持的 ASCII、UTF-8、CR/LF/BS/TAB 和 CSI | 继续执行原来的屏幕规则 |
| 未支持的 ESC | 消费所有 intermediate 字节，到 `0x30..0x7e` final；不显示序列正文。识别 `ESC [` 和下述字符串引导符 |
| 未支持/非法 CSI | 消费到 `0x40..0x7e` final。私有参数、colon/intermediate、过大数值、过多参数和超长序列进入丢弃状态，后续参数不作为普通文本 |
| 非法 SGR 或擦除模式 | 整个命令不生效；SGR 先在临时 Style 验证，再一次性提交，避免部分参数泄漏样式 |
| OSC (`ESC ]`) | 从引导符后直接丢弃，BEL 或 `ESC \\` 结束 |
| DCS/SOS/PM/APC (`ESC P/X/^/_`) | 从引导符后直接丢弃，只以 `ESC \\` 结束，BEL 也是正文 |
| 字符串中的 ESC | 除 `ESC \\` 外一直留在字符串模式；不会把内嵌 CSI/OSC 当命令，也不输出字符串正文。重复 ESC 以最后一个 ESC 配对 ST |
| CAN/SUB (`0x18/0x1a`) | 在任何控制序列/字符串内取消该序列并恢复 ground；ground 中忽略 |
| ESC 重同步 | 在未完成 ESC/CSI 中重新开始 ESC；字符串遵守上面的 ST 规则 |
| 其他 C0/DEL | ground 中忽略；ESC/CSI 内忽略并继续原序列。CR/LF 等不会在序列中执行。字符串中的控制字节继续丢弃，除 BEL/CAN/SUB/ST 的特殊规则 |
| 非法 UTF-8 | 以 Unicode maximal-subpart 策略输出 U+FFFD；不能续接当前字符的字节重新交给解析器，合法文本或 ESC 不被吞掉 |
| 有效但当前宽度策略不支持的标量 | 单个 U+FFFD 替换，包括无基字的组合标记、每格超过 16 个组合标记、单列屏上的宽字符 |
| 原始 `0x80..0x9f` / 编码后的 U+0080..U+009F | 原始孤立字节按非法 UTF-8 处理；有效 UTF-8 解码所得控制标量按不支持标量替换。当前仅支持 7-bit ESC 控制编码，不重新解释 UTF-8 标量为 C1 控制 |

没有终止符的字符串会继续吞掉随后的字节，直到 CAN/SUB、合法终止符或调用 `finish()`。不能猜测某个普通文本已经脱离字符串；也不以长度阈值擅自恢复 ground，否则超长字符串尾部可能变成命令或文本。

`finish()` 是调用方明确的流结束屏障，不是每次 `feed()` 的收尾。Recovery 遇到未完成 UTF-8 时输出一个 U+FFFD，遇到未完成控制序列时直接丢弃；清理解析缓存，但不重置屏幕/样式/历史。重复调用没有额外影响，之后可以 `feed()` 新的独立片段。普通数据块边界不能调用 `finish()`。严格模式仍在截断输入时抛错。

## 上限和诊断

Recovery 不保存字符串正文；ESC/CSI 只保存状态、计数和最多 16 个整数参数，UTF-8 最多 4 字节。CSI 数值上限 99,999；计数超过 32 字节则进入丢弃。ESC intermediate 即使超长也继续消费到 final。字符串超过 4,096 字节只计一次超长事件，仍继续直接丢弃，不分配正文缓存。ESC 引导符之间的 C0 不妨碍识别后续 `[`/字符串引导符；它们仍计入诊断长度。

`RecoveryDiagnostics` 的事件计数为饱和 `uint64_t`，不随输入长度分配；`max_sequence_bytes` 饱和在当前类别上限加一，最大 4,097，它不是总消费字节数。`max_parameters <= 16`、`max_utf8_bytes <= 4`。`buffered_input_bytes()` 是当前参数整数与 UTF-8 内容的逻辑字节数，不是 STL 容量或进程内存。容器最多存 16 个整数和 4 个字符，容量不会随后续恶意输入持续增长。屏幕/历史仍由已有尺寸上限和每格 16 个组合标记约束。

`ignored_sequences` 计开始丢弃的控制序列，`ignored_controls` 计 ground/ESC/CSI 中忽略的单字节控制；`malformed_utf8` 计 U+FFFD 替换的非法 UTF-8 子段；`replaced_scalars` 计有效但无法布局的标量。取消、重同步、截断和超长分别计数，可能与已开始丢弃的序列重叠，不能把所有计数相加当作错误数量。

可恢复错误使用专门 `InputError`；解析器只捕获此类型。`std::bad_alloc`、输入输出错误和其他程序错误仍会传播，不当作正常输入恢复。解析器没有发送回复的接口，也没有执行外部命令、访问剪贴板或设置窗口标题的路径。

## 自动验证

根 CMake 构建 `recovery_probe`，测试使用真实编译的解析器：

```powershell
$env:TERMFORGE_BIN_DIR = (Resolve-Path build/bin/Release).Path
.venv/Scripts/python -m unittest experiments.core.test_recovery -v
build/bin/Release/recovery_probe.exe input.bin 24 80 100 1,7,2,4096 recovery
```

可选第七个参数 `finish_offset` 在指定字节偏移执行 EOF 屏障，然后继续输入，用于验证截断清理与恢复；探针每个屏障重复调用 `finish()`。输入上限 64 MiB，分块模式每项 1..1,048,576 字节、最多 1,024 项；子进程测试超时 15 秒。每条失败保留输入、命令参数和 stdout/stderr 到 `TERMFORGE_TEST_ARTIFACTS/recovery`，未设变量时使用 `build/test-artifacts/recovery`。

30 项测试的期望文本和样式独立手写；非法 UTF-8 子段额外用 Python 标准解码器交叉检查。短案例遍历固定块长并用 seed `20261023` 产生非均匀拆包；长案例覆盖百万字节 ESC intermediate、CSI 数字、OSC 正文。还覆盖取消、内嵌 ESC、C1 边界、无效 SGR 原子性、标量/组合标记限制、EOF 重入和恢复后的合法文本。任意字节流测试验证不同拆包结果一致，并在显式取消后执行已知合法文本。严格模式负面用例与共同子集快照也保持回归。

## 依据与边界

- [XTerm Control Sequences](https://invisible-island.net/xterm/ctlseqs/ctlseqs.html) 给出 ESC/CSI/字符串的语法及 OSC/BEL/ST 行为，也明确错误恢复策略由实现决定。本实验选择持续丢弃和显式屏障，不声称复制 xterm 的完整恢复行为。
- [Unicode 17.0 第 3.9.5–3.9.6 节](https://www.unicode.org/versions/Unicode17.0.0/core-spec/chapter-3/) 说明恢复时不得吞掉合法后继子段，并描述 maximal-subpart 替换。宽度表仍固定 Unicode 15.0；本次不升级字符布局数据。

此处的恢复计数证明错误路径发生，逻辑缓存高水位与实现结构共同检查上限；它们不是整机内存采样、兼容性百分比或性能测量。真实 shell 窗口、回复查询、备用屏幕、滚动区域和 resize 仍由后续任务验证。
