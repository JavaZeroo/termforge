# 自主 C++ pending-wrap：延迟换行边缘跨 1049/DECAWM/DECOM（#27 限定切片）

自主 C++ strict/recovery 锁定末列写入后的延迟换行（pending-wrap）边缘态：1049 进出时的保存恢复、与 DECAWM 开/关和 DECOM 开/关的组合、跨包/逐字节喂入一致、异常序列恢复不串扰。依据是两份固定的原版观察，没有新增原版运行：

- #123 备用屏样式观察的阶段 8–9：(3,91) 写 W 后 1049h 进备用屏、1049l 退出后写 X 得到 DSR (4,2)；独立对照 (6,91) 写 VZ 得到 (7,2)。见 [备用屏样式参考](../../../../docs/research/windterm-alternate-style-reference-2026-10-28.md)。
- #151 origin/wrap 观察的阶段 14–18：末列 DECAWM 开/关五条序列与逐条 bare DSR。见 [origin/wrap 参考](../../../../docs/research/windterm-origin-wrap-reference-2026-10-29.md)。

## 固定输入与范围

[测试](../../pending_wrap_test.cpp) 通过公开 Parser 的 feed/finish/take_replies 接口锁定：

- 七阶段累计字面回放：阶段 14–18 的 DECAWM 边缘 + 1049 pending-wrap 往返 + VZ 对照，每阶段后追加 ESC[6n 并核对字面 DSR。
- pending-wrap × 1049：进入清掉备用屏可见 pending，退出恢复主屏 pending；(3,91)W 跨 1049 往返后 X 落到 (4,2)，重复切换不丢；W 行保留软续行与下一行身份链接。
- pending-wrap × DECAWM 开：末列写 AB 后 A 武装延迟边、B 换到下一行；关：末列写 CD 只留 D、关后再 H 原地覆盖、开后再 J 换行（阶段 14–18）。
- pending-wrap × DECOM：开时末列写 XY 换到区域内下一行、DSR 行相对区域顶；关时末列写 CD 原地覆盖。DECOM 开通的两格是组合模型检查，不是字面 oracle 阶段（#151 的 wrap 阶段均在 origin 关闭下观察）。
- 分块/逐字节一致：对完整含 DSR 的累计源做整体、逐字节、151 个二段分割，回复与状态一致；另把末列 W 单独拆到不同包验证仍得到 (4,2)。
- 异常序列：recovery 下 CAN/SUB/ESC 重同步、EOF 截断、不支持私有形式均不移动光标也不清 pending；strict 对不支持私有形式原子拒绝。恢复后继续写 X 仍换行。

## 设计边界（区分实测/推断/未验证）

- 实测：pending-wrap 在 1049 退出后保留（#123 阶段 8–9 的 (4,2) 与 VZ 对照 (7,2)）；DECAWM 开/关的五条末列定式（#151 阶段 14–18）。
- 推断（组合模型）：DECOM 开 × 末列 wrap 的区域界内换行与相对 DSR；DECOM 开 × DECAWM 关的原地覆盖。原版对此两格未观测，不计为原版匹配。
- 未验证：宽字/结合字跨 1049、完整 active grid 与逐格样式、每屏 origin 位、固定 ConPTY 运输与真实 TUI。

## 红绿与实际验证

[验证记录](validation.json) 保留源码、二进制 SHA、配置与通过/失败/跳过；最终二进制在定向构建与测试结束后取哈希，不声称全程连续监控。

- 实现源/测试合同 3a8fc1a：MSVC 19.42.34433.0、Release，Qt/candidate 关闭。定向构建成功；16 项核心 C++ CTest 全绿（15 项既有 + 新 pending_wrap_core 15/15 checks），0 跳过。
- Python experiments.core.test_core/test_unicode/test_recovery 66 项通过（补建 receive 依赖）。
- 唯一中途红是测试自身对累计回放末态光标的断言行差一（row 5 与 row 6），修正后 15/15；生产实现代码无改动，与合同基线无冲突。

复验只需普通 C++ 构建，无新增依赖：

    cmake -S . -B build/pending-wrap -G 'Visual Studio 17 2022' -A x64 -DTERMFORGE_BUILD_QT_SHELL=OFF -DTERMFORGE_BUILD_CANDIDATE_ADAPTER=OFF
    cmake --build build/pending-wrap --config Release --target pending_wrap_test decawm_mode_test alternate_mode_test decom_origin_test buffer_state_test history_test resize_test resize_edges_test selection_test search_test search_reorder_test reply_test edit_csi_test order_query_test view_mapping_test private_mode_test replay terminal_trace recovery_probe receive --parallel 2
    ctest --test-dir build/pending-wrap -C Release -R '^(resize_core|resize_edges_core|history_core|selection_core|search_core|search_reorder_core|buffer_state_core|view_mapping_core|alternate_mode_core|decawm_mode_core|private_mode_core|decom_origin_core|reply_core|edit_csi_core|order_query_core|pending_wrap_core)$'

这里只声明有限 core feed 能力；Qt、原生 GUI、固定 ConPTY 路径、真实 TUI、呈现与性能均未由本项运行或验收。#27 保持开放。
