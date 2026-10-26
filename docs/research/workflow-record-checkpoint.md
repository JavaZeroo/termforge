# 替换会话的完整记录检查点

原八会话 data-only 工作流的 `cycle-eight` 快照必须在替换会话仍持续输出时取得，并满足既有精确文字、身份及回收断言。[#87](https://github.com/JavaZeroo/termforge/issues/87) 修正的是合成测试的消费与采样协调；正常产品输出默认不启用检查点，Python oracle、生产输入和期限保持不变。

## 失败及因果证据

历史两份失败在 F5 第 127 条记录只得到 12 个点，独立 oracle 要求 35 个点；源已有 16384 B，队列还有 8217 B。原件仍可离线重现 `replacement core payload/role mismatch`。那两次没有保存后续 native 运输，不能追溯宣称已证明其完整性。

原 stage35 只要求 source≥4096 B 和任意编号前缀，而消费者一次最多取 8192 B。实际 READY 前缀 25 B 与两个 4096 B 源块可以合并为 8217 B；重分片回放在 8192 B 处产生相同的第 127 条半行，余下 25 B 补齐它。这证明了机制，不等于重现历史 GUI 调度。最初四次有界活体观察均未捕获半行，这些未复现结果单列保留。

随后用同一诊断 EXE 做受控旧判据／新判据对照：在第一轮 F5 替换输出前保持消费者，源到达 16384 B 后只放行一次真实 8192 B 消费；保存状态，再继续原工作流。没有改生产字节、4096 B 分块、20 ms 间隔、32000 条／2048000 B、五轮替换、精确 oracle 或期限。

- 旧判据在第 117 条中途采样，原 oracle 真红。相同 session 后续实际 39 B 补齐该条，运输前缀保持；独立候选核心回放得到完整原文字。
- 仅被动等待空行的中间方案仍失败：持续消费跳过边界，直到生产结束才采样，原 oracle 报 `replacement output not active`。这不是可接受修复。
- 新检查点在实际 LF 后持有快照，通过原 oracle；该检查点源为 241664 B，小于 2048000 B，`fixture_running=true`、`output_done=false`。同 session 的实际后续 41 B 补齐受控半行。两次运行各有自己的真实运输，未声称它们逐字节相同。

诊断记录上限为 128 KiB／1024 events。新方案的后续 collected 运输超出字节上限，但所需补齐字节在保存的前缀内；held/checkpoint 未溢出。这里只证明该前缀内的补齐，不声称完整后续运输已捕获。受控消费者调度仅用于因果实验，最终原场景回归移除诊断调度和记录。

## 最终机制与边界

`OutputRecordCheckpoint` 仅由 UI 线程显式启用。已有空行边界时立即保持；否则每次取一个真实队列字节并交给原核心，遇 LF 后保持，最多读取 256 B，并受原场景 70 秒总期限约束。两个替换会话各自就绪后才采集同一检查点，随即释放，恢复原 8192 B 消费块及 65536 B／4 ms tick 总预算。底层队列仍有界。

检查点不验证或修补正文：完整的 `BROKEN` 编号行也会就绪，再由原 Python oracle 拒绝。空启动、只有 READY、CR 无 LF、当前半行、只有其他角色等不会被完整记录判据接受。关闭会解除保持，关闭中或无有效 HWND 的对象拒绝重新启用，避免阻碍排空及回收。

`workflow_checkpoint_test` 在实际默认核心和候选 native-viewport 核心验证 8192／8217 分片、后续补齐、保持／释放及边界负例；最初红测试和最终测试保留相同触发用例，最终另增边界与状态用例，不能称整个测试源码相同。`runtime_observation` 用自有非输入 desktop 验证默认关闭、显式启用、关闭解除和关闭后拒绝重新启用。普通 cmd 回显不保证运输中有字面 LF，因此一个依赖此假设的临时关闭测试已排除；最终真实关闭测试只宣称关闭时已 armed，未宣称它必为 held。

## 正式验证与复现

固定源码、EXE／依赖哈希、旧红、新绿、完整回归、内部 skip 和回收结果见[公开白名单记录](../../experiments/shell/results/2026-10-27-workflow-record-checkpoint/validation.json)。原运输、窗口路径和 desktop 身份保留本地。最终原完整八会话测试包含两个新进程、每次五轮替换；它与受控诊断运行分别记录。

最终源码 `c662011`、候选 `.5`／固定官方后端 flags24 的完整 CTest 为 **56/57，402.66 秒**，两轮原八会话均通过，Job active=0、desktop 已回收。唯一失败为 Vim stage59，仍由 #52 定位；8 项内部 skip 逐项列出，不算真实 CF/IME 通过。关闭 guard 的实际新用例先红后绿；红例未独立保留 EXE 哈希，该字段明确为 null，源码、相同测试、构建日志和失败日志仍在。更早 `f89b0ac` 的 57/57 只属于关闭 guard 前版本，不能拼入最终源码，也不能解释 Vim 历史失败。候选 CI 另增加该跨平台核心回归；CI 的 system 后端与本机官方配置分列。

在依赖和全部 helper 已构建的候选／显式官方配置运行：

```powershell
ctest --test-dir build/candidate -C Release -R '^(workflow_checkpoint|runtime_observation)$' --output-on-failure
$env:TERMFORGE_BIN_DIR = "$PWD/build/candidate/bin/Release"
$env:TERMFORGE_EXPECT_CONPTY_BACKEND = 'reviewed-side-by-side-1.25.260930003'
$env:TERMFORGE_TEST_ARTIFACTS = "$PWD/local/workflow-record-check-new"
$env:PYTHONUTF8 = '1'
python -m unittest experiments.shell.test_workflow_sessions.WorkflowSessionsTests.test_two_fresh_owned_eight_session_rounds -v
ctest --test-dir build/candidate -C Release --output-on-failure
```

完整回归还需固定 Vim 在当前进程 PATH 中。官方后端、system 后端、默认核心和候选核心分别记录身份，不能互相借用结论。data-only 通过不代表真实 CF/IME、实际呈现、性能或完整 R2；#52、#63 及 #47 的其他门槛仍独立处理。
