# 保留列宽 resize 后的 EL0 / EL2：未通过诊断

本记录保留 `afdcf59f942f2b7cd3607ad7ab9bd8be9dc907dd` 核心上的实际失败：两例共 **40 阶段，24 阶段通过；600 条断言中 96 条失败，退出 1**。测试已成功编译并执行，当前没有相应修复或 GREEN 结果。

`resize_erase_test` 参与默认构建，但未注册为验收 CTest，也没有设置 `WILL_FAIL`。这是一项未解决的内容差异，不因其他测试通过而获豁免；默认 `Screen::resize`、Qt 与完整 #27/#29 也不由本诊断验收。

## 字面来源与测试范围

来源为[两例独立原版擦除记录及补充审计](../../../../research/ui/results/2026-11-02-resize-erase-reference/README.md)：两次原版运行均完成五阶段，但原冻结机械审计失败；随后各自的独立离线补充审计通过，保留原失败且没有新增原版运行。不能将来源写成“两例原 strict 成功”。

初始正文为 **85B**，SHA-256 `a301c656926ae72c07d2c91b67e1734286ce50b7cb38c5b4f6b64f06db0563b3`：在宽屏时先写 L，再写旧尾部 `uv/wx`。它与历史 78B 源、窄屏打印 L 的旧准备失败分开。两例按 `91×20 → 61×20 → 91×20`，依次观察初始正文、缩窄、仅 `CUP(8,61)`、擦除和恢复宽度；各阶段 DSR 为 `11;2 → 11;2 → 8;61 → 8;61 → 8;61`。

| 案例 | 擦除字节 | 原版阶段 3/4 的逻辑文档行 7 | 原版 w/x 位置 |
|---|---|---|---|
| `erase-right` / EL0 | `ESC[0K` | `L + 59 个空格`，长度 **60**；v 消失 | 文档 `(9,90)/(9,91)`，全局 position **175/176** |
| `erase-whole-line` / EL2 | `ESC[2K` | **60 个空格**；L/v 消失 | 同上 |

文档坐标从零开始，DSR 是一基坐标。擦除前 w/x 的 position 为 206/207；擦除后窄屏显示坐标为 `(10,29)/(10,30)`，恢复宽度后为 `(9,90)/(10,0)`。这些是固定源的独立字面观察，不由当前实现推算。

[resize_erase_test.cpp](../../resize_erase_test.cpp) 调用真实 `resize_columns_preserving`，再通过 `TextDocument` 与 `HistoryViewport::project` 分别观察源正文和显示。每例 strict/recovery × 整块/逐字节，共 8 次核心回放、40 阶段。每阶段检查实际网格、DSR、完整逻辑正文及六类 marker 的完整源/显示出现集合，源位置另含绝对字符 offset。

正文比较只忽略**末尾的空字符串容量**；不删除中间空行、不 trim 空格，也不忽略整行空格。两份私有录制的 10 个阶段完整文档数组已逐项重算规范 JSON SHA，与公开补充结果一致，再与测试字面数组核对。此处的逻辑正文不是完整 active grid 或样式网格。

## 实际失败与冻结身份

| 项目 | 固定身份 / 结果 |
|---|---|
| 生产核心基点 | `afdcf59f942f2b7cd3607ad7ab9bd8be9dc907dd` |
| 测试源码 SHA-256 | `8d2d020c43a5c8e6af5e14520a77081384c027d93332258dc10b913535442fad` |
| RED 二进制 SHA-256 | `66053d4bee07489c11fe8ec041b943c23c6910aa0eb307fef5d2d07f5571937e` |
| 构建日志 | `resize-erase-red-build.log`，编译成功 |
| 运行日志 | `resize-erase-red.log`，24/40 阶段、96/600 断言失败，退出 1 |

例如 EL0 擦除后，核心目标逻辑行仍为 91 字符，v 仍在 `(7,90)`，w/x position 仍是 206/207；原版要求目标行长度 60、v 消失、w/x position 为 175/176。恢复宽度后的同类断言也失败，测试没有只检查擦除瞬间或 marker 是否曾出现。

[validation.json](validation.json) 保存执行后立即采集的源码、RED 二进制、构建配置与日志哈希。采集时所有顶层核心 header 均与上述基点逐字节一致；新测试和当时已修改的根 CMake 分列为工作区输入，不能把整棵工作树称作未修改的基点。完整运行前输入 manifest 未留存，明确为 `null`；执行后身份不冒充运行前完整封存。

构建配置为 Windows x64、Visual Studio 17 2022、MSVC 19.42.34433.0、SDK 10.0.26100.0、Release；Qt 与候选适配器关闭。直接复验入口：

```powershell
cmake --build <core-build> --config Release --target resize_erase_test --parallel 1
& <core-build>/bin/Release/resize_erase_test.exe
```

## 尚未决定的语义

当前原版动作恰好同时使用 **61 列网格和第 61 列光标**。观察到 60 字符，不能区分其来源是光标位置、宽度减一还是其他内容策略，因此不据此编写通用 extent 规则。还需中间列等独立样本区分；后续证据或 GREEN 应追加独立身份，保留本次 RED。

本记录不宣称一般擦除、其他光标/尺寸、连续编辑、擦除后下一字符、宽字/组合字符、样式、软续接与帧交叉、Qt/固定 ConPTY、性能或完整 R2 验收。
