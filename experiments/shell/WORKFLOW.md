# R1 组合工作流的受控夹具

本文件定义 `r108-console-v1` 的数据与控制协议，供 [#18](https://github.com/JavaZeroo/termforge/issues/18) 后续组合验收复用。它验证生产字节、实际 ConPTY 传输和接收端，不单独证明八会话、系统剪贴板、真实 IME、屏幕内容或呈现性能。整项合同见 [R1-08](../../docs/milestones/R1-local-text.md#r1-08--验收完整文字工作流与多会话资源边界)，实时完成状态只在 issue 维护。

## 自动复验

在根构建后执行：

```powershell
ctest --test-dir build -C Release -R "^(workflow_fixture|ime_input_expectation)$" --output-on-failure
```

`workflow_fixture` 包含跨平台纯校验与 Windows 两轮 fresh ConPTY 运行。外层 runner 使用新目录、OwnedJob、`GO` 门控和超时；原生 host 再核对内层 Job、正常退出和回收。Linux 跳过原生运行，不能据此宣称客户端支持。默认产物在 `build/test-artifacts/workflow-fixture/run-*`；显式 `TERMFORGE_TEST_ARTIFACTS` 可改产物根目录。

## 角色和负载

| 角色 | 固定内容 | 控制与限制 |
|---|---|---|
| A | `TF_R108_A_0000 中文 é\r\n`，末尾 `e` 加 U+0301；先编号0–159，再160–179，最后180–679 | `output` / `append` / `evict` 依次触发160/20/500行，总680行、18,360字节；不能并发或跳过阶段 |
| B | 无生产日志；原始输入逐次 flush 到接收文件 | 数据测试先发送 `TF_R108_PASTE 中文\rNEXT\r`，再直接发送“你好”，预期32个UTF-8字节；直接 Unicode 不算真实 Paste 或 IME |
| F1–F5 | 每行64字节，含角色、五位编号、中文及组合字符，以点补至62字节再加 CRLF | 每64行一块4096字节，共500块、32,000行、2,048,000字节；块起点目标间隔20ms，实际写耗时与迟到另外记录 |

当前原生回归运行 A、B、F1完整、F2提前停止、F2乱序拒绝各两轮。F3–F5沿用同一协议，尚不计为分别跑过，更不代表五路同时洪流已验收。提前停止只接受非空、未满500块的完整块前缀，不制造完成事件。

## 父进程控制

`shell_text_client` 接受：

```text
--workflow-v1 --capture NEW --events Local\TermForgeWorkflow-<parentpid>-<nonce>-<generation>-<role> --generation N --role A|B|F1..F5
```

父进程先创建全新、未置位的九个 named manual-reset events，后缀为 `ready`、`output`、`output-done`、`append`、`append-done`、`evict`、`evict-done`、`stop`、`stopped`。子进程核对角色/代次/所有者进程、Job 和事件初态；控制来自事件，不能把控制字符混入接收字节。停止优先于下一条命令；父进程退出、超时、重复或乱序命令均失败。完成与停止事件不代表消费或呈现完成。

旧 `shell_text_client` 80行模式和 `shell_ime_client` 20行模式保持。后者的新 workflow 入口仍位于原严格 hosted IME 守卫之后；本机数据回归使用 text client，不伪造环境标志。

## 证据分层

| 文件 | 含义与校验 |
|---|---|
| `capture.bin.source.bin` | 实际成功 `WriteFile` 的生产 payload；部分写保留成功前缀并失败。仅含编号记录，不含控制/READY/DONE标记 |
| `transport.bin` | 实际 ConPTY 字节流，独立核对每个编号、内容、顺序及控制标记；不是屏幕快照。可能含主机 OSC 绝对路径，留私有目录，禁止上传 |
| `capture.bin` / `.receiver.json` | 原始接收字节及 receiver 的 read/cancel/join 状态；沿用4096 UTF-16单元上限，不能清空接收文件来对齐期望 |
| `capture.bin.workflow.json` | schema `termforge-workflow-fixture-v1`：角色、代次、阶段偏移/哈希/完成状态、实际时刻、接收汇总与错误 |
| `capture.bin.progress.jsonl` | schema `termforge-workflow-progress-v1`：递增序号、相对原生时钟、阶段事件与累计字节/记录/哈希；失败事件保留 |
| `stdout.json` / `validation.json` | 原生控制结果和独立 Python 审核，分列数据限定范围、二进制哈希、退出/回收/强制清理和失败，不含私有身份 |

`workflow_checks.py` 用独立字面量生成预期，交叉检查源文件、传输、原始接收、阶段日志、receiver 和 Job。该 oracle 尚不验收 UI range、确认前零增量或会话切换后的目标隔离。`.identity` 含原生 PID/代次，仅供父进程验证，不公开；`stderr.txt` 也不在上传白名单。CI 成功产物只上传审核文件；失败上传仍排除这三类私有文件，保留本地原件用于离线核查。

公开 artifact 可重新计算源文件、接收字节和阶段日志。传输原件不上传，因此外部复核只能读取 runner 的传输审核摘要及哈希，不能声称已从下载产物重新核对该原件。

## 运行快照

输出队列、输入队列和 resize worker 提供在各自 mutex 下按值取得的 `snapshot()`，不暴露活对象引用。输出快照记录实际队列字节、高水位和停止状态；输入还分列排队字节与 worker 持有的整个字符串 payload、拒绝数、启动/join/失败状态。`in_flight_bytes` 不表示内核缓存、尚未写出的精确字节或内存分配容量，不能因排队为零就宣布发送完成。

Resize 分列 requested/completed、实际 working 代次与合并后的最新 pending 代次，最多一个在途加一个待处理；代次差不等于队列深度。停止或失败后未完成的代次仍保留，但不标成待执行。旧公开计数只在 producer/writer join 后读取；运行中只能使用带锁快照。多个模块的快照不是整个进程的原子观测，UI工作流、资源与提交端点仍待 #18 集成。

`completed_generation` 表示 resize API 已返回，不代表子进程已应用该尺寸，更不代表已绘制或呈现。队列探针另外从真实子进程查询尺寸并有界等待固定预期；两种端点分别检查。

## Paste 后输入的期望

`ImeInputExpectation` 在创建 receiver 时固定已接受的 UTF-8 前缀。提交前必须逐字等于前缀，提交后必须等于前缀加恰好一次“你好”；不能事后把意外输入重新命名为前缀。严格解码后前缀最多4094 UTF-16单元，为“你好”保留2单元；容量没有扩大。

旧五个真实 IME 模式仍使用空前缀，独立校验拒绝其非空前缀元数据。边界单元测试通过不替代未来同次真实 Copy→Paste→IME 组合验收。源字节、ConPTY字节、核心状态、实际绘制提交及呈现时刻必须分别记录；`presented_done` 继续未知，不能由目标20ms间隔或 producer-complete 推导性能。
