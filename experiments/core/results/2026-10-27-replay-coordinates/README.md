# resize 回放中的文字列位置

本记录修复 [#29](https://github.com/JavaZeroo/termforge/issues/29) 的一个候选适配层错误：相同文字移到其他列后，旧选区和 Find 命中仍可能被当作有效。发布核心源码为 `fbcbe3844b49d3151dea60c71d0aee066ead6283`，与已测本地提交 `f8692d4203bdcae9a362fc1dc6ace88567bf9511` 的 Git tree 完全相同；基点为 `8a13782aaac03780617e177b56ec55d3cb9c79be`。固定 Rust 候选仍为 `0.26.1-dev+termforge.4`，未修改上游、FFI 或默认核心。

## 原因与修复

回放完整行键及后续身份缓存比较均去掉首尾未写 padding，因而把第 0 列的 `ABC` 与第 2 列的 `ABC` 视为相同，保留了依赖坐标的旧版本。选择和 Find 可以继续返回旧坐标，而当前投影已是 `  ABC`。

两条匹配路径现在都保留首个内容列号；末尾未写 padding 仍不影响匹配。文字移位后原选区失效，旧 Find 命中失效，增量重扫取得正确列范围。这遵循已有选择/版本合同，没有采用新的 reflow 或原版差异策略。

## 独立反例与覆盖

用公开 `CandidateTerminal::feed/resize`、`TextSelection::copy` 和 `TextSearch::match/notify_change` 验证。2×8、history=4 输入 `H\r\nABC\r\nTAIL`，resize 到 2×10，再输入：

```text
ESC[?25l ESC[1;1H ESC[2K ESC[1;3H ABC CR LF ESC[?25h
```

上行空格仅分隔控制序列；实际字节及独立预期写在 `candidate_terminal_test.cpp` 的 `replay_text_coordinates` 中。旧代码返回失败 `replay kept a selection at the old columns after moving its text`；修正后通过。另将缓存比较单独恢复旧逻辑，保留新行键，完整测试仍失败，证实缓存入口也需要修正。原始失败消息保留在本目录；首轮负例的完整构建产物未归档，不以它声明二进制身份。

矩阵有 12 个组合：

- 列 `0→2`、`2→0`，以及 `2→2` 的不变对照。
- 在 hide/show 包围的 resize 回放中，或完成回放后的普通输出中重画。
- 整段 feed 或逐字节 feed。

移动用例要求旧选区和旧命中失效，Find 重扫精确命中 `[2,5)` 或 `[0,3)`；不变对照要求选择仍复制 `ABC`。原有固定 861B 回放、历史和其他候选检查保持执行，没有改预期、删除用例或放宽期限。

## 复验与证据边界

```powershell
cmake -S . -B build/candidate-adapter -A x64 -DTERMFORGE_BUILD_CANDIDATE_ADAPTER=ON
cmake --build build/candidate-adapter --config Release --target candidate_core_test
ctest --test-dir build/candidate-adapter -C Release -R '^candidate_core$' --output-on-failure
```

[validation.json](validation.json) 保存代码文件及二进制哈希、环境和该候选构建的完整 CTest 结果；运行时间仅为测试诊断。源改动经过独立 Standards/Spec 审查。

Windows 11 x64、MSVC 19.42、Rust 1.92.0 的首次完整候选 CTest 为 **48/49 通过、1 失败**。失败套件 `candidate_shell_tui` 的 6 个用例中，Vim 场景在 stage50 报 `'vim' is not recognized as an internal or external command`，其余 5 个用例通过。保持代码、二进制和期限，仅为测试进程的 PATH 加入已有 MSYS2 Vim 9.1（patches 1–532），单独复验该套件 **1/1 通过**，包含 6 个用例、每个场景两轮。首次失败与补齐依赖后的独立复验分别保留，不拼成一次 49/49 结果。

本次没有运行 WindTerm 对照或性能测量。冻结期间网格尺寸、补回历史行宽和回放期间显式清空的源码疑点由 #29 接续；未复现的疑点不写成已证实故障。真实子进程尺寸、备用屏/滚动区交叉、实验官方后端、真实不同 DPI、完整 R1 系统输入组合及完整 R2 仍需各自验收。这里的成功不等于完整 resize/reflow、WindTerm 等价或最终核心选型批准。
