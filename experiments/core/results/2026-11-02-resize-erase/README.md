# 保留列宽 resize 后的 EL0 / EL1 / EL2：五例字面红绿

提交 `ed3b8e8c1825dc3a952e3ceced244a3b97209a46` 修正保留列宽后的行擦除：EL0/EL2 收拢文档终点，EL1 保留已用内容范围中的擦除空白。五个独立原版案例的完整回放从 **56/96 阶段、200/1440 断言失败**变为 **96/96、零失败**。首批仅两个末列案例的 **24/40 阶段、96/600 断言失败**仍按其原身份保留。

`resize_erase_test` 参与默认构建，并已注册 `retained_resize_erase_core`；该头重新构建后的定向 CTest **1/1，通过，0.05 秒**，未设置 `WILL_FAIL`。默认 `Screen::resize` 和 Qt 路径未切换；本页不继承旧头的核心 24/24 或 Qt 4/4，不声明本头完成整合回归、完整 #27/#29 或 R2 验收。

## 字面来源与测试范围

五例分为三组：[两个末列 EL0/EL2](../../../../research/ui/results/2026-11-02-resize-erase-reference/README.md)原冻结审计失败后分别补充通过；[两个中间列 EL0/EL2](../../../../research/ui/results/2026-11-02-resize-erase-interior-reference/README.md)原审计与严格导出通过；[较早 EL1](../../../../research/ui/results/2026-11-02-resize-address-reference/erase-line-start-supplement.json)原失败后独立补充通过。合计 **2 个原 strict + 3 个补充审计案例**，共五次独立原版运行、24 个原版阶段；不把原失败改写为原 strict，也不因核心回放新增原版运行计数。

EL0/EL2 四例使用同一 **85B** 源，SHA-256 `a301c656926ae72c07d2c91b67e1734286ce50b7cb38c5b4f6b64f06db0563b3`：在宽屏先写 L，再写旧尾部 `uv/wx`。两组只将准备位置分别固定为 `CUP(8,61)` 和 `CUP(8,31)`，均分开观察初始、缩窄、CUP、擦除、恢复五阶段。前两阶段 DSR 为 `11;2`，后三阶段分别为 `8;61` 或 `8;31`。

EL1 使用独立 **78B** 源，SHA-256 `c7dd4a7fa283dd0003ca8b2ae13dcb46f143a4717bf5a14eb5f5acb068a9fa1c`，没有预置 L；缩窄后直接发送 `ESC[1K`，不补 CUP。它的四阶段 DSR 都是 `11;2`。所有案例只改变 `91×20 → 61×20 → 91×20`，不改变高度。

| 案例 | 准备 / 擦除 | 擦除后及恢复后的逻辑正文 | 相关 marker |
|---|---|---|---|
| 末列 EL0 | `CUP(8,61)` / `ESC[0K` | 行 7 为 `L + 59 空格`，长度 **60** | v 消失；w/x position **175/176** |
| 末列 EL2 | `CUP(8,61)` / `ESC[2K` | 行 7 为 **60 空格** | L/v 消失；w/x position **175/176** |
| 中间列 EL0 | `CUP(8,31)` / `ESC[0K` | 行 7 为 `L + 29 空格`，长度 **30** | v 消失；w/x position **145/146** |
| 中间列 EL2 | `CUP(8,31)` / `ESC[2K` | 行 7 为 **30 空格** | L/v 消失；w/x position **145/146** |
| EL1 | 不补 CUP / `ESC[1K` | 行 9 为 `90 空格 + w + 1 空格`，长度 **92**；尾部内容空格不能丢 | x 消失；w 仍在 `(9,90)`、position **206**；v 保留 |

文档坐标从零开始，DSR 是一基坐标。EL0/EL2 的 w/x 文档坐标仍为 `(9,90)/(9,91)`；擦除后窄屏显示 `(10,29)/(10,30)`，恢复后 `(9,90)/(10,0)`。EL1 的窄屏 w 显示 `(11,29)`，恢复后 `(9,90)`。这些预期均来自各自原版记录，不由当前实现推算。

[resize_erase_test.cpp](../../resize_erase_test.cpp) 调用真实 `resize_columns_preserving`，再通过 `TextDocument` 与 `HistoryViewport::project` 分别观察源正文和显示。每例 strict/recovery × 整块/逐字节，共 **20 次核心回放、(5+5+5+5+4)×4=96 阶段、1440 条断言**。每阶段检查实际网格、DSR、完整逻辑正文及六类 marker 的完整源/显示出现集合，源位置另含绝对字符 offset。

正文比较只忽略**末尾的空字符串容量**；不删除中间空行、不 trim 空格，也不忽略整行空格。全部 24 个原版阶段的合成文档字面数组均重算规范 JSON SHA 与公开结果核对；末列两例及 EL1 另与本地私有录制全文逐项核对。此处的逻辑正文不是完整 active grid 或样式网格。

## 实际失败与冻结身份

| 记录 | 测试源码 SHA-256 | 实际结果 / 日志 |
|---|---|---|
| 首批两个末列案例 RED，生产 `afdcf59` | `8d2d020c43a5c8e6af5e14520a77081384c027d93332258dc10b913535442fad` | 24/40 阶段；96/600 断言失败，退出 1；`resize-erase-red.log` |
| 五例完整 RED，生产仍为 `afdcf59`，源码头 `45c53f5` | `6448e33f87ecf3bc0d9abeb3279599530478a985b61f6aee8aae9a5208996d17` | 56/96 阶段；200/1440 断言失败，退出 1；`resize-erase-all-red.log` |
| 同一五例源码 GREEN，生产 `ed3b8e8` | 同上 | 96/96 阶段；0/1440 断言失败，退出 0；`resize-erase-all-green.log` |

完整 RED 二进制另存后再修生产，SHA-256 为 `f5ddc2c61ecedf4431460faf9a9cab8d864a37760bf82b9842b211a9157c213f`；GREEN 二进制为 `f5001d74b54afecf43af2c969054c5730e93428ff4da9e16a361c788d1df6081`。首批 RED 二进制身份 `66053d4bee07489c11fe8ec041b943c23c6910aa0eb307fef5d2d07f5571937e` 也保留，三者不互相替代。

历史 RED 例如 EL0 擦除后目标逻辑行仍为 91 字符，v 仍在 `(7,90)`，w/x position 仍是 206/207；原版要求末列例长度 60、中间列例长度 30。完整五例 RED 与 GREEN 测试字节完全相同，修正没有删除 EL1 尾空格断言或改变旧末列预期。

[validation.json](validation.json) 保留初始 RED 的完整旧记录，并分别记录完整 RED 和 GREEN。初始 RED 的新测试/修改 CMake 不冒称已在 `afdcf59` 内；完整 RED 只有已保存的选择源码身份，不补造全输入清单。GREEN 的 12 份选择源码在执行后、后续 margin 改动前采集并逐字节匹配 `ed3b8e8`，不回填之后的代码或二进制。三轮完整运行前输入 manifest 均未留存，保持 `null`。

`ed3b8e8` 重新配置、构建后的 `retained_resize_erase_core` **1/1，0.05 秒**见 `resize-erase-registered-build.log` / `resize-erase-registered-ctest.log`；构建成功与实际运行分开记录。这是测试执行时间，不是性能测量。

构建配置为 Windows x64、Visual Studio 17 2022、MSVC 19.42.34433.0、SDK 10.0.26100.0、Release；Qt 与候选适配器关闭。直接复验入口：

```powershell
cmake --build <core-build> --config Release --target resize_erase_test --parallel 1
ctest --test-dir <core-build> -C Release -R '^retained_resize_erase_core$' --output-on-failure
& <core-build>/bin/Release/resize_erase_test.exe
```

## 实现与未验收边界

两个定位点在同一 61 列网格分别得到 60 和 30 字符，已排除本组样本无条件取 `width-1` 的解释。[screen.hpp](../../screen.hpp) 将目标行按当前协议宽度物化后擦除；EL0/EL2 将已保留内容终点限制到协议光标前缀，EL1 保留内容范围中的空白。内容 extent 与网格容量、`Cell.written` 保持区别。这是满足被测结果的独立工程表示，不声称原版内部使用同一算法。

本记录不宣称一般擦除、其他光标/尺寸、连续编辑、擦除后下一字符、宽字/组合字符、样式、软续接与帧交叉、Qt/固定 ConPTY、性能或完整 R2 验收。
