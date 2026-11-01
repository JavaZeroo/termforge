# 自主 C++ 宽字/结合字跨 1049 与 DECAWM 关闭换行（#27 限定切片）

自主 C++ strict/recovery 锁定宽字（CJK 双宽）与结合字序列跨 1049 备用屏切换的行为：末列宽字不足格预换行策略与 DECAWM 开/关组合、宽字 lead/trail 不分裂与隐藏/恢复、结合字聚类跨 1049 保持、原文复制一致、resize（1 列/1 行）与 1049 交叉的宽字边界、逐字节与分块喂入一致、异常序列与 malformed UTF-8 恢复。

重要边界：本切片没有新增 WindTerm 原版运行。原版从未观测宽字或结合字跨 1049 的组合，因此下面所有交叉都是组合模型检查，以现有实现为基线锁定当前行为，而不是独立的字面 oracle。原版未观测的组合保持未知并记录，不猜。

## 固定输入与范围

[测试](../../widechar_1049_test.cpp) 通过公开 Parser 的 feed/finish/take_replies 接口锁定：

- 末列宽字不足格预换行 × DECAWM：双宽字在末列只剩一格时整体预换行到下一行，DECAWM 开与关结果一致（许可位只门控延迟窄字的消费）；源行保留软续行 metadata，目标行 lead/trail 配对完整。
- 宽字恰好填满末两格 × DECAWM：开时下一窄字标准换行，关时下一窄字覆盖 trail 并清空 lead（不分裂，不留半字）。
- 宽字 × 1049 隐藏/恢复：进入 1049 清掉备用屏可见 pending，退出恢复主屏的宽字 lead/trail 与 pending 边，往返不分裂。
- 结合字聚类 × 1049：主屏 base+marks 聚类跨 1049 往返保持并可续接；主屏与备用屏各自独立；1049 再进入清空备用屏（含其 pending 结合基）。
- 宽字后的结合标落在 lead 格并跨 1049 继续聚合；原文复制（行文本）对窄 base+多结合、宽字+结合的原始 UTF-8 逐字节回环，宽字不重复、结合字聚合不回退。
- resize 宽字边界：1 列收缩清空被裁剪的 lead（不分裂）；1049 内 resize 同步裁剪隐藏主屏宽字；1 行裁剪保留第 0 行宽字；1 列屏放不下宽字（strict 拒绝，recovery 以 U+FFFD 替换）。
- 真实尺寸变更清掉 pending 结合基（后续结合标 strict 抛错、recovery 替换），宽字/结合字簇的已写文本保留。
- 分块/逐字节一致：对固定含宽字+结合字+1049+DECAWM 的源做整体、逐字节、37 字节对应 38 个二段分割，状态签名一致。
- 异常序列：strict 拒绝无相邻 base 的结合标，recovery 以 U+FFFD 替换并计数 replaced_scalars；malformed UTF-8 在 recovery 计 malformed_utf8 并替换，不扰动前序宽字。

## 设计边界（区分实测/推断/未验证）

- 实测：宽字恰好填满末两格在 DECAWM 开下的标准换行，以及末列宽字不足格预换行策略（源自 #161 批次里兼容保留的 wide fit policy，原版未观测 DECAWM 关下的宽字）。均为本核心既有行为，本切片不修改实现。
- 推断（组合模型）：上述全部宽字/结合字与 1049、DECAWM 关、resize 的交叉，因为原版对这些组合无观测，不算原版匹配。
- 未验证：完整 active grid 与逐格样式、每屏 origin 位、固定 ConPTY 运输、真实 TUI、真实 DPI/呈现与性能。

## 红绿与实际验证

[验证记录](validation.json) 保留源码/二进制 SHA、配置与通过/失败/跳过。

- MSVC 19.42.34433.0、Release，Qt/candidate 关闭。定向构建成功；17 项核心 C++ CTest 全绿（16 项既有 + 新 widechar_1049_core 23/23 checks），0 跳过。
- Python experiments.core.test_core/test_unicode/test_recovery 66 项通过。
- 唯一中途红是测试自身把 recovery 模式误当 strict：recovery 以 U+FFFD 替换（不抛 InputError），已按 strict/recovery 区分修正两处；生产实现代码零改动，与合同基线无冲突。

复验只需普通 C++ 构建，无新增依赖：

    cmake -S . -B build/widechar-1049 -G 'Visual Studio 17 2022' -A x64 -DTERMFORGE_BUILD_QT_SHELL=OFF -DTERMFORGE_BUILD_CANDIDATE_ADAPTER=OFF
    cmake --build build/widechar-1049 --config Release --target widechar_1049_test pending_wrap_test decawm_mode_test alternate_mode_test decom_origin_test buffer_state_test history_test resize_test resize_edges_test selection_test search_test search_reorder_test reply_test edit_csi_test order_query_test view_mapping_test private_mode_test replay terminal_trace recovery_probe receive --parallel 2
    ctest --test-dir build/widechar-1049 -C Release -R '^(resize_core|resize_edges_core|history_core|selection_core|search_core|search_reorder_core|buffer_state_core|view_mapping_core|alternate_mode_core|decawm_mode_core|private_mode_core|decom_origin_core|reply_core|edit_csi_core|order_query_core|pending_wrap_core|widechar_1049_core)$'

这里只声明有限 core feed 能力；Qt、原生 GUI、固定 ConPTY 路径、真实 TUI、呈现与性能均未由本项运行或验收。#27 保持开放。
