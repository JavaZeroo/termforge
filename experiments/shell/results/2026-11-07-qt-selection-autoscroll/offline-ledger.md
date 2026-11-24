# #327 独立 oracle 失败：离线尝试账本与诊断

## 故障指纹

`pr327-literal-oracle-up-reverse`：独立字面 oracle 在 `up-reverse` 阶段失败；所有失败输出均含 `up-reverse: copied source text differs from literal oracle`。**未证明为产品缺陷，也未证明仅为夹具问题。**

## 尝试账本（只记录已发生的执行，不再重跑）

| 顺序 | 时间 (UTC) | 动作 | 结果 |
|---|---|---|---|
| 1 | 13:09:49 | 首次运行独立 oracle | 失败，result seq22186 |
| 2 | 13:10:01 | 修改起点为列 0 后重跑（恢复 1） | 失败，seq22198 |
| 3 | 13:10:20 | 增加 copy debug 打印后重跑（实际重跑 2） | 失败，seq22210 |
| 4 | 13:10:36 | 标为读取 debug 但实际再次运行 run_smoke 并写 `build/auto-debug.out`（实际重跑 3） | 失败，seq22217 |
| 5 | 13:10:48 | restore 源码但未重建，运行旧二进制（实际重跑 4） | 失败，seq22224 |
| 6 | 13:10:59 | 重建已回退的 ac5dd4f；旧内部一致性测试恢复通过 | 通过，但**不是**独立 oracle 成功，不解除本故障 |

结论：首次失败后已有 4 次实际重跑，授权的一次恢复已耗尽；不得通过改名、修改期望、回退源码或新一轮 heartbeat 重置。本轮仅离线诊断，未运行被测程序。

## 保全的原始产物

- `build/auto-debug.out`，749 bytes，SHA-256 `93e6824a89e6dff40756900205f338f37ad9c8319a32fd6771bea8555f886e21`。
- `build/autoscroll-literal.py`，SHA-256 `3ef26b7e8d213432a8ba94cad5c45f0325a6ea70f9c5d96b970c058f70849374`。
- `build/fix-autoscroll-literal.py`，SHA-256 `eba4a95b892160c73df33e3cd26df6855120ff9378fef21728e8ab62102a18f7`。
- `build/debug-autoscroll-copy.py`，SHA-256 `97c9784e64e1c0ae829f8cd1090935000480b5104a9633b5868141657fec0c75`。
- 失败期间的完整 `autoscroll_test.cpp`/`terminal_widget.hpp` 快照**未保留**（工作树已回退到 ac5dd4f）；不根据补丁脚本反推或补造完整失败源码。当前工作树与 ac5dd4f 一致。
- 只使用合成 fixture 和脱敏片段；不发布整份私有会话。

## 观测

`auto-debug.out`：

- `expected_len=88`，`actual_len=170`。
- expected = `TF_H_135` … `TF_H_143`（9 行，含 CRLF）。
- actual = `TF_H_143` … `TF_H_159`（17 行，含 CRLF）。
- 日志中行间空行是 CRLF 在文本输出的显示结果，不代表空源行。

## 静态分析（观察 / 假设 / 未验证）

观察：

- 产品鼠标选区的端点由 `TerminalWidget::selection_position` 从 `painted_page_->mapping` 取源位置；该页可带 `painted_row_offset_`（底部 200 行 projection 的 caret 偏移，见 `terminal_widget.hpp` 中 `painted_row_offset_ = first`）。
- 失败测试的期望构造使用了 `screen.visible_lines()` 的行号（在失败文件已不保留，补丁脚本显示该方向），而不是 painted page 的显示起点；两者在底部历史 projection 时不一定相同。
- 历史 fixture 在启动后 1500ms 自发追加 161–180；拖选/采样期间可能发生追加，`HistoryViewport::reconcile` 可能改变 top/页面偏移。
- 自动滚动 timer 在方向首次置位时启动并清零 tick 计数；测试在 qWait 后读取 tick 数，存在采样窗口。

假设（按支持度）：

1. **高**：期望的显示行/源行映射用错坐标（screen.visible 行 vs painted page 偏移），导致 expected 起点 135、actual 起点 143 的 8 行偏移；端点按 tick 计算也继承偏移。
2. **中**：自发追加/异步 viewport reconcile 改变了采样时的 top/锚点，使实际选中范围扩展到 159。
3. **中**：tick 采样窗口/自动滚动启动时刻与 expected 使用的 ticks 不一致，导致范围长度 9 vs 17。
4. **未验证**：产品端点在边缘/页面偏移下确实选错源行。没有证据支持或排除；不能据此改产品。

以上都不能在没有一次受控重跑的情况下定论；不得用现有失败反推产品缺陷。

## 可审批的恢复计划（未执行，需额外授权）

- 冻结输入：固定 1200×900 窗口，等待 initial 1–180 与 prompt 完全 settled，阶段内禁止异步追加；使用已知固定行列表作为唯一字面来源。
- 冻结视图：先用公开滚动条/键盘把页面停在已知源行 `TF_H_100`，校验显示 row0 字面文本；选择起点固定为该 row0、column0，仅执行 **1 次** 向上自动滚动（1 tick），期望 range 为 `TF_H_099`+CRLF+`TF_H_100`，Copy 完整字面比较；不做多 tick、不做宽字/软行混合，先验证最小路径。
- 先做 no-autoscroll 单行 Copy sanity（同一起点/终点），再执行上述 1-tick 路径；两者都保存原始 expected/actual。
- 运行一次，通过出口：Copy 字节==固定字面、实际源 range 端点对应显示 `TF_H_099`/`TF_H_100`、painted 高亮列与 range 一致；失败出口：立即停止并归档，不再重跑，给出具体原因/解除条件。
- 在该最小路径通过后，另立计划逐步加入软/硬行、宽字/结合标、缩窄/恢复、追加/淘汰/ED3/RIS、Find/滚动条、正确 child 输入；每一步先冻结输入，禁止用旧单独测试替代联合行为。

## 当前状态

- PR #327 保持 blocked，不合并；当前 head `ac5dd4f` 的既有 10/10 CI 与本地 12/12 只属于已有内部一致性覆盖。
- 未执行：独立 oracle 通过、软/宽/结合标、协调缩窄/恢复、追加/淘汰/ED3/RIS 联合、Find/滚动条联合、公开输入正确 child 独立接收。
- 未动 #324/BCD/v2/个人 CF/桌面/未跟踪脚本；未触发 Codex review。


## 替代提案

本文件的旧一次运行提案已被 `recovery-protocol-v2.md` 替代；v2 采用 row0 字面 TF_H_100 起步与默认 40ms 公开 scrollbar valueChanged 首 tick 释放，具体见 PR #327 comment 6717182670。旧提案与旧账本保留，不修改历史失败。
