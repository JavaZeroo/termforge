# 同 owner 保留列宽事务与有限编辑物化

本文记录 `a35be70` / `fdeaed6` 的固定切片和当时验证；后续原版样本进一步修正了[缩放后下一字符](../2026-11-02-resize-pending/README.md)与[隐藏主屏](../2026-11-02-resize-hidden/README.md)的处理。下述两帧保留、光标不变等早期工程合同不再代表当前所有帧策略。

本切片在现有 Screen 中增加 `resize_columns_preserving(columns)`：只改变协议列数，保留同一 owner 的源 Cell、行身份与软续接，再由[显式 ASCII 显示投影](../2026-11-02-display-projection/README.md)按新宽度显示。后续普通打印与 ICH/DCH 编辑会将目标行落实为当前协议宽度，并通过独立内容长度保留有意义的尾部空白。[四个原版寻址样本](../../../../research/ui/results/2026-11-02-resize-address-reference/README.md)的回放为 **64/64 阶段、896 条断言零失败**；Q/R、ICH、DCH 三个独立编辑样本的回放为 **64/64 阶段、1024 条断言零失败**。

**默认 `Screen::resize` 及 Qt 仍走原物理裁剪路径。** `--retained-columns` 仅是诊断程序的选择参数，不是用户 UI、产品后端或第二套内容存储开关。本切片没有接入正式 Qt，也没有完成通用 resize/reflow；[#27](https://github.com/JavaZeroo/termforge/issues/27) / [#29](https://github.com/JavaZeroo/termforge/issues/29) 保持开放。

## 源码与验证身份

- `a35be708a43336726a5a9400fe82977fecd01eff`：同 owner 宽度事务及其存储/寻址测试；该头的核心回归 **21/21，0.94 秒**，日志 `retained-core-ctest.log`。
- `fdeaed654d47d3f96ab7e7521142bc5bbeced000`：普通打印、ICH/DCH 的当前协议宽度物化，`Line.content_columns` 与 `Cell.written` 分离，以及文本/显示投影和编辑回放测试。

`fdeaed6` 的新源码已重新构建并通过核心 **22/22（0.98 秒）**及独立 Qt / 固定 ConPTY **4/4（8.82 秒）**。Qt 回归仍执行默认路径，不证明新增保留入口已接入 UI。新编辑与补充审计工具的 CTest 注册另有 **3/3（92.10 秒）**，包括 Python normal / `-O` 各 31 项和 JS 8 项；这些离线测试不启动原版。执行时间只作测试记录。

选定源码、二进制、配置、分轮结果及日志 SHA-256 见 [validation.json](validation.json)。哈希在运行后采集；未保存的中间 RED 二进制身份为 `null`。原日志含本地路径，仅发布摘要与哈希。旧头的 21/21 与更早的 19/19 不合并到这一轮。后续独立原版探针的源码与运行身份另列，不冒充这些 native 测试所调用的产品路径。

## 内容与协议边界

| 责任 | 本切片的行为 | 限制 |
|---|---|---|
| 单一内容 owner | 保留已有 Cell、ID/version、soft link、wrap 元数据、历史成员和顺序/epoch；没有复制一份平行正文或重新按文本匹配身份 | 保留状态是本次工程事务合同，不自动证明原版所有帧和编辑行为如此 |
| 缩窄 | 更新协议宽度，不截短源行，不分配新行存储 | 源列范围可长于当前协议宽度；缩窄本身与后续编辑分别验证 |
| 增宽 | 只为活动及已有隐藏帧中不足宽度的 live 行准备 padding；两帧准备完成后才发布，padding 不标为写入内容 | 不扫描、复制或补齐 history；分配失败的事务结构已在代码中分离准备/发布，本轮没有分配失败注入测试 |
| 打印、ICH/DCH | 目标行的源片段宽于协议宽度时，编辑前截至当前协议宽度；`Line.content_columns` 记录保留下来的内容长度，文本与显示投影据此保留尾部空白 | 原版匹配限下述 78B 源及右边界动作；EL0/EL2、其他位置/宽字/帧交叉未由此验收 |
| 空白语义 | 内容长度与 `Cell.written` 分离；ICH/DCH 后的 61 个空格不会被当作未使用 padding 丢弃，也不伪装成显式打印的空格 | 工程检查补充增长、再次编辑和非法 extent 拒绝，不等于完整擦除语义 |
| 协议状态 | 宽度事务自身不改光标、pending-wrap、origin/margin、当前样式和结合字符附着 | 不定义旧光标超出新宽度后的一般打印、编辑或换行策略，也不改变高度 |
| 有界性 | 只接受 1–500 列，源片段不因缩窄丢字，测试中的片段均不超过 500 Cell；既有行数与历史容量不变 | 不增加历史预算，不声明整个产品内存或性能预算已通过；指针不变检查不是分配器/性能测量 |

实现入口见 [screen.hpp](../../screen.hpp)、[文本投影](../../text_projection.hpp)与[显示投影](../../history_view.hpp)。默认 `resize(rows, columns)` 的物理裁剪合同未被此新增方法替换；后续接线仍须解决剩余擦除、帧、越界写入与运输合同。

## 原版字面回放与工程验证分开

[resize_address_test.cpp](../../resize_address_test.cpp) 保留同一份 78B 初始正文、四个动作及各阶段 DSR/文档 marker/显示 marker 的字面预期。变化只有尺寸事务调用：显式参数下调用新方法，未传参数仍调用原 `Screen::resize`。四个 case 为自然追加 `Q`、`CR Q`、`LF Q`、`CUP 11;2 H Q`；strict/recovery × 整块/逐字节组成 16 次回放，每次四阶段。没有把 16 次核心回放计为新增原版运行，也没有补造全文 grid、空白样式或像素预期。

[resize_edit_test.cpp](../../resize_edit_test.cpp) 使用同一 78B 源，分别回放 [Q/R](../../../../research/ui/results/2026-11-02-resize-edit-reference/right-edge-wrap.json)、[ICH](../../../../research/ui/results/2026-11-02-resize-edit-reference/insert-char.json)、[DCH](../../../../research/ui/results/2026-11-02-resize-edit-reference/delete-char.json) 三次独立原版运行的字面观察。原版阶段数为 `6+5+5=16`；strict/recovery × 整块/逐字节构成 12 次核心回放、**64 阶段**。ICH/DCH 结果相同不合并为一次原版运行。

每阶段分别检查实际协议宽度、DSR、逻辑文档坐标/全局字符位置和显示 marker 完整集合。Q 当次即删除旧尾部 `v`，`w/x` 的全局位置减少 30；随后 R 与 Q 软续接，`w/x` 的逻辑行从 9 变为 8。`R` 的集合包含 `ANCHOR` 中固有项及新增项。ICH/DCH 则删除 `v`、保持 `w/x` 的逻辑行 9，全局位置为 176/177；这些 offset 约束保留下来的 61 个空格，不能以清空整行获得通过。上述文档/显示坐标均从零开始，DSR 仍为协议的一基坐标。

| 验证 | 实际结果 | 来源与解释 |
|---|---|---|
| 旧默认裁剪诊断 | 历史运行 16/64 阶段通过，896 条断言中 308 条失败，退出 1 | [原失败报告](../2026-11-02-resize-address/README.md)与其冻结身份保持；这是默认路径仍未解决的差异，不能被新参数入口的成功覆盖 |
| 显式保留列宽诊断 | 64/64 阶段通过，896 条断言零失败 | `resize-address-retained.log`；同一组固定 marker/DSR 预期，没有放宽为前缀匹配 |
| 定向 CTest | **2/2，通过；总计 0.05 秒** | `retained_columns_core` 与 `retained_resize_address_core`，日志 `retained-columns-ctest.log`。执行时间不是性能测量 |
| 新增 owner 存储检查 | 两组工程场景通过 | [retained_columns_test.cpp](../../retained_columns_test.cpp) 的历史/边界及 Cell/帧检查；没有保存其先行行为 RED，不声明该新增测试完成过独立红绿过程 |
| Q/R 编辑回放 | RED 12/24 阶段、80/384 断言失败 → GREEN 24/24、0/384 | `resize-edit-edge-red.log` / `resize-edit-edge-green.log`；独立保留首次 Q 与后续 R 的观察 |
| Q/R + ICH + DCH 回放 | 追加 RED 48/64 阶段、80/1024 断言失败 → GREEN 64/64、0/1024 | `resize-edit-characters-red.log` / `resize-edit-characters-green.log`；包含前一行的 Q/R，不把两组阶段数相加为独立覆盖 |
| 新增 `blank_extent` 工程场景 | 所在 `retained_columns_core` 定向 CTest **1/1，0.03 秒** | `retained-extent-ctest.log`；没有该场景独立先行 RED，不宣称额外原版比较或独立红绿过程 |

工程检查覆盖 5 行历史在 `1→8→500→2→500` 列序列中的成员/源内容/身份保留、收缩 live 指针及 history 指针/长度不变、非法 `-1/0/501` 宽度拒绝；另核对样式/宽字/结合标记保存、缩窄后的结合标记附着、活动备用帧的 pending-wrap/origin/margin 状态以及隐藏主屏增宽 padding。该组没有为原版未知项建立 oracle，尤其“保存了 pending 状态”不代表 resize 后下一个可打印字符的位置已经匹配原版。

`blank_extent` 另检查编辑后恰有 61 个内容空格且没有误置 `written`、增宽不延长内容或改变版本、再次缩窄/打印后尾空白及身份/版本正确，以及拒绝超过源行长度的 extent。它只补充存储与投影不变量。

定向构建为 Windows x64、Visual Studio 17 2022、MSVC 19.42.34433.0、SDK 10.0.26100.0、Release、并行度 1、Qt OFF、候选适配器 OFF。此次没有 Qt/ConPTY 运输、物理显示或性能验收。

## 构建与回归入口

`resize_address_test` 与 `resize_edit_test` 参与默认构建；CTest 只以显式 `--retained-columns` 参数注册前者的保留入口，后者直接调用保留事务，另注册 owner 存储测试。无参数的默认裁剪诊断仍是独立未通过项，没有设为 `WILL_FAIL`，也没有通过移除其历史失败来豁免默认产品差异。

```powershell
cmake --build <core-build> --config Release --target retained_columns_test resize_address_test resize_edit_test --parallel 1
ctest --test-dir <core-build> -C Release -R '^(retained_columns_core|retained_resize_address_core|retained_resize_edit_core)$' --output-on-failure
& <core-build>/bin/Release/resize_address_test.exe --retained-columns
& <core-build>/bin/Release/resize_edit_test.exe
```

无参数运行 `resize_address_test.exe` 仍选择原裁剪调用，历史失败身份见旧报告；本页不将该历史二进制结果冒称新二进制的又一次运行。

Q/R 与 ICH/DCH 仅闭合固定来源的窄屏右边界样本。EL0/EL2、一般擦除/插删、主/备用屏 resize、旧光标超出新宽度后写入、高度变化与运输交错仍未验收。工程 Cell 保留不等于完整 Unicode 显示，显式 ASCII 投影仍拒绝宽字/marks；Qt 历史、选择、Find 与 B/CD 缩放诊断也未由本切片交付。完整实现与当前能力继续由 [FEATURES](../../../../docs/FEATURES.md)维护。
