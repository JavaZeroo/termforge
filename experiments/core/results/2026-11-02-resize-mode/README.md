# 保留列宽 resize 的活动区域与保存末列：三例字面红绿

提交 `3c3d3f33409df23d163044a5f8202b21c7536090` 将实际列宽变化后的活动滚动区重置为整帧，修正原版三例中的两个活动屏案例。相同的 [resize_mode_test.cpp](../../resize_mode_test.cpp) 从 **32/64 阶段通过、124/2192 条断言失败**变为 **64/64、零失败**；保存主屏末列的案例在修正前已通过，不把它称为新增修复。

本头重新构建的 mode、owner、erase 三个目标，定向 CTest **3/3，通过，0.10 秒**。本记录只归档这次定向验证；完整 core 与 Qt 回归不包含在本记录中，旧头的 24/24、4/4 不能继承。默认 `Screen::resize` 和 Qt resize 路径未切换，不能据此关闭完整 #27/#29 或宣称 R2 验收。

## 原版样本与比较范围

[原版报告](../../../../research/ui/results/2026-11-02-resize-mode-reference/README.md)来自 **3 次独立原版运行、3 份原严格审计通过的记录，共 5+5+6=16 阶段**。各案只有一次 attempt，均使用固定 WindTerm 2.7.0 和合成 loopback Telnet；不是本地 shell 联合测试。C++ 每案执行 strict/recovery × 整块/逐字节，合计 **12 次回放、16×4=64 阶段、2192 条断言**，不是每案各 16 阶段。

| 案例 | 原阶段数 / 核心阶段数 | 原版 DSR 顺序 | 核心 RED → GREEN |
|---|---|---|---|
| `active-primary` | 5 / 20 | `2;12 → 3;12 → 1;3 → 20;3 → 20;3` | 4/20 → 20/20；92 → 0 条失败 |
| `active-alternate` | 5 / 20 | 同上 | 4/20 → 20/20；32 → 0 条失败 |
| `saved-primary-edge` | 6 / 24 | `8;91 → 2;12 → 2;12 → 8;61 → 8;61 → 8;61` | 24/24 → 24/24；一直零失败 |

活动屏两案先明确设置 `2..5` 滚动区和 origin ON，在物理第 3 行写入 MP/AP，再缩窄；未修改光标的 DSR 行从 2 变为 3。随后 `CUP(1,1)` 的 UP 与 `CUP(99,1)` 的 DN 分别出现在可见首行和末行，原哨兵保留。它们约束外部寻址结果；本协议没有区域 LF，不能证明全部滚动行为，也不能区分私有 origin 位的不同内部实现。

保存末列案在主屏第 91 列写 W 后进入备用屏，再缩至 61 列；只发 `1049l` 返回并先独立采样，没有补 CUP。返回正文目标逻辑行是 **61 个空格，W 消失**；随后 X 位于零基源 `(7,60)`、绝对 position 83、显示 `(7,60)`，恢复到 91 列后不变。返回正文的完整数组 SHA-256 为 `d406c228d3792cc9550b15fdc7e4ea405e0be7c4a53d859b3159a5f2ef6da919`，X 后两阶段为 `09574749f53998f85aa66dfbcb8083375c03543ddbdcd89a0ce16e573f1008bc`。

测试调用真实 `resize_columns_preserving` 及 VT/1049 输入，并逐阶段检查实际网格和 DSR。9 个主屏原阶段另比较完整逻辑正文及 marker 源行、列、绝对 offset，形成 **36 次完整正文/source 观察**；这 9 个数组均独立重算规范 JSON SHA，与原严格公件一致。正文仅排除末尾空字符串容量，不 trim 空格、整行空格或中间空行。

全部 64 阶段比较 `HistoryViewport::project` 的可见 marker，使用原测矩形换算的零基位置（原点 `(147,0)`、单元格 `8×18`）。备用屏的 7 个原阶段只比较 DSR 与可见显示，不比较原版额外文档索引，不对候选 source 加一修正。隐藏主屏 W 的负 y 矩形不被当作可见标记。十五类 marker 的完整出现集合同时检查缺失、重复与不应存在的标记；这仍不是全网格、样式或私有模式位比较。

## 冻结身份与真实执行

| 身份 | SHA-256 |
|---|---|
| RED/GREEN 相同的 `resize_mode_test.cpp` | `f44f8fbd09e32aaa19010c2689b72b42a881f993853fbbf7e3a0c59e5afbe7a6` |
| RED `screen.hpp`，生产基线 `ed3b8e8` | `5deae8f71240cc67e2b98fcec794b6f38aa5612b0be116fca10ed584cc81ea29` |
| GREEN `screen.hpp`，提交 `3c3d3f3` | `a71e964128ca894dd55c9eb59517ad95eff552822417383461616a21dbd34329` |
| 保存的 RED `resize-mode-red.exe` | `1931744f3c04d77b1a4f0d6adcf8cad3384781eafdda2471f510d4626b269b30` |
| GREEN `resize_mode_test.exe` | `b91de04f55bcc96dcd646ee621ec0d3af5913833a3ac7c726fbcba8f52310fa1` |
| GREEN `retained_columns_test.exe` | `72d459f8c6e6248cc9fa72a779a34469a57523d71b6231bc0bc426138b28058a` |
| GREEN `resize_erase_test.exe` | `60db11d43124e47b36205e9d25d645e7014253ad539c391672ebcf90cdf68420` |

RED 二进制、`resize-mode-red-identity.json`、构建/运行日志均保存。该 RED identity 是运行后采集的选择文件清单；新增测试和 CMake 工作区更改不冒称已经包含在生产基线 `ed3b8e8`，未保存的完整运行前 manifest 保持空缺。

GREEN 执行后、后续整套构建前，于 `2026-11-02T03:39:40.630524+00:00` 固定 **12 份 core 头文件、3 份实际构建测试源码和根 CMake，共 16 个文件**，逐字节匹配 `3c3d3f3`；三个二进制及 9 份日志/RED 资料另存副本，采集期间原文件未变化。完整身份在 [validation.json](validation.json)，本地副本位于 `local/r29-retention/resize-mode-green-capture/`；其 `identity.json` SHA 为 `6bc43991930eb4e634c78b88cab99e1772401f0ac6354e88784f8d658f0c1432`。这是执行后的身份固定，没有重新执行测试或补造运行前清单。

原始 RED/GREEN 直接运行日志分别为 `local/r29-retention/resize-mode-red.log` 和 `resize-mode-green.log`；对应构建日志后缀为 `-red-build.log`、`-green-build.log`。`resize-mode-green-ctest.log` 单独记录 `retained_columns_core`、`retained_resize_erase_core`、`retained_resize_mode_core` 三项全部通过。owner/erase 在此只是回归通过，不添加新的独立 RED 或原版运行次数。0.10 秒是测试执行时间，不是性能测量。

配置为 Windows x64、Visual Studio 17 2022、MSVC 19.42.34433.0、SDK 10.0.26100.0、Release，Qt 与候选适配器关闭。复验入口：

```powershell
cmake --build <core-build> --config Release --target resize_mode_test retained_columns_test resize_erase_test --parallel 1
ctest --test-dir <core-build> -C Release -R '^(retained_columns_core|retained_resize_erase_core|retained_resize_mode_core)$' --output-on-failure
& <core-build>/bin/Release/resize_mode_test.exe
```

## 工程表示与边界

生产改动仅补充活动帧滚动区的整帧重置；此前隐藏帧已有相同范围处理。本实现继续保留 frame-local origin 设置，因而在零上边界下得到原版相同的 DSR/CUP 结果。保留该设置是工程表示，**本次边界观察没有证明原版私有 origin 位保留或清零**。`retained_columns_test` 的工程范围断言随已观测策略改为整帧；三例原版字面测试没有改预期，erase 字面测试也未修改。

尚未覆盖其他尺寸与高度变化、区域内 LF、完整保存/恢复组合、连续交叉编辑、宽字/组合字符、样式、分配失败、性能、Qt 正式 resize 与固定 ConPTY 联合路径；本页不继承其他源码头的回归结果，也不作完整 R2 验收结论。
