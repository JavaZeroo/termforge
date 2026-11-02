# 光标定位与保留列宽后的组合字符：字面红绿

`20e217c0b2555b9e8c01c470da193d00e15cd943` 修正了已观察到的有限正文差异：显式定位或保留列宽事务后，后续组合字符使用协议光标前的存活 Cell；组合字符也属于编辑，因此目标行先按当前协议宽度物化，再附加 UTF-8。两组固定原版字面回放由 RED 转为 GREEN，测试源码和预期未改。

这是一种独立实现选择。它没有证明原版使用相同 Cell、组合附着位、字节游标或字形归属，也没有把保留入口接到默认 Qt。完整身份、原版输入和逐阶段正文摘要见 [validation.json](validation.json)。

## 原版合同和比较范围

独立原版来源分为两组，不合并其计划或执行身份：

- [旧 combining-boundary](../../../../research/ui/results/2026-11-02-resize-unicode-reference/README.md)：1 个原进程、4 阶段，观察初始 → 缩窄 → U+0302 → 恢复宽度；核心 [`resize_combining_test.cpp`](../../resize_combining_test.cpp) 以 strict/recovery × whole/bytewise 重放，合计 16 阶段。
- [新 caret 三例](../../../../research/ui/results/2026-11-02-unicode-caret-reference/README.md)：3 个独立原进程、12 阶段，分别是只查询、显式 CUP、缩窄，随后各输入 U+0302 和 Q；核心 [`resize_caret_test.cpp`](../../resize_caret_test.cpp) 同样以四种 feed 重放，合计 48 阶段。

各例起始输入都是精确 **60 bytes**，SHA `c6d08514fb8897ee31059833506de1354f04b69b9721aa1153f34f750e444d49`，最终在 `CUP(8,61)` 写 `e + U+0301 + Z`。后续 mark 只含 `cc82`，Q 只含 `51`。逐字节 feed 在 UTF-8 和 CSI 内部也拆开，不把分块完整字符当作 bytewise。

每阶段单独比较协议网格、DSR 和完整逻辑 UTF-8 正文。只忽略末尾空字符串容量，所有空格、空格行和中间空行必须精确匹配；原版额外末端空项仍保留在字面数组。原 API 位置、矩形和内部附着位均不作为 C++ Cell oracle；不以规范化、视觉相似或 marker 子集替代全文。

确定的正文包括：不移动时 mark 追加到 Z 后；CUP 后正文为 `60空格 + U+0302 + e + U+0301 + Z`，Q 后为 `60空格 + U+0302 + Q + Z`；缩窄后相应正文不再含 Z，Q 后为 `60空格 + U+0302 + Q`，DSR 仍 `8;61`。原 API 在相同数值查询上还记录到不同 x/width，保留在原版报告，不能据此判定 mark 实际属于哪一个字形。

## 两组真实 RED 与定向 GREEN

| 回放 | 首次 RED | 定向 GREEN |
|---|---|---|
| combining-boundary | 8/16 阶段通过，8/176 断言失败，退出 1 | 16/16 阶段，0/176 失败，退出 0 |
| caret 三例 | 32/48 阶段通过，20/508 断言失败，退出 1 | 48/48 阶段，0/528 失败，退出 0 |
| retained owner 工程检查 | 本轮没有独立新增 RED | 通过；不算新原版策略观察 |

旧 combining RED 都是目标行的 UTF-8 后缀错误：预期 `cc8265cc81`，实际 `65cc815acc82`；DSR 与网格正确。新 caret RED 的 CUP/strict 在两个 feed 中因无附着 base 抛错，后续检查被跳过；CUP/recovery 生成 U+FFFD 并推进列；resize 两种模式保留错误旧 base，后续 Q 又丢掉 mark。只查询控制通过。

新 RED 的 **508** 与 GREEN 的 **528** 不能视为同一次断言总数：两个 strict CUP 异常各记录一次失败，并各跳过该阶段剩余 10 个检查；修复后全部检查都执行。测试源码没有通过改预期取绿：caret SHA 始终为 `79bfe6b847e03199fb0ed7787136f93ce384bca928d7c138b076184a44fb6912`；combining SHA 始终为 `48accd489520c5d25dd4ab65275f128fd997d3523254605cace432be6ead5a0f`。

| 固定捕获 | 真实执行时 HEAD / 工作区状态 | identity SHA-256 |
|---|---|---|
| `combining-red-capture` | `0a44d44740a6a27c1a949926fda6fc9947ed58b1`；新诊断与 CMake 为工作区输入，之后提交为 `fd96c67` | `ca3ba6519dc7281269101af154f53c06b7e4241e062bc42b6fbcb5fc921469d4` |
| `caret-red-capture` | `fd96c6753a2f266d5e427ba5554b68e6a14bf3a1`；新诊断与 CMake 为工作区输入，之后提交为 `6da4620` | `7680ec184c43e81c8fc32937a511de4443099c273121540f0662b1312a62d561` |
| `caret-green-capture` | `6da462068671f27bdb5002476816197d8aff000a`；Screen/owner 测试修复仍在工作区，之后进入 `20e217c`；当次 CMake 尚未含后加的两个 CTest 注册 | `3ae136d0dd91e67ab00925fbe3cf3cdbac2bc3c40b3b6689a712dd969718a54c` |

这些捕获位于 `local/r29-retention/`，全部所列源码、EXE、构建与执行日志已重新计算哈希。两次 RED 的选定生产头文件与当时 HEAD 一致；GREEN 的选定核心源与后续修复提交一致，但不能把后提交的 CMake 身份回填到定向运行。这里只具有选定文件的执行后身份，完整运行前构建 manifest 仍为空。

| 实际二进制 | SHA-256 |
|---|---|
| combining RED | `ef2fece677c8577a11e39dcd87ad4810daf04b1bb33f6c69c2950b66a361f14b` |
| caret RED | `dd5177ae995f2cfcd7b85e419e6986f1c21d59f261fef45b0ac8d44ebf50217e` |
| combining GREEN | `e2a29c75a89409c6fda3b1a9e73a85921f75456d88d1d0b1197d4641a6a35bc2` |
| caret GREEN | `d32b8e7b088adaab4b8ee58731583d10c9e84f73538547afd1e82c75a86ec1c7` |
| owner GREEN | `2b1b94a1457866869863c8f31a3d13ee75ecb9f81e9ff6ddd0d04b9630fa18c3` |

## 实现选择与工程边界

[`Screen`](../../screen.hpp) 保留现有 owner、行身份/版本与 `Cell` 的 base + 最多 16 个后置 mark 表示。`move()` 完成定位、保留列宽事务完成夹列后，以协议光标前一个完整 Cell 作为下一零宽输入的 base；wide trail 回到 lead，列 0 仍没有前驱。连续普通输入保持既有最后 base，DSR 不改变它。零宽编辑在 base/16-mark 守卫通过后递增源版本、按当前宽度物化目标行，再追加 mark。

因此，空格也能作为现有 Cell 的 base，投影出的文字恰为 `60空格 + mark + e...`；后续 Q 覆盖 e 的 Cell，前面的 mark 仍留在空格 Cell 上。不必新增前导零宽 Cell、文本字节插入游标或并行内容 owner。默认物理 `resize()` 继续清除附着状态，inactive 帧按既有策略失效，本修复没有替换其未观察合同。

宽字前驱解析、首列无 base、其他使用 `move()` 的寻址控制，以及 owner 测试中的样式/宽字窄化场景属于明确工程规则，本批原版没有逐项验收。owner 测试旧“缩窄仍附原末尾 e”断言已按新输入事实改为“附当前光标前的存活 A，编辑裁去尾部”；这不是另一个独立原版 case。现有投影/选择/Find 表示未被修改，也不能据此声称所有编辑后的用户流程已通过。

## 同提交整合回归

`20e217c0b2555b9e8c01c470da193d00e15cd943` 同一选定源码下，Release 核心 CTest **30/30**（1.11 秒）、Qt/固定 ConPTY 默认路径 **4/4**（8.45 秒）分别通过。两个新字面回放已在本提交注册为 `retained_resize_combining_core` / `retained_resize_caret_core`，10 秒上限、无 `WILL_FAIL`。这些时间是本次 CTest 用时，不是性能验收。

运行前冻结 65 份选定源码与 30 个核心 CTest 清单；运行后核对选定源未变，保存 34 个实际测试 EXE、1 个仅构建产品 EXE、2 个辅助程序副本、54 个部署 runtime 副本、6 份配置、6 份日志和 1 份注册清单。全部保存副本已重新计算哈希，65 份源与固定提交 Git blob 一致；这些仍不是完整构建输入 manifest。

- `caret-integrated-capture/pre.json` SHA：`4cebf103f6646c20813c5ea212d8c3a7f8a9a9c2b8c0574b4967359c73905ec8`。
- `caret-integrated-capture/post.json` SHA：`4764f647f22d3b308f2693a9ee1d69c7f7b82a8c9034ca08f3dfd9d13e72acc6`。
- 核心 CTest 日志 SHA：`b0b9f0d9c49b00298e9383e51508658e96bb37b1eaca0f28ec40a0647a3f93a1`。
- Qt CTest 日志 SHA：`3c93cff593fa27af10e68eee38e1ffa34ff6b733478b94174f6ba84ddccb2421`。

Qt 四项分别为 `qt_cpp_shell`、`qt_unicode_input`、`qt_cpp_resize`、`qt_terminal_modes`，仍运行默认物理 resize/旧 snapshot；其通过不验证显式保留入口、正文投影接线或第六阶段 B/CD 缩放。产品 EXE 只构建，不另称已执行。这里是本地分配置回归，不是新的 hosted CI、原版比较或两配置合并通过率。

定向复验命令（需使用已配置的同类 Release 核心目录）：

```text
cmake --build <core-build> --config Release --target resize_caret_test resize_combining_test retained_columns_test --parallel 1
ctest --test-dir <core-build> -C Release -R "^(retained_resize_caret_core|retained_resize_combining_core|retained_columns_core)$" --output-on-failure
```


## 未覆盖范围

默认 Qt 尚未接保留正文/新投影，默认物理裁剪与既有 B/CD 缩放失败不由本报告豁免。完整主/备用帧 Unicode、首列/跨行/wide 前驱组合输入、所有脚本/字素/字体与实际 glyph、原 API 的通用位置单位、编辑后完整选择/Find、运输整合及性能仍各有边界；#27/#29 和 R2 未据此关闭。报告编写未再运行原版、VM 或原生测试，保留 [首次 combining RED](../2026-11-02-resize-combining/README.md) 的原始结果与 validation。
