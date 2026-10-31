# 实际输入 worker 的持块合同与 sender fence

本合同对应 [#62](https://github.com/JavaZeroo/termforge/issues/62)，为 [#53](https://github.com/JavaZeroo/termforge/issues/53)提供可复用的协调机制。**实现属于 `fix/conpty-backend-r47` / [原 Draft PR #50（已关闭）](https://github.com/JavaZeroo/termforge/pull/50) 的实验整合，尚未进入 main。** 固定运行源码为 `519a3f37d52983062cd99b7efd805f183961fa4d`；完整 source/tree/blob、实际 EXE、官方后端文件对、原始日志 SHA 和逐字段结果见[验证记录](../../experiments/shell/results/2026-10-26-sender-fence/validation.json)。这是实际 `InputQueue` / `PasteTransfer` / ConPTY worker 的限定验证，不是 UI、VT 或操作系统背压验收。

## 修正的夹具假设

旧夹具先启动输出洪流并将 reader 持在 gate，再向完全不读输入的 child 发送 4MiB ASCII。它要求观察到 writer 已提交至少 16KiB，但固定官方后端的一次实际拒绝状态是 `sent=5120 / accepted=6144 / in_flight=1024`，未达到原门槛。不能假设所有宿主都能在 child 不读且输出被持住时预接收 16KiB；本批没有测定内核或宿主缓冲容量，也没有证明其是唯一影响因素。

原 `af563ba` 的 focus 单测真实失败及只有 EXE 身份的 `2ac023b…` 诊断保留；最终同一 EXE 的 `--ungated-writer-observation` 仍按旧顺序返回 2，并再次给出上述低 sent 状态。**没有把旧顺序改写为已通过。** 首次 Python 准备缺少 `termforge_shell.exe`、运行零项测试，单独归为构建前置失败，不计产品失败。缺少固定源码绑定的早期 WIP 绿色不计最终验证。

## 新的明确顺序

4MiB 请求、真实 1024B 分块、16KiB 已发送门槛、reader/output gate、resize 与回收检查保留。夹具明确改变交错顺序：

1. child 用实际控制台读取核对恰好 16KiB `a`，之后不再读输入；只在核对完成后设置独立 primed event。
2. 原 writer 经真实 `take_chunk()` 领取第 17 块，在 queue 发布 `in_flight=1024` 并解锁后，由 sender fence 持住。此时真实计数为 `sent=16384 / accepted=17408 / total=4194304`。
3. parent 同时确认 held 状态和 child primed event，再开启原 flood phase、使 reader 进入原输出 gate。
4. 执行真实 stop、cancel 或 cancel/stop 竞争；继续原 resize generation、child 实际尺寸 102×42、自然退出及自有 Session Job0 检查。

sender fence 只能固定达到门槛后的观察，不能使原来在门槛之前阻塞的 OS 写入自行前进。held 表示 writer 拥有一块真实 payload，不表示 `WriteFile` 已进入或被系统阻塞。输出 gate 的记录来自 reader callback 进入；本批没有独立 flood 字节计数或实际呈现端点。

## 生命周期与接口

`SenderFence` 是绑定单次 transfer 的测试控制面，普通产品 `InputQueue::begin_paste(transfer)` 默认不启用；队列探针显式传入 fence。构造时固定 transfer 身份、非零 generation 和绝对 deadline，`begin_paste(transfer, fence)` 在通知 writer 前完成单次 admission。绑定不同 transfer、重复 admission、错误 ticket、未 held 就 release、重复 release 和过期 release 均被拒绝。

- `snapshot(ticket)` 按值读取 held 状态及真实 progress；不读取或复制输入正文。
- `release(ticket)` 只允许匹配的 held ticket 在 deadline 前继续发送，不生成写确认。
- `InputQueue::cancel_paste()` 先调用真实 `PasteTransfer::cancel()`，再通知 fence；已领取块继续原 `Session::send → WriteFile → acknowledge_written → finish_chunk`，最终取消保持 `sent==accepted`。
- `InputQueue::stop()` 先发布 stopping 并唤醒 fence，再沿原取消同步 I/O 与 join 路径退出。writer 醒来后重新检查 stopping；尚未提交的持块由原 writer failure 清理，不能伪造 ack 或直接当作已发送。
- `abort(ticket, control_eof)` 将控制面中止表现为失败；本批 EOF 仅是纯协议中的方法调用模拟，没有执行实际 IPC EOF。

等待 fence 时不持 queue mutex；cancel/stop 在释放 queue mutex 后通知 fence，防止与 join 形成锁环。`CancelSynchronousIo` 本身不能唤醒 condition-variable wait。fence 强持有绑定 transfer，writer 和 queue 持有 fence 到等待/退出结束。

达到持块、child primed、reader gate 及取消完成的观察共同使用原 3 秒绝对期限；没有每阶段重新延长该观察期限。其他既有 ready、resize、child 尺寸及退出等待分别保留 3 秒，原 Python probe 进程外层仍为 10 秒。纯协议缺 GO 使用 1 秒绝对 deadline，单独证明有限等待；这些带同步的运行不是性能样本。

## 固定结果与计数

最终原始批次有**两种后端各两次 probe 进程运行，每次实际执行三个 worker 动作，共 12 条逐字段审计记录**。原始 launcher 标签 `held-stop-0/1` 不表示只运行 stop。另有两种配置各一次原 focus Python 单测通过，单独记录，不重复并入上述 12 条矩阵。两种配置的 `paste` / `sender_fence` CTest 各 2/2。

| 动作 | 动作前 sent / accepted / in_flight | 动作后 sent / accepted / in_flight | 实际状态与回收 |
|---|---|---|---|
| stop | 16384 / 17408 / 1024 | 16384 / 17408 / 0 | failed，retained capacity 15，Job0 |
| cancel | 16384 / 17408 / 1024 | 17408 / 17408 / 0 | cancelled，retained capacity 15，Job0 |
| cancel/stop 竞争 | 16384 / 17408 / 1024 | 本批为 16384 / 17408 / 0 | 本批均由 stop 获胜、failed、Job0；不能宣称两个交错都被实际观测 |

所有正例保持 input join、resize working/pending/completed generation 1/3/3 和 child 102×42；这些 generation 字段属于 **resize worker**，不是 sender ticket 的独立运行日志。官方配置的构建定义及固定包选择 Console flags24，系统配置 flags0；worker JSON 没有独立输出 flags，证据来源在验证记录中分列。

`shell_queue_probe` 没有链接候选 VT 核心。构建目录叫 `candidate` / `candidate-system`，以及适配开关为 ON，均不能把该结果扩大为候选 VT、Unicode 或 TUI 验收。

## 复验与剩余边界

在已经明确配置并按贡献指南完成构建的目录运行：

```powershell
cmake --build build/candidate --config Release --target shell_queue_probe paste_test sender_fence_test
ctest --test-dir build/candidate -C Release -R '^(paste|sender_fence)$' --no-tests=error --output-on-failure
$env:TERMFORGE_BIN_DIR = (Resolve-Path build/candidate/bin/Release).Path
python -m unittest experiments.shell.test_shell.ShellTests.test_queues_apply_bounded_backpressure_and_reject_input -v
```

系统配置单独使用对应构建目录，不能复用另一 EXE 的身份。旧顺序诊断使用 `shell_queue_probe --ungated-writer-observation`，由 10 秒 subprocess 监督，保留预期 return2 与具体进度；不能将它作为需要重跑取绿的产品测试。

#53 的真实 UI cancel/close、原 3,000,000B 汉字负载、768B receiver checkpoint、真实 flood、完整输入前缀及同次八会话尚未由本批验收。后续必须在发送前预绑定 fence，并让真实 UI 取消进入 queue 的取消通知入口；不能把本夹具额外读取 16KiB 的 priming 原样移到必须保持 768B checkpoint 的 #53。完整官方回归、长期资源、呈现、平台和 WindTerm 性能差距继续独立处理。
