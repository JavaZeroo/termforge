# 多行 hosted 粘贴失败账本（未闭合）

## 首个失败

- head `eecf21f`，run `42181178347` / job `127770923069`（Windows QPA、默认 reader、真实 CF）。
- 动作：先做三次单行公开入口，再写 CF `L1\r\nL2`，经 Ctrl+Shift+V 进入真实多行 `QMessageBox`，测试端点击 Cancel 后重新发起并点击 Paste。
- 实得：child 文件仍只有前三次单行内容：
  `5053542de4b8ade696872065cc815053542d4d454e555053542d494e53455254`
  （`PST-中文 éPST-MENUPST-INSERT`）；`notice=Paste cancelled`；随后 `paste child bytes differ`，exit 1、`timed_out=false`、Job `active=0`。预期 `baseline + L1\rL2` 未出现。

## 已执行尝试（同故障恢复次数）

| 尝试 | head | run / job | 结果 |
|---|---|---|---|
| 首个失败 | `eecf21f` | `42181178347` / `127770923069` | 失败：无 L1/L2，notice cancelled |
| 修复后重试 1 | `bc6a5af` | `42181721249` / `127772638312` | 仍失败；该修复为窗口 activate/raise/focus + 测试确认改用产品 trigger |
| 诊断重试（同故障，不新增额度） | `5ea8e44` | `42182236823` / `127774271059` | 仍失败；实际仍为三条单行，notice cancelled；调试显示多行 read 均 finish，但无传输/child 字节 |
| 删除多行 hosted 分支后的单行回归 | `10df02e` | `42182751375` / `127775920833` | 通过，但 `multiline_confirm=false`，不能算多行成功 |
| 交付前巡检 | `9b48627` | `42184032136` / `127780005871` | 通过；sample 明示 `multiline_cancel=false`/`multiline_confirm=false` |

结论：该多行故障已经历一次修复后重试仍失败，既有恢复额度已使用；本轮不再启动相同 hosted 多行重试，也没有把单行/本地 fake 成功或删除失败分支当作修复。

## 离线诊断（事实与假设分开）

事实：

- 5ea8e44 的 hosted 日志中，多行两次动作均出现 `PASTE_INIT reading=0` 与 `PASTE_FINISH status=0`（读取成功），但没有 child 的 `L1`/`L2` 字节，最终 notice 仍为 cancelled。
- 当时测试 helper `click_paste_dialog` 在找不到文字按钮时直接 `box->reject()`，且没有记录点击结果；因此无法证明对话框被接受，也无法证明产品 transfer 一定启动。
- 本地 fake reader 的多行取消/确认通过，但 product reader 是真实 CF、目标是真实 child，不能据此消除 hosted 失败。

假设（未证明，不得当结论）：

1. 测试 helper 未命中 `Paste` 按钮而走 reject，导致“确认”实际是取消；这是夹具候选原因。
2. 对话框接受后，焦点/状态使 `finish_paste_read` 未启动 transfer；这是产品候选原因。
3. 真实 CF 内容/生命周期与 fake reader 不同导致读取后未进入确认；需要插桩定位。

## 最小修复方案（待授权的一次 hosted 重试）

- 恢复 hosted 多行分支，但先给测试 helper 记录 `clicked button text/role/exec result`，并在产品 `finish_paste_read`/transfer 启动处记录状态与 target PID；不删除断言、不改为直接业务调用。
- 若记录显示按钮未命中，修 helper 按 AcceptRole/RejectRole 选择按钮并重跑；若产品未启动 transfer，按目标绑定合同修最小路径。此修复只允许一次 hosted 重试，并记录唯一 run/job 与结果。
- 在获得该一次恢复额度或明确环境阻塞决定前，不声明多行 hosted 通过；本 PR 仅保留单行三入口 + 占用/释放通过，多行保持历史失败/未闭合。
