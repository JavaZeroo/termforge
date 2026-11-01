# 自主 C++ DECOM origin 模式与 margin/1049 交叉（#27 限定切片）

自主 C++ strict/recovery 识别精确单参数 `CSI ?6h/l`（DECOM）：origin 相对寻址、与 DECSTBM 上下界交互、与 DECAWM 独立共存、1049 进出时 origin 状态保持。依据是 [#151 原版报告](../../../../docs/research/windterm-origin-wrap-reference-2026-10-29.md) 全部 25 阶段的字面合成源与逐阶段 bare DSR 回复；没有新增原版运行。

## 固定输入与范围

新建 25×91 主屏，逐阶段累计喂入 25 段合成源共 334B，每阶段后追加 `ESC[6n` 并核对回复。这是原 25 阶段完整回放（含 origin、margin 区域 LF 与 origin 关闭的 DECAWM 阶段），DSR 一基坐标直接沿用原版有限观察：

| 阶段 | 关键输入 | 字面回复 |
|---|---|---|
| 0–3 | 清屏/复位/`2;5r`/CUP(1,3)`D` | (1,3)→(1,1)→(1,4) |
| 4–8 | `?6h`、CUP(1,3)`O`、CUU99/CUB99、CUD99、`?6l` | (1,1)→(1,4)→(1,1)→(4,1)→(1,1) |
| 9–13 | 哨兵、区域底 CRLF、区域上/下 LF、`r` | (5,3)/(2,3)/(25,3)/(1,1) |
| 14–18 | 末列 wrap 开关五序列（origin 关闭） | (9,2)/(10,91)/(13,2)/(14,91)/(17,2) |
| 19–24 | 主屏 `?6h`、1049 进出、备用 `3;6r?6h`、返回、最终复位 | (2,5)/(1,1)/(2,5)/(2,5)/(1,7)/(1,5) |

[测试](../../decom_origin_test.cpp)通过公开 `Parser::feed/finish/take_replies` 逐阶段核对字面回复，并对完整源（434B 含 DSR）在 strict/recovery 各做一次整体、一次逐字节、435 个二段分割点，回复序列一致。异常序列（CAN/SUB/ESC 重同步、EOF 截断、双参数/非法终字符）在 recovery 被消费不污染下一条 CSI、在 strict 原子拒绝；RIS 复位 origin；origin 与 DECAWM 互不串扰且都跨 1049 保持。

## 设计边界（区分实测/推断/未验证）

- 实测：`?6h` 归位到区域左上、`?6l` 归位到屏幕原点；origin 下 CUP/VPA 行相对区域顶、CUU/CUD 行夹在 [区域顶,区域底]；DSR 6n 行相对区域顶。均由阶段 4–8、19–24 的字面回复锁定。
- 推断（标准行为）：origin 位终端级共享（与 DECAWM 同层），1049 进出不重置；DECSTBM 在 origin 开启时经同一 clamp 归位区域顶。原版把“每屏 origin 位”与“返回后 margin 下界”列为未观测，故此二处是保守工程选择，不计为原版匹配。
- 未验证：origin × 末列 wrap 是模型组合检查（wrap 阶段 14–18 均在 origin 关闭下观察），不是字面 oracle 阶段；完整 active grid、逐格样式、本地 ConPTY 运输仍未知。

## 红绿与实际验证

[验证记录](validation.json)保留源码、执行前工具身份、最终二进制 SHA、命令期限及通过/失败/跳过；最终二进制在定向构建与测试结束后取哈希，不声称全程连续监控。

- 实现源/测试合同 `91e0c55`：MSVC 19.42.34433.0、Release，Qt/candidate 关闭。定向构建成功；13 项核心 C++ CTest 全绿（12 项既有 + 新 `decom_origin_core` 11/11），0 跳过。
- Python `experiments.core.test_core/test_unicode/test_recovery` 66 项通过（含补建 `recovery_probe` 与 `receive` 依赖）。
- 唯一中途红是测试自身对“CUP(2,2)+单字符后光标列”的断言差一，修正后 11/11；实现代码无回退改动。

复验只需普通 C++ 构建，无新增依赖：

    cmake -S . -B build/decom -G 'Visual Studio 17 2022' -A x64 -DTERMFORGE_BUILD_QT_SHELL=OFF -DTERMFORGE_BUILD_CANDIDATE_ADAPTER=OFF -DPython3_EXECUTABLE=<Python>
    cmake --build build/decom --config Release --target decom_origin_test decawm_mode_test alternate_mode_test buffer_state_test history_test resize_test selection_test search_test search_reorder_test reply_test edit_csi_test order_query_test view_mapping_test replay terminal_trace recovery_probe receive --parallel 2
    ctest --test-dir build/decom -C Release -R '^(resize_core|history_core|selection_core|search_core|search_reorder_core|buffer_state_core|view_mapping_core|alternate_mode_core|decawm_mode_core|reply_core|edit_csi_core|order_query_core|decom_origin_core)$'

这里只声明有限 core feed 能力；Qt、原生 GUI、固定 ConPTY 路径、真实 TUI、呈现和性能均未由本项运行或验收。#27/#41 及 R2 继续保持各自出口。
