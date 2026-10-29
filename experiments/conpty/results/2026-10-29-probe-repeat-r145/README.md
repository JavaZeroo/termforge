# 原生命周期探针重复与“测试后采样”（#145）

本片回答一个具体问题：**把 [#145](../..#145) 记录的同一条样本原样重复，或在其它 ConPTY 测试之后采样，能否复现 resize 后仍回复旧尺寸？**

结论：**都没有复现 resize 旧尺寸**；但在“先跑控制台密集测试、再采样”的条件下，复现了另一类红色——探针的会话后**线程基线**在 1 秒结算窗口内未回到基线（4→6），`steady_resources=false`，该样本的 resize 回复本身是正确的（`resized=1`、`TFSIZE:120x40`、child exit 37）。

## 方法

新增 `experiments/conpty/repeat_run.py`：每次迭代复用既有 `run.execute()`（自有 Job、GO 门、原 5 秒等待、原断言），保留每次独立结果；红色迭代保留探针原始失败字段，不重跑取绿。两个 dispatch-only 作业：

| 作业 | 前置 | 采样 |
|---|---|---|
| `windows-2025-original-probe-repeat` | 无（隔离） | 40 次 ×（1 startup + 3 normal） |
| `windows-2025-probe-after-conpty-tests` | 先跑 `conpty_ctrl_c`（5.31s）与 `conpty_margin`（0.31s），两者通过；其退出只记录不设门 | 20 次 ×（1 startup + 3 normal） |

## 结果

| 条件 | run/job | 样本 | resize 旧尺寸 | 其它红色 |
|---|---|---|---|---|
| isolated（hosted windows-2025） | [40847785625](https://github.com/JavaZeroo/termforge/actions/runs/40847785625) | 160 | 0 | 0 |
| 控制台密集测试之后 | [40849464556](https://github.com/JavaZeroo/termforge/actions/runs/40849464556) | 80 | 0 | 1（iteration 10, repeat 1） |
| 对照：克隆探针（#171） | 40841303342 / 40841934880 | 2800 | 0 | 0 |

- 隔离重复：每迭代 1.312 / 1.344 / 1.390 s（min/median/max），40/40 完成。
- 测试后采样：20/20 完成；唯一红色样本为 `valid=false`，原因 `steady_resources=false`：`threads_before=4`、`threads_after=6`、`resource_settle_ms=1011.02`；同一样本 `resized=1`、`output_bytes=608`、`exit_code=37`、`read_error=109`，最终 VT 以 `TFSIZE:120x40:END` 结束。同迭代的 startup 样本因 `startup=true` 豁免基线比较（`handles 45→51` 属已知首次初始化）。
- 未捕获任何 `resized=0` 或 80×24 旧回复。

## 解释与边界

- `#145` 的旧尺寸回复在这三种条件下都没有复现；因此“原样本 × 原宿主 × 隔离或轻度前置”仍不足以触发，根因继续未知。
- 复现的红是**另一类机构**：会话结束后控制台宿主线程比基线多 2 个且超过 1 秒结算窗口。它属于既有 `steady_resources` 夹具合同的边界（首次初始化只对 startup 豁免），与 resize 回复无关；记录为 #38 族的可靠性观察，不并入 #145 根因。
- 本片没有改变原探针、其 5 秒等待、断言或任何产品代码；两个作业都是 `workflow_dispatch`，不进入常规 CI 门槛。
- 分支上曾有“先跑完整 ctest 再采样”的作业，因该 dispatch 作业缺少 build.yml 的夹具准备而在 `benchmark_tools`/`differential`/`workflow_*`/`shell` 失败，未进入采样阶段；该失败保留在 run 40848143873，作为“作业环境不等价”的反例，不用于 #145 推断。

## 复现

```powershell
cmake -S . -B build
cmake --build build --config Release --target termforge_conpty_probe --parallel 2
python -m experiments.conpty.repeat_run --binary build/bin/Release/termforge_conpty_probe.exe `
  --iterations 40 --repeats 3 --mode normal --out build/r145-repeat
```

作业入口见 [conpty-compat.yml](../../../../.github/workflows/conpty-compat.yml) 的两个 `windows-2025-*` 作业。
