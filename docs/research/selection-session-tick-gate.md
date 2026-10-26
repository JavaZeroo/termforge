# 选择字节场景的双会话阶段门槛

`ui-selection-bytes` 与仅限一次性 hosted VM 的 `ui-copy-bytes-system` 共用九阶段场景。独立 Python oracle 要求每次相邻观察之间，bottom 和 main 各自至少推进三个 timer tick；这验证两个真实会话都获得执行机会，不是实际上屏端点。

[#66](https://github.com/JavaZeroo/termforge/issues/66) 的[原 Windows CI](https://github.com/JavaZeroo/termforge/actions/runs/40306319630/job/121516200920)在第二轮系统复制的 reverse 阶段失败。原件拆解为 bottom `14→16`，main `11→14`：只有 bottom 的两 tick 不满足三 tick 门槛。grid、DPI、capture 和完整重绘字段符合期望；native 进程正常退出，两会话已关闭，自有 Job 为零且 desktop 已删除。不能把复合错误字符串解释成绘制或剪贴板损坏，也不能把正常退出当作该轮验收成功。

旧场景只根据 main 计数推进。现在每个原有 `three_ticks` 等待点都使用 `TwoSessionTickGate`，在动作后的同一位置保存两会话独立基点，再分别要求至少三个 tick。没有增加 sleep、修改时限或降低 Python oracle。相同 gate 同时用于正逆拖选、受控 DPI、清除、空 Copy 和实际输入后的观察，因为这些阶段使用同一双会话合同。

共享 helper 的确定性反例直接使用原 CI 数值：`{14,11}→{16,14}` 必须等待；只有 bottom 再推进到 17 后才满足门槛。另验证主会话过慢、不同绝对计数、基点重置及计数倒退。真实数据模式继续由独立文本、运输、物理行、完整重绘及回收 oracle 验证；系统 CF 必须在真实 hosted VM 运行，不能用本地数据模式或伪造环境变量代替。

[固定源码与脱敏复核](../../experiments/shell/results/2026-10-26-selection-tick-gate/validation.json)保留原 artifact 身份、原失败和新数据模式结果。原始运输、完整屏幕、frame 与 desktop 信息留本地；最终 `frame.bmp` 不是 reverse 阶段截图。每阶段 `frame_matches_full` 也不证明实际呈现完成。

```powershell
ctest --test-dir build/r66 -C Release -R '^scenario_tick_gate$' --output-on-failure
$env:TERMFORGE_BIN_DIR = "$PWD/build/r66/bin/Release"
$env:PYTHONUTF8 = '1'
python -m unittest experiments.shell.test_selection_bytes -v
```

普通本地环境会明确跳过实际系统 CF，用例仍检查 native/Python 的 hosted 保护。完整系统门槛由 PR 的 Windows hosted CI 执行；本页和固定记录不借用其他核心/后端或历史成功。
