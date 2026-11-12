# #327 独立字面 oracle 失败（up-reverse）离线诊断结论

故障指纹：`pr327-literal-oracle-up-reverse`。本文件只记录离线诊断结论；旧 5 次失败/4 次重跑账本见 `offline-ledger.md`，历史失败与产物保持不变。本诊断未运行被测程序，全部依据为已保全的原始产物、补丁脚本与 ac5dd4f 源码。

## 证据基线（已保全）

- `build/auto-debug.out`（749 B，SHA-256 `93e6824a…6e21`）：`copy_debug label=up-reverse expected_len=88 actual_len=170`；expected = `TF_H_135`…`TF_H_143`（9 行含 CRLF）；actual = `TF_H_143`…`TF_H_159`（17 行，末尾带 CRLF）。
- `build/autoscroll-literal.py`（SHA-256 `3ef26b7e…9374`）：把 `check_exact("up-reverse")` 改为 `check_exact("up-reverse", literal_range(up_top_line - ticks, up_start_line))`；`up_top_line`/`up_start_line` 取自 up 阶段前 `screen.visible_lines()` 的第 0/4 行；`ticks` 为 up 阶段后读出的计时器 tick 数。
- `build/fix-autoscroll-literal.py`（SHA-256 `eba4a95b…18f7`）与 `build/debug-autoscroll-copy.py`（SHA-256 `97c9784e…0c75`）：仅去重声明与追加失败打印，未改变场景结构。
- ac5dd4f `src/qt/autoscroll_test.cpp`：up 拖选阶段（119–131 行）之后、 `check_exact("up-reverse")`（179 行）之前，存在**向下拖选阶段**（132–140 行：移到网格下方边缘外并 qWait(120)，产生 down 自动滚动）。

## 机制级根因（观察 → 推断）

观察：

1. 提交场景在 up 阶段之后、检查之前**故意执行了一个 down 阶段**。down 拖动把活动端点拉到 anchor 之下，选区方向翻转，归一化后范围变为 `[anchor 行, 底部行+down ticks]`。
2. 字面 oracle 的期望公式 `literal_range(up_top_line - ticks, up_start_line)` **只建模 up 阶段**（端点在 top 行、anchor 在按下随行），完全没有表达中间的 down 阶段。
3. actual 的起点 `TF_H_143` 恰等于 `up_start_line`（anchor 行），终点 `TF_H_159` 在 anchor 之下——这正是"端点越过 anchor 后范围翻转为 `[anchor, 下方]`"的特征签名。170 B = 17 行全文 + 每个非末行 CRLF + 末行 CRLF（half-open 终点 `(160, col0)`）。
4. 同一源码在 ac5dd4f 的内部一致性 oracle（expected 从 actual range 推导）下通过；CI 10/10 与该内部一致性绿色不覆盖此构造错误。

推断（高支持度）：**失败是字面 oracle（夹具）的构造错误，不是产品缺陷。** 期望公式在错误的场景点（down 阶段之后）断言了只适用于 up 阶段的状态；actual 与提交场景的既定语义（anchor 固定、端点跟随边缘映射、范围归一化、copy 按 half-open 拼接）完全一致。旧 oracle 的 `literal_range` 同时忽略列裁剪（按全文生成期望），是第二个夹具不准确处，但被主错误掩盖。

未验证/不排除：旧失败运行从未隔离出"仅 up 阶段"的观测，因此不能凭旧产物对 up 阶段本身的产品行为下任何结论（无论正面还是负面）。该隔离正是 v2 最小路径的目的。

## 结论与修复

- 产品代码零改动：既定 `HistoryViewport`/`ViewMapping`/`TextSelection` 与源身份保持不变；没有发现需要修的产品缺陷。
- 夹具修复：以 `recovery-protocol-v2.md` 替代旧独立 oracle——固定字面预期（`TF_H_100` / `TF_H_099\r\nTF_H_100`），公开定位到 painted row0=`TF_H_100`，非滚动 sanity 先行，恰好 1 个默认 40ms tick 并在首个有效 `QScrollBar::valueChanged` 槽内公开 Release，release 后 200ms 不得有第二 tick。预期只来自固定字面，不从 actual range 生成。
- 旧账本不变：5 次失败、4 次重跑、一次恢复额度已耗尽的事实保留；v2 单次授权见 PR #327 comment 6717182670。
- v2 入口为 `src/qt/autoscroll_literal_test.cpp`（目标 `qt_autoscroll_literal_test`）：**不注册 CTest**，仅在显式 `--literal-v2` + Job 主管下手动运行一次，防止 CTest/push/PR 双跑消耗额度；普通离线回归仍是原 `qt_autoscroll` CTest。
