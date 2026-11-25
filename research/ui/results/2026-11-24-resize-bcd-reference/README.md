# WindTerm 主屏双轴缩放后的 B/C/D 寻址

2026-11-24 在专用 Windows 11 x64 VM 上验证了固定 WindTerm 2.7.0 的 `80×24 → 60×12 → 80×24` 直接 Telnet 样本。校准和正式取证分别启动一次。两次运行均通过阶段审计，自有 Job 最终活动进程为 0，专用 VM 显示恢复为 `1024×768`。 [#29](https://github.com/JavaZeroo/termforge/issues/29) · [#27](https://github.com/JavaZeroo/termforge/issues/27)

## 校准与来源

- 固定窗口客户尺寸 `1228×839` 对应 `80×24`，`988×509` 对应 `60×12`。四次 DSR 均为 `1;1`，三个 NAWS 尺寸事件按顺序出现，同尺寸检查点无新 NAWS。校准没有 B/C/D 或 CUP 正文。 [校准数据](calibration.json)
- 正式取证源为 `8442e3abf98caf81d62155f51299d5f65fa427b0`。Python 与 JS 均沿用实测的两组窗口尺寸。旧 `1228×869` 请求得到 `80×25` 的失败及旧计划保留。 [初始几何失败](../2026-11-06-resize-bcd-address-pre-write-failed/README.md) · [旧双轴校准失败](../2026-11-09-bcd-recovery/README.md)
- 首次离线导出因沿用另一探针的标记表报 `Marker text binding differs`。在离线副本中改用本探针的 `B`、`C`、`D`、`RESIZED` 标签并检验顺序，再对同一次录制完成严格导出。正文字节、冻结源、计划及录制保持原样。 [严格导出](addressing.json)

## 四阶段观察

文档行、列从 0 开始，DSR 行、列从 1 开始。`scrollTop` 为 WindTerm 公共接口的观测值。

| 阶段 | 网格 | DSR | `scrollTop` | 文档观测 |
|---|---|---|---|---|
| 初始正文 | `80×24` | `11;2` | 0 | `B` 在 `(7,79)`，`C` 在 `(9,79)`，`D` 在 `(10,0)` |
| 缩小、写入前 | `60×12` | `11;2` | 1 | 三个标记的文档行、列及绝对位置保持，`B/C` 的可见矩形向左移动，`C/D` 的可见矩形向下移动 |
| `CUP 12;1` 后写入 `RESIZED` | `60×12` | `12;8` | 2 | 三个原标记保留，`RESIZED` 出现在 `(11,0)` |
| 恢复 | `80×24` | `12;8` | 0 | 三个原标记及 `RESIZED` 的文档位置保持 |

固定样本支持正文坐标与显示投影分离。旧 Qt／固定 ConPTY 六阶段例子的原生网格在写入后同一行可见 `RESIZED` 和 `CD`，本次原版观察中 `RESIZED` 位于原 `D` 的下一文档行。后续正式 Qt 复验使用本样本相同字节，独立核对源、可见坐标、DSR 与原生网格，旧六阶段断言的处置仍待决定。 [Qt 四阶段同输入记录](../../../../experiments/shell/results/2026-11-25-qt-bcd-comparison/README.md) · [Qt 六阶段记录](../../../../experiments/shell/results/2026-11-05-qt-resize-divergence/README.md) · [方案材料](../../../../docs/research/r29-bcd-local-addressing-decision.md)

自主 C++ 核心以相同字节和行列回放四个阶段，逐阶段核对文档和 DSR，新增 8 个断言全部通过。Windows 本地 GNU 16.2.0 构建的组合回放为 19 个断言，连同两组探针离线测试，定向 CTest 为 3/3。核心回放覆盖文档和协议回复，显示矩形、`scrollTop` 和真实 shell 留待正式 Qt 配置验证。 [核心回放](../../../../experiments/core/resize_dual_axis_test.cpp)

证据覆盖单次直接 Telnet、固定 ASCII、主屏与四个观察阶段。真实本地 shell、原生 ConPTY 网格、备用屏、完整样式及物理呈现仍待各自证据。私有原件保存在专用 VM，随仓库记录的是标量、合成标记位置及来源哈希。
