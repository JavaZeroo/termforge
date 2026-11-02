# 中间列 CUP(8,31) 的独立 EL0 / EL2 观察协议

本协议由 [运行工具](resize_erase_interior_reference.py)、[控制器](resize_erase_interior_reference.js)及[严格导出器](export_resize_erase_interior_reference.py)实现，限于 #27/#29 的一个剩余歧义：此前在新末列 61 擦除后得到的文档长度 60，究竟随光标前缀移动，还是固定于宽度减一或其他正文策略。它定义新观察，不是原版结果或生产实现验收。

[末列擦除证据](results/2026-11-02-resize-erase-reference/README.md)的两次原失败与两次离线补充成功保持原样。新协议不修改旧源码、不复用旧计划、不重放旧录制；每个新 case 使用独立 fresh 原版进程和一次性计划。

## 固定输入与五阶段

沿用已明确身份的 **85 B** 正文：在原 78 B 的唯一 `DECAWM OFF + CUP(8,91)` 前插入 7 B 的 `CUP(8,1), L`（hex `1b5b383b31484c`）。L 只在初始 91 列时写入。正文 SHA-256 保持 `a301c656926ae72c07d2c91b67e1734286ce50b7cb38c5b4f6b64f06db0563b3`；新 plan 和结果仍分别记录 78 B、插入段和 85 B 的字节数、hex 与 SHA，不把它称为原样 78 B。

唯一行为变量是阶段 2：从旧 `CUP(8,61)` 改为 **`CUP(8,31)`**（hex `1b5b383b333148`），不打印字符。两 case 名称仍为 `erase-right` 和 `erase-whole-line`，但所属模块、执行模板、source manifest、plan/schema、attempt、输出文件与 provider scenario 均为独立 `resize-erase-interior` 身份。

| 阶段 | 窗口 / 必须观察的网格 | 输入本体 | 观察目的 |
|---|---|---|---|
| 0 | 1360×749 / 91×20 | 固定 85 B 正文 | 建立 L、v、w、x 基线 |
| 1 | 1000×749 / 61×20 | 空 | 只缩窄，先采自然 DSR 和文档 |
| 2 | 1000×749 / 61×20 | `CUP(8,31)` | 单独采中间列定位后的正文和 DSR |
| 3 | 1000×749 / 61×20 | EL0 `1b5b304b` 或 EL2 `1b5b324b` | 分别采 L/v 留删、完整有限正文、marker 与 DSR |
| 4 | 1360×749 / 91×20 | 空 | 恢复宽度，只观察，不重画 |

每阶段统一追加一次 `ESC[6n`（4 B），上表不包含这部分。阶段 2 的观察先保存，再检查 **DSR 为 `8;31` 且完整文档逐行等于阶段 1**，通过才写 ack 并放行 EL。旧 `8;61` 不能当成有效准备。该控制不预设 L/v 必须在缩窄后仍存在；若 CUP 改变正文或 DSR 不符，保留含第三阶段的失败原件，后续 EL 不发送。

## 可区分的有限结果

DSR 使用一基协议列，文档列与前缀长度从零开始。在 `CUP(8,31)` 后，光标前缀长度为 30；新宽度减一为 60，当前完整宽度为 61，旧宽度为 91。这些是候选解释，不是期望通过答案。

阶段 3/4 在旧有有限源字符串、固定 61/91 边界片段、连续区间填空格及尾空白形式之外，只增加这些源派生结果的固定前缀长度 **`0 / 30 / 60 / 61 / 91`**。计划显式绑定 `preparation_column=31` 和 `document_candidate_prefix_lengths`；输出也保存后者。阶段 0..2 不增加这些擦除后候选。

例如，两 case 都允许记录 `L + 29 空格`、`30 空格`、`L + 59 空格`、`60 空格`、61/91 列固定源派生及空串，也保留原有限集合里的 L/v 皆留、只留 L、只留 v 或皆无。不因结果不符合“光标前缀”猜测而拒绝；也不允许任意长度 padding、任意新文本、重复 marker 或未发送的字母。源文字、marker 和空白范围必须来自同一固定输入。

记录的是完整有限合成文档数组摘要、marker 的文档行/列/position、矩形和独立 DSR；不能用 marker 缺失代替空白长度，也不能把文档长度直接当成 active grid 宽度或原版 Cell 类型。即使本组区分了 30 与 60，仍不证明其他光标列、其他尺寸、Unicode、样式或完整擦除算法。

## 隔离、身份与停止条件

两 case 各最多一次启动，合计最多 **2 个原版进程、10 阶段**。只允许公开 QWidget 的 `1360×749 → 1000×749 → 1360×749`；NAWS 必须为 `91×20 → 61×20 → 91×20`。只有阶段 1/4 调整窗口，前一阶段已观察并 ack、peer 已 armed 后才 resize。NAWS 连续稳定 600 ms，文档/marker/DSR 快照稳定 400 ms。

继续固定 WindTerm 2.7.0、REA 6.7.0、完整 4047 文件原包与实际模块、Python/Node 身份、合成 profile、受控 station/Job；使用真实 `reference-source.json`，不 mock Git。运行源是本协议两份新文件加既有八份依赖，共十项 `SOURCE_FILES`；导出另绑定三个 `EXPORT_SOURCES`。旧 `resize_erase_reference.py/.js` 不成为新执行入口。

source/case/schema/phase 不符、旧列 61 准备、窗口/NAWS/行数不符、未 armed 的尺寸变化、过期握手、缺 ack、重复/额外回复、非有限正文或限界失败都停止并保留原件和 `.attempted`。有限集合遗漏也不能为取得成功结果而重跑；必要的离线补充须另有来源身份并保留原失败。

内部期限保持 41/42/43 秒，REA 45 秒、CLI 等待 60 秒；不扩大期限取绿。这些不等于整个复制、启动与清理生命周期上限。DSR 的安全数值上限独立为行 1..200、列 1..500，不用 61 列视图代替尚未证明的协议约束。受控终止后 Job 空不等于自然 GUI 退出。

公开输出只有合成输入、DSR、marker/矩形/网格标量、正文 SHA、来源和原件摘要；原文全文、路径、PID、会话、profile、资源及 provider 诊断留在私有录制目录。未知结果不自动通过语义比较，也不构成正式 Qt/固定 ConPTY、性能或 R2 验收。

## 离线验证和执行入口

```powershell
python -B -X utf8 -m unittest research.ui.test_resize_erase_interior_reference research.ui.test_export_resize_erase_interior_reference -v
python -B -O -X utf8 -m unittest research.ui.test_resize_erase_interior_reference research.ui.test_export_resize_erase_interior_reference -v
node research/ui/test_resize_erase_interior_stages.js research/ui/resize_erase_interior_reference.js
```

Python 使用受控内存连接执行真实 peer，并校验 archive/完整合成包/来源/计划/导出边界；Node 执行实际控制器源码，验证准备阶段、armed resize、稳定窗口与 ack 顺序。均不启动原版、连接网络或操作原生 UI。不同正文终点可完整导出，旧列 61、旧 plan schema、错误来源、private/超界/重复 marker 与未完成录制均有拒绝例。合成包夹具仅在测试命名空间使用五个无可执行内容的模块；生产仍要求 4047 文件。

实际执行使用 `python -m research.ui.resize_erase_interior_reference --prepare --case <case> --source <固定原包> --out <新的local目录> --rea <固定REA入口> --plan <新的local计划> --authorize-execution` 创建新计划，随后执行只传 `--plan`。严格导出使用 `research.ui.export_resize_erase_interior_reference`，仍需外部提供该新计划 SHA。准备和离线测试不计原版成功次数；实际运行由执行方独占环境协调。
