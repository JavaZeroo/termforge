# #327 v2 字面恢复：单次执行账本

授权：PR #327 comment 6717182670 v2 协议；预算 1 次执行。本账本只追加，不修改。

## 执行 1（2026-11-12，已消耗）

- 场景身份：`qt_autoscroll_literal_test.exe --literal-v2`（opt-in 单次入口，未注册 CTest）。
- 源码/构建身份：branch head `8d01e9a324a1d707ef7fb010feedbcc73cd63bd6`（merge `742b399` + 本测试提交）；`src/qt/autoscroll_literal_test.cpp` SHA-256 `04328457e66218cdaafb0e14dc236d858910d778866e08757344b6b8d9dccf6b`；`src/qt/terminal_widget.hpp`（合并 #324 后）SHA-256 `cf5ac6d3e2bdfd5028058241657346705dfe5e95ffac23bf9487ae6867ffa815`；二进制 SHA-256 `3207a523b9a94b940fd748b88386d4039ace0c6fc43ca97d3e4d3fe6e32fe1e0`。
- 入口：run_smoke.py（offscreen、Job 主管、GO 门）；未调慢 timer、未直接调用内部滚动函数、未换 oracle。
- 结果：**失败**，`literal autoscroll fixture did not reach 160 lines`（8s 阶段上限）。supervisor JSON：`status=failed, exit_code=1, job_after{active:0,total:4,terminated:0}`，无遗留进程。
- 原始产物：`literal-v2-run-1.json`（证据文件，仅含 error/schema/status）；`build/literal-v2-run-1.supervisor.log`（supervisor 原始输出）。
- child PID：未到达记录点（失败发生在 settled 记录之前）；Job 会计 total=4。
- 协议路径消耗：settled 定位 0 次、非滚动 sanity 0 次、1-tick 路径 0 次——失败发生在三者之前的夹具活性门。

## 根因（离线确诊，无需重跑）

测试活性门 `history_lines().size() >= 160` 在任何几何下都不可达：fixture 首批写 160 行 + `TF_END` + prompt（prompt 为 live 行），屏幕高 H（24–45 行）时历史行数 = 161−H ≤ 137；自发追加（161–180 + `TF_END2`）后 = 182−H ≤ 158 < 160；只有公开 `'m'` 批（181–220）后历史才超过 160——而 `'m'` 的发送被该门死锁。即：测试夹具前置条件写死，非产品行为、非 v2 协议路径失败。提交版 `autoscroll_test.cpp` 用 `>= 100`（首批后 ≈116–137 可达）正因同一算术。

## 解除条件（待授权方决定）

- 修复：把初始活性门改为 `>= 100`（与提交版一致）；settled 仍由 `TF_END`→`TF_END2`→`'m'`→`TF_END3`→500ms 安静窗逐字锁定，行数门仅作活性证明。改动只触测试源码，不触产品/协议/期望。
- 然后由授权方明确：执行 1 未消耗任何协议路径（settled/sanity/tick 均为 0 次），是否批准一次修正后的执行。未获批准前不再运行。
