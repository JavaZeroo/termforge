# More 捕获端点与 ECH 合同（#42）

## 原因与实现边界

默认核心原先没有 `CSI X` 分派，Recovery 消费但不执行 ECH。366 字节输入的末尾 CR 将列号置为 0，随后 `CSI 79 X` 被忽略，因而留下 `) --`。这与捕获器的另一个问题分开：旧代码在仅看到 `TF_PAGE_1` 时就发送 q，且在进程退出后、reader 结束前保存快照。进程退出不能证明输出已经排干。

默认核心现在选择以下最小合同，strict 与 Recovery 共用分派：

- `CSI n X` 从当前列填充 n 个空格，省略或 0 为 1；仅当前物理行，裁剪到右边界，不移动光标或滚动。
- 清除 pending wrap。固定 Microsoft Terminal [`7ff11ee1` 的 `AdaptDispatch::EraseCharacters`](https://github.com/microsoft/terminal/blob/7ff11ee107c984e236960ec948b450ccbc10187b/src/terminal/adapter/adaptDispatch.cpp#L731-L753)明确调用 `ResetDelayEOLWrap()`。[Microsoft VT 表](https://learn.microsoft.com/en-us/windows/console/console-virtual-terminal-sequences#text-modification)与 [xterm ECH 说明](https://invisible-island.net/xterm/ctlseqs/ctlseqs.html)提供擦除及默认计数的依据。这不是 WindTerm 运行行为已经验证的声明。
- 沿用项目既有屏模型：擦除使用当前样式，触及宽字任一半时清除整个旧字符，断开组合标记附着；擦除到行尾时清除旧 continuation 元数据。宽字和全样式政策不声称与所有参考等价。

保留一个真实候选差异：2×4 输入 `ABCD ESC [ X Z`，默认核心得到首行 `ABCZ`，termforge.3 候选得到首行 `ABC `、次行 `Z`。366 字节实例在 ECH 前有 CR，不涉及此分歧。本项没有把它登记为自动 `reference_difference`，固定 fuzz 也未覆盖该输入；完整模式与核心整合仍归 #27。

## 完整原始回归输入

两个 fixture 来自 40 行 `TF_PAGE_1`…`TF_PAGE_40` 的真实 more.com ConPTY 输出，只含合成文字、提示与控制序列，未截尾或重建。

| fixture | 字节数 | SHA-256 |
|---|---:|---|
| [more-pager-360.bin](../../experiments/core/fixtures/more-pager-360.bin) | 360 | `d035d53385788d2b68dcc0d866b5a2fe68897cdcb28a761bd4d36efcc6df6cf9` |
| [more-pager-366.bin](../../experiments/core/fixtures/more-pager-366.bin) | 366 | `bb9dbe77353245a2aad7ef07e12da9a8371cc7ac56d6b47ebc0bfecee35b9958` |

来源为既有 #42 [margin 记录](../../experiments/shell/results/2026-10-26-margin-state/validation.json)所指的捕获及相同哈希的历史 360 字节捕获。原始字节继续保留，不把旧记录补写为 EOF 已验证。3×12 独立断言保留第二行 `-- More (56%`；360 字节第三行为 `) --`，366 字节第三行清空。整段、逐字节与不均匀拆包分别核对，不删除 ECH 来制造一致。

## 真实应用捕获合同

Python 监督器先将原生宿主加入自己的 Job，再发送 GO；原生入口还要求自身已在 Job 中。More 必须输出完整的英文 `-- More (<百分比>%) -- ` 提示，之后才发送一个 q，不使用固定延时替代同步。此实验目前绑定英文提示，其他语言尚未验收。

q 写完后要求退出码 0，独立线程经 `ClosePseudoConsole()` 后继续读取并 join。成功还必须有真实 `ReadFile` 终点 `ERROR_BROKEN_PIPE=109`、无强制取消、无捕获溢出及内外 Job 归零。`close()` 返回或 `cleanup_complete` 单独为真不能替代 EOF。[Microsoft 关闭文档](https://learn.microsoft.com/en-us/windows/console/closepseudoconsole)要求关闭后继续读取，[匿名管道文档](https://learn.microsoft.com/en-us/windows/win32/ipc/anonymous-pipe-operations)说明读端与写端关闭的关系。

`pager-lifecycle.json` 保存完整提示末偏移、实际 q 字节数、退出码、reader 终点、最终长度和取消状态；Python 独立检查原始提示及这些条件。提示等待期间、关闭前和失败路径先保留实际字节，再进行可能阻塞的关闭；60 秒外层监督只清理自己的 Job。超时留下的前缀标为未完成，不伪造 EOF。

新鲜完整样本为 382 字节：提示结束偏移 360，后续 CR、模式关闭和 ECH 全部保留。合法长度和重绘可能变化，验收不锁定长度或改写尾部。三应用比较通过仅代表这些核心轨迹，不代表 GUI、物理呈现或完整 VT 验收。修复前后、同输入候选/pyte 对照和端点证据见[审阅摘要](../../experiments/differential/results/2026-10-26-more-ech/validation.json)。

## 复验入口

```powershell
cmake --build build --config Release --target recovery_probe terminal_trace real_app_capture
python -m unittest experiments.core.test_recovery -v
python -m unittest discover -s experiments/differential -p test_real_app_matrix.py -v
```

真实应用测试要求构建固定候选且 PATH 中可找到 Vim；可选依赖缺失导致的 skip 不代替运行证据。新增断言覆盖默认/0 计数、裁剪、光标、pending wrap、宽字政策，完整历史字节、伪造 EOF/部分提示、GO 门禁及监督器故障；共同矩阵和原 fuzz 裁决保持不变。
