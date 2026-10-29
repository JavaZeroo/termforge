# 系统 ConPTY resize 后客户端尺寸回复：有界重复采样（#145）

结论先行：**把“resize 成功后立即排入 SIZE 查询”这一孤立时序放大到 2800 个样本，没有复现 [#145](../..#145) 记录的旧尺寸回复。** 客户端 buffer 与 window 从未分歧，resize VT 在全部样本中先于回复出现。因此“ResizePseudoConsole 与紧随的输入之间存在必然的时序竞态”不能解释已记录的 CI 失败；那两条失败仍是真实失败，根因未知。

## 已记录的失败（不因本切片改变）

- [PR141 默认工作流 40742159646](https://github.com/JavaZeroo/termforge/actions/runs/40742159646)：watchdog 前的 startup(normal) 样本，`resized dimensions mismatch`，ready/echo 1、resized 0，child 回复仍为 `TFSIZE:80x24:END`。
- [PR168 push 40814207316](https://github.com/JavaZeroo/termforge/actions/runs/40814207316)：close 第三轮的 resize reply gate，同样未见 120x40；该次 596B 捕获与上一条逐字节一致（SHA256 `c63fc584…6085`）。
- 同一 tree 的 PR 运行为 61/61。原失败、原件保留在该 issue，未被本切片重跑或覆盖。

## 本切片做了什么

新增独立诊断（不改原探针、其 5 秒等待与新尺寸 oracle）：

- `conpty_resize_reply_probe`：真实系统 ConPTY 会话；客户端在每条输入上报告**自己**的 `GetConsoleScreenBufferInfo`，同时给出 buffer(`dwSize`) 与 window(`srWindow`) 两个尺寸，回复带独立 tag，慢回复不会被下一条请求误认。
- `raw` 模式：同一会话交替 120×40 / 80×24，每次 resize 返回后立即排入查询，共 200 轮/会话。
- `startup` 模式：每次全新会话，复刻记录的失败序列 `ready → SIZE → ECHO → resize(120×40) → SIZE`。
- `resize_reply_run.py`：复用生命周期套件的 OwnedJob 监管，记录平台、源/二进制身份、每轮尺寸与微秒级时序；只断言结构完整性（可解析、tag 匹配、Job 归零），陈旧计数写进证据而不作为本切片的门槛。

## 结果

| 环境 | 模式 | 样本 | 陈旧回复 | buffer/window 分歧 | resize VT 先于回复 | 回复中位耗时 |
|---|---|---|---|---|---|---|
| Windows 11 10.0.26200（本机） | raw | 200 | 0 | 0 | 200/200 | 11.4 ms |
| Windows 11 10.0.26200（本机） | startup | 100 | 0 | 0 | 100/100 | 15.6 ms |
| hosted Windows Server 2025 10.0.26100（run 40841303342） | raw | 1000 | 0 | 0 | 1000/1000 | 8.7–12.6 ms |
| hosted Windows Server 2025 10.0.26100（run 40841934880） | raw | 1000 | 0 | 0 | 1000/1000 | 9.6–12.6 ms |
| hosted Windows Server 2025 10.0.26100（run 40841934880） | startup | 500 | 0 | 0 | 500/500 | 16.0 ms |

- 全部 2800 个样本中位回复耗时 8.7–16.1 ms，最大 64.7 ms，远低于原探针 5 秒等待：失败样本不是“等得不够久”。
- 若陈旧回复来自“window 已更新、buffer 未更新”的瞬时状态，本探针应记录到 buffer/window 分歧；实际为 0。
- 运行入口见 [conpty-compat.yml](../../../../.github/workflows/conpty-compat.yml) 的 `windows-2025-resize-reply` job（仅 workflow_dispatch，不进入常规 CI 门槛）。

固定身份见 [validation.json](validation.json)（源哈希、各环境二进制哈希、运行编号与样本计数）。

## 边界

- 本切片不解释那两条 CI 失败，也不关闭 #145/#38；只否定一个具体时序假设。
- 未覆盖：原始探针在完整 ctest 套件中的上下文、hosted 运行器镜像/host 版本差异、以及“每次运行的第一条会话”这类稀有例外。本切片的 job 在全新 VM 上启动，但没有复刻 build.yml 同期其它测试的并发与顺序。
- 探针二进制在本地与 hosted 构建之间哈希不同（构建路径/PDB 身份差异），源哈希一致；逐运行 EXE 与系统 DLL 身份未单独取证。
- 这里的耗时只用于排除 5 秒等待，不是性能测量，也不构成任何预算。

## 下一步（保持有界）

1. 在同一 hosted 运行器上跑**原探针本身**的有界重复批次，确认 clone 与原 oracle 在同宿主同镜像下的失败率一致；若 clone 与原探针都为零，说明失败依赖尚未建模的宿主/镜像条件。
2. 若继续追查，优先固定“首个会话/首条 resize”的宿主状态（会话创建时刻、渲染器就绪、并发测试顺序），而不是扩大同一时序的样本量。
