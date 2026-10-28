# 主屏 ASCII DECAWM 与末列开关（#161）

自主 C++ strict/recovery 识别精确单参数 `CSI ?7h/l`，在本项主屏 ASCII 样例中关闭后覆盖末列，重新开启后可继续换行。依据是 [#151 原版报告](../../../../docs/research/windterm-origin-wrap-reference-2026-10-29.md)阶段14–18；没有新增原版运行，也没有实现查询回复、origin或margin。

## 固定输入与范围

新建25×91主屏，抽取五段合成源共79B，不插入DSR。原阶段13的margin reset不在当前核心支持范围，因此这不是原25阶段完整回放。文字投影与光标一基坐标直接沿用原版有限观察：

| 阶段 | 输入（每个模式操作带CSI前缀） | 字面结果 |
|---|---|---|
| 14 | `?7h`、CUP(8,91)、`AB` | 保留AB，(9,2) |
| 15 | `?7l`、CUP(10,91)、`CD` | 仅D，(10,91) |
| 16 | `?7h`、CUP(12,91)、`EF` | 保留EF，(13,2) |
| 17 | CUP(14,91)、`G`、`?7l`、`H` | 仅H，(14,91) |
| 18 | `?7l`、CUP(16,91)、`I`、`?7h`、`J` | 保留IJ，(17,2) |

[测试](../../decawm_mode_test.cpp)通过公开 `Parser::feed/finish` 检查独立字面文字及位置；对每个阶段前缀检查所有二段分割点和逐字节输入，strict/recovery各235例。物理行、软连接和版本属于自研模型不变量；原版文档不是完整active grid。混合、未知、畸形及超长private输入保留原拒绝/消费策略，不部分应用模式；取消、截断和ESC重同步有单独回归。

wrap许可位由Screen持有、默认开启，独立于FrameState；开关不抹掉末列已写信息。这是内部工程选择，**不证明原版跨1049继承、保存或恢复规则**。宽2字符空间不足仍沿既有有界预换行路径，安全回归验证正文及续格保全，不新增替换/丢字策略，也不计为原版off×宽字匹配。结合字、wrap×resize、每屏origin、返回margin下界及完整样式仍未知。

## 红绿与实际验证

[验证记录](validation.json)保留源码、执行前工具身份、最终二进制SHA、命令期限及本地原始stdout/stderr的SHA；直接执行的RED/首个GREEN另有执行前测试二进制SHA。最终二进制在补建与测试结束后取哈希，不声称全程连续监控；日志留在`local/decawm-r2/`。

- RED `85c867f1b5307651ba4b444840d0901d2fe4a04d`：MSVC构建成功，直接检查0/2、exit1。strict报`unsupported private CSI command`；recovery忽略开关，末列CD不在(10,91)。
- 首个局部GREEN `f638f7e`：同两例2/2。该中间源曾加入未执行验证的off×宽字拒绝分支，随后在 `a64376ae16f609db97af2cd05367b42a659c4bb4` 移除，保留既有宽字fit路径；不把旧分支写为原版结论。
- 最终运行源/测试合同 `2ed20754696ed8ed530c1fbddb5da262f1e5b2cb`：Visual Studio 2022、MSVC19.42.34433.0、Release，Qt和candidate关闭。定向构建成功；首次9项CTest为8/9、exit8，唯一core错误是构建清单遗漏其所需的`receive.exe`，其中31条Python测试有1个依赖错误。其余6项既有C++测试、新DECAWM的9组及recovery的34条Python测试均通过。
- 同源补建`receive`，仅重验core：1/1、31条Python测试通过、0跳过。初次失败和补充结果分列，**没有合称一次9/9**。定向范围含既有1049 pending恢复、双帧/历史、ID/版本、选择/搜索、Unicode和物理resize；未运行完整项目或GUI矩阵。

复验只需普通C++构建，无新增依赖：

```powershell
cmake -S . -B build/decawm -G 'Visual Studio 17 2022' -A x64 -DTERMFORGE_BUILD_QT_SHELL=OFF -DTERMFORGE_BUILD_CANDIDATE_ADAPTER=OFF -DPython3_EXECUTABLE=<Python>
cmake --build build/decawm --config Release --target decawm_mode_test alternate_mode_test buffer_state_test history_test resize_test selection_test search_test replay terminal_trace recovery_probe receive --parallel 2
ctest --test-dir build/decawm -C Release --verbose -R '^(decawm_mode_core|alternate_mode_core|buffer_state_core|history_core|resize_core|selection_core|search_core|core|recovery)$'
```

这里只声明有限core feed能力；Qt、原生GUI、固定ConPTY路径、真实TUI、呈现和性能均未由本项运行或验收。#27/#28/#29/#30及R2继续保持各自出口。
