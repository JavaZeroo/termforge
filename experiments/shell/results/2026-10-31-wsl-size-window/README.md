# WSL 尺寸标记观察窗口：成因与修法（#80）

## 现象

`ui-shells` 场景在 stage 5（四会话尺寸就绪验证）超时，`shells-size-readiness.jsonl` 里三个会话 `transport_completed=true`，**只有 wsl 为 false**（`observed_output_tail_bytes=185`）。

## 观察窗口原文

失败作业 124536048854（run 41200805448）的 artifact 中，`shells-transport-probe.jsonl` 记录了判定窗口：

```
window_bytes = 185
解码： "  ESC[?1004h ESC[?25l /mnt/d/a/termforge/termforge/build/bin/Release/shell_size_client.exe"
       ESC[10;1H      ← conhost 把 helper 的 CRLF 渲染成绝对定位
       TF_SIZE_54x12 CRLF
       TF> 
```

即：命令回显与标记落在同一条记录里，中间只有一个 CUP。

## 为什么不能改 oracle

`transport_completion_test.cpp` 有三条合同用例明确要求带 `ESC[6;1H`/`ESC[7;1H` 的回显前缀**不得**被当作换行（`CUP cannot discard an echo prefix to invent marker output` 等）。把 CUP 当边界会直接违反它们。

## 修法（已合并）

在场景层让窗口变干净，而不是让判定变宽松：`workspace_shells_scenario.hpp` 的 `sizes_ready()` 中，当窗口含 `shell_size_client`（回显已完成）但**不含 marker**（标记未到）时，重新 `begin_output_observation()`。两个条件同时成立才重开，避免标记已到却被清掉。

## 修前/修后对照

| 版本 | Windows 回归结果 |
|---|---|
| 修前（同内容，不含窗口修复） | 红：`workspace-failure mode=ui-shells stage=5` |
| 修后（含 #290） | 绿 |

两次运行差异只有窗口重开逻辑一处。

## 限制

- 只覆盖四会话 + 固定尺寸这一配置；
- 未取得修后运行的 `shells-size-readiness.jsonl`（该文件只在失败产物里），因此 wsl 行是否变为 `true` 未直接观测；
- 本项 #80 的原始 hosted stage-6 事件仍未复现，本记录不构成其因果证据。
