# 结构重排下的增量 Search 修复（#169 延期项 / #27 前置）

#169 的封存与精确 rank 解决了"身份不是位置"，但它明确把**运行中跨结构重排的增量 Search** 留作后续。本片处理这一项：Search 的扫描游标与失效判定不再用行 ID，而用**当前文档 rank**。

## 变更

| 位置 | 变更 |
|---|---|
| `TextSearch` 状态 | 记录 `document_order_revision_` 与 `order_changed_`；`start()` 绑定当时的 order revision |
| `notify_change` | 除原有 change serial 外，order revision 变化时置位 `order_changed_` |
| `step` | 只在**无序文档**上执行 `repair_order()`（计一个 work 单位）；有序文档仅记录新的 order revision 并清标记 |
| `repair_order` | 以已扫首行当前 rank 为基址，找到第一个不再处于"基址 + 序号"的行；只从该位置重扫，之前的命中保留。仅当已加载行移动到别处（或消失）时，单独重载该行 |
| `rewind` | **有序文档保留原 ID 阈值**（ID 与文档序一致，比较精确且便宜，避免把淘汰导致的绝对 rank 位移误当重排）；**无序文档**才用 `rows_` 首行的当前 rank 作基址、按"基址 + 变化起点"失效 |
| `consume` | 有序文档沿用原 ID 阈值；无序文档比较当前行 rank 与重扫起点 |
| `prune_one` | 淘汰行后按"命中两端是否仍存活"（`TextDocument::valid`）裁剪，而不是 `begin.id <= removed` |
| `match()` | loaded 行是否落在命中区间内改为比较三者当前 rank |

有序文档的失效阈值仍按 ID、新命中接纳仍按 ID（与原实现一致，也是 CI 上 shell Find 连续输出路径的实际行为）；rank 逻辑只在新出现的无序文档上生效。这一点是本地 shell Find 连续输出测试暴露后修正的：最初对所有文档都做 rank 重扫，导致连续输出时每行都触发前缀重扫，Find 在 stage 4 超时。

## 新增测试 `search_reorder_core`

1. **扫描中重排**：6 行、扫描到一半时把 id5/id6 互换后继续；断言 6 个命中、按文档 rank 递增、无重复、`match()` 全部存活。
2. **重排后的导航**：换序后 next/previous 按新的文档序前进/后退，而不是按 ID。
3. **淘汰**：2 行活动区 + 2 行历史，连续换行淘汰最旧行；命中只随死行消失，不整段重启。
4. **有序回归**：普通有序文档的完成状态与命中行不变。

## 验证（本机，隔离 worktree）

- 默认 Release 构建 exit 0（`/W4` 无新增告警）。
- `search_core`、`search_reorder_core`、`order_query_core`、`resize_core`、`history_core`、`selection_core`、`buffer_state_core`、`alternate_mode_core`、`decawm_mode_core`、`core`（Python 回归）**10/10 通过**。
- 说明：`core` 首次失败是本隔离构建缺少 `replay`/`receive` 辅助二进制，补齐后通过，与本次改动无关。

固定身份见 [validation.json](validation.json)。

## 边界

- 只覆盖 core 层：Find/overlay 的 UI 集成、Qt 会话与真实 TUI 未运行；本片不改变有序增量 Find 的对外语义与预算参数。
- 运行中重排的**性能**（重建/重扫成本）未测量；仍无用户可达的重排命令，也没有新 VT 模式。
- 复杂情况（已加载行移动、跨行命中被切断、同时淘汰并重排）由固定用例覆盖到，但不等价于"任意交错序列已穷尽"；不据此声明 #27 或 R2 通过。