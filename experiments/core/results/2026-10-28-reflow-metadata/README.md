# #29：原生重排边界修复，源历史重复仍未解决

本地工作分支代码 `67ac9c12447884584098657f06742dcd3e0f3d59`，tree `476e25606ce93359c4d855726de93777e752139d`，基点为 PR #110 的 `b59badb135b6ab53dcc55cd5b1a4c3493273ae6d`。这是未完成工作的检查点，不是 #29 的完成交付；尚未发布或合入 main。候选版本为 `0.26.1-dev+termforge.7`，固定上游、注册表依赖与许可未变。

原生 resize 只搬移单元格时，硬断行仍留在旧前缀、后缀丢失硬边界，软行宽度也可能沿用旧宽。现在硬边界跟随最终片段，合并时从被吞入的 continuation 转移到结果行；内部软边界记录当前宽度。六个 Grid 用例先全部失败，修复后通过。未改变既有单元格文字和光标重排算法。

另一公开 C ABI 反例从 `2×8/history=1` 输入 `ABCDEFGH\r\nIJKLMNOP\r\nQRSTUVWX`，缩到 `2×4` 后只剩 `MNOP/QRST/UVWX`，但原生淘汰计数错误地保持 0。修复后计为 3；空 feed、相同尺寸和 ED3 不重复计数，合并物理容器不计为淘汰。该计数单位是候选的物理历史行，不是原版逻辑行容量政策。

## 验证范围

Windows 11 x64、Rust 1.92.0、MSVC Release 候选构建；[机器可读身份与结果](validation.json)保存源码、二进制、日志哈希和命令。

| 检查 | 结果与限制 |
|---|---|
| Rust workspace | 163 个 vendor 单元测试、9 个适配/程序测试、1 个 doc test 全部通过，包含真实 feed/resize 的 C ABI 边界与淘汰检查 |
| 重建 trace 后的候选 runner | 6/6；固定输入、字素与来源报告检查，不是性能或原版等价证明 |
| CMake `candidate_core` | **0/1，仍失败**：新增长历史整段复制测试发现重复文字；没有修改断言或跳过。先运行 whole-feed 分支即失败，bytewise 分支尚未执行 |
| 12 个公开接口案例 | 36 条 before/resize/repaint 记录；源历史重复、软链错误和空硬行丢失仍存在 |
| 代码审查 | Standards 的来源登记问题已以 `.7` 修正；Spec 未发现本原生修复切片的新增缺陷。两个 resize 完成点的短元数据规范化重复保留为可选建议 |

完整 CMake 产品矩阵尚未执行；已知核心回归为红，不能据 Rust 通过宣布候选正确性通过。没有实际 renderer、系统剪贴板、ConPTY child ack、实验官方后端或性能验证；此前版本的通过与历史失败不受此结果覆盖。

## 仍然失败的用户行为

`CandidateTerminal(2,8,16)`，输入以下文本，执行 `resize(4,4)`，随后输入 `ESC[?25l ESC[2J ESC[H C CRLF D ESC[?25h`（控制序列之间无表述用空格）。三个案例在 resize 后、repaint 前均保留完整文本；repaint 后：

| 初始输入 | 实际完整复制文本 |
|---|---|
| `ABCDEFGH\r\nB\r\nC\r\nD` | `ABCDEFGH\r\nABCDB\r\nC\r\nD\r\n\r\n` |
| `ABCDEFGHIJKL\r\nC\r\nD` | `ABCDEFGHABCDIJKL\r\nC\r\nD\r\n\r\n`；`GHIJ` 从一个 Find 命中变为零 |
| `\r\nB\r\nC\r\nD` | `B\r\nC\r\nD\r\n\r\n`，首个空硬行消失 |

末尾空行是剩余可见空行；整段复制回归明确止于重绘 D 的末尾，仍因前面的重复内容失败。混宽历史视口也仍失败，不能用裁剪文本解决。下一步需要明确旧历史与原生重排片段的来源和顺序，再统一显示、选择与 Find 的映射。

另行发布的[原版长历史逐字符观察](../../../../docs/research/windterm-history-layout.md)只支持固定 ASCII 长逻辑行随视图宽度重新分段；历史重新进入视图后再覆盖写入的行为仍需调查。它不确认候选缓存策略，也不替代完整 #29 或 R2 验收。
