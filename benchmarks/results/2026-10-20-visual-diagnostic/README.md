# 自建窗口视觉链路诊断：2026-10-20

**状态：链路功能跑通，性能测量门槛未通过。没有 WindTerm 测量，也没有物理输入到屏幕延迟结论。**

在同一 Windows 11 / Python 3.12.10 环境，使用自建 Tk 窗口、SendInput F13、可见桌面 GDI ROI 捕获。每批重新启动窗口，各执行 10 次无主动延迟和 10 次主动延迟 80 ms 的输入，两批独立运行共 40 次全部观察到预期序列标记。

| 指标 | 批次 A | 批次 B |
|---|---:|---:|
| 无主动延迟：捕获观测延迟中位数 | 14.65 ms | 24.82 ms |
| 主动延迟 80 ms：捕获观测延迟中位数 | 99.81 ms | 98.51 ms |
| 两组中位数差 | 85.16 ms | 73.69 ms |
| capture-only 调用中位数 | 8.20 ms | 8.20 ms |
| 匹配轮询实际捕获间隔中位数 | 16.27 ms | 16.36 ms |
| 实际捕获间隔最大值 | 24.99 ms | 24.59 ms |
| 最小 RTT 时钟偏移区间 | [-36.2, 27.5] μs | [-36.0, 26.0] μs |

两组差值与注入的 80 ms 延迟在本次捕获粒度下相符，但没有建立严谨误差模型，不视为精度认证。无延迟组的批次波动也说明采样/显示相位会影响结果。微秒级时钟区间不代表显示端有微秒级测量精度。

8 ms 是轮询 sleep 参数，不能当作捕获帧率。本轮实际采样粒度不足以可靠区分几毫秒差异。下一步需要评估低扰动捕获后端或其他端点观测方法，并在 T01 路径完成捕获/采样 on/off 对照。

每组仅 10 个样本，P95/P99 保持 null。只记录中位数和原始事件，未做尾延迟或“开销小于 5%”声明。capture-only 的方法耗时不等于其对被测程序的扰动。

## 可检查证据

- [批次 A summary](a/summary.json)、[全部捕获记录](a/frames.jsonl)。
- [批次 B summary](b/summary.json)、[全部捕获记录](b/frames.jsonl)。
- 两批 source_sha256 与当前用于此次实验的三个工具文件一致。
- 文件仅包含合成事件、时钟、版本和哈希，没有图片、个人路径、PID、会话或服务器内容。

```powershell
.venv\Scripts\python benchmarks/tools/validate_visual.py benchmarks/results/2026-10-20-visual-diagnostic/a
.venv\Scripts\python benchmarks/tools/validate_visual.py benchmarks/results/2026-10-20-visual-diagnostic/b
.venv\Scripts\python benchmarks/tools/calibrate_visual.py --out benchmarks/local-results/visual-new --events 10
```

校验器检查捕获时序、事件差值、汇总数、时钟区间和样本门槛，不将“记录一致”解释为“性能基准合格”。15 项 unittest 通过，包含旧标记/部分更新拒绝、RGB 模板、Windows INPUT ABI、时钟区间和篡改延迟拒绝。

## 调试过程与覆盖边界

首次尝试因 Windows venv 启动器 PID 与实际窗口解释器 PID 不同而中止，没有注入或有效样本。随后改为只接受已启动进程树内的实际 fixture PID，并在运行中校验身份。两次早期调试跑通后，调整 watchdog 以覆盖允许的较长测试；这里公开的是最终工具版本的两次独立运行，没有将早期数据合并。

当前没有记录实际刷新率、完整 GPU/驱动、电源或温度条件，不满足正式产品基准环境要求。仅可复现工具链流程，不能跨机器比较表中数字。

固定像素模板观察器已经实现，但只做了像素匹配单元测试，未在 WindTerm 中验证；它不做 OCR，不能据此认定终端历史无丢失。正式目标适配、T01 测量和整体开销校准均待完成。
