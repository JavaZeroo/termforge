# R2 流式字素成本：测量前合同 v1

本合同是 [#54](https://github.com/JavaZeroo/termforge/issues/54) 的测量准备，不是性能结果。机器可读来源为 [r54_contract.py](r54_contract.py)；执行前生成独立计划，固定其提交、`protocol_sha256` 和 harness 文件哈希。计划故意保留 `builds=null`、`environment_evidence=null`：源码身份和二进制哈希不等于已证明二进制来自该源码。实际构建、环境与运行记录必须另附，不能以计划生成成功宣布 #54 完成。

## 固定比较对象

| 对象 | 核心源码 | trace 全对象摘要：project / interactive |
|---|---|---|
| 修前 `.3` | `65988f5f20f62294ff52fcac5b5b81e563f93f3b` | `11966868740884721388` / `8035857386658397897` |
| 修后 `.4` | `3d6d44d3b3e9a9a3e41da98e8711e9f19a211fd8` | `16178973312633948535` / `6154007251599136798` |

两份固定核心使用**同一份已审核的测量 harness**；只把本合同提交的根 `Cargo.toml` 与 `src/measure.rs` 应用到相应候选目录，记录覆盖前后文件哈希与 diff。保留各自原有 `Cargo.lock`、`src/main.rs` 和整个 vendor 树；`.3` 没有新增计数器字段，因此全对象摘要不同，不能统一成一个摘要或略掉新字段。若覆盖产生这些文件之外的变化，停止并审查新配置，不把它算进 v1。

每份用 `cargo build --release --locked --bin alacritty-measure --no-default-features`，独立 target 目录；记录完整命令、退出、编译器/链接器和 Rust target/toolchain、构建环境变量、锁文件、完整源码树身份及 EXE SHA-256。记录 `--identity` 的实际 JSON，但该 JSON 只说明编译配置，不证明源码与二进制绑定。构建步骤的记录和独立哈希审计仍不可省。该对照是 headless `Term/Processor` 层，系统/官方 ConPTY 后端不会进入这些成本数值。

## allocator 和观察端点

`measure-allocation-counting` 是默认 Cargo feature，原命令继续编译历史 Rust global allocator 包装。`observe=0` 只关闭计数和逐轮投影，仍不等于无包装。新增 `--no-default-features` 模式在编译期完全排除 `#[global_allocator]`、atomic 计数器和转发路径；`--identity` 与每个结果都给出 `allocator_wrapper_compiled=false`。该模式的 `observe=1` 仍执行原本的每轮快照/JSON/完整哈希检查，但分配次数/字节均为 null。标准 allocator 的内部开销仍存在。

两个模式都保留区间计时门槛，所以 `counter_off_is_uninstrumented=false` 不改成 true。未执行真正产品级无测量探针对照前，不称为整体“无插桩”。C++ `core_measure` 的替换 new 仍在；本合同没有移除它，也不将 C++ 普通 new 和 Rust global allocator 请求视为等价指标。

为定位包装开销，另做同一固定核心上的 wrapper 开/关 × projection 开/关对照，记录独立二进制身份、次序、全部原始成本与正确性。该诊断不与主 A/B 样本合并，也不从主结果扣掉估算开销。

## 负载、区间与顺序

- project 为 1,000 次 `76*x + CR/LF`，78,000B；分块 512/4096/65536B。
- interactive 为 1,000 次 `abcdefgh`、三次左移、`Z`、退格、`Q`、CR/LF，22,000B；分块 1/16/64/4096/22000B。4096 保留原 12 样本的诊断粒度，其他边界继续覆盖。
- 两者屏幕 24×80、历史 100 行。计划写出完整字节 SHA-256；ASCII 成本负载不代表完整 Unicode 工作流。
- 每个样本至少 2 秒 CPU 且至少 2 秒墙钟，一个连续区间；Windows CPU 为 `GetProcessTimes` kernel+user 的单次结束减开始，Unix 为 `getrusage RUSAGE_SELF` user+system。保留 8 轮墙钟检查与至少约 250ms CPU 查询间距，内部 60 秒、子进程 90 秒期限不放宽。
- 区间含每轮构造、上一轮析构、feed/状态和门槛；输入准备、预热、最终 oracle、结果序列化与最后一次析构在区间外。预热和最终完整状态检查必须成功，中间轮次不逐轮检查。
- 主 A/B 固定 `observe=0` 且无 allocator 包装；两批独立开始的种子为 20261026 / 20261027，每个 route/chunk 各 7 对，单批 112 样本。组顺序冻结为计划中的完整数组；同组两个源码相邻，每轮交替 AB/BA，不根据结果改变次序。
- 112 来自 8 种块配置 × 7 对 × 2 源码，单批最低约 224 秒 CPU，而非长时间 soak。保留两次独立批次用于暴露宿主漂移，7 对便于同时报告配对中位数和离散度；这些选择不保证统计显著性。全块配置防止只在一个 4096B 粒度上推导普遍成本，本合同不自动展开更多负载或无限重跑。
- 记录 OS/build、CPU、电源策略、toolchain、flags、实际二进制、背景 CPU 与已知并行任务。构建和其他测试结束后才能启动正式批次；交错顺序不能证明宿主空闲，共享宿主始终注明限制。不同宿主/电源/编译环境不能无说明拼成同一批。

## 正确性、统计和预算程序

主统计为**同 repeat/route/chunk 配对的 candidate/baseline CPU ns/B 比值的中位数**；同时列出全部配对比值、每组样本 CPU 与墙钟 ns/B 的中位数、最小/最大值和中位绝对偏差。两批、各块大小分别报告；不混合 workload/块大小形成一个总分，也不以比值的中位数冒充两个独立中位数之比。样本量小，不自动生成有保证的置信区间或“显著”结论。

任何非零退出、超时、错误状态/负载/编译身份或缺失端点字段都保留原 stdout/stderr 与当前 manifest，将批次标失败并停止。不原地重跑、丢弃异常值、补替代样本或只保留最快批次。噪声或两批方向不一致时保留不确定性；下一次调查有新身份和明确假设，不能偷偷替换原批次。

**v1 尚无数值回归预算，`performance_gate=not_evaluated`。** 先用受控基线和开销对照了解成本/噪声，在单独可审查记录中说明用户交互与正确性理由、环境、可接受 project/interactive 限值及裁决规则，然后冻结预算；之后才能评估拟提交优化。不能看到优化成绩后把阈值改成刚好通过。#34 的产品决定仍需要真实 GUI/输入/并发/长期资源证据，本 headless 合同不能单独代替。

若优化 segmentation、宽度、Cell 保留、投影或分配，先有同一输入上可证伪的成本归因对照，不能只凭总吞吐推断某一层；完整控制边界、Unicode 17 语料、全部输入分块、实际 glyph、原文选择/复制及取消边界继续回归。接收/输入延迟/实际呈现端点仍为 null，不声明 WindTerm 等价。

原 [12 样本](../shell/results/2026-10-26-streaming-graphemes/cost/) 及其二进制/源码身份保持原样；共享宿主、allocator 包装和旧顺序都不同，只作为历史诊断，不能升格、覆盖或与 v1 合并统计。

## 自动入口和当前缺口

```powershell
# 不执行构建或负载；输出文件必须不存在，父目录先准备好。
python -m experiments.measure.r54_contract --out local/r54-plan-v1.json

# 纯计划、oracle、身份和失败负例检查；不占用正式计时环境。
python -m unittest experiments.measure.test_r54_contract.CostPlanTests -v

# 两个实际构建完成后才设置本进程环境，执行短 sanity；不是正式样本。
$env:TERMFORGE_R54_WRAPPED = '<default-feature alacritty-measure.exe>'
$env:TERMFORGE_R54_UNWRAPPED = '<no-default-feature alacritty-measure.exe>'
python -m unittest experiments.measure.test_r54_contract.OptionalAllocatorBuildSmoke -v
```

## 冻结批次执行器（Windows）

[r54_batch.py](r54_batch.py) 执行原 v1 的完整种子顺序，没有重试、补样或缩短时长开关。它只读取已有构建；不会在计时阶段编译。`samples_complete` 只表示样本及顺序通过验证，外层 `complete_diagnostic` 还要求构建绑定与本节环境检查通过；二者均保留 `performance_gate=not_evaluated`，不等于性能验收。

执行前需要保留以下私有构建目录。其 v1 格式由 [r54_inputs.py](r54_inputs.py) 检查，不接受只填源码 SHA 和 EXE SHA 的简化声明：

- `build-plan.json`：`termforge-r54-local-build-plan-v1`，包含原计划文件哈希、共同 harness 提交、编译器/工具版本、环境变量、完整构建命令、两份原提交/树及每个候选目录跟踪文件覆盖前后的 SHA。
- `source-baseline/`、`source-candidate/`：固定 `.3/.4` checkout，只覆盖根 Cargo.toml 和 src/measure.rs；原锁文件和 vendor 不变。
- `baseline-build.json`、`candidate-build.json`、各自 `*-build.stdout/stderr`、`*-harness.patch` 和 `*-identity.stdout/stderr`：实际构建、原始日志、覆盖 diff 与编译模式。
- `build-audit.json`：原构建审计，包括 receipt 哈希、实际 linker 路径和 SHA、核心来源、版本与二进制身份。执行器重新核对当前源文件、全部日志和 EXE，再实际运行 `--identity`。这是一组可审计记录，不是二进制来源的密码学证明。

当前绑定明确支持已有 Rust 1.92.0 / x86_64-pc-windows-msvc / Release / locked/offline / no-default-features / jobs=1 / 独立 target 配置；其他构建需要另行形成记录和审查，不能静默混入。该准备格式及构建产物保持在 `local/`，不包含在公开 CI 的默认产物中。

环境声明 JSON 必须显式填写 `host_kind`（`shared_workstation`、`hosted_vm` 或 `dedicated_host`）、`known_concurrent_work` 字符串列表和 `no_known_concurrent_build_or_test=true`。声明不能取代实际观测。源工作树必须已提交且干净，输出目录必须不存在。

实际 Windows 采样依赖 [benchmarks/requirements.txt](../../benchmarks/requirements.txt) 固定的 psutil；纯统计和计划检查仅依赖 Python 标准库，不因导入环境统计函数而要求安装宿主采样依赖。

```powershell
python -m experiments.measure.r54_batch --plan local/r54-batch/plan-v1.json --build-bundle local/r54-batch --environment local/r54-environment.json --seed 20261026 --out local/r54-run-20261026
# 第二批另行启动，使用原第二种子和新目录；第一批失败时先保留并调查原因。
python -m experiments.measure.r54_batch --plan local/r54-batch/plan-v1.json --build-bundle local/r54-batch --environment local/r54-environment.json --seed 20261027 --out local/r54-run-20261027
```

环境护栏 `termforge-r54-environment-guard-v1` 在任何正式成本样本之前冻结：一次观测十个连续约一秒区间，**每个**区间全机 busy CPU 必须不超过 5%；任一区间失败即保留完整预检，不启动成本负载，也不循环等到一次通过。该 5% 是保守的环境诊断筛选值，**不是用户批准的性能回归容差或 WindTerm 等价标准**。两批之间仍需重新观察；宿主背景工作发生变化是新环境证据，不能替换原失败。

Windows 1–64 逻辑 CPU 上使用 [GetSystemTimes](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getsystemtimes) 原始 user/kernel/idle 计数，busy 为 user+kernel−idle，保留原始计数；kernel 已含 idle，不重复累加。每个子进程前后继续记录全机计数，将全机 busy 减去**内层**测量 CPU 后的余额作为背景活动的保守上界估计（包含测量区间外的准备、oracle、序列化及 runner 工作）。余额为负或其占全机总 CPU 时间超过 5% 时，保留该样本并停止批次。不从主样本成本扣除该余额，不把它当成精确的进程归因。该护栏不能证明虚拟机物理宿主独占、频率稳定或完全无干扰。

原始目录保留 OS build/UBR、CPU、逻辑/物理核数、电源策略前后、进程 CPU 身份、并行工作声明、所有环境区间及执行器源码身份。进程记录不读取命令行或秘密环境变量，但仍属私有证据；公开前须脱敏审核。

每个计划位置先落盘命令和编号，再把 stdout/stderr **直接写入二进制文件**；超时、无效 UTF-8、坏 JSON、非零退出和 oracle 错误都保留原字节与 manifest。子进程超时按原 90 秒处理；监督对象是不会再派生子进程的测量 EXE，不据此声称 Windows Job 回收。中断保留当前位置；不存在续跑或覆盖旧目录的入口。每个有效样本重查 EXE 哈希，结束后再核对源/构建证据及电源策略。

全部 112 个位置完整有效后才生成统计；缺失、重复、乱序或失败不能得到成功摘要。摘要逐批、逐 route/chunk 列出七个配对 CPU ns/B 比值，以及各源 CPU/墙钟 ns/B 全部值、中位数、范围和 MAD。合成进程测试覆盖整个顺序、原始字节和停止规则，其虚构时长只验证统计与进程协议，绝不作为性能样本。

实际受控重复样本、allocator/projection 扰动归因、预算提案和优化验收仍由 #54 接续；构建、短 sanity、执行器测试或环境预检均不关闭该 issue。

独立 Windows 虚拟机上的来源构建、顺序执行、白名单产物和宿主限制见 [R54 hosted 流程](R54-HOSTED.md)。该入口复用本合同，不把CI可运行或环境预检通过解释为产品性能验收。
