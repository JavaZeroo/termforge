# 行结构发布、精确身份查询与文档序（#169 首片）

> 后续切片（2026-10-29）：存储已封为私有，`Screen::lines/history` 不再是公开成员；本片为“绕过发布口写入”保留的 `mark_order_unknown()` 因此没有调用者并被移除。见[封存行存储](../2026-10-29-sealed-row-storage/README.md)。

本片把“稳定行 ID 被当作文档顺序”的隐含假设收敛到 `Screen` 的 owner 发布口与精确查询：行身份只表示身份，位置由当前保留序列决定。没有新增 VT 模式、区域 LF 或任何用户可触发行为；`Screen::lines/history` 仍是公开成员，封住直接写入属于下一片。

## 交付内容

| 入口 | 变更 |
|---|---|
| `Screen::publish(history, live)` | 导入的显式提交口：先校验行宽、非零且唯一 ID、活跃历史预算与尺寸，再把新存储准备好并交换，校验或分配失败都不留下半提交；成功才推进结构 revision，并把光标夹回新帧内 |
| `Screen::locate(id)` | 精确 ID→rank；缺失 ID 返回空，**不再**退化为相邻行。只在可证有序时二分，其余逐行精确扫描 |
| `Screen::order_revision()` | 结构 revision：保留成员、文档顺序或 history/live 分界改变时前进；内容写入、仅移动光标的新行、同尺寸 resize 与同结构重复发布都不前进。内部结构变更走同一推进点，缓冲切换与回放恢复也计入 |
| `Screen::mark_order_unknown()` / `restore_frame(origin)` | 回放恢复改为显式入口：保留原帧镜像、推进 revision、声明顺序不再可证；绕过发布口的直接写入仍须调用 `mark_order_unknown()` |
| `TextDocument` | `index_of` 改用 `locate`（因此依赖它的位置比较与校验拿到精确 rank）；新增按当前文档序的 `compare` 与 `ordered()` |
| `HistoryViewport` | 活 anchor 用精确 rank；已淘汰 anchor 保留原有 `lower_bound` 回退，两条路径分开 |
| `TextSearch::navigate` | 前后导航改用文档序；`match()` 在顺序不可证时不再按 ID 二分校验扫描记录，改为整体校验 |
| `CandidateTerminal` | 导入改经 `publish`；回放恢复改经 `restore_frame`；保留历史先按物理行策略适配到当前帧宽，不再把上一帧宽度的行混进新帧 |

## 固定夹具与期望

```
4 列、history_limit=4
history ids [1,3,4,5]   live ids [7,6]
```

| 断言 | 期望 |
|---|---|
| `locate(7)` / `locate(6)` / `locate(5)` | rank 4 / 5 / 3 |
| `locate(2)` / `locate(0)` | 缺失 / 缺失（不落到邻行） |
| `lower_bound_line(2)` | 1（已淘汰 anchor 的旧回退保持独立） |
| 文档序比较 | 7 在 6 之前，尽管 6 < 7 |
| 新增强制 Find 的 previous/next/no-wrap | previous(6)→7、next(7)→6、no-wrap previous(6)→7 |
| 结构 revision | 只在下滚、物理 resize、缓冲切换、结构发布、回放恢复时前进 |

## 实际验证

环境：Windows 11 10.0.26200、Visual Studio 17 2022 / MSVC 19.42.34433.0、CMake 3.31.0、Release；源码状态见 `validation.json`。

- 完整默认 Release 构建（Qt 与 candidate 关闭）exit 0。
- 核心 CTest 筛选 8 项 `8/8` 通过：新增 `order_query_core`，`resize_core`/`history_core`/`selection_core`/`search_core`/`buffer_state_core`/`alternate_mode_core`/`decawm_mode_core` 维持通过。
- 候选适配器另在本机以 `TERMFORGE_BUILD_CANDIDATE_ADAPTER=ON` 构建：`candidate_core` `1/1` 通过。CI 上同一项在首个推送版本为红（见下），修复后本地复跑为绿。
- **首个推送版本的真实失败（保留）**：CI 的 `alacritty-headless` 两个 Ubuntu 作业在 `candidate_core` 报 `published row width differs from the screen`。原因是回放保留历史来自上一帧，宽度仍是旧值（111 列）而被发布进 149 列的新帧；旧代码直接 `assign` 掩盖了这个混合宽度的帧。修复方式是按 `Screen::resize` 的物理行策略把保留行适配到当前帧宽（缩窄置 `cropped`、宽带尾格清空），不是放宽发布校验。该红→绿只针对候选适配器，不转记为其他配置。
- 新测试自身的首轮失败（`y`/`x` 夹具、`order_known` 期望、光标夹取期望）只改测试，**不作为产品红→绿证据**。

身份与原始命令见 [validation.json](validation.json)。

## 未完成与本片不主张的结论

- `Screen::lines/history` 仍公开可写，外部仍可绕过 owner 发布；封口与全部 caller 迁移属下一片。
- 有序增量 Find 内部的 `emit_from_id_`/`prune`/`rewind` 仍假设有序：运行中跨重排的搜索未验收；本片只把导航比较与 `match()` 的校验范围改成与顺序无关。
- 未实现区域 LF、新的 VT 模式、reflow 或用户可达重排；默认物理裁剪策略不变。
- 未运行 Qt、固定 ConPTY、真实 GUI、真实 TUI 或任何性能负载；没有性能数值或容差主张，#169 与 #27/#29 均未关闭。

## 复验

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target order_query_test history_test selection_test search_test resize_test buffer_state_test alternate_mode_test decawm_mode_test --parallel 2
ctest --test-dir build -C Release -R "^(order_query_core|history_core|selection_core|search_core|resize_core|buffer_state_core|alternate_mode_core|decawm_mode_core)$" --output-on-failure

# 候选适配器（需要 Rust 工具链）
cmake -S . -B build/cand -G "Visual Studio 17 2022" -A x64 -DTERMFORGE_BUILD_CANDIDATE_ADAPTER=ON
cmake --build build/cand --config Release --target candidate_core_test --parallel 2
ctest --test-dir build/cand -C Release -R "^candidate_core$" --output-on-failure
```
