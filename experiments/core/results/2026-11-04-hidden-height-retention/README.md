# 隐藏主屏纯高度变化的正文保留（工程合同）

`resize_preserving` 从 #308 起负责高度事务。隐藏帧在早先缩宽后可能保留宽于当前协议宽度的源行（例如原版 75 B 样本的 89 空格 + `uvw` 尾）；本切片修复一个会导致该正文永久丢失的实现缺陷。

**缺陷与影响。** 旧实现不论宽度是否变化，都把隐藏帧的 live 行与移入历史的行裁剪到当前协议宽度。当前正式 Qt 路径已使用 `resize_preserving`（#310），因此备用屏（TUI）活动期间只改变窗口高度，会裁掉隐藏主屏保留的右尾；返回主屏后正文、选择与 Find 源文本随之缺失。该行为与 ADR-0010 已确认的正文保留方向冲突。

**修复。** 隐藏帧只在 **宽度实际改变** 时适配到新协议足迹（沿用既有隐藏宽度证据策略）；纯高度变化保留隐藏 live 与追加历史的源 Cell，不重写其宽度。活动帧不受影响。

## 验证

| 入口 | 实际结果 | 口径 |
|---|---|---|
| RED：修复前 `resize_hidden_height_test` | `hidden/height-only` 失败：隐藏 live 行丢失保留尾；返回主屏正文丢失 | 固定 75 B 源；先缩宽保留 91 列源行，再隐藏主帧并 20→14→20 行 |
| GREEN：修复后同测试 | **3/3 场景通过**（纯高度保留、历史前缀保留、隐藏宽度变化仍适配足迹），退出 0 | 工程不变量，不是原版 oracle |
| 组合字面回放 `resize_combined_test` | **20/20 阶段、0/560 失败**；映射/选择 20/20、0/744 | 与 #308 相同字面预期，未放宽 |
| 事务分配失败/历史检查 `resize_preserving_transaction_test` | 通过（151/42 次注入失败后重试一致等） | 既有工程检查，未改断言 |
| 核心 CTest | **37/37 通过**（新增 `retained_resize_hidden_height_core`） | 本工作树定向构建 |
| Qt 正式路径 CTest | **5/5 通过**（含 `qt_terminal_modes_retained` 12/12 契约） | 真实 child → 固定 ConPTY → core → widget，offscreen |
| 隐藏高度探针离线 CTest | **3/3 通过，65.76 秒**（normal / `-O` / node controller） | 只校验冻结探针与导出器，不启动原版 |
| 旧六阶段 `--resize` 诊断 | **exit 1，失败保留** | 未弱化、未设 `WILL_FAIL`；原生重排网格与保留行坐标层差异不变 |

构建/运行环境：Windows 11 x64、Visual Studio 17 2022、MSVC 19.42.34433.0、Qt 6.10.3 offscreen、随包固定 ConPTY 1.25.260930003、app-local CRT、Release。源码/二进制 SHA-256、实现提交与逐项计数见 [validation.json](validation.json)。

```powershell
cmake --build build --config Release --target resize_hidden_height_test resize_combined_test resize_preserving_transaction_test
ctest --test-dir build -C Release -R "_core$" --output-on-failure
ctest --test-dir build/qt-cpp -C Release -R "^(qt_cpp_(shell|resize)|qt_unicode_input|qt_terminal_modes(_retained)?)$" --output-on-failure
```

## 边界

- 这是 ADR-0010 方向下的 **工程保留不变量**，不是原版隐藏主屏高度行为的对照通过。原版七阶段隐藏高度记录仍保留为严格审计失败；其离线补充审计草稿可运行，但归档记录缺少取证运行时的完整发行物与 profile，当前工作区无法完成严格导出的来源复核。未重跑、未改写原失败，也不据此分类原版隐藏高度策略。
- 纯高度变化后隐藏行可宽于当前协议宽度，与活动保留行同一表示；显示投影/编辑路径沿用既有有界处理。隐藏帧在宽度改变时仍按单独记录的隐藏宽度策略适配足迹。
- 未覆盖备用屏活动期间的后续输出、保存光标交叉、Unicode/宽字/结合字、历史淘汰上限、Qt 物理呈现/DPI、选择/Find 用户流程或性能。
- #27/#29 保持 OPEN；本切片不声明完整 resize/reflow、WindTerm 等价或 R2 完成。

**2026-11-04 后续：** 取证环境内完整录制恢复后，隐藏主屏高度七阶段记录的离线补充审计已完成，旧严格失败保留；审计显示返回主屏时按当前宽度裁剪隐藏行，与本报告的保留合同存在差异。见[补充审计](../../../../research/ui/results/2026-11-02-resize-hidden-height-reference/README.md)；本报告的工程验证与 #311 身份保持不变。
