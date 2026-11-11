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
| 恢复尝试 3（授权额度 1） | `b2536fe` | `43101420107` / `130793595825` | **通过**：`multiline_cancel=true`、`multiline_confirm=true`、`multiline_scope=hosted-real-cf`，sample 全项真；日志见下 |

结论：该多行故障已经历一次修复后重试仍失败，既有恢复额度已使用；本轮不再启动相同 hosted 多行重试，也没有把单行/本地 fake 成功或删除失败分支当作修复。恢复尝试 3 按根因修复（exec 返回值语义）在唯一一次授权 hosted run 中通过，故障闭合。

## 离线诊断（事实与假设分开）

事实：

- 5ea8e44 的 hosted 日志中，多行两次动作均出现 `PASTE_INIT reading=0` 与 `PASTE_FINISH status=0`（读取成功），但没有 child 的 `L1`/`L2` 字节，最终 notice 仍为 cancelled。
- 当时测试 helper `click_paste_dialog` 在找不到文字按钮时直接 `box->reject()`，且没有记录点击结果；因此无法证明对话框被接受，也无法证明产品 transfer 一定启动。
- 本地 fake reader 的多行取消/确认通过，但 product reader 是真实 CF、目标是真实 child，不能据此消除 hosted 失败。

假设（未证明，不得当结论）：

1. 测试 helper 未命中 `Paste` 按钮而走 reject，导致“确认”实际是取消；这是夹具候选原因。
2. 对话框接受后，焦点/状态使 `finish_paste_read` 未启动 transfer；这是产品候选原因。
3. 真实 CF 内容/生命周期与 fake reader 不同导致读取后未进入确认；需要插桩定位。

## 根因定位（尝试 3 恢复轮内离线完成，已证明）

事实（本地离屏复现，未触碰个人 CF/桌面）：

- 独立探针（[dialog_probe.cpp](dialog_probe.cpp)，offscreen，无剪贴板）构造与产品完全一致的 `QMessageBox`：点击 `Paste`（AcceptRole）后 `box.exec()` 返回 `2`，Escape/Cancel 返回 `3`；`QDialog::Accepted=1`、`Rejected=0`。即 `QMessageBox::exec()` 返回被点击按钮的标准按钮值，**不是** `QDialog::DialogCode`。
- 产品 `finish_paste_read` 原为 `confirmed = box.exec() == QDialog::Accepted`，对多行确认恒为假 → 对话框被真实接受也仍走 “Paste cancelled”、不启动 transfer。键鼠四种交互（含 `QPushButton::click()` 直连）全部复现，排除夹具点击/焦点/坐标因素。
- 修复：按 `box.clickedButton()` 的 `buttonRole() == AcceptRole` 判定；探针与本地 `qt_paste`（真实对话框、role 选择、无 auto-confirm）验证通过，取消 `accepted=0` 零字节、确认 `accepted=1` + transfer（`total=3`，即 `A\rB`）。
- 因此前两次 hosted 失败的产品侧解释确定：读取成功 → 对话框真实出现 → 用户确认被代码误判为拒绝。旧假设 1（夹具未命中）不是根因；旧假设 2/3 由该 bug 统一解释。

结论：根因是产品对 `QMessageBox::exec()` 返回值的语义误判；hosted 真实 CF 与 windows QPA 组合仍待唯一一次授权 run 实测闭合（见下）。


## 最小修复方案（待授权的一次 hosted 重试）

- 恢复 hosted 多行分支，但先给测试 helper 记录 `clicked button text/role/exec result`，并在产品 `finish_paste_read`/transfer 启动处记录状态与 target PID；不删除断言、不改为直接业务调用。
- 若记录显示按钮未命中，修 helper 按 AcceptRole/RejectRole 选择按钮并重跑；若产品未启动 transfer，按目标绑定合同修最小路径。此修复只允许一次 hosted 重试，并记录唯一 run/job 与结果。
- 在获得该一次恢复额度或明确环境阻塞决定前，不声明多行 hosted 通过；本 PR 仅保留单行三入口 + 占用/释放通过，多行保持历史失败/未闭合。

## 恢复尝试 3（预写计划，运行前授权额度 = 1，运行后回填）

授权与边界：

- 旧表内 `bc6a5af`/`5ea8e44` 两次修复/诊断重试保留计数（retryCount=2 不重置）；本次为第 3 次 hosted 多行执行，也是本轮唯一且最后一次授权额度。
- 唯一执行路径：opt-in workflow `qt-hosted-paste-multiline.yml`（push 限定本 PR 分支 + 路径过滤）；`qt-cpp.yml` 的默认 hosted paste 步骤通过测试内 `TERMFORGE_QT_HOSTED_MULTILINE` 门控保持历史单行范围，不执行多行。预期恰好 1 个 run / 1 个 job。
- 停止条件（有界，预写）：上述唯一 run 无论结论如何，首失败即停止；不重跑、不自动派生诊断 run、不以另一 CI 副本重复同一 hosted 失败路径。若基础设施在 paste 步骤之前失败，同样记为未通过并给出解除条件，不补跑。
- 入口合同：取消动作走真实公开入口 `Ctrl+Shift+V`；确认动作走真实公开入口 Paste 上下文菜单；默认产品 CF reader、真实 `QMessageBox`、真实独立 child 字节记录全部保留；不调用 `setPasteDialogAutoConfirmForTest`、不对产品做业务方法级伪造接受。
- 断言不弱化：取消后 child 字节 == 动作前基线（零增量）；确认后 child 字节 == 基线 + `L1\rL2`（CRLF 归一化为 CR，恰好一次）；占用失败/释放后第二次动作的最终字节增量不变；三入口中文/NFD、忙/取消/关闭通知分列不变。

插桩与判据（本次 run 的唯一证据来源）：

- helper 侧（测试夹具）：`dialog_helper: box=<activeModal|topLevelScan> buttons=N`；每个按钮 `button='<text>' role=<n>`；`click '<text>' via role|text`；`click sent`；未找到/未命中分别记录并 reject（保留原合同，但留下记录）。
- 产品侧（临时痕迹，本轮后移除）：`PASTE_FINISH status=%d`（与 5ea8e44 同名以便对照）、`PASTE_PREP confirmation_required=%d newlines=%zu target=%llu current=%llu`、`PASTE_CONFIRM accepted=%d`、`PASTE_TRANSFER state=%d total=%zu target=%llu`。
- 判别矩阵：
  - `dialog_helper: click ... via role` + `click sent` + `PASTE_CONFIRM accepted=1` + `PASTE_TRANSFER state=...` + child 字节正确 ⇒ 修复成功，hosted 多行通过。
  - `click via role` + `click sent` + `PASTE_CONFIRM accepted=0` ⇒ 点击到达但对话框仍拒绝（产品/平台侧候选），需新的离线证据，不重跑。
  - 无 `click sent` 且出现 `no QMessageBox after 250 polls` ⇒ 对话框未成为 activeModal（平台侧候选），需新的离线证据，不重跑。
  - `PASTE_CONFIRM accepted=1` + `PASTE_TRANSFER state=1`（sending）但 child 无字节 ⇒ 发送/传输侧候选，按目标绑定合同离线修，不重跑。
  - 任何 `no match ... rejecting` ⇒ 夹具选择失败（角色缺失），属于夹具 bug，记录后修复需新额度。

结果（运行后回填）：

- 唯一 run `43101420107` / 唯一 job `130793595825`（opt-in workflow、windows-2025 QPA、`TERMFORGE_QT_HOSTED_MULTILINE=1`）：**通过**，exit 0、`timed_out=false`、`job_after.active=0`（资源全回收）、sample `multiline_cancel=true`/`multiline_confirm=true`/`multiline_scope=hosted-real-cf"`，三入口与占用/释放全项真。
- 关键日志（21:16:08Z）：取消动作 `dialog_helper: box=activeModal buttons=2` → `button='Paste' role=0` / `button='Cancel' role=1` → `click 'Cancel' via role` → `click sent` → `PASTE_CONFIRM accepted=0`，child 字节 == 基线（零增量断言通过）。确认动作（真实 Paste 菜单入口）`click 'Paste' via role` → `click sent` → `PASTE_CONFIRM accepted=1` → `PASTE_TRANSFER state=0 total=5 target=2960`（`L1\rL2` 恰好 5 字节，CRLF→CR、恰好一次，wait_file 断言通过）；占用动作 `PASTE_FINISH status=2` → `Paste failed: OpenClipboard: 5`；释放后 `PST-OWN` 再成功（`total=7`）。
- 执行顺序核对：单次 push 仅触发该 workflow 一次；`qt-cpp.yml` 的 push/PR 双触发在本轮全部保持门控关闭（单行范围），未重复执行真实 hosted 多行路径。
- 判定矩阵落在第一行：修复成功，hosted 多行确认闭合。根因（exec 返回值语义误判）与修复与本次 hosted 实得一致。
- 运行 head 为 `b2536fe`（含临时 PASTE_* 痕迹）；结果回填后提交移除痕迹并把该 workflow 触发改为 workflow_dispatch-only，行为代码与运行 head 完全一致，差异仅诊断输出。

