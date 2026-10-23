# 第二批：恢复、窗口与连续区间计量

> 历史归档：保留当时的范围、结论和限制，不作为当前待办。当前能力见[矩阵](../../FEATURES.md)，后续任务见[路标](../../PLAN.md)。

日期：2026-10-23（Asia/Shanghai）。实施基点 `51b50ef`，范围为 C2、V1 与 P1 计量修正，不含 A1 shell 整合。来源仅为原创代码、自生成输入和本程序的窗口图像；没有 WindTerm 私有材料或用户桌面截图。

## 本机验收

Windows 11 build 26200、Intel Core i7-9700、MSVC 19.42.34433、CMake 3.31.0、Python 3.12.10，x64 Release。统一根构建 `build/batch2-final`，本机最终源码的 10 套 CTest 共 126 项测试通过；ASCII/Unicode 完整回放各 17 次通过。

- C2：30 项，包括百万字节 ESC/CSI/OSC、非法 UTF-8、随机字节流、各种拆包、取消与 EOF 重入；[契约](../../../experiments/core/RECOVERY.md)。旧核心/Unicode 30 项仍通过。
- C1：95 次运行保持 65 一致、30 精确已知差异，没有放宽差异匹配或参考版本。
- V1：4 项集成测试；另有 8 次固定场景运行与 1 次真实窗口创建后的超时清理。[原始样本](../../../experiments/presentation/results/2026-10-23/samples.jsonl)、[身份](../../../experiments/presentation/results/2026-10-23/manifest.json)。两张自有图像经实际查看，中文、组合重音、颜色和移动后的光标可见。
- P1：9 项，包括真实短区间、失败/超时输出保留及失败 manifest。其余现有工具、ConPTY 生命周期与资源回收回归均通过。

一次合并测试使用了 V1 新增校验字段前的 EXE，4 个窗口组合因缺字段失败；重编译 `presentation_probe` 后该套通过。原始失败仍保留于本地 CTest/合成产物目录。随后新增的计量失败 manifest 用例也做了定向重测。不把开发期间的构建不同步记录隐去或当成通过。

远端 Windows Server 2025 / Ubuntu 24.04 由[根 CI](../../../.github/workflows/build.yml)在干净检出上验证。本轮没有扩大已声明的平台范围，也没有重新启动已知失败的 Server 2022 手工探针。

## 窗口证据与选择

窗口真实存在于本程序的非输入 desktop，`WM_PAINT` 消费只读快照；`WM_PRINTCLIENT` 复制绘制后的自有 DIB，另行记录自有 HWND 的 client DC 捕获结果。固定场景的局部更新、旧光标清除、字体/缺字、前景/背景、重音负例及 HWND 尺寸扩大/还原均通过。

本机 DirectWrite 对实际绘制 layout 观察到 Consolas 与 Microsoft YaHei UI。GDI 的实际 fallback 身份仍未知。所有实际窗口 DPI 为 96；144 只是显式逻辑缩放。没有物理 DPI 切换、核心 resize/reflow、真实 shell、键盘输入或实际用户呈现完成时间。

[ADR-0004](../../adr/0004-presentation-backend.md)临时选择 DirectWrite 接入 A1，依据是正确性与字体可观测性；短时 draw/BitBlt 数据没有支持后端性能排名。`presented_done` 保持 `null`。

## 两个连续区间计量批次

两批在统一构建、其他测试结束后顺序运行。期间本任务未编译或运行其他测试，仍是共享开发机，没有宣称后台负载、电源与调度完全受控。每批 8 配置 × 3 次重复，独立打乱顺序；批次前后各做一次时钟/计数校准。

```powershell
.venv/Scripts/python experiments/measure/run.py --bin-dir build/batch2-final/bin/Release --out local/experiments/core-cost-v2-a --cpu-seconds 2 --repeats 3 --seed 20261023
.venv/Scripts/python experiments/measure/run.py --bin-dir build/batch2-final/bin/Release --out local/experiments/core-cost-v2-b --cpu-seconds 2 --repeats 3 --seed 20261024
```

| 批次 | 原始记录 | 校验与区间 |
|---|---|---|
| A | [身份](../../../experiments/measure/results/2026-10-23-v2-a/manifest.json)、[24 样本](../../../experiments/measure/results/2026-10-23-v2-a/samples.jsonl)、[2 校准](../../../experiments/measure/results/2026-10-23-v2-a/calibration.jsonl) | 全通过，CPU 2.000–2.250 秒 |
| B | [身份](../../../experiments/measure/results/2026-10-23-v2-b/manifest.json)、[24 样本](../../../experiments/measure/results/2026-10-23-v2-b/samples.jsonl)、[2 校准](../../../experiments/measure/results/2026-10-23-v2-b/calibration.jsonl) | 全通过，CPU 2.000–2.234375 秒 |

四次时钟校准的最小正 CPU 增量均为 15.625 ms。该值是观察结果，不是保证的时钟精度。schema v2 使用整个连续区间的一次起终点差值，包含低频门槛检查以及除最后一轮之外的 Parser 析构；预热、前后独立 oracle 和摘要在区间外。中间轮次没有逐一检查。v1 的短轮次累加数据原样保留，计时边界不同，不能直接比较速度变化。

两批所有开启计数的核心样本均为每轮 1,137 次普通分配、累计请求 4,533,686 字节；它们不是存活内存，也不能定位分配来源。计数关闭仍使用替换 `new` 的同一程序，不是无插桩基线。校准量不从核心结果中相减。

例：parse/4096/计数关闭的 CPU ns/byte，A 为 32.80、35.77、33.56，B 为 32.80、34.63、32.73（四舍五入；完整精度在原始记录）。这说明有可复查的重复测量，也仍有波动；不据此选“最快”块长。`parse` 与直接 `state` 是两条完整工作路径，不能相减得到解析器净成本。

P1 仍未完成解码、屏幕更新和历史维护的独立归因，也未覆盖 Unicode、大历史、真正无插桩构建和产品端到端延迟。当前结果不能推导与 WindTerm 的性能距离。

## 下一交付

A1 将 ConPTY、显式 Recovery 与窗口快照接成最小 shell 交互回路，验收普通命令、Ctrl-C、连续输出响应和关闭回收。核心 resize 归 C5；A1 与 C5 合流才是 G1。备用屏幕、滚动区域与完整 TUI 兼容仍有各自的后续任务。
