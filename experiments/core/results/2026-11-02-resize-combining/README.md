# 缩窄后的结合标输入：独立字面失败诊断

> 本页保留首次 RED 的固定执行身份与失败；后续 [独立控制样本及修复红绿](../2026-11-02-resize-caret/README.md) 已另行记录。以下正文和 `validation.json` 均按首次诊断理解，不代表当前修复状态。

[resize_combining_test.cpp](../../resize_combining_test.cpp)实际结果为 **8/16 阶段通过、8/176 条断言失败、退出 1**。失败全部位于后续 U+0302 输入及恢复宽度后的逻辑正文第 7 行；四种 feed 的 DSR、网格和其余正文检查均通过。没有修改生产代码，也没有将这一失败转为验收通过。

## 原版来源与差异

唯一原版来源是 [combining-boundary.json](../../../../research/ui/results/2026-11-02-resize-unicode-reference/combining-boundary.json)，SHA `f6ac486eb30ebcc0908d402299728727970df72386f042c8345a0f471e3ab93e`，独立计划 SHA `4225438550c7b3c22353deeeb53a708d2f33545527cd46970799d1faccfa8b89`；它已通过原严格审计。[原版报告](../../../../research/ui/results/2026-11-02-resize-unicode-reference/README.md)保留位置单位及附着机制的未知。

固定 60B 输入 SHA 为 `c6d08514fb8897ee31059833506de1354f04b69b9721aa1153f34f750e444d49`，在 `CUP(8,61)` 写 `e + U+0301 + Z`。随后只缩窄、只输入 `cc82`（U+0302）、只恢复宽度，各阶段独立 DSR；无补 CUP。四原阶段 × strict/recovery × 整块/逐字节，共 16 阶段。

| 阶段 | 网格 / 原版及核心 DSR | 原版目标行 | 核心实际目标行 |
|---|---|---|---|
| 初始 | 91×20 / `8;63` | 60 空格 + `e + U+0301 + Z` | 相同 |
| 仅缩窄 | 61×20 / `8;61` | 同上 | 相同 |
| 仅输入 U+0302 | 61×20 / `8;61` | 60 空格 + `U+0302 + e + U+0301` | 60 空格 + `e + U+0301 + Z + U+0302` |
| 仅恢复宽度 | 91×20 / `8;61` | 与上一阶段相同 | 与上一阶段相同，仍不匹配 |

失败行精确后缀分别为原版 **`cc8265cc81`**、核心 **`65cc815acc82`**；不是 Unicode 规范化或视觉近似比较。完整原版数组的三个不同摘要分别为 `881daac9af8dedd04673a48efbd6ac10450ed08c23e461c1b26666e16fa82040`、`aa7ae5dd2da851a8c6e32d3db80ab078942418ae66b0bf480e501bf8c1e821ec`、`f585595566948a83a6c1b172e9d1c8a0830e6e9cb62ca80dfcbfbb18b17372a6`（紧凑 JSON、默认 ASCII escapes）。比较仅规范化末尾空字符串容量；阶段 1 的原末端空字符串仍保留在字面数组中，不 trim 空格或中间空行。

## 真实执行身份与边界

执行时 HEAD 为 `0a44d44740a6a27c1a949926fda6fc9947ed58b1`，新增诊断 / CMake 当时是工作区更改；后续提交 `fd96c6753a2f266d5e427ba5554b68e6a14bf3a1` 不改写这次执行身份。源码、EXE、构建及运行日志共 8 个文件保存在 `local/r29-retention/combining-red-capture/`，于 `2026-11-02T04:23:56.205478+00:00` 采集，完整明细见 [validation.json](validation.json)。

- 诊断源码 SHA：`48accd489520c5d25dd4ab65275f128fd997d3523254605cace432be6ead5a0f`。
- 实际 RED EXE SHA：`ef2fece677c8577a11e39dcd87ad4810daf04b1bb33f6c69c2950b66a361f14b`。
- 冻结 identity SHA：`ca3ba6519dc7281269101af154f53c06b7e4241e062bc42b6fbcb5fc921469d4`。

该目标可构建并直接运行，**未注册 CTest、没有 `WILL_FAIL`**，不计入此前 core 28/28 或 Qt 4/4。Release 构建成功与直接运行退出 1 分开记录；本次只保存选定文件的执行后身份，没有完整运行前构建 manifest。

诊断仅证明固定正文差异，不使用 marker 矩形、私有 caret 偏移或内部附着位作 oracle，也不决定“孤立 mark”存储政策。DSR、resize、列夹取与原附着状态仍需独立控制实验；本报告没有新原版运行、修复、Qt 接线或完整 Unicode / R2 验收。
