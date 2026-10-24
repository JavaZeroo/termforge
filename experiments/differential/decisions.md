# 六类 pyte 已知差异裁决（#25）

## 证据层级与结论

输入、分块和检查点沿用 `cases.py`。pyte 0.8.2 是第一独立参考，`alacritty_terminal 0.26.0` 是第二独立参考；两者都不自动成为真值。2026-10-25 修复前的[候选共同矩阵](results/2026-10-25-candidate-common/summary.json)显示，第二参考在五类上与 TermForge 一致，在 `styled_scroll_blank` 上与 pyte 一致。规范未定义的细节明确标为本项目策略，不把多数实现相同写成标准事实，也不据此声称 WindTerm 行为已经验证。

| 场景 | 规范与第二参考 | 裁决 |
|---|---|---|
| `wide_at_right_margin` | [ECMA-48 第五版](https://ecma-international.org/wp-content/uploads/ECMA-48_5th_edition_june_1991.pdf) 6.1.6 说明行末隐式移动可 wrap 或 blocked，但未规定宽字如何拆分；Alacritty 与 TermForge 都在写入双格字符前换行 | 保留 TermForge。双格字符不能留下半格，pyte 的行末半格是参考差异，不是必须追随的行为 |
| `wide_lead_overwrite` | ECMA-48 6.1.3 允许 erased 与 SPACE 有区别，但不定义双格覆盖；Alacritty 与 TermForge 都清除对应尾格 | 保留 TermForge 的双格整体性，覆盖首格时同时清除尾格；pyte 残留尾格不是目标 |
| `wide_trailer_overwrite` | 同上，规范未完整定义双格覆盖；Alacritty 与 TermForge 都清除对应首格 | 保留 TermForge，不能留下半格旧字；pyte 保留原首格是参考差异 |
| `combining_at_pending_wrap` | [Unicode 15.0 第 7 章](https://www.unicode.org/versions/Unicode15.0.0/ch07.pdf)说明 combining mark 与前述 base 的关系；ECMA-48 8.3.54 GCC 也以单一组合图形符号为目标。Alacritty 与 TermForge 都把标记留在原基字格 | 保留 TermForge。待换行不是把组合序列拆到下一行的理由；完整字素/复杂文字不在本例扩展验收 |
| `vertical_move_at_pending_wrap` | ECMA-48 6.1.6 的 pending 状态来自字符后的隐式移动，8.3.22 CUU 是显式移动；Alacritty 与 TermForge 都在 CUU 后取消 pending | 保留 TermForge。显式位置已确定，下一字符不应再在旧行末触发隐式换行 |
| `styled_scroll_blank` | ECMA-48 8.3.147 只定义 SU 的移动和光标不受影响，没有指定新空白行样式；pyte 与 Alacritty 都使用默认前景，TermForge 原使用当前前景 | 项目决定统一采用“滚动新行使用默认样式”，修正自研实现。原行为是本项目实现错误，不是必须保留的参考差异；这不改变擦除使用当前样式的独立行为 |

## 修复与持续门槛

`Screen::newline` 在底部滚动时改用默认 `Cell` 创建新行，历史行仍保留写入时的样式。`styled_scroll_blank` 从已知差异清单移除，现在必须同时匹配 pyte 与 Alacritty；其余五项继续精确锁定原 pyte 差异，避免为迎合参考而改坏边界。

修复后两套矩阵均为 95 次、1011 检查点：pyte 为 70 次一致、25 次保留的已知差异；Alacritty 为 90 次一致、5 次待裁决差异。剩余差异全部属于 `erase_display` 的 ED/历史行为，不在上述六项裁决中自动放行，另由 #25 继续处理。

## `erase_display` 的 ED1/ED2 分离裁决

复合用例混有两个行为，因此另跑 `ed1_only` 与 `ed2_only`，公开[最小报告](results/2026-10-25-erase-display/)如下：

- **ED1 是候选实现错误。** ECMA-48 8.3.39 明确规定参数 1 将页首至当前位置（包含当前位置）置为 erased。pyte 与 TermForge 都清除这些位置，Alacritty 却保留前序行；保留 TermForge，不把候选行为当成规范差异。
- **ED2 的历史迁移是参考策略差异。** 同一节参数 2 只要求活动页全部置为 erased，不要求创建、迁移或清空终端历史；xterm 还把 saved lines 单独列为 ED3。TermForge 与 pyte 清屏但不新增历史，Alacritty 将三行可见内容迁入历史。保留 TermForge 当前“清屏但不新增/清空历史”的策略；候选策略可供 #26 适配成本分析，但不是必须照搬的目标。

`candidate-known-differences.json` 对复合 `erase_display` 的五种分块分别固定输入哈希、最早差异和双方最终状态哈希，状态改为 `reference_difference`。新输入、后续状态变化或未裁决差异仍为 `difference`，不能复用该裁决。当前矩阵因此为 90 次一致、5 次已裁决参考差异；`differences_adjudicated=true` 只覆盖这批现有差异，`semantic_acceptance=false` 仍表示它们不是兼容一致。

本裁决不涉及新增 VT 模式、真实 TUI、性能或 #26 核心路线决定。公开规范只支持上述具体边界，不证明所有终端或 WindTerm 都采用同一策略。
