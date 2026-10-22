# 接收批量与通知合并实验

这是独立 C++17 机制实验，不依赖 WindTerm、Qt 或反编译实现，不决定完整产品技术栈。Windows 匿名管道连接生产线程与读取线程；读取线程将数据送入有界队列，消费线程逐字节比较确定性输入。只有全部字节顺序、内容、总数及计数约束通过，样本才有效。

## 自动执行

需要 Windows、Visual Studio 2022 C++ 工具链、CMake、Python 3。仓库根目录运行：

```powershell
cmake -S experiments/receive -B build/receive -G "Visual Studio 17 2022" -A x64
cmake --build build/receive --config Release
.venv/Scripts/python experiments/receive/run.py --out local/experiments/receive-new
.venv/Scripts/python -m unittest experiments.receive.test_receive -v
```

结果目录必须不存在。每次独立进程超时 60 秒，失败立即停止并保留已完成样本和失败记录，不自动重试。`manifest.json` 记录源码/程序/运行器哈希；`samples.jsonl` 保留每轮；`summary.json` 汇总中位数和范围。无需 GUI 或逐轮确认。

## 实验协议

- 固定字节流：xorshift32，初值 `0x12345678`，依次 XOR 左移 13、右移 17、左移 5（32 位截断），取低字节。输入在计时前生成。
- 生产线程每次请求写 16 KiB；真实 `ReadFile` 请求上限分别为 512 B、4 KiB、64 KiB。记录成功非空读取的次数与最小/最大长度，EOF 不计入读取数。
- 对照 `each` 每批 `notify_one`；`edge` 仅队列由空变非空时 `notify_one`。这不是 Qt 排队信号模型，通知次数也不是 OS 唤醒次数。
- 所有队列状态与 EOF 状态受同一互斥锁保护，消费者以 `非空或 EOF` 为条件等待。消费者取出数据后唤醒等待空间的读取线程。EOF 额外通知一次，不计入数据通知数。
- 队列有效载荷上限 256 KiB；每次消费者只取一块，队列外还有读取块、消费块和 OS 管道。vector 容量可能大于有效长度，元数据、输入 fixture、延迟样本也占内存。**该上限不是进程总内存上限**。进程峰值工作集另行记录，包含计时前初始化。
- 每个配置先跑 131,101 字节、队列容量等于读取上限的正确性场景，覆盖非整块 EOF 与紧容量背压。然后每组合 1 次预热、3 次测量，固定种子随机化每轮顺序。总计 24 次正确性、24 次预热、72 次测量。

| 模式 | 负载 | 人工调度 |
|---|---:|---|
| stream | 8 MiB | 无休眠 |
| burst | 1 MiB | 生产每 64 KiB 请求休眠 2 ms |
| slow | 256 KiB | 消费每块请求休眠 100 us |
| yield | 1 MiB | 每次入队和消费主动 yield |

sleep 是请求值，Windows 实际休眠受时钟粒度与调度影响，不能把 slow 模式当成精确 100 us 处理成本。它按块引入延迟，较小块会人为放大总等待，只用于积压和唤醒压力验证，不能用来证明大块的真实产品收益。

## 指标与限制

计时从创建线程前开始，到读取结束、生产与消费线程全部 join 后结束，包含字节比较、队列分配、锁、时钟采样、延迟样本增长和线程创建。输入生成与最后排序不计时。CPU 是同一区间进程 user+kernel 时间，Windows 计量粒度会使短样本出现 0 或量化台阶。

`queue_to_validated_block_*_us` 从获得队列空间后入队前开始，到该块字节校验结束；不含生产等待、管道等待和读取端等待队列空间。按块等权，以排序后 `floor((n-1)*q)` 取分位数。汇总报告中的 p95 是各运行 p95 的中位数，不是所有字节或所有运行合并后的 p95。不能解释成按键或显示延迟。

逐字节比较可以发现本次运行的丢失、重复和乱序；有限调度测试不构成所有并发交错无丢唤醒的证明。未引入竞争检测器、故障注入、多个生产者/消费者、真实 ConPTY、SSH、解析、历史或渲染。这里的条件变量和按块消费策略也不等于 WindTerm 实现。

本轮结果见[实验报告](../../docs/research/receive-experiment-2026-10-22.md)。性能数字仅用于指导下一项实验，不作为 WindTerm 对照或产品预算。
