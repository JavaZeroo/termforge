# R1 组合工作流与受控观测

本文件定义 `r108-console-v1` 的数据/控制协议，以及复用它的 E2 八会话 data-only 场景和资源接缝。独立夹具验证生产字节、实际 ConPTY 传输和接收端；E2另行检查真实工作区、界面状态及反复回收。两者均不执行实际系统Copy/Paste或真实IME，不代替 [#18](https://github.com/JavaZeroo/termforge/issues/18) 整体验收。整项合同见 [R1-08](../../docs/milestones/R1-local-text.md#r1-08--验收完整文字工作流与多会话资源边界)，实时完成状态只在 issue 维护。

## 自动复验

在根构建后执行：

```powershell
ctest --test-dir build -C Release -R "^(workflow_fixture|ime_input_expectation)$" --output-on-failure

# Windows E2：全量构建所有helper，并使用已安装项目依赖的Python。
$env:TERMFORGE_BIN_DIR = (Resolve-Path build/bin/Release).Path
$env:TERMFORGE_TEST_ARTIFACTS = Join-Path (Get-Location) 'local/workflow-check'
python -m unittest experiments.shell.test_workflow_sessions -v
```

`workflow_fixture` 包含跨平台纯校验与 Windows 两轮 fresh ConPTY 运行。外层 runner 使用新目录、OwnedJob、`GO` 门控和超时；原生 host 再核对内层 Job、正常退出和回收。Linux 跳过原生运行，不能据此宣称客户端支持。默认产物在 `build/test-artifacts/workflow-fixture/run-*`；显式 `TERMFORGE_TEST_ARTIFACTS` 可改产物根目录。

`workflow_sessions` 的 Windows 驱动为每轮创建新目录、Job和自有非输入desktop，默认执行两个fresh rounds；实际完成轮数、固定源码及回归结果以[公开审计](results/2026-10-24-r1-eight-sessions/validation.json)和[复验](results/2026-10-24-r1-eight-sessions/regression.json)为准。native场景/launcher/外层runner时限分别为70/80/90秒，整组CTest为300秒。`resources=True`仅由此专用驱动启用，普通启动和其他默认执行不采集资源、不写观测文件。

## 角色和负载

| 角色 | 固定内容 | 控制与限制 |
|---|---|---|
| A | `TF_R108_A_0000 中文 é\r\n`，末尾 `e` 加 U+0301；先编号0–159，再160–179，最后180–679 | `output` / `append` / `evict` 依次触发160/20/500行，总680行、18,360字节；不能并发或跳过阶段 |
| B | 无生产日志；原始输入逐次 flush 到接收文件 | 数据测试先发送 `TF_R108_PASTE 中文\rNEXT\r`，再直接发送“你好”，预期32个UTF-8字节；直接 Unicode 不算真实 Paste 或 IME |
| F1–F5 | 每行64字节，含角色、五位编号、中文及组合字符，以点补至62字节再加 CRLF | 每64行一块4096字节，共500块、32,000行、2,048,000字节；块起点目标间隔20ms，实际写耗时与迟到另外记录 |

独立 `workflow_fixture` 数据探针运行 A、B、F1完整、F2提前停止、F2乱序拒绝各两轮；该探针不分别运行F3–F5，也不证明五路同时洪流。五路并行由下述E2工作区场景独立验收。提前停止只接受非空、未满500块的完整块前缀，不制造完成事件。

## E2 八会话 data-only 场景

`ui-workflow-sessions`在真实Workspace中建立A、B、C和F1–F5共8个本地会话；用实际成功窗口创建计数及进程/会话代次验证第9次创建被拒绝。A提供分批编号日志，B接收独立前台输入，C承接受控粘贴取消/关闭，F1–F5同时生产上述有限洪流。生产间隔20ms是负载目标，完成事件只表示生产完成。

场景核对阅读锚点跨追加、运行中Find/完成命中、前台输入、75%缩放/切换、历史淘汰，再进行5次8→6→8循环。每次保留同一6个驻留会话，只重建F4/F5并验证新身份；最终共18个会话身份、26个主检查点。快照、关闭前/最终状态和有界背景观测合计235/256条。受控粘贴通过产品的合成数据入口开始，以真实gated receiver验证发送中取消、关闭的非空精确前缀及无迟到/串会话字节；它不读取系统剪贴板。

`workflow_sessions_checks.py`按固定角色与阶段用独立literal/raw/journal oracle交叉核对实际UI状态、source/receiver文件、会话代次、队列高水位及join/Job/desktop回收；不能把某一零值、生产完成或退出码单独当作整个场景通过。旧窗口须先实际销毁，新会话另有session/generation/creation serial；操作系统再次分配相同HWND数值不等于沿用旧窗口。整个场景不发送真实IME输入，B的前台字面量与独立夹具B的32字节预期分开维护。单项既有CF/IME通过不能拼接成同次八会话完整流程。

固定Windows场景还检查实际预留容量：查询最多4096字节、分配观测预算8192字节，结果最多1024项、分配观测预算2048项；后两者容纳分配器余量，不是`std::string/vector`的跨平台保证或产品性能门槛。搜索元数据最多300个物理行、待处理导航最多1项；元数据行数不等于容量字节。活动粘贴核对源单元数、发送/接受/在途关系、最多4MiB+32字节预留容量及无错误；取消/关闭后核对精确接收前缀、在途归零和容量释放。无法观测的历史/选择容量和实际搜索工作量继续为null。

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

`workflow_checks.py` 用独立字面量生成预期，交叉检查源文件、传输、原始接收、阶段日志、receiver 和 Job。此独立夹具oracle不验收UI range或实际系统Paste确认前零增量；E2的UI/目标校验由工作区oracle另行完成。`.identity`含原生PID/代次，`transport.bin`可能含绝对路径，`stderr.txt`、controller的`stdout.private.json`、E2原始`sample.json`和`_resources-private/`也保留私有。成功及失败上传均排除这些文件，公开结果仅含明确白名单中的合成字节/日志、脱敏`sample.public.json`、资源observations/summary与审核事实。

公开 artifact 可重新计算源文件、接收字节和阶段日志。传输原件不上传，因此外部复核只能读取 runner 的传输审核摘要及哈希，不能声称已从下载产物重新核对该原件。

## 运行快照

输出队列、输入队列和 resize worker 提供在各自 mutex 下按值取得的 `snapshot()`，不暴露活对象引用。输出快照记录实际队列字节、高水位和停止状态；输入还分列排队字节与 worker 持有的整个字符串 payload、拒绝数、启动/join/失败状态。`in_flight_bytes` 不表示内核缓存、尚未写出的精确字节或内存分配容量，不能因排队为零就宣布发送完成。

Resize 分列 requested/completed、实际 working 代次与合并后的最新 pending 代次，最多一个在途加一个待处理；代次差不等于队列深度。停止或失败后未完成的代次仍保留，但不标成待执行。旧公开计数只在producer/writer join后读取；运行中只能使用带锁快照。

Shell的`enable_runtime_observation()`为opt-in；`runtime_snapshot()`只允许UI线程调用，按值返回schema `termforge-runtime-snapshot-v1`，记录采集begin/end时间。UI自有的screen/history、search结果/容量、selection位置、paste进度、viewport、可见/关闭状态与worker短锁快照分列，不提供全文、对象引用、PID/HWND或私有错误原文。多个模块的观测不是整个进程的原子快照；不可观测的工作量/分配容量保持null，不能由声明上限补出测量值。采样不触发复制或推进搜索。

关闭前快照在清除Find/search/paste之前取得；最终生命周期证据只在实际关闭线程join及Session关闭完成后收集。运行中或查询失败的子进程/worker状态保持unknown，不能把尚未读取当成0。场景以实际字节、状态、join与Job查询共同判断回收；单独queued=0不证明内核管道排空。

`operation_observation.hpp`为显式启用的窗口提供最多8条按值滚动记录。记录实际WM_VSCROLL接收、该处理器发布的状态revision，以及包含同一revision且成功BitBlt/GdiFlush的实际WM_PAINT提交；WM_PRINT、离屏绘制、隐藏、被新revision覆盖和关闭均不能冒充提交。未知端点保持null并附原因，`presented_done`始终为null。系统组合场景预先固定两次A会话滚动及实际`110→109→110`内容/身份检查，合成单元测试不替代[同次场景托管验收](results/2026-10-24-r1-system-workflow/validation.json)。

`completed_generation` 表示 resize API 已返回，不代表子进程已应用该尺寸，更不代表已绘制或呈现。队列探针另外从真实子进程查询尺寸并有界等待固定预期；两种端点分别检查。

## 资源采样的范围与质量

`workflow_resources.py`复用`benchmarks/tools/sample.py`和既有资源汇总，不另造sampler。外层runner在实际launcher分配OwnedJob后、GO前核验PID+创建时间并等待首个真实root样本；finally请求停止并join监督线程，失败/超时保留原始错误，不能假报complete。公开scope为`launcher-tree`，root是实际launcher，不能命名为UI-root归因。进程身份在私有原始记录中保留，公开改为PID+创建时间的稳定哈希；PID复用不能合并成一个进程。

`resource-observations.jsonl`使用独立schema `termforge-workflow-resource-observations-v1`，在CI本地与私有raw逐条对比；它不是把假PID写回原采样schema。公开记录保留相对时间、实际CPU/memory/handles、调度缺口及固定错误类别，可离线重算`resource-summary.json`。同一样本的内存/handles求和峰值、退出进程最后已观测的CPU增量均保留；sampler另组，不能计入launcher树；该组是承载runner与采样线程的整个Python进程，CPU不等于采样线程单独开销。全程未观测的指标为null，部分缺失只能称观察下界；working set求和可能重复shared pages。

默认请求间隔100ms、后代发现间隔1s均不是实际采样达成保证；短命后代可能漏采。当前样本中观察进程成本显著、实际间隔与skipped ticks存在缺口，不能称低扰动或用这些数值下性能通过结论。该成本覆盖整个Python监督进程，没有关闭观测的配对实验时不能解释为采样的增量成本。不设置新的性能通过线，也不由短回归推断长期无泄漏。

native另写最多32条`resource-phases.jsonl`（schema `termforge-workflow-resource-phase-v1`），记录实际QPC counter/frequency。完整E2有19个marker，其中每轮`cycle-six`→`cycle-six-end`是同6个身份、无新建的1秒等待区间，之后到`cycle-eight`包含创建。

`workflow_resource_phases.py`使用采样前后各4次`perf_counter_ns→QPC→perf_counter_ns`实际夹测，验证频率与时钟偏移区间交集，保留分辨率和tick误差。公开夹测可离线复算；不能假设时钟同纪元或直接相减两个elapsed。整个样本采集区间必须落在保守阶段内界；CPU还须同身份连续有效样本对，跨创建、缺失或窗口边界不拼接。缺夹测、校准不一致、无有效样本均为unknown；旧产物不能追溯补出校准。五次短驻留比较只描述观察值，不能推出长期无泄漏。阶段接点已在[最终源码的系统组合与同源码数据回归](results/2026-10-24-r1-system-workflow/validation.json)独立验证；证据与性能结论分列。

## Paste 后输入的期望

`ImeInputExpectation` 在创建 receiver 时固定已接受的 UTF-8 前缀。提交前必须逐字等于前缀，提交后必须等于前缀加恰好一次“你好”；不能事后把意外输入重新命名为前缀。严格解码后前缀最多4094 UTF-16单元，为“你好”保留2单元；容量没有扩大。

旧五个真实 IME 模式仍使用空前缀，独立校验拒绝其非空前缀元数据。边界单元测试不单独替代同次真实Copy→Paste→IME组合验收。源字节、ConPTY字节、核心状态、实际绘制提交及呈现时刻必须分别记录；`presented_done` 继续未知，不能由目标20ms间隔或 producer-complete 推导性能。

## 显式系统组合入口与验收边界

`python -m research.ime.workflow_system_runner --launcher build/bin/Release/shell_test_launcher.exe --out local/hosted-ime/workflow-system`只允许指定仓库的一次性GitHub托管Windows环境，普通本机在创建目录前拒绝。通过`r1-ime.yml`显式`mode=verify`调用；共享输入desktop只查询，双GO和内外OwnedJob仍约束所有进程。固定两个全新目录，每轮native/launcher/Python监督上限分别70/80/90秒；失败保留并停止本次重复。

它复用同八会话、26主检查点和18实例，在后台负载下完成实际两行选择、Ctrl+Shift+C、CF_UNICODETEXT、Ctrl+Shift+V、真实确认按钮与隐藏目标接收，再由固定Microsoft拼音以受保护SendInput组合/提交“你好”并取消下一次组合。大粘贴取消和关闭也经过实际clipboard worker。原始前缀固定51字节，提交后57字节，关闭另一目标后继续输入最终58字节；不能清空接收文件或事后重定义前缀。

新增`workflow-system.json`的13个观察点与`workflow-system-progress.jsonl`的28条动作/检查点逐项对照，系统记录总预算512，data-only仍为256且不读取剪贴板/IME。独立oracle核对源/raw/日志、真实选区、候选位置、代次、容量、两个滚动端点与回收；退出码或原生完成标记不能单独通过。系统产物使用独立白名单，私有进程身份、完整监督记录、EXE与任意异常原文不公开。

公开`result.public.json`保留实际GO顺序、Job计数和原始安全流身份；只有通过严格字段及递归重复键检查的两行原生JSON可原字节导出`public/stdout.jsonl`，stderr仅为空时导出。下载后可调用`research.ime.workflow_system_checks.validate_workflow_system_public(case_directory)`直接复核公开事实、所有原始内容与阶段资源，不制造私有record替身，也不依赖上次runner的verified标记。[固定源码独立审计](results/2026-10-24-r1-system-workflow/validation.json)已结合R1-01–07和同源码回归验收有限R1出口；单轮公开记录自身的complete_r1_verified仍为false，不冒充总项结论。
