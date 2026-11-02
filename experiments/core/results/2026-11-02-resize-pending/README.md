# 保留列宽 resize：末列之后的下一字符

提交 `9b678860f9388aae38d172aeda3d948b2552b2ad` 修正显式 `Screen::resize_columns_preserving` 的活动协议光标：实际列宽变化时，将 column 限制在新末列以内并清除延迟末列写入状态；增宽不额外前进一列。正文仍由原 owner 保留，显示由显式投影生成。四个原版字面案例的核心回放从 **44/72 阶段通过、108/720 断言失败**变为 **72/72、零失败**。

**默认 `Screen::resize` 和 Qt 尚未接入此入口。** 本报告是定向核心验证，不表示该头的整合回归、Qt/固定 ConPTY、完整帧语义或 #27/#29 已通过。前序保留列宽与编辑切片的身份见[原报告](../2026-11-02-retained-columns/README.md)，不把其旧头结果转用于本提交。

## 原版合同与独立回放

依据为[四个独立原版末列案例](../../../../research/ui/results/2026-11-02-resize-frame-reference/README.md)。每例固定 `91×20 → 61×20 → 91×20`，动作前后分别采集；测试调用真实保留列宽事务，再检查 `HistoryViewport::project`。测试没有额外补 CUP、重绘正文或修改预期来弥合失败，也没有将内部 `wrap_pending` 位作为原版 oracle。

| 原版案例 | 阶段 | DSR 序列（一基） | 文档与显示约束 |
|---|---:|---|---|
| `pending-on-shrink` | 4 | `8;91 → 8;61 → 8;61 → 8;61` | W 初始在文档 `(7,90)`，缩窄后仍保留、显示到 `(8,29)`；X 写文档/显示 `(7,60)` 并删除 W，恢复后不复活 |
| `wrap-off-shrink` | 4 | 同上 | 独立关闭 DECAWM 的源序列，固定观察与 ON 相同 |
| `pending-on-grow` | 5 | `3;7 → 3;7 → 8;61 → 8;61 → 8;62` | 窄屏末列 W 在文档/显示 `(7,60)`；增宽保持位置，下一 X 覆盖 W，随后协议光标前进一列 |
| `wrap-off-grow` | 5 | 同上 | 独立关闭 DECAWM 的源序列，固定观察与 ON 相同 |

表中文档和显示坐标从零开始。`W/X` 及 `0123456789`、`ANCHOR` 控制 marker 的完整出现集合分别比较；文档还检查包含硬换行分隔符的绝对字符位置。缩窄样本 W 的 position 为 113，X 为 83；增宽样本 W/X 均为 83。无 W/X 的阶段检查其缺席，不以“至少找到一次”代替精确集合。

原版共有 `4+4+5+5=18` 阶段、四次独立运行；strict/recovery × 整块/逐字节组成 16 次核心回放、**72 阶段、720 条断言**。核心回放不是新增原版运行。原版全文合成文档摘要的独立核对由原版报告保存；本 C++ 测试不声称验收完整 grid、样式或像素。

## 红绿记录与源码身份

| 记录 | 测试源码 SHA-256 | 结果 | 日志 |
|---|---|---|---|
| 首例 `pending-on-shrink` RED | `ef430c5467a2e310513803b400ae0ac31af645416d2f7cc878d80e8c7fc94402` | 4/16 阶段通过；44/160 断言失败 | `resize-pending-first-red.log` |
| 四例完整 RED | `3414245c16fbbbb0efc0404cc2f8def3de2e75a15cb903cd7880a1380649286c` | 44/72 阶段通过；108/720 断言失败 | `resize-pending-all-red.log` |
| 同一四例源码 GREEN | 同上 | 72/72 阶段通过；0/720 断言失败 | `resize-pending-all-green.log` |

首例 RED 已构建并实际运行；其后才追加另三例。四例 RED 与 GREEN 使用相同测试源，修改在 [screen.hpp](../../screen.hpp) 的宽度事务中。原 RED 例如保留了旧 W，并把 X 续在文档 `(7,91)`、显示 `(8,30)`；目标要求 X 在 `(7,60)` 覆盖新末列。增宽 ON 的下一字符也必须覆盖旧 W，而不是消费旧延迟状态换行。

[validation.json](validation.json) 分列三个运行的计数、测试源身份、实现文件、二进制和日志哈希。源码与本次 GREEN 二进制哈希在执行后、后续隐藏帧改动及重建之前采集；采集时列出的源码字节与 `9b67886` 对应文件逐项一致，不混入之后的 Screen/CMake 或二进制状态。完整运行前输入 manifest 未留存，历史 RED 二进制没有可核对的留存身份，均明确为 `null`，不拿 GREEN 二进制倒填历史运行。

## 定向回归与工程边界

同轮重建并运行 `retained_columns_core`、`retained_resize_address_core`、`retained_resize_edit_core`、`retained_resize_pending_core`：**4/4，通过，总计 0.11 秒**，见 `resize-pending-green-ctest.log`。这是一次测试执行时间，不是性能测量或容差验收。

owner 工程测试相应改为检查活动光标夹取与取消 pending；相同列宽仍保持原状态。行身份、正文和软续接保留的工程检查继续存在，但不能把这些工程场景当作未知原版隐藏帧、origin/margin 或结合字符策略的证明。

配置为 Windows x64、CMake 3.31.0、Visual Studio 17 2022、MSVC 19.42.34433.0、SDK 10.0.26100.0、Release、并行度 1；Qt 与候选适配器关闭。可复验入口：

```powershell
cmake --build <core-build> --config Release --target resize_pending_test resize_address_test resize_edit_test retained_columns_test --parallel 1
ctest --test-dir <core-build> -C Release -R '^(retained_columns_core|retained_resize_address_core|retained_resize_edit_core|retained_resize_pending_core)$' --output-on-failure
& <core-build>/bin/Release/resize_pending_test.exe
```

本切片不解决默认裁剪及 Qt B/CD 缩放失败，不包含主/备用屏激活时的正文差异、一般擦除、高度变化、单列、完整 Unicode、在途输出、历史/选择/Find 的产品接线或性能等价；ON/OFF 仅在这些固定动作上结果相同。后续擦除证据与测试另行闭合，不在本报告提前计入。
