# 同 owner 宽高组合保留事务与五阶段字面回放

[原版五阶段样本](../../../../research/ui/results/2026-11-02-resize-combined-reference/README.md)在 `91×20 → 61×20 → 61×14 → 61×20 → 91×20` 中保持完整正文，DSR 为 `19;7 → 19;7 → 14;7 → 14;7 → 14;7`。本切片把该观察建立为独立字面回放，并在现有 Screen owner 内新增 `resize_preserving(rows, columns)`：高度变化时把移出视口的前缀行交给主屏历史，存活行保留源 Cell、身份与软续接；两帧在准备完成后一起发布，分配失败不发布任何一帧。

同一份字面预期下，旧"宽度保留事务 + 旧高度物理裁剪"路径为 **8/20 阶段、80/560 条断言失败、退出 1**；改为新事务后为 **20/20 阶段、零失败**，其中映射/选择工程检查从 8/20、60/648 变为 **20/20、744 条断言零失败**。断言总数增加不是放宽分母，而是失败消失后执行了后续映射/选择检查。

**默认 `Screen::resize` 与 Qt 仍走原物理裁剪路径，本切片只增加显式入口与核心回归门禁。** 原版观察、核心回放、Qt/固定 ConPTY 与 CI 仍是不同配置；本页不声明 #27/#29、完整 reflow 或性能验收。

## 源码与验证身份

- 基线 `8eec75d0b47570681850747e070e2d3e1d8a8ae3`（本次任务开始时 `origin/main`）。
- 旧路径失败复现源码 `13186bff79bb522276364c2f397aab6398fd02f0`：回放调用既有 `resize_columns_preserving` + `Screen::resize`；实际构建并运行于本工作区。
- 实现与回放源码 `eb09421`（事务，含分配失败检查）与 `4d24c792cf8721167df3bbdeb4c0f768babfc81c`（切换回放并注册两个 CTest 门禁）。本报告提交只增加文档，不改代码。

构建/运行环境：Windows 11 x64、Visual Studio 17 2022 17.12.6、MSVC 19.42.34433.0、Windows SDK 10.0.26100.0、Release、Qt OFF、候选适配器 OFF、Python 3.12.10、Node 24.22.2。定向构建后收集源码与 EXE SHA-256，见 [validation.json](validation.json)。

## 结果

| 验证 | 实际结果 | 口径 |
|---|---|---|
| 旧路径组合回放（`13186bf`） | **8/20 阶段、80/560 失败**，映射/选择 8/20、60/648 失败，退出 1 | 在本次工作区重新构建并运行；失败阶段为缩高、恢复高、恢复宽 |
| 新事务组合回放（`4d24c79`） | **20/20 阶段、0/560 失败**；映射/选择 **20/20、0/744 失败**，退出 0 | 同一字面预期、同一 75 B 源；仅替换尺寸事务调用 |
| 事务分配失败 sweep | 活动主屏缩/增各 **151/42** 次注入失败后重试成功；活动备用屏（隐藏主屏）同 | 每次失败后检查两帧、历史、身份、守卫与后续附着；重试结果与无故障一致 |
| 历史预算/空操作/越界 | 限额 0/3/10 的内容、淘汰与 `discarded` 一致；同尺寸不分配；非法尺寸与可达计数溢出不发布状态 | 工程不变量，不是时延或分配性能测量 |
| 增长分配与历史长度无关 | 8 与 4000 行历史下，增高事务的分配次数与字节相同，旧历史地址不变 | 只证明准备路径不遍历/复制历史 |
| 核心 CTest | **34/34 通过，4.52 秒** | 含 `resize_combined_core` 与 `resize_preserving_transaction_core`；时间不是性能结果 |
| 组合探针离线 CTest | **3/3 通过，44.63 秒** | `windterm_resize_combined_reference`、`_optimized`、`_controller`；不启动原版 |

回放覆盖 strict/recovery × 整块/逐字节四次执行，每次五阶段，共 20 阶段。每阶段分别检查协议宽高、DSR、完整 19 行逻辑文档、选择复制全文、源位置/显示位置映射和高亮；没有前缀匹配或跳过失败阶段。

## 复现命令

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target resize_combined_test resize_preserving_transaction_test --parallel 4
ctest --test-dir build -C Release -R "_core$" --output-on-failure
ctest --test-dir build -C Release -R "windterm_resize_combined" --output-on-failure
& build/bin/Release/resize_combined_test.exe
& build/bin/Release/resize_preserving_transaction_test.exe
```

旧路径失败可用 `13186bf` 的 `resize_existing_path` 版本重建 `resize_combined_test`；该调用已被后续提交替换，本报告不把旧 EXE 当作新验证。

## 边界与未完成

- 只覆盖固定 75 B ASCII 主屏样本与五组固定尺寸、后续仅 DSR；不覆盖 resize 后继续输出、双轴同时变化、备用屏活动、隐藏主屏原版对照、Unicode、历史淘汰或保存光标交叉。
- 新事务是显式入口；默认 `Screen::resize`、Qt 呈现、选择/Find UI 与固定 ConPTY 运输尚未切换。Qt 的 B/CD 缩放失败未被本切片修复或豁免。
- 分配失败检查与历史预算检查是工程不变量；原版内部历史行数、分配器和时间成本均未测量。
- `resize_preserving` 的隐藏帧拟合沿用早先"隐藏 live 仅保留协议足迹"策略，不是本次新证实的原版行为；隐藏主屏原始审计仍为失败，见 #27 记录。
- #27/#29 保持 OPEN；本切片不以核心回放成功代替原版、Qt 或 R2 联合验收。
