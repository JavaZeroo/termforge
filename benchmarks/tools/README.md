# 工具 v0.1

Python 3.12 已验证。生成器/播放器仅用标准库；资源采样与校准限 Windows。测试数据目录必须不存在，工具不覆盖旧结果。

在仓库根目录运行：

```powershell
python -m venv .venv
.venv\Scripts\python -m pip install -r benchmarks/requirements.txt
.venv\Scripts\python -m unittest discover -s benchmarks/tests -v
.venv\Scripts\python benchmarks/tools/generate.py --out benchmarks/generated/ascii-8m --kind ascii --bytes 8388608 --variant pseudorandom
.venv\Scripts\python benchmarks/tools/generate.py --out benchmarks/generated/history-1m --kind history --count 1000000 --variant repeated
```

生成器支持 ascii、ansi、unicode、history、longline、overwrite、alternate；`--count` 优先于 `--bytes`。SHA-256 counter 生成伪随机 ASCII，不依赖 Python PRNG 版本。数据按完整记录边界生成，字节数包含末尾 marker。

每个目录包含 `payload.bin`、`manifest.json`、`oracle.txt`。后者是主屏逻辑文本的逐行预期，含 marker；ANSI 样式规则另存 manifest，不能用纯文本 oracle 验证颜色/布局。`longline` 默认正文 4096 字符，属扩展探针，不能标作某个既有正式案例。`alternate` 只生成屏幕切换序列，T05 的 resize/逐步确认仍需交互驱动。

`manifest.json` 最后写入；没有 manifest 的目录视为中断结果。数据和 oracle 流式写入，仅保留最后 39 个逻辑行。发布前仍需实际终端校验。

## 播放

下面的命令应该在隔离的目标终端测试会话中运行；它会输出控制序列。不要运行在日常会话中：

```powershell
.venv\Scripts\python benchmarks/tools/play.py benchmarks/generated/ascii-8m --result benchmarks/local-results/play.json --chunk 65536
```

先手动创建 `benchmarks/local-results/`。stdout 仅输出 payload，JSON 写独立文件，避免 shell 管道转码。`--chunk 1/7/4096/65536` 用于写入边界实验。Win32/PTY 可能再次合并或拆分块，因此这不保证接收端同样的分块。

播放器在计时前预加载并核对文件哈希；该内存属于 producer，不应算进“纯终端内存”。它用 [os.write](https://docs.python.org/3/library/os.html#os.write) 循环处理部分写入。数据写完不意味着终端解析/呈现完成，相关结果保持 `null`。失败时部分写入块的实际字节数未知，只报告完整写入块数。

本版没有节流播放器、PTY 驱动或 SFTP 工具；不能直接执行所有协议场景。

## 资源采样

```powershell
.venv\Scripts\python benchmarks/tools/sample.py --pid 12345 --out benchmarks/local-results/resources.jsonl --seconds 10 --interval-ms 100
.venv\Scripts\python benchmarks/tools/calibrate.py --out benchmarks/local-results/calibration-01
.venv\Scripts\python benchmarks/tools/summarize_resources.py benchmarks/local-results/resources.jsonl --out benchmarks/local-results/resource-summary.json
```

将 12345 换成隔离测试进程 PID。工具不会启动/终止该目标；只读指定树的资源，不读取命令行、连接、文件内容和凭据。

JSONL 有 metadata、sample、schedule_gap、end 类型。采样包含 root、descendant、sampler 分组；进程按 PID+create_time 识别，保留退出/权限错误。CPU 是自首次观察起的差值和 lifetime 原值，内存为 Windows working set (`rss`) 与 private bytes (`private`)；API 依据 [psutil 文档](https://psutil.readthedocs.io/)。轮询可能漏掉短命进程和最后一段 CPU，未知不补零。

资源默认每 100 ms 读取；子进程发现默认每 1000 ms（`--discovery-ms`），避免每次读取都枚举进程树。短于发现周期的子进程可能整个漏掉，不能用本工具声称短命进程开销全覆盖；需要精细生命周期记录时后续增加事件驱动观察。

`collection_ns` 不包含当前 JSON 序列化/写盘耗时，`actual_interval_ns` 和 `schedule_lateness_ns` 反映调度偏差；采样器自身 CPU 单列，不能把 collection_ns 当总开销。工作集之和可能重复计算共享页。

校准工具启动自己的无窗口 CPU/内存负载，以五对交错 on/off 运行观察差异，原始资源样本在本地。它是初步自检，不代替协议要求的 T01 采样/捕获开销验证。

### 资源汇总与校验

`summarize_resources.py` 使用标准库，流式读取 v0.1 JSONL，校验记录顺序、完整结束、时钟间隔、进程身份、CPU 累计值与差值的一致性，以及非负有限数值。拒绝截断、重复进程行、未知版本和不一致数据；校验失败时不生成汇总，已有输出不会覆盖。此工具是资源记录的语义校验器，尚不替代完整运行记录的 JSON Schema。

汇总按 root、descendant、sampler 分组，不包含 PID、创建时间或本机路径，并记录原始文件 SHA-256。公开前仍需检查来源与其他运行信息。

- CPU：对每个 PID+create_time 保留最后观察到的累计差值，再按组求和。进程退出不会抹去已观察 CPU；首次观察前和最后观察后的 CPU 仍未知。
- 核心当量：观察到的 CPU 秒数除以首末样本的时间跨度；不是整机 CPU 百分比，也不是工作负载运行时长归一化。只有一个样本时为 `null`。
- 内存：先求每次采样中同组可观察进程之和，再取峰值，避免相加不同时刻的进程峰值。权限失败或退出行不补零；字段显式命名为 `sampled_peak_observed_*`，不能解释为完整进程树峰值。完全没有观察时为 `null`。
- 质量：保留缺失进程行数、发现错误数、跳过 tick 数、最大采集耗时/实际间隔/调度迟到。`validation_status: valid` 只表示数据内部一致，`performance_gate` 始终为 `not_evaluated`。

测试覆盖进程退出、PID 重用、非同时内存峰值、权限失败、截断及损坏记录，并将真实受控子进程采样结果送入汇总器。采样本身的覆盖和开销限制仍然成立。

## 完成边界

- 已实现：确定性数据、逻辑文本 oracle、字节播放器、Windows 进程树资源 JSONL、合成负载校准。
- 已实现视觉诊断：[自建窗口捕获、序列标记匹配、F13 注入、时钟往返校准](VISUAL.md)，另有实验性的固定像素模板录制/等待工具。
- 已实现资源汇总与语义校验：观察到的分组 CPU、同时采样内存峰值、缺失与调度质量、来源哈希。
- 待完成：WindTerm 实际文本标记/输入驱动验证、T01 捕获/采样开销校准、完整运行记录 JSON Schema、节流/交互驱动。
- #3 保持进行中；任何工具校准均不代表 WindTerm 基线或性能达标。
