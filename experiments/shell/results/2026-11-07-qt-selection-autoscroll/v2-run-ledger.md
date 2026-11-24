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

## 额外授权执行（2026-11-24，已消耗）

- 以 `main` `cd6a43b51c50f8ced1b777b6d756e368f32aa3d2` 的固定源码，在 Windows hosted runner 上手动 dispatch 一次。历史活性门、三个结束标记和 500ms 稳定窗均已通过。 [运行 45399066643](https://github.com/JavaZeroo/termforge/actions/runs/45399066643/job/138462569257) · [脱敏结果和身份](literal-v2-recovery-20261124.json)
- 夹具实际有 24 个活动行、100 个历史行。公开 Home 后首行是 `TF_H_101`，随后遇到 `TF_END`，首个失败为 `positioning row0 is not a TF_H_ literal`。两条固定期望源行 `TF_H_099` 和 `TF_H_100` 已淘汰。两项复制及单次滚动路径均未到达，主管退出码 1、Job 后置活动进程为 0。
- 字面夹具没有布局，`1200×900` 父窗请求未传到终端控件。核心对同一固定输出做容量回放，24 行配 100 条历史首行是 `TF_H_101`。按本次 22 像素单元格和现有边距计算，布局后的 38 行配同一历史上限，首行是 `TF_H_087`，两目标行相邻且保留。此回放是独立的核心验证，Qt 真实布局仍待复验。
- 下一版夹具补全终端布局，定位前独立确认两条目标行相邻，并按完整三位行号识别越过目标。固定输出、500ms 稳定窗、8/18 字节预期和默认 40ms 定时不变。本次额外授权已执行 1/1。

## 布局修复后的授权复验（2026-11-24，已消耗）

- 冻结 `f883941`，手动执行一次。实际网格 38 行，历史 100 行，公开 Home 后首行是 `TF_H_087`。13 次公开滚动条单步到达目标 rank 13，非滚动复制得到固定 `54465f485f313030`（8 字节）。 [运行 45402742788](https://github.com/JavaZeroo/termforge/actions/runs/45402742788/job/138475308265) · [脱敏结果和身份](literal-v2-layout-20261124.json)
- 首个失败是 sanity 复制后 `pre-drag state drifted`，未启动定时器拖拽，18 字节仍待验证。主管退出 1，Job 后置活动进程为 0。本次授权执行 1/1。
- v2 第 3 步要求在正式拖拽前重新定位 `TF_H_100`，原夹具只断言视口仍在原处。Qt 6.10.3 的 `QTest::keyClick` 会先投递单独的修饰键 press，当前 `TerminalWidget` 会把此类事件送进历史回到底部的路径。这解释了复制字节正确且 viewport 可能改变的机制，失败记录没有保存漂移终点。 [v2 协议](https://github.com/JavaZeroo/termforge/pull/327#issuecomment-6717182670) · [Qt 6.10.3 源码](https://github.com/qt/qtbase/blob/v6.10.3/src/testlib/qtestkeyboard.h)
- 下一版夹具在 sanity 使用明确的同行鼠标 Move，记录复制后视口，通过独立的公开 Home 和滚动条单步重新定位后再拖拽。正式产品逻辑、固定字面、500ms 稳定窗、默认 40ms 定时均沿用原协议。
