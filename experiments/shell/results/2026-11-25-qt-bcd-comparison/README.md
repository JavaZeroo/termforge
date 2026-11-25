# Qt／固定 ConPTY 的 B/C/D 四阶段同输入对照

以自主 C++ 核心、Qt Widgets 与随包固定 Microsoft ConPTY，在 Windows hosted runner 的 Qt offscreen 中运行独立 `--bcd-comparison`。公开 Qt 键事件驱动一个自有受控子进程。四组子进程 VT 输出及 DSR 查询字节与 [固定 WindTerm 2.7.0 直接 Telnet 记录](../../../../research/ui/results/2026-11-24-resize-bcd-reference/README.md)逐字节相同，网格为 `80×24 → 60×12 → 60×12 → 80×24`。 [#29](https://github.com/JavaZeroo/termforge/issues/29)

每阶段先核对 Qt 请求和 transport 完成，再采集实际 child 收到的 DSR、原生 `CHAR_INFO`、核心保留源行、同次 SessionModel 消费的 transport 字节和 Qt 显示投影。写前和恢复阶段各只输出 DSR。原生网格在读到 DSR 后采样，时间顺序在数据中保持。

## 结果

坐标为零基 `(行, 列)`，DSR 为协议一基。原版 marker 矩形按其 `8×18` 网格和已观测网格原点 `(147,0)` 换算，Qt 取实际投影页的可见行切片。比较的是字面位置，物理呈现保持未知。

| 阶段 | child DSR | WindTerm 与 Qt 的可见位置 | 同次 ConPTY 原生网格 |
|---|---|---|---|
| 初始 `80×24` | `11;2` | `B(7,79)`、`C(9,79)`、`D(10,0)` | 同位置 |
| 缩小后、写入前 `60×12` | `11;2` | `B(7,19)`、`C(10,19)`、`D(11,0)` | 同位置 |
| `CUP 12;1` 写入后 `60×12` | `12;8` | `B(6,19)`、`C(9,19)`、`D(10,0)`、`RESIZED(11,0)` | `B(7,19)`、`C(10,19)`、`RESIZED(11,0)`，原 `D` 所在格被覆盖 |
| 恢复 `80×24` | `12;8` | `B(7,79)`、`C(9,79)`、`D(10,0)`、`RESIZED(11,0)` | `B(6,79)`、`C(8,79)`、`RESIZED(9,0)`，原 `D` 未恢复 |

四阶段的保留源位置为 `B(7,79)`、`C(9,79)`、`D(10,0)`。从写入阶段起，源第 `(11,0)` 格另有 `RESIZED`。Qt 显示页偏移和 WindTerm 滚动观测各是 `0、1、2、0`。Qt、transport、child 的尺寸逐阶段一致。两次 resize 观测各有额外的 `CSI 6n`，写入阶段还记录了前导 `CSI 11;2H`，均保留在 transport 字节记录中。

## 复现与范围

首轮冻结 `b7ef9ce`，四个阶段的源、DSR、尺寸断言通过。离线从原版矩形核对可见行列后，第二轮冻结 `1aafddd`，新增可见行断言并逐阶段通过。两轮的源行、原生行、投影行、DSR 和尺寸相同，各有独立的构建、产物及 Job 记录。 [首轮 CI](https://github.com/JavaZeroo/termforge/actions/runs/45530280243) · [可见行复验](https://github.com/JavaZeroo/termforge/actions/runs/45530643291) · [验证数据](validation.json)

通过现有监督器传入 `--bcd-comparison`。子进程完成四阶段后由公开 Qt 键退出，核对退出码 37、输出 EOF、内外 Job 活动进程为 0。此入口核对保留源正文、真实 DSR、child 尺寸和可见行，保留原生网格作为对照。

固定样本支持保留源坐标与独立显示投影。旧六阶段 `--resize` 使用另一套输入，保留历史失败、原断言和退出码。旧诊断的正式定性、其他应用和 Console API、连续缩放、宽字、历史及选择／Find 交叉仍待处理。 [旧六阶段](../2026-11-05-qt-resize-divergence/README.md) · [寻址决策材料](../../../../docs/research/r29-bcd-local-addressing-decision.md)
