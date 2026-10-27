# WindTerm 2.7.0 resize：文本保留与视图回带

这是 #29 的原版运行观察，与候选修复及产品验收分开。固定官方 Windows x64 EXE SHA-256 为 `7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0`；Windows 11 Pro 10.0.26200，自有非输入窗口站/desktop、全新合成 profile、loopback Telnet peer。按事先冻结计划运行两次，每次 REA 观察期限 45 秒，两次均完成后停止，没有修改探针后反复取绿。

## 观察

输入为 1091 字节、42 条已知逻辑文本，包含唯一编号历史、185 字符软长行、两条各 77 字符的 CRLF 硬行、中文/组合字符/ZWJ 和尾部标记。[合成输入](../../experiments/core/results/2026-10-27-resize-snapshot/windterm-input.json)记录精确逻辑文本；[事实与哈希](../../experiments/core/results/2026-10-27-resize-snapshot/windterm-observations.json)记录两次结果及私有原件身份。

| 阶段 | 请求外框 | peer 实收 NAWS | scrollTop | screenLineCapacity | visibleLineCount | H0019 起点 y |
|---|---|---|---|---|---|---|
| 稳定基线 | 1230×887 | 80×26 | 19 | 26 | 44 | 0 |
| 缩宽 | 1010×887 | 62×26 | 20 | 26 | 46 | -18 |
| 恢复宽度 | 1230×887 | 80×26 | 19 | 26 | 44 | 0 |
| 缩高 | 1230×679 | 80×18 | 26 | 18 | 44 | -126 |
| 恢复高度 | 1230×887 | 80×26 | 19 | 26 | 44 | 0 |

两次的五阶段摘要一致。最初 NAWS 是 80×25，载荷按该尺寸生成，稳定后才变为 80×26；不把外框请求视为实际网格。在每阶段，`activeText.lineText(0..41)` 均能读到全部已知输入，`lineStart/lineEnd` 不变。恢复宽度、恢复高度后，采集到的整个文本投影精确回到基线。

软长行保持一个 `lineText` 条目，CRLF 硬行保持各自条目。增高后 `scrollTop` 从 26 回到 19，H0019 起点 y 从 -126 回到 0，支持**该文本视图接口重新纳入较早历史**的观察。不能再用单一静态分支推断“原版增高绝不回带历史”；原有屏幕分配路径与 Document/文本视图之间的动态关系仍待调查。

## 证据边界

- `visibleLineCount` 的 44/46 与已知输入的 42 个 `lineText` 条目粒度不同；额外索引返回空串并别名到末尾位置，不能据此计算物理格或额外空行。
- `topLine` 为 undefined；selection caret/anchor 的文档位置不是物理光标行列。没有验证选区保留、复制或 Find 的用户操作。
- 只证明固定输入与五个尺寸的文本保留/投影恢复；完整 reflow、物理格、Unicode 宽度、清空历史、所有配置和序列仍未知。
- NAWS 来自独立 Telnet peer，不是 ConPTY child ack。实际呈现、本地 shell、性能与产品验收均未执行；250 ms 稳定采样间隔不构成性能门槛。
- 两次核对 4047 文件分发 manifest、EXE/脚本/载荷 SHA；Job 最终 active=0，desktop/station 句柄已关闭，仅有一个合成会话。没有操作个人剪贴板、DPI 或输入桌面。完整原件、app 和 profile 留在本地，公开事实不含专有实现或个人数据。

## 对实现的约束

现有默认物理裁剪及候选缓存只描述本项目实现。原版上述文本保留事实要求继续调查当前跨宽度历史的表示和显示，不能用永久裁掉缓存文本来消除行宽不一致。修正候选 pending 快照与显式清空属于[独立不变量修复](../../experiments/core/results/2026-10-27-resize-snapshot/README.md)，不等于本次原版观察已经验证这些候选行为。
