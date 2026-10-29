# 行结构发布、精确身份查询与文档序（#169 首片）

本片把“稳定行 ID 被当作文档顺序”的隐含假设收敛到 `Screen` 的 owner 发布口与精确查询：行身份只表示身份，位置由当前保留序列决定。没有新增 VT 模式、区域 LF 或任何用户可触发行为；`Screen::lines/history` 仍是公开成员，封住直接写入属于下一片。

## 交付内容

| 入口 | 变更 |
|---|---|
| `Screen::publish(history, live)` | 导入/回放显式提交拥有值的行镜像：校验宽度、非零且唯一 ID、保留上限与尺寸；成功才替换存储并推进结构 revision；失败不改任何已发布状态 |
| `Screen::locate(id)` | 精确 ID→rank；缺失 ID 返回空，**不再**退化为相邻行。只在可证有序时二分，其余逐行精确扫描 |
| `Screen::order_revision()` | 结构 revision：保留成员、文档顺序或 history/live 分界改变时前进；内容写入、仅移动光标的新行、同尺寸 resize 与同结构重复发布都不前进 |
| `Screen::mark_order_unknown()` | 绕过 `publish` 的直接结构写入必须调用；之后查询改走扫描，宁可慢也不再信任已经失效的二分 |
| `TextDocument::index_of` | 改用 `locate`，因此 `valid/normalize/meta` 及所有依赖都拿到精确 rank |
| `TextDocument::compare` | 按当前文档序比较两个有效位置，而不是比较 ID |
| `HistoryViewport` | 活 anchor 用精确 rank；已淘汰 anchor 保留原有 `lower_bound` 回退，两条路径分开 |
| `TextSearch::navigate` | 前后导航改用文档序；有序时与旧 ID 比较等价 |
| `CandidateTerminal` | 导入改经 `publish`；回放帧直接恢复后显式声明 order 未知 |

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
| 新增强制 Find 的 previous/next | previous(6)→7、next(7)→6 |
| 结构 revision | 只在下滚、物理 resize、缓冲切换、结构发布时前进 |

## 实际验证

环境：Windows 11 10.0.26200、Visual Studio 17 2022 / MSVC 19.42.34433.0、CMake 3.31.0、Release、Qt 与 candidate 关闭；源码 `33b58fa4452c4d4f23dcfc463f65c5c521e59065`。

- 完整默认 Release 构建 exit 0。
- 核心 CTest `8/8` 通过：`order_query_core` 新增，`resize_core`/`history_core`/`selection_core`/`search_core`/`buffer_state_core`/`alternate_mode_core`/`decawm_mode_core` 维持通过。
- 候选路径本机未启用适配器：`candidate_terminal.cpp` 仅做 `/W4` 独立编译（exit 0、无诊断），未链接、未运行 `candidate_core_test`，因此不构成候选行为验证。
- 保留的首轮失败：新测试第一次运行 1/8，原因是夹具第二可见行写成 `y` 却期望两个 `x` 命中；另一次是把“已发布为乱序”错误期望成 `order_known()==true`。两处都只改测试夹具/期望，**不作为产品红→绿证据**。

身份与原始命令见 [validation.json](validation.json)。

## 未完成与本片不主张的结论

- `Screen::lines/history` 仍公开可写，外部仍可绕过 owner 发布；封口与全部 caller 迁移属下一片。
- 有序增量 Find 内部的 `emit_from_id_`/`prune`/`rewind` 与 `match()` 的 ID 区间筛选仍假设有序：运行中跨重排的搜索未验收。
- 未实现区域 LF、新的 VT 模式、reflow 或用户可达重排；默认物理裁剪策略不变。
- 未运行 Qt、固定 ConPTY、真实 GUI、真实 TUI 或任何性能负载；没有性能数值或容差主张，#169 与 #27/#29 均未关闭。

## 复验

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target order_query_test history_test selection_test search_test resize_test buffer_state_test alternate_mode_test decawm_mode_test --parallel 2
ctest --test-dir build -C Release -R "^(order_query_core|history_core|selection_core|search_core|resize_core|buffer_state_core|alternate_mode_core|decawm_mode_core)$" --output-on-failure
```
