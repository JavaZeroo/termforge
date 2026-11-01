# #27 模式交叉与文本读者的限定核心回归

基点为 `f72c5c4f68dd7eb579ad1e523762333b135aa074`。本项独立开发修复两处实际文本错误，并补齐 ED3 删除历史的结构通知；验证对象为自主 C++ 核心的公开 `Parser`、`Screen`、`TextDocument`、`TextSelection`、`TextSearch` 接口。没有新增 WindTerm 运行，也没有据此接受完整 #27、真实 TUI 或 R2。

## 复现与最小修复

1. **区域底部自动换行断开逻辑行。** 新建 4×4 屏幕，输入 `ESC[2;3r ESC[3;1H ABCDE`（展示空格不属于输入）。原先可见 `ABCD`、`E` 两行，但前行的 `next_id` 指向区域外旧行，复制得到 `ABCD\r\nE`，搜索 `ABCDE` 无命中。修复在滚动区域底部选用即将分配的新行 ID，不把区域外物理下一行当作续行。普通全屏滚动、区域止于最后一行及显式硬换行是独立正控/负控。
2. **RIS 删除历史后增量 Find 漏字。** 新建 4×4、history=10，依次输入 `A\r\nB\r\nC\r\nD\r\nE\r\nF` 和 `ESC[1;3r ESC[3;1H \r\nN`。启动查询 `Z`，先执行三次 budget=2 的搜索步，再输入 `ESC c Z` 并通知搜索变化。原先屏幕含 `Z` 而 Find 无命中。修复只在 RIS 实际删除非空历史后发布结构 revision，让依赖行 rank 的读者重定位。
3. **ED3 结构合同缺口，单独计列。** `CSI 3J` 原先删除历史但不发布结构 revision。此次补通知，保留所有可见行与仍有效的选择；history 为空或仅清理 `discarded` 计数时不增加结构 revision。先前有界探针及本次回归未观察到 ED3 导致实际漏 Find，因此不将它描述为另一处已复现用户错误。

RIS 的既有清屏、模式、margin、备用屏政策未在本项改写；备用屏 RIS 的原版目标仍未知。行 ID、revision、版本与 epoch 是自研读者一致性约束，不是 WindTerm 内部实现结论。

## 真实 RED → GREEN

同一 VS 2022 x64 / MSVC 19.42.34433.0 / Release 配置，Qt 与 candidate adapter 关闭。先写测试、保留生产 `screen.hpp` 的基点内容，五个目标全部编译成功，再运行 RED；之后只修改 `screen.hpp` 的三处逻辑并重建同五个目标。

| 测试 | RED | GREEN | 计数含义 |
|---|---:|---:|---|
| `alternate_mode_core` | 通过 | 通过 | 一个 CTest 入口；不把内部断言虚构成原版场景总数 |
| `decawm_mode_core` | 15/15 | 15/15 | 命名检查组；包含旧的每模式 235 个字面前缀分块子例 |
| `decom_origin_core` | 19/23 | 23/23 | 命名检查组；红例为区域底自动换行 × strict/recovery × 整段/逐字节 |
| `order_query_core` 新增清历史结构组 | 16/32 | 32/32 | 两种解析模式 × 两种分块 × history 0/10 × 有序/重排 × ED3/RIS |
| `order_query_core` 新增增量 Find 组 | 60/64 | 64/64 | 上一矩阵再乘查询 F/Z；四个红例均为有历史、重排行、RIS 后查询 Z |
| `region_wrap_core` | 108/144 | 144/144 | 两种解析模式 × 三种区域 × history 0/5 × 三种分块 × 四种文本 |

区域文本四种为 ASCII、末列宽字带结合字、宽字提前换行带结合字，以及显式硬换行控制。分块长度为 1、3、256 字节；每例均核对独立字面复制和搜索结果、区域外哨兵、历史成员与精确行定位。144 是核心工程回归组合数，不能换算成 144 个 WindTerm 比对场景。

RED：CTest 2/5，exit 8，0.32 秒。GREEN：CTest 5/5，exit 0，0.29 秒。耗时仅记录执行过程，不是性能测量或验收门槛。`order_query_core` 还有既有回归，本表只统计本次新增的 96 个矩阵用例。

后续另外补验两个核心入口，不能并入上述首次 5/5 的源码身份：

- `private_mode_core` 新增 2244 个私有命令分块组合：`?1049/?7/?6` 的七类非法格式与 CAN/SUB/ESC/EOF，strict/recovery、默认/改变后的模式、全部二段分割点及逐字节；另有 52 个备用屏 Find 完成/进行中与相同文本重复进入的 epoch 失效和重新搜索组合。这些是既有严格拒绝/恢复消费及读者所有权政策，不是新增原版匹配。
- `view_mapping_core` 新增四个 strict/recovery × 整段/逐字节组合，通过 `HistoryViewport::snapshot`、mapping、selection、Find 核对区域换行、1049、隐藏主屏增宽、旧页失效与值快照。首次测试曾错误期望 4→6→4 缩宽仍复制 `ABCDE`，实际既有 `cropped` 政策连仅删 padding 的缩宽也产生 `ABCD\r\nE`。因此保真路径改为增宽→增宽，另显式保留 padding-only 缩宽断开的现状断言；没有修改 resize 实现或把此政策算作 WindTerm 匹配。初次失败保留在 `local/r27-history-wrap/qt-first-ctest.log`：那次同时选了 Qt 和 mapping 两项，2/2 失败，其中 mapping 是本测试预期不符既有政策，不把另一 Qt 失败混为核心实现错误。

上述两项新构建后 2/2、exit 0、0.07 秒。随后同一最终核心源码重新构建 resize、history、selection、search、search_reorder、buffer_state、view_mapping、alternate_mode、decawm_mode、private_mode、decom_origin、reply、edit_csi、order_query、region_wrap 共 15 个目标，15/15 CTest 通过、exit 0、0.64 秒。最终源码包括两处只澄清工程政策的 `screen.hpp` 注释修订；[validation.json](validation.json) 的 `later_core_validation` 分别保留新运行、28 个源码文件与五份后续日志哈希，首次五项的旧哈希继续保留。15 项仍是定向 C++ 核心回归，不是全项目/Qt 联合验收。

## 证据来源与工程策略

- 既有原版依据：[1049 有限观察](../2026-10-29-cpp-1049/README.md)、[#151 origin/wrap 的 25 阶段](../../../../docs/research/windterm-origin-wrap-reference-2026-10-29.md)、[DECOM 固定回放](../2026-11-01-decom-origin/README.md)。本轮重跑对应测试，不新增原版观察数量。
- 新增工程交叉：宽字/结合字跨重复 1049、DECAWM 关闭后的尾格覆盖、完整格子属性、双帧 margin/origin 与物理 resize、局部自动换行及历史删除读者一致性。这些字面结果约束自研实现，不能推导原版完整 active grid、逐格样式、每屏 origin 或所有 resize 语义。
- 原始构建/测试日志与 RED 源码快照留在 `local/r27-history-wrap/`。公开 [validation.json](validation.json) 记录源码与日志 SHA-256、实际命令及结果；不提交终端私有会话、原版资源或原始二进制提取物。最终二进制哈希在定向运行结束后采集，不宣称持续监控了整个执行过程。

## 复验与未验收项

```powershell
cmake -S . -B local/r2-core-build
cmake --build local/r2-core-build --config Release --target order_query_test region_wrap_test alternate_mode_test decawm_mode_test decom_origin_test --parallel 1
ctest --test-dir local/r2-core-build -C Release -R '^(order_query|region_wrap|alternate_mode|decawm_mode|decom_origin)_core$' --output-on-failure
```

上述第一条复用了已配置的 VS 2022 x64 核心构建目录；从空目录复验时应明确生成器、`-A x64`、`TERMFORGE_BUILD_QT_SHELL=OFF` 与 `TERMFORGE_BUILD_CANDIDATE_ADAPTER=OFF`。本记录尚未包含全项目测试、Qt/固定 ConPTY 运输、真实 TUI/GUI 动作、真实系统剪贴板或性能等价验收；后续运行必须保持各自配置与结果身份。
