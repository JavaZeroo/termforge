# 封存行存储：结构只经 owner 发布（#169 第二片）

首片（`85eb5a0`，PR #170）给出了 `publish()` / `locate()` / `order_revision()`，但 `Screen::lines` 与 `history` 仍是公开成员：任何调用方仍可直接改存储，绕过发布口的写入会留下陈旧 rank。本片把这条路径关掉。

## 交付内容

| 变更 | 说明 |
|---|---|
| 存储私有化 | `Screen::lines` / `history` 移入 private；新增只读口 `visible_lines()`、`history_lines()`、`history_size()`，函数注释明确“短生命周期、不可跨结构变化保留” |
| 移除逃生口 | 首片为“绕过发布口的直接写入”保留的 `mark_order_unknown()` 在存储私有后没有调用者，已删除；结构只经 `resize` / `newline` / `activate_buffer` / `publish` / `restore_frame` 改变 |
| 调用方迁移 | 23 个文件、363 处只读访问改经只读口；`selection_test` 的 3 处身份替换与 r164 乱序夹具改经 `publish()`；`measure`、`render`、`trace`、shell 场景与 Qt 源码同步迁移 |
| 真实 caller：Find overlay | `ShellWindow::search_overlay` 的“匹配是否落在可见范围/覆盖哪些行”由数值 ID 比较改为文档 rank（可见首行 rank = `view.viewport.top`，匹配端点用 `TextDocument::index_of`）。这是 issue 明确列出的数值 ID 裁剪迁移 |
| 回放/导入 | `CandidateTerminal` 的回放恢复仍经 `restore_frame()`、导入仍经 `publish()`；本片只跟随只读口迁移，不改其策略 |

r164 的冻结研究树（`experiments/core/line_order`，由 `prepare.py` 以固定源码再生）**不随主线迁移**：它编译对照的是冻结的旧 `screen.hpp`，改动会破坏可复现性，故保持原样。

## 验证

环境：Windows 11 10.0.26200、Visual Studio 17 2022 / MSVC 19.42、Release、Qt 与 candidate 关闭；隔离 worktree 构建。

- 默认 Release 全量构建 exit 0（`/W4` 无告警输出）。
- 核心 CTest 8/8：`order_query_core`、`resize_core`、`history_core`、`selection_core`、`search_core`、`buffer_state_core`、`alternate_mode_core`、`decawm_mode_core`。
- 扩展快测 20/20（另含 `core`、`recovery`、`binary_paths`、`text_actions`、`keyboard`、`paste`、`clipboard_policy`、`text_action_policy`、`presentation`、`measure`、`scenario_tick_gate`、`transport_completion`）。
- `candidate_terminal.cpp` 以 MSVC `/W4 /std:c++17` 独立编译 exit 0（本机未启用候选适配器；候选链接与运行由 PR 的 `alacritty-headless` 作业复核）。
- Qt 相关三处只读访问已迁移，但 Qt 目标未在本机构建（隔离 worktree 内无 SDK），由 PR 的 `qt-cpp-windows` 作业复核。

固定身份见 [validation.json](validation.json)。

## 边界

- 仍无用户可触发的重排、无区域 LF、无新 VT 模式；运行中跨重排的增量 Search 未验收（issue 已明确该条件另行验收）。
- 有序增量 Find 的 `emit_from_id_` / `prune` / `rewind` 区段假设未改。
- 本片仅内部正确性改造：不声明 GUI/TUI、固定 ConPTY 运输、原版等价或任何性能结论。
- 私有化不改变 `Screen` 的可复制语义；副本仍各自拥有存储与顺序。
