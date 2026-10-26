# R2 流式字素、宽度与固定 emoji 合同（#39）

本合同属于显式候选 `Term/Processor` 路线，不扩展默认自研解析器。固定首批样本为 `e\u0301`、`🙂`、`👩‍💻`、`1️⃣`；要求原始标量、不经 NFC 的原文、单元格、光标、选择复制和实际绘制一致。WindTerm 2.7.0 的对应运行时行为仍未知，不能由静态 QString 测量路径推断。

## 依据与路线

- [UAX #29 revision 47 / Unicode 17](https://www.unicode.org/reports/tr29/tr29-47.html)定义扩展字素边界。固定官方 `GraphemeBreakTest-17.0.0.txt` 的 766 个用例验证流式**分段 helper**边界（不冒充完整 Term/VT 状态正确性），保留 [Unicode 许可证](../../experiments/candidates/alacritty/vendor/alacritty_terminal/tests/data/UNICODE-LICENSE.txt)。分段不改变编码，不把 feed 结束当作文本结束。
- [UTS #51](https://www.unicode.org/reports/tr51/)定义 emoji ZWJ/keycap 等序列。[UAX #11](https://www.unicode.org/reports/tr11/tr11-44.html)明确终端需要额外宽度策略，不能把“所有字素两列”称为 Unicode 强制要求。
- 本项目选择 `unicode-width 0.2.2` 的非 CJK 字符串宽度策略，ambiguous 为窄；其[规则](https://docs.rs/unicode-width/0.2.2/unicode_width/)支持合格 emoji 序列宽 2。现有独立 `wcwidth 0.2.13` / Unicode 15 的 `wcswidth` 对固定 ZWJ/keycap 也得到 2；它不充当完整字素或终端状态真值。
- [kitty 官方文字尺寸说明](https://github.com/kovidgoyal/kitty/blob/master/docs/text-sizing-protocol.rst)说明 VS15 缩窄保留已经发生的 wrap。这只作为流式策略的独立旁证，不是 WindTerm 比较或 kitty 全量兼容声明。

选择在候选 `Term` 内保存**一个立即可见、可修订的尾字素**。延迟最后一个字素会让单字输入等待未来字节；在 C ABI 前另写 UTF-8/ANSI 解析器或在 Cell 后拼接会重复终端语义。两条路线均不采用。宽度变更继续使用候选的写格、换行和滚动路径；不保存整屏/整行回滚副本，不撤销已发生的历史滚动。

## 状态与动作合同

| 事件 | 行为 |
|---|---|
| UTF-8 分块、feed 结束、snapshot、普通选择观察 | 不结束尾字素；前缀立即可见，光标反映当前前缀宽度 |
| 后续标量 | 用 UAX #29 判断是否延续；完整序列由一个 lead Cell 保存，wide spacer 不复制第二份文本；末列 leading spacer 是明确可绘制的换行填充，选择投影跳过它但保留真实写入空格 |
| SGR、超链接、颜色、窗口标题、只读查询、bell | 保持尾字素；首标量的样式拥有整个字素，修改后的模板供下一字素使用 |
| CPR/其他查询 | 立即观察当前前缀的真实光标；不会等未来 VS16，也不会预知最终宽度 |
| 光标移动、清除/插删、滚动、屏幕/模式/字符集变更、resize、RIS、外部可变 grid 访问 | 结束尾字素；控制之后不会重新打开先前的 ZWJ/RI 分段上下文；零宽 Prepend 则建立向前延续状态，不能在后继 ASCII 到来时丢失 |
| 控制边界后的孤立零宽标量 | 保留既有候选的前格附着策略；这不重新打开复杂字素上下文。超过格预算时进入无损分片 |

这些终端事件边界、样式归属和剪裁规则是 **TermForge 策略**，并非 UAX #29 对 VT 字节流的直接规定。未知或被解析器忽略、没有产生 Handler 动作的控制序列不增加一个伪造动作。

## 宽度变化、边缘和有界输入

- 固定首批样本宽度依次为 1、2、2、2。`👩‍💻` 与 `1️⃣` 必须各为一个 lead、一个 spacer，后续字符不得占错列；选择端点落在 spacer 内时向外展开，复制完整原文。
- 从宽 1 增长到 2 时补占一格。若在最后一列且开启 autowrap，旧前缀成为 leading spacer，整个字素换至下一行；原生滚动规则继续执行。已显示前缀造成的覆盖不会反事实恢复。
- 从宽 2 缩至 1 时清理 spacer，并调整光标；已经发生的换行、旧内容覆盖和历史滚动保留。INSERT 模式按宽度差移动尾部，不重复整字素插入；裁剪边缘不得留下孤立 wide lead；从 wide trailer 开始插入时先原子清理命中的整对格，再移动尾部，避免将旧 spacer 与 lead 拆离。
- 关闭 autowrap 后最后一列、或整个窗口只有一列时，保留完整字素文本，以宽 1 的**明确 clipped footprint**显示。候选内部、C ABI 和 C++ 拥有值均保留 clipped 标志。超出当前 1/2 列模型的宽度同样明确标剪裁，不吞标量。已有 clipped 格随窗口增宽不主动反向重排；后续重绘/覆盖遵从新的输入状态。
- 缩到一列时，reflow 必须把完整 wide lead 改为 clipped 格，只移除其专用 spacer；不能反复把一个永远装不下的 wide 格推到下一行。主/备用屏均覆盖这一边界。
- 每片最多 **64 标量、256 UTF-8 字节**。超长组合输入无损 spill 到后续片，标明 `grapheme_fragment`，以饱和 `u64 grapheme_overflows` 汇总；无逐片无限事件、静默截字或杀死会话。可选 serde 入口同样逐个标量检查 63 字符后缀上限，拒绝外部超长格；旧无 fragment 字段的数据按 false 读取。分片后的原文仍可精确选择复制，但不宣称超限输入仍按一个完整字素布局。下一边界探测使用固定 260 字节栈缓冲，持久文本不会为探测第 65 个标量而无界增长。

## 实际绘制

候选产品使用 DWrite，对完整 Cell 文本整形。固定 ZWJ/keycap 必须实际产生 **1 个非缺失 glyph，字体为系统 Segoe UI Emoji**，且 advance 在分配网格内。仅检查 `missing==0` 不足：旧 keycap 会由 Consolas 数字和 Segoe UI Symbol 方框两个 glyph 组成。完整合格 keycap 集合 `[0-9#*] VS16 U+20E3` 因此作为一段指定 Segoe UI Emoji；不改变 ASCII 的 Consolas 字体和中文 fallback 合同。默认 GDI 路径不因此获得复杂字素绘制声明。

真实 owned ConPTY 场景先捕获 80×24，再通过真实 resize 和独立 `GetConsoleScreenBufferInfo` 客户端确认 100×30；两阶段均经实际产品拖选/复制、实际 DWrite glyph 审计与 retained/full frame 比较。原生 Windows 控制台的非 BMP 替换投影继续与 transport 原文分列。固定增宽场景通过不代表所有缩放、所有原生重放或 WindTerm 运行时等价。

## 依赖与复验

`unicode-segmentation = 1.13.3`，Unicode 17，MIT OR Apache-2.0，MSRV 1.85.0，注册表 SHA-256 `c6f5d3c3b1bf09027a88a6bc961fc00497d651009560b5463668dc81b0fa87a8`；无运行时依赖，开发依赖不进入本项目锁定图。与已锁定 `unicode-width = 0.2.2` 同为 Unicode 17。当前图为 59 个注册表包、两个 path crate；最高声明 MSRV 仍为 1.85，实际工具链为 1.92.0。默认自研 Unicode 15 策略不变。候选补丁版本为 `0.26.1-dev+termforge.4`，来源见[候选台账](../../experiments/candidates/alacritty/PROVENANCE.md)。

```powershell
cargo test --manifest-path experiments/candidates/alacritty/Cargo.toml --locked --workspace
python -m experiments.candidates.alacritty.grapheme_checks --out local/graphemes-new
cmake -S . -B build/candidate-r39 -A x64 -DTERMFORGE_BUILD_CANDIDATE_ADAPTER=ON
cmake --build build/candidate-r39 --config Release
ctest --test-dir build/candidate-r39 -C Release --output-on-failure
```

性能仍有成本：同宿主交错的每路线 3 对、至少 1 秒 CPU 诊断中，project 吞吐中位数从 25.86 降至 21.66 MiB/s（约 16%），interactive 从 13.84 降至 12.54 MiB/s（约 9%）。原始 12 份样本和二进制身份全部保留；共享宿主、allocator 仍被包装的 observe=0，以及缺少呈现/传输/输入时延端点使它只能说明当前成本，不能作为性能验收通过或 WindTerm 等价。[#54](https://github.com/JavaZeroo/termforge/issues/54) 负责受控定位与预算，并阻塞 #34 的性能裁决；不能由本项正确性证据豁免。

固定红/绿、规范数据哈希、源/二进制身份和实际场景结果见[验证记录](../../experiments/shell/results/2026-10-26-streaming-graphemes/validation.json)。本项不代表完整 IME、所有字体/脚本、真实跨屏、呈现完成或 WindTerm 性能等价；相关 R2 父项保持其独立出口。
