# 自主 C++ 极窄/极短 resize 与软硬行合同（#29 有界切片）

自主 C++ strict 核心把「极窄（1 列）/极短（1 行）」resize 与「DECAWM 软换行 / CR-LF 硬换行」的组合，按 [ADR-0005](../../../../docs/adr/0005-physical-row-resize.md) 现有合同基线逐字节锁定为字面 oracle。这里没有新增 WindTerm 原版运行：本切片的参照是「现有实现 + ADR-0005」，不是新的原版观察。

## 固定输入与范围

新建对应尺寸主屏，喂入手写字面源，在 resize 前后按 ADR-0005 的字面期望逐项核对内容、历史、行 ID、光标与结构不变量：

| 用例 | 关键组合 | 锁定的字面事实 |
|---|---|---|
| no-op 尺寸 | 2×2 / 2×4 同尺寸 | 保留 wrap_pending 与组合字附着（L13）；同尺寸 resize 后仍按延迟换行把下一个字符卷到下一行 |
| 1 列裁剪 | 3×4 软换行六字符 → 3×1 | 只留最左格，右侧不可恢复；软换行元数据保留但被 cropped 断链；光标夹到列 0；strict 拒绝宽字；单次/逐字节/二分块签名一致 |
| 1 行硬换行与身份 | 软续行 → 高度裁剪 | 光标行保留、被裁续行身份「已用不复用」，底部裁剪不计历史淘汰，回增铸严格递增新身份 |
| 80×24 → 1×1 → 80×24 | 24 行硬换行标记 | 只留光标行最左格、顶部 23 行按 1 列入历史、回增补默认空行且不回拉历史、物理 resize 不改 buffer epoch |
| 60×12 ↔ 100×30 多次 | 20 轮交替 | 顶行身份稳定、每轮结构不变量（光标界/行宽/宽字配对/历史界/身份唯一且递增）不破坏 |
| origin/margin × pending-wrap | 区域+origin+末列写后 resize | resize 清延迟换行、复位 DECSTBM 到全屏、origin 位保持；DSR 回复随复位区域从 (1;5R) 变 (2;5R)；下一字符原地覆盖 |
| 非法尺寸 | 1×1 下试 0/201 行、0/501 列 | 非法尺寸不改变 1×1 极值状态（L18） |

[测试](../../resize_edges_test.cpp)只讲 strict；recovery 宽字替换不在本 oracle 内（既有 resize_test 已单独覆盖）。signature() 只是跨分块一致性工具，不是合同 oracle 本身。

## 设计边界（区分实测/推断/未验证）

- 实测（对本实现，按 ADR-0005 基线）：上述全部字面事实在 MSVC 19.42.34433.0 / Release 下复现，7/7 通过。
- 推断（标准行为）：resize 复位 DECSTBM、但 origin 位终端级保持，与 [decom-origin 切片](../2026-11-01-decom-origin/README.md) 及 screen.hpp resize 注释一致，属于保守工程选择，不计为原版匹配。
- 未验证：WindTerm 2.7.0 在这些极值尺寸 × 软硬换行的真实行为未知；软续行被淘汰后的重排不是活跃终端 reflow；Qt shell、固定 ConPTY 运输、真实 GUI 与性能均不由此项验收。

## 红绿与实际验证

[验证记录](validation.json)保留源码/二进制 SHA、执行前工具身份、命令出口与通过/失败/跳过；最终二进制在定向构建与测试结束后取哈希，不声称全程连续监控。

- 实现源/测试合同 72715d5（rebase 到 origin/main f72c5c4 后复验）：MSVC 19.42.34433.0、Release，Qt/candidate 关闭。定向构建成功；新增 resize_edges_core 7/7。
- 15 项核心 C++ CTest 全绿（14 项既有 + 新 resize_edges_core），0 失败 0 跳过。
- Python experiments.core.test_core / test_unicode / test_recovery 66 项通过（含补建 receive 依赖）。
- 唯一中途红是测试自身把硬换行误写成裸 LF（LF 保列导致字母在递增列漂移），改为 CRLF 后全绿；实现代码零回退改动——现有实现已符合 ADR-0005 基线，无需最小修正。

复验只需普通 C++ 构建，无新增依赖：

    cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DTERMFORGE_BUILD_QT_SHELL=OFF -DTERMFORGE_BUILD_CANDIDATE_ADAPTER=OFF
    cmake --build build --config Release --target resize_edges_test resize_test history_test selection_test search_test search_reorder_test buffer_state_test view_mapping_test alternate_mode_test decawm_mode_test private_mode_test decom_origin_test reply_test edit_csi_test order_query_test replay terminal_trace recovery_probe receive --parallel 2
    ctest --test-dir build -C Release -R "^(resize_core|resize_edges_core|history_core|selection_core|search_core|search_reorder_core|buffer_state_core|view_mapping_core|alternate_mode_core|decawm_mode_core|private_mode_core|decom_origin_core|reply_core|edit_csi_core|order_query_core)$" --output-on-failure

这里只声明有限 core feed 能力；逻辑行连续性、完整 reflow、备用屏/滚动区全语义、真实 DPI 与性能继续由 #29 及后续切片验收。
