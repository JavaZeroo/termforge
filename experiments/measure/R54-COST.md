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

该批只交付准备合同与可完全移除包装的构建机制。两个实际 build manifest/EXE、真实空闲环境证据、冻结计划执行器和重复样本、成本归因、数值预算与优化验收仍由 #54 接续；生成计划或短 sanity 都不关闭该 issue。
