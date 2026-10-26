# 核心连续区间计量与校准

`core_measure` 使用原创固定输入与独立状态检查。v2 修正了 v1 将很多短轮次 CPU 差值相加的计量问题；`compare.py` 另提供自研/候选 project 与 interactive 成本诊断。解码、屏幕更新、历史仍未完整独立归因，不能据此判断 WindTerm 性能差距。R2 流式字素成本的受控定位与预算由 [#54](https://github.com/JavaZeroo/termforge/issues/54)承接，完整产品性能和跨目标基线仍按 [R4 工作包 #9](https://github.com/JavaZeroo/termforge/issues/9)验收。

#54 的[测量前合同 v1](R54-COST.md)与 `r54_contract.py` 固定 `.3`/`.4` 源码、负载、相邻配对顺序、统计和预算决定程序；新增 Rust `--no-default-features` 测量构建可在编译期排除 allocator 包装。该准备仍缺实际两构建身份、受控重复测量和数值预算，不改变下述历史结果或性能边界。

```powershell
# 先按 CONTRIBUTING.md 从仓库根目录构建。输出目录必须不存在。
python experiments/measure/run.py --bin-dir build/bin/Release --out local/experiments/core-cost-v2-a --cpu-seconds 2 --repeats 3 --seed 20261023
# 另一独立批次；运行期间不要同时编译或执行其他负载。
python experiments/measure/run.py --bin-dir build/bin/Release --out local/experiments/core-cost-v2-b --cpu-seconds 2 --repeats 3 --seed 20261024

# 只运行时钟和普通分配计数校准，不运行终端负载。
python experiments/measure/run.py --bin-dir build/bin/Release --out local/experiments/clock-check --calibration-only --cpu-seconds 1 --repeats 3
```

Linux 单配置构建使用 `build/bin`。每个核心子进程限时 90 秒；单个连续区间内部限时 60 秒。校准子进程包含三个区间，外部限时 200 秒。失败、超时、无效 JSON 的原始 stdout/stderr 与退出状态都写入 JSONL，manifest 标为 `failed`；不覆盖已有结果。

## v2 输入与计时边界

- 固定 1,000 行，每行 76 个 `x` 加 CR/LF，共 78,000 字节；24×80 屏幕、100 行历史。输入在计时前准备。
- `parse` 路径按 512/4096/65536 字节块 feed；`state` 路径直接执行相同字符、移动和滚动操作。这是两条路径的总成本，**不能相减当成解析器净成本**。
- 先按当前配置预热并用独立格位预期检查屏幕、历史、淘汰数与样式。随后重复重建 Parser 和处理输入，直到一个连续区间同时达到 CPU 与墙钟门槛。区间 CPU 是结束与开始的**一次差值**；不累加短轮次的 CPU 差值。
- 区间内包含 Parser 构造、前一轮 Parser 析构、feed/状态更新、循环及低频门槛检查。每 8 次回放读墙钟，CPU 门槛查询间距至少约 250 ms，查询次数记录在样本中；这些开销不做估算扣除。两种时钟的端点之间有一次调用的偏移，不宣称完全同步。
- 最后一轮 Parser 保留到区间外做格位检查与完整 JSON 摘要。因此计入 `rounds` 次构造、`rounds - 1` 次析构；最后一次析构在区间外。只有预热和最后一轮做 oracle，**中间轮次不逐轮检查**。准备、oracle、JSON 序列化、最终析构均不计入区间。
- 8 种配置按固定种子交错，每种默认重复 3 次。默认批次在开始和结束各校准一次，普通分配计数的顺序相反；两个批次用不同种子。调度次序不是隔离运行环境的证明，正式报告仍需记录并发负载与环境。

v2 的字段为 `interval_cpu_seconds` / `interval_wall_seconds`，并记录 `schema_version: 2`、门槛检查次数、时钟身份和实际析构次数。v1 的计量端点和检查策略不同，旧结果保持原样，**不可直接比较数值或拼接统计**。

## 原生候选 project / interactive 对照（#26）

`project` 复用同一 78,000 字节 ASCII 长输出负载，`interactive` 使用 1,000 次 `abcdefgh`、三次左移、覆盖写、退格改写和 CR/LF 组成的 22,000 字节固定输入；两者均为 24×80 屏幕和 100 行历史。交互负载逐格检查 `abcdeQgh`、光标、历史、待换行和空白末行，并固定双方各自的状态哈希。观察关闭只执行构造、前一轮析构、分块 feed 和门槛；观察开启还在每轮生成真实状态投影/JSON、校验哈希并记录分配。`alacritty-measure` 与 `core_measure` 使用相同的连续 CPU/墙钟端点、8 轮墙钟检查间隔和独立最终状态检查。`compare.py` 以固定种子交错双方配置并保存原始 JSONL：

```powershell
cargo build --manifest-path experiments/candidates/alacritty/Cargo.toml --release --locked --bin alacritty-measure
python experiments/measure/compare.py --out local/core-compare-new --cpu-seconds 1 --repeats 2
python experiments/measure/compare.py --route interactive --out local/core-interactive-new --cpu-seconds 1 --repeats 2
```

2026-10-25 [project 正式批次](results/2026-10-25-core-compare/)在 Windows 11 build 26200 上完成 12 组×2 个连续 1 秒样本，未并行构建/测试，但不是隔离机器。纯 feed 的 CPU 中位数：自研 42.61–44.42 ns/字节，候选 28.12–29.70；观察开启后自研 50.89–52.82，候选 539.40–580.09。它说明当前候选全量投影/JSON 成本远高于其纯 feed，不能把核心处理和适配观测混成“语言速度”，也不证明产品路径更快。

同日 [interactive 正式批次](results/2026-10-25-core-interactive/)按 1/16/64/22000 字节分块完成 16 组×2 个连续 1 秒交错样本。纯 feed CPU 中位数范围为自研 64.64–76.49、候选 62.82–90.22 ns/字节；观察开启为自研 89.50–102.21、候选 1834.75–2041.90。交互批次同样只描述固定核心回放和当前观测适配，不是键盘到呈现延迟、真实 ConPTY 或产品 GUI/TUI 验收。

普通 C++ `new` 与 Rust global allocator 统计范围不同，不是等价内存指标；peak working set 包含整个进程准备/检查。观察开启/关闭不是无插桩 A/B，任何差值都不扣除。当前仅覆盖固定 ASCII 长输出与编辑回放，无插桩对照、真实输入/接收/呈现、跨平台与维护成本另审；`performance_gate=not_evaluated`，不能据此作最终路线决定或 WindTerm 性能结论。

候选 `.4` 为完整 trace 投影新增 `grapheme_overflows: 0`，因此 project / interactive 的全对象摘要分别从 `11966868740884721388` / `8035857386658397897` 迁移为 `16178973312633948535` / `6154007251599136798`。屏幕、历史、光标、样式、模式和回复逐字段不变；字段继续进入哈希，不排除计数器来维持旧摘要。独立构造的完整固定负载状态、旧/新摘要迁移及实际 trace 对象由 `test_compare.py` 验证，实际计量批次继续覆盖全部块大小与观察开关，错误摘要继续拒绝。旧结果原样保留；首次 CI 红反馈及身份见[迁移记录](results/2026-10-26-projection-digest/validation.json)。这次修复仅更新诊断身份合同，没有改变产品、负载、计时或性能验收标准。

## 校准能回答什么

`core_measure calibrate seconds [observe_first]` 提供两个可重复诊断：

1. 无终端工作负载的时钟读取循环，记录 CPU 读数的最小/最大正增量、正增量次数，以及墙钟包围一次 CPU 读取所观察到的跨度。它包含循环、查询、记账与门槛检查，平均值不能当成单独 API 的净调用成本；观察到的最小增量也不是时钟分辨率的保证。报告区分 API 表示单位与实测步长，例如 FILETIME 的 100 ns 表示单位不意味着 CPU 以 100 ns 更新。
2. 普通 `new(64)` / `delete` 合成循环，计数开/关各运行一个连续区间，并交替运行顺序。开启时独立检查次数等于循环次数、请求字节等于次数乘 64。它帮助识别计数开销与时钟波动，不能代表终端的真实分配分布。

校准结果写在 `calibration.jsonl`，不从核心时间中扣除。计数开/关都使用同一个替换 `new` 的二进制，**关闭计数不是无插桩产品基线**。真正的无插桩构建、不同诊断策略与更低噪声环境仍属后续工作。

## 内存与尚未测量的层

`ordinary_allocations` / `ordinary_allocated_bytes` 是区间内普通 C++ new/new[] 的累计请求次数/字节，不包含对齐分配、C/runtime 分配，也不是存活内存。Windows 的 peak working set 是整个进程峰值，包含准备和检查；Linux 暂为 null。

接收和呈现不在这些核心探针中，保持 null；屏幕更新与历史维护仍混合。`core_measure` 负载为 ASCII 滚动，`compare.py` 的 interactive 另含固定 ASCII 编辑回放；二者都不代表真实输入到呈现延迟。剩余的解码/状态/历史独立归因、Unicode 和不同历史规模、无插桩对照及受控重复样本由 [R4 性能工作包 #9](https://github.com/JavaZeroo/termforge/issues/9)承接；不得据此发布速度排名或认定数据结构瓶颈。

## v1 历史结果（未改写）

2026-10-22 的 [身份](results/2026-10-22/manifest.json) 与 [原始样本](results/2026-10-22/samples.jsonl) 保持原样。当时存在同时构建与测试活动，且采用有缺陷的短轮次 CPU 累加端点；达到累计时长门槛不能证明无偏，分配计数也不能证明它主导耗时。v2 独立保留新身份、schema 与数据，旧结果不用于速度排名。

## 自动验证

根 CTest 的 `measure` 套件覆盖两种路径与块边界的最终状态、CPU/墙钟门槛、端点字段、校准分配 oracle、参数拒绝，以及非零退出、坏 JSON、超时的原始证据保留。测试仅跑短 sanity，不断言某个平台速度，也不替代正式批次。

v2 历史批次的环境、原始记录、波动和结论边界见[第二批验证记录](../../docs/archive/validation/second-batch-2026-10-23.md)。
