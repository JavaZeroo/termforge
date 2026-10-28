# Node 栅格纯夹具的有界阶段诊断

对应 [#144](https://github.com/JavaZeroo/termforge/issues/144)。旧 Windows CI 在 `test_raster_native_contract.js` 的原 10 秒等待中超时，没有 Node 阶段记录。这里提供后续有限调查的工具，**没有解释或修复旧超时**；新源码、新宿主或受控挂起的结果不能补写旧样本。

新工具在冻结源码 `02aac45` 上完成了[一次正式三 case 诊断](../../research/ui/results/2026-10-29-node-contract-deadline/README.md)：正常通过、受控挂起超时和受控异常失败分别保留。CLI0仅表示预定义诊断结果齐全，原超时根因仍未知；原始记录留私有目录，公开只投影白名单标量、阶段和哈希。

## 执行与证据边界

这是 Windows 专用、纯合成的诊断入口，不启动 WindTerm、Qt、ConPTY 或输入法，也不访问个人桌面。现有 CTest 仍按原入口执行该 JS；本工具没有自动接入或重试失败的 CI。

`test_raster_native_contract.js` 的原合成输入、七种生命周期情形及断言保留，只添加可选 observer；普通执行时 observer 为 no-op。`raster_style_reference.js` 等原版生产探针、旧计划与旧证据不改动。专用 bootstrap 在加载被测 JS 前等待 `GO`，宿主先将根进程加入新的 kill-on-close Job，再通过保留的根句柄核对归属，最后释放 `GO`。

固定 checkpoint 区分 Node 首次记录、GO、夹具加载、原文件读取、VM helpers、纯检查及七个 lifecycle case 的开始/结束。stderr 使用同步短记录；首条记录之前仍包含 Node 启动和内建 `fs` 加载的未知区间，不能将“未见首条”直接归因于进程创建。Node 单调时钟与宿主时间分别保存，不混作已校准的性能端点。观测开销未校准。

执行预算为原 **10 秒**；进程创建返回后的 Job/GO 设置占用同一预算，不另加等待。同步进程创建本身和准备/哈希不具备整程硬时限，所以 `whole_lifecycle_bound_seconds=null`。超时保留 `TimeoutExpired`、已收到的 stdout/stderr 和最后有效 checkpoint；截断或非法的下一条记录不删除此前有效前缀。检查点的数量、顺序、PID、字段和单调时钟不合约时，不能报告完整成功。

即使全部23个必需 checkpoint 已出现，后续多余或非法记录仍使 `trace.complete=false`，子记录判为 `failed`；正常 stdout、exit0 和 Job 回收不能覆盖协议错误。状态判定也独立拒绝带有 `protocol_error` 的旧完整标记，前缀仍保留供诊断。

回收使用独立、共享的 **5 秒**预算，验证根已回收及自有 Job active=0；不能验证时不派发下一 case。Job 仅约束该可信、无外部代理调用的自包含测试进程及正常继承成员，不是通用沙箱或外部服务回收证明。原错误、退出码、是否强杀与回收结果分别保留；清理成功不会把超时或异常变成通过。

## 固定计划

先提交工具源码，再在新的 `local/` 路径准备不可复用的离线提案：

```powershell
python -B -X utf8 -m research.ui.node_contract_deadline prepare-plan --plan local/node-144-proposal.json --out local/node-144-proposed-output
```

提案默认 `execution_authorized=false`，`run-plan` 在创建 attempt marker 或子进程前拒绝。计划固定 Git HEAD、五个实际源码文件、Node/Python EXE 哈希、实际 `windows_version` / `python_version`、预算和三个顺序 case。`run-plan` 在 attempt marker 前重新读取并严格比较两个版本字符串，漂移时不创建 marker 或输出目录。批次记录使用该次实际读取值，各子观察再记录当时实际读取值，不把计划请求值当实际身份。这仅是两项版本核对，不是完整机器指纹；旧 CI 的实际 Node 身份和缺失阶段仍未知。只在独立审查后，依据既有授权用新的路径准备 `--execution-authorized` 计划；保留原 false 提案，不编辑它。

三个 case 为正常执行、在 VM helpers 开始处受控挂起、在 grab case 开始处受控异常。每份计划最多尝试一次；独占 `.attempted` marker 在成功或失败后均保留，输出目录须不存在。控制异常和挂起仅检验诊断及清理能力，不能证明旧原因相同。

`run-plan --plan <新计划>` 将原始合成记录写入该计划的私有输出目录。正常 case 只有完整检查点、原 stdout、自然 exit0 和回收均成立才通过；另两 case 分别仍为 `timed_out`、`failed`。CLI 只有观察到三种预先固定的诊断结果才返回0，汇总名为 `expected_diagnostic_observations`，不是产品成功或旧缺陷修复。

## 针对性验证

公开边界为计划/launcher 和 checkpoint 记录。`research.ui.test_node_contract_deadline` 检查前缀保留、顺序/PID/字段/时钟拒绝、假 final、false/漂移计划拒绝，以及 Windows OwnedJob 中正常、受控挂起和异常三条独立路径。后两条不能因 Job 清空而转为通过。

原件、环境路径和 stderr 栈留 `local/`；[正式批次的白名单结果](../../research/ui/results/2026-10-29-node-contract-deadline/validation.json)经离线核对后另行发布，不为旧超时填写根因。#144、#38 和 R2 继续开放。

离线出口的公开 seam 是 `export_node_contract_deadline.export`。纯内存回归替换文件字节读取，不启动 Node，也不修改真实原件；normal/throw 的 timeout Job 非空、hang Job 任意额外字段/非整数，以及任一 false 提案的源码或字节漂移必须拒绝。固定批次出口钉住三份 false 原 SHA256/commit，所有 Job 都只投影三项计数；版本、commit、hash、阶段与状态分别受类型、格式或枚举约束。CTest 的 `node_contract_export` 只含纯出口和 trace/launcher 判定类，`node_contract_plan` 仅在 WIN32 注册纯计划类；不注册 `OwnedNodeTests`，不自动运行授权批次。

## 预审发现与限定修正

`5ddecd4` 预审发现：完整23条前缀后追加非法/多余 checkpoint 时，解析错误保留完整前缀，原子状态判定却只检查前缀的 `complete`，可能误报 `passed`。批次层已有拒绝仍不足以保证子记录正确。该缺陷属于新诊断工具，与旧 CI 的10秒超时没有因果关系。

公开 trace-data/launcher 判定边界的两个负例在 `66a0391` 均实际失败：正常 stdout、exit0、清理成功时，非法 JSON 尾和额外事件仍被旧子判定返回 `passed`。修正同时清除错误轨迹的完整标记、在子判定直接拒绝协议错误，并保留23条有效前缀；测试另保留完整合法对照及带错误的旧 `complete=true` 输入。相关纯测试的普通/`-O` 运行身份与原始记录留新的 `local/node-deadline-p2/`，不重跑此前的 Node/受控10秒挂起观察，也不改写旧计划或原件。

实际修正验证源为 `3b74a947f09880d67d12eececa6907b7278e7640`：普通与 `-O` 各8项纯边界测试通过、无跳过。此前的 Node/受控进程观察仍仅属于 `5ddecd4`，不转记为新源码的执行结果；该阶段末尾只补说明和准备新 false 提案，正式有限批次当时尚待审查。

## 版本漂移的预审修正

`0949ac6` 预审发现，计划虽记录 Windows/Python 版本字符串，执行前却只复验了 EXE 哈希，没有核对这两个已声明值。公开计划边界的受控反例在 `cebc530` 将 `platform.version()`、`sys.version` 分别替换为不同字符串，两案均实际到达 attempt gate；测试在该文件边界拦截，未创建 marker、输出或 Node 进程。这不是实际操作系统版本改变，也不能解释旧 CI 超时。

修正将两个当前版本的严格比较放在 marker 前，并在新批次和新子观察中记录实际值。相关纯计划测试的源码身份和日志保留在新的 `local/node-deadline-p3/`；此前 false 提案、原始观察和首次 P2 的纯日志保持原字节，不重新运行 Node 或10秒受控场景。

实际修正验证源为 `b8097064df229e5f6363be5c08a4acb31cd26554`：相关3项纯计划测试在普通与 `-O` 均通过、无跳过；两种漂移均在 marker 前拒绝。此次没有执行新的 Node 或正式诊断批次，也不把先前8/10项验证转记为这一源码的进程观察结果。

## 白名单出口的发布预审修正

`30da569` 的真实公开文件没有泄漏；纯内存恶意输入表明，未校验的非超时 `job_at_timeout` 和未固定身份的 p3 false 源码字段可被公开。red `5c8bc63` 的3项测试有5个失败子例；green `504c0fd` 在公开离线 export seam 拒绝注入，普通与 `-O` 各15项相关纯测试通过。三份 false 的原 SHA256、commit 和未执行状态均固定，非超时项只能缺失/null，超时 Job 只重建三个计数。旧原件、执行五源、旧公开 `db97e95…` 不改；新[派生结果与出口验证](../../research/ui/results/2026-10-29-node-contract-deadline/README.md#离线出口的发布预审修正)只有导出器源码哈希变化。此次不重跑 Node/10秒控制场景、原版、guest 或 CI；这些是新出口缺陷的修复，与旧 CI 超时原因分列。
