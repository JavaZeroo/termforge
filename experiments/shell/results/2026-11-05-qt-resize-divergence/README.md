# 正式 Qt + 固定 ConPTY：旧六阶段 B/CD 首次可证实分歧（同次 SessionModel 会话）

旧六阶段 `--resize` 断言仍以 exit 1 失败，失败信息保持：`stage 6 failed: core resize is not synchronized with the literal native B/CD rows`。本记录不改写该失败，也不把原生网格当作产品应同步的目标；它补上此前缺少的**同一次 SessionModel 会话**内逐阶段对应：child 尺寸 ACK、transport 实际消费字节/偏移、原生网格、核心源正文/协议光标、显示投影与 Qt 消费页。

旧报告已有一个独立 `tf_conpty::Session` 的运输诊断（265 B，resize 边界 offset 217），但那不是 Qt SessionModel 会话。本切片在 `SessionModel` 增加**默认关闭、上限 64 KiB 的诊断捕获**，仅显式 `--resize` 诊断启用；捕获不改变解析、resize 或运输行为。旧 `--resize` 断言、退出码和字面比较都保留。

## 身份与配置

- 基线 main `8b9c977634ab0b5e301dbcc4d923710ebd864d6b`（已含 #316）；本 PR 源码提交见 [validation.json](validation.json)。
- Windows 11 x64 build 26200、Visual Studio 17 2022 17.12.6、MSVC 19.42.34433.0、Qt 6.10.3 offscreen、固定 Microsoft ConPTY 1.25.260930003、app-local CRT 14.51.36247.0；自有 Job、25 s 监督期限、固定后端身份守卫不变。
- 源码/二进制/sanitized trace 哈希见 validation.json；trace 只含合成输入、行文本 hex、几何/光标/偏移标量。

## 方法

显式 `--resize` 仍执行原六阶段：公开 Qt 键触发 child 写源，child 通过独立命名管道返回自己的 `CONSOLE_SCREEN_BUFFER_INFO` 与选中原生行；`record()` 额外输出该阶段：

- 本阶段 transport 实际消费的新字节（hex）与其起始 offset；
- 原生/核心尺寸与光标、选中行；
- Qt 实际消费页（`painted_page` rows、offset、display caret）与核心 source rows。

阶段 6 在公开 Qt `resize()` 的 transport 完成后、发送第六阶段键之前，额外记录一次 `post-resize-pre-stage6` 快照（核心/显示/transport offset；此时没有 native ACK，不能伪造原生网格）。

复现命令（同一条显式诊断，旧失败仍会 exit 1）：

```powershell
python src/qt/run_smoke.py build/qt-cpp/qt-modes-bin/Release/qt_terminal_modes_test.exe --resize
```

## 同次会话逐阶段结果

| 阶段 | transport 新字节（offset） | 原生 | 核心 | 断言行差异 |
|---|---|---|---|---|
| 1 | 60 B @ 0 | 80×24，cursor `[0,2]` | 80×24，cursor `[0,2]` | 无 |
| 2 | 51 B @ 60 | 80×24，alt | 80×24，alt，cursor `[0,2]` | 无 |
| 3 | 25 B @ 111 | 80×24，alt | 80×24，alt | 无 |
| 4 | 17 B @ 136 | 80×24，主屏 | 80×24，主屏 | 无 |
| 5 | 41 B @ 153 | 80×24 | 80×24 | 无 |
| 6 | **26 B @ 194** | **60×12**，cursor `[0,2]` | **60×12**，cursor `[0,2]` | **row 7/8/9/10/11 全部不同** |

阶段 1–5 的旧断言行均无差异；**第一次失败出现在阶段 6**。

### 阶段 6 前后的关键状态

- `post-resize-pre-stage6`（transport 已完成、尚未送入 S6）：核心 60×12、cursor `[0,2]`，源行仍保留 `B`（row7 col79）、`C`（row9 col79）、`D`（row10 col0）；Qt 显示页已重排为 row8=`19 空格+B`、row11=`19 空格+CD`。**此时没有原生网格观测**（child 的阶段 6 ACK 只会在写完 S6 后返回）。
- 阶段 6 transport offset `194..220` 共 **26 B**：`CUP 12;1H + RESIZED + CUP 1;1H + S6`（22 B）+ `ESC[6n`（4 B）。没有 `B/C/D` 搬移、擦除或整屏重绘字节；原生 reflow 是 ConPTY 内部行为，不经 transport 下发。
- 阶段 6 native ACK：60×12，row7/9/10 为空，row8=`19 空格+B`，row11=`RESIZED`+12 空格+`CD`，row12 为空；核心仍保留 row7 B/row9 C/row10 D，且 CUP 12;1 落在保留源 row11（显示 row12）为 `RESIZED`；Qt 显示 row8=`19 空格+B`、row11=`19 空格+CD`、row12=`RESIZED`。
- 原生与核心 cursor 均为 `[0,2]`；DSR 查询没有造成可观测光标差异（child 查询本身只回 DSR，不移动光标）。

## 首次可证实的分歧与解释边界

1. **阶段 6 是第一次可证实的失败点**：阶段 1–5 的原生/核心断言行相同。阶段 6 的差异不是“尺寸未完成”：原生与核心都已是 60×12，child ACK 也是 60×12。
2. 在阶段 6，原生 row8 与 Qt 显示 row8 相同（都是 `19 空格+B`）；差异集中在：
   - 原生 row7/9/10 已被 reflow 清空/平移，核心仍保留 B/C/D 源行（保留合同）；
   - 原生 row11=`RESIZED`+12 空格+`CD`，Qt 显示 row11=`19 空格+CD`、row12=`RESIZED`；这是 child 的 `CUP 12;1` 在原生 reflow 网格与保留源坐标层的不同落点。
3. **没有发现搬移/擦除字节**：resize 后 transport 只有阶段 6 源和 DSR 查询；因此旧失败不能归因为“parser 忽略了 ConPTY 下发重排数据”。它是原生网格 reflow 与保留源/显示坐标层的差异。
4. 未采到的中间态：resize 完成到 child 阶段 6 ACK 之间没有原生网格快照；`B/C/D` 具体何时在原生侧移动/清空、原生 reflow 的完整内部序列仍未知。不能用最终 ACK 反推该中间态。
5. 本记录不改变产品策略，不裁定应接受新的 native-grid 差异；不能用 #316 的 WindTerm 写后端点作为本会话的 oracle。旧 `--resize` 仍 exit 1；正常/retained Qt 入口仍分别通过。

## 验证

- 同次 trace 运行：`--resize` 退出 1，旧断言原文保留；trace 见 [trace.json](trace.json)（SHA256 `e1663ac0…`）。
- 正常 `qt_terminal_modes` 1/1、`qt_terminal_modes_retained` 1/1 通过（同一构建）。
- 本诊断只增加默认关闭的 `SessionModel` 捕获；未改解析、resize、显示或运输行为。
- 新 head CI 见 PR。

## 边界

单显示器 offscreen、固定 80×24→60×12、单次会话；不覆盖物理呈现、DPI、历史/Find/剪贴板、其他尺寸/顺序、Unicode、原生完整网格/样式或性能。本记录不关闭 #27/#29，也不声明 WindTerm 等价；旧失败与旧报告身份保留。
