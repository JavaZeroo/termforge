# 真实终端采样开/关诊断

> 2026-10-22：该手工流程不再是默认路线。用户已结束当前批次（5/12 步），无需补跑。旧工具与记录保留供可选诊断；后续执行方式遵循 [ADR-0002](../../docs/adr/0002-automation-first-investigation.md)。以下为原操作说明，不能据此再次要求用户逐轮操作。

`terminal_calibration.py` 在用户当前的本地终端会话中运行一个步骤。2 次预热后执行 5 对交错关闭/开启采样，共 12 步；每步都是新的 Shell 会话。它不通过 UI 输入命令或创建标签页。

## 配置边界

2026-10-21 核对官方 2.7.0 发行包的配置字面值：Roboto Mono 10、回滚 999999 行、初始 80×24、自动调整行列为 true、自动补全为 true；隔离用户配置没有这些键的覆盖。文件值不能直接证明活动会话的有效设置，字体回退、屏幕缩放及会话覆盖仍未验证。

这不同于协议 C0（Cascadia Mono 12、回滚 10000 行、120×40、关闭自动补全）。因此当前入口标记 `D0-diagnostic-not-C0`，只能诊断当前配置下采样对生产时间的影响，不是 C0 基线。不得在批次中改变设置或调整窗口大小；每次记录继承控制台报告的实际尺寸，未知或不一致会阻止汇总比较。

## 操作

在同一个官方隔离 WindTerm 进程中，为每步新建一个本地 Cmd 或 PowerShell 会话，进入仓库根目录运行同一命令：

```powershell
.venv\Scripts\python benchmarks/tools/terminal_calibration.py benchmarks/generated/ascii-8m --app D:\Code\termforge\local\windterm-official\WindTerm_2.7.0\WindTerm.exe --batch benchmarks/local-results/windterm-t01-diagnostic-01
```

当前工作区已准备快捷入口 `D:\Code\termforge\local\run-windterm-calibration.cmd`。只在隔离实例里执行；第一步也请新建测试会话，不复用烟测标签页。

1. 等待输出结束，检查 **Producer status 上方负载输出中** 的结束标记 `TERMFORGE-END ascii 0000108943`。提示区也会打印预期文本，不要把提示文本当作负载成功的证据。
2. 在 `Payload marker visible? [y/N]:` 后，确认看见负载标记则输入 `y` 回车，否则输入 `n` 并停止检查。
3. 关闭本次测试标签页，再新建本地会话执行同一命令。程序自动选择下一步；确认上一 Shell 退出后等待至少 30 秒，再播放下一个负载。保持 WindTerm 应用本身运行，不改变设置、窗口尺寸或开展其他任务。
4. 完成第 12 步后检查 `comparison.json`。原始 `step-XX` 目录、预热、失败和人工标记确认均保留。

这段等待只保证最短间隔，不自动证明 CPU/内存已恢复稳定；`stable_idle_verified` 保持 false。与正式协议不同，等待发生于新会话内，未提供关闭到稳定的完整时间序列。因此即使诊断完成，也不能视为正式校准门槛通过。

入口固定应用文件、应用进程、负载及相关工具的身份；进程重启或代码变化必须另建批次。最外层 Shell 身份用于区分会话，避免 PowerShell 启动的短命 cmd 包装器冒充新会话。无法只凭进程证明其历史从未使用过，首次新建会话仍依靠操作约定。

异常中断会保留 pending step；失败或未确认的标记会阻止继续。不要删除失败记录、锁或改写步骤来让结果通过；检查后用新批次重做。并发启动由独占锁拒绝。

## 汇总语义

只有 12 步完整、顺序一致、生产成功、全部标记人工确认、控制台尺寸一致时才生成生产时间的开/关中位数比较。预热不计入比较；每组 5 个原始值全部保留。

`median_elapsed_change_percent = (median_on / median_off - 1) * 100`，绝对值超过 5% 标记触发检查，包括明显负差异。无论是否触发，`performance_gate` 都是 `not_evaluated`：未测捕获开销、完整正确性、可见完成时间，也未验证 C0 或稳定空闲。

当前仅工具与统计逻辑通过测试；真实批次尚未运行。
