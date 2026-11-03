# 缩窄后跨前方重排的 CUP/DSR 字面回放

[原版单例四阶段记录](../../../../research/ui/results/2026-11-03-reflow-addressing-reference/README.md)在 `91×20 → 61×20 → 91×20` 中先于目标行写入一条 123 字符软折行；缩窄使后续显示行整体 +1，而保留的源片段行号不变。原版把 `CSI 8;2H Q` 的 Q 写到文档行 6（保留行口径），DSR 为 `8;3`，窄屏可见矩形 (155,144)、恢复后 (155,126)——显示页独立重排。

本切片把该观察固化为可重复的核心回放门禁：使用既有 `Screen::resize_columns_preserving` 与独立显示投影，逐阶段比较**协议网格、DSR、完整源逻辑行（真实软续接合并）与显示行/marker 位置**。**未修改任何生产源码**：现有同 owner 保留 + 投影表示已与本原版样本一致；默认 `Screen::resize` 与 Qt 仍走物理裁剪，未在本切片切换。

## 结果

| 验证 | 实际结果 | 口径 |
|---|---|---|
| 字面回放 `resize_reflow_addressing_test.exe` | **16/16 阶段、0/620 断言失败，退出 0** | strict/recovery × 整块/逐字节 × 4 阶段；每阶段检查网格、DSR、源逻辑行与显示行/marker |
| 核心 CTest | **35/35 通过，4.11 秒** | 新增 `reflow_addressing_core`；上一基线为 34/34，未改其他测试 |
| 生产源码变化 | **无**：`screen.hpp` SHA-256 与 main `0420137` 相同（`9107356…`），仅新增测试与 CMake 注册 | 不把"实现已一致"写成修复或验收 |

首次门禁运行的 12/16 来自测试自身第 3 阶段预期分支写错（把已写入 Q 的阶段 3 当成动作前），不是生产 RED；保留该记录、修正测试后 16/16，没有跳过失败阶段或删减断言。620 条断言数与最终二进制身份见 [validation.json](validation.json)。

回放逐阶段字面预期（来自原版公开记录）：

| 阶段 | 网格 | DSR | 源行 6 | 显示行（v / Q） |
|---|---|---|---|---|
| 0 初始正文 | 91×20 | `12;1` | `6` | v (5,31) |
| 1 缩窄、动作前 | 61×20 | `12;1` | `6` | v (6,0) |
| 2 缩窄、CUP+Q | 61×20 | `8;3` | `6Q` | v (6,0)，Q (8,1) |
| 3 恢复宽度 | 91×20 | `8;3` | `6Q` | v (5,31)，Q (7,1) |

源行/显示行均以零基记录；显示坐标按 8×18 单元、x=147 原点折算为原版公开矩形，回放只比较单元行列。源逻辑行按真实软续接合并后核对，保留正文 123 字符行在三种宽度下不丢失、不重复。

## 复现命令

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target resize_reflow_addressing_test --parallel 4
& build/bin/Release/resize_reflow_addressing_test.exe
ctest --test-dir build -C Release -R "^reflow_addressing_core$" --output-on-failure
ctest --test-dir build -C Release -R "_core$" --output-on-failure
```

构建/运行环境：Windows 11 x64、Visual Studio 17 2022 17.12.6、MSVC 19.42.34433.0、Windows SDK 10.0.26100.0、Release、Qt OFF、候选适配器 OFF。定向构建后收集源码与 EXE SHA-256，见 [validation.json](validation.json)。

## 边界与未完成

- 只覆盖单个固定 222 B ASCII 主屏样本、三组固定宽度与一个 CUP+Q 动作；不覆盖高度变化、备用/隐藏帧、连续 resize、在途输出、历史淘汰、宽字/结合字/Unicode、样式、margin/origin 或保存光标。
- 只走显式 `resize_columns_preserving` 入口。默认 `Screen::resize`、正式 Qt 呈现、选择和 Find 的源位置迁移，以及 Qt 六阶段 B/CD 失败均未被本切片修复或豁免。
- 原版样本仍是单例、最大一次运行；本页不声称原版内部存储结构、完整 reflow、WindTerm 等价或性能结果。
- #27/#29 保持 OPEN；核心字面回放通过不替代原版、Qt/固定 ConPTY 或 R2 联合验收。
