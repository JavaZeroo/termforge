# WindTerm 2.7.0：origin/wrap 与滚动区域取证计划

状态：**离线准备，原版运行次数为 0，尚无行为结论**。本项由 [#151](https://github.com/JavaZeroo/termforge/issues/151) 承接 [#27](https://github.com/JavaZeroo/termforge/issues/27) 的直接观察缺口；没有改动自主 C++ 核心或产品模式。通用规范、旧候选和本地控制台结果均不作为原版行为。

目标固定为官方 WindTerm 2.7.0 Windows x64，EXE SHA256 `7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0`；受保护复制器在创建自有副本后校验 4,047 文件分发清单 `e459adf2f12adb6a1b7f55e66a9da1c22794185e66e848057d21e0c60d669203`，不改动官方原件。

## 固定观察边界

[Python 执行器](../../research/ui/origin_wrap_reference.py)复用既有 loopback Telnet 解析器和 `r1_launch.py`，但使用独立命名空间和本项 [Qt 观察脚本](../../research/ui/origin_wrap_reference.js)。原有执行器、探针和旧证据不变。单个合成 profile 位于新 `local/` 目录；原版窗口在独立非输入 WindowStation/Desktop 挂起创建，分配自有 Job 后恢复。REA CLI 树也沿用既有自有 Job/句柄监督。本流程没有产品 stdin GO 协议，不把挂起创建写成 GO 握手。

先等待实际 NAWS 和 `screenLineCapacity` 相符且稳定 600ms，再允许 peer 冻结完整网格。接受范围为 40–160 列、20–60 行；实例计划展示 91×25，但实际输入按冻结后的实际尺寸生成并逐阶段保存。每个完整 NAWS 事件均保留，包括同一次 recv 中瞬时改变后恢复的事件；冻结后任意改变立即失败。Qt capacity 改变同样失败。

每阶段仅发送表中输入和一个末尾 `CSI 6n`；等待一条完整 DSR 及稳定 400ms 的 Qt 文档快照，再写独立 ack 推进下一阶段。完整接收字节、DSR 私有前缀和行列分别记录；分包前缀可等待，重复回复、附加字节和错误格式不能被“取最后一条回复”掩盖。允许报告的 0 坐标被保留，不预设原版始终采用一基原点。DSR 不插入同一阶段末列两个字母之间。

Qt 的 `caret_api`、文档行号/文字、DSR 行列各自保留；**文档数组不等于活动屏幕网格**。本次不调用未知的样式接口，也不捕获像素或声称实际上屏。

## 25 个阶段

`r` 表示 `CSI r`，`CUP(r,c)` 表示显式光标定位。详细十六进制、字节数和 SHA256 由 `phase_records()` 写入执行前计划，实际源再按冻结网格存入 raw peer。

| 序号 | 阶段 | 合成输入与要观察的问题 |
|---|---|---|
| 0 | fresh-session-cup | ED2、Home、`FT`；不先改 origin/wrap，保留新会话入口观察 |
| 1 | explicit-origin-wrap-reset | `?6l`、`?7h`、`r`、ED2、Home，建立显式控制状态 |
| 2 | main-margin-set | `2;5r` 后的光标回复 |
| 3 | origin-reset-cup | CUP(1,3)、`D` |
| 4 | origin-set | 单独 `?6h` 后的光标回复 |
| 5 | origin-set-cup | CUP(1,3)、`O` |
| 6 | origin-set-upper-relative-bound | CUU99、CUB99 |
| 7 | origin-set-lower-relative-bound | CUD99 |
| 8 | origin-reset-after-margin | 单独 `?6l` |
| 9 | region-sentinels | `r`、ED2；顶行 `TP`、最底行 `BT`；第2–5行 `R0`–`R3`；再设置 `2;5r` |
| 10 | region-bottom-newline | CUP(5,1)、CRLF、`NW`，读取区域内/外文字变化 |
| 11 | above-region-newline | CUP(1,1)、LF、`UP` |
| 12 | below-region-newline | CUP(实际最底行,1)、LF、`DN` |
| 13 | main-margin-reset | 单独 `r` |
| 14 | wrap-enabled-edge | `?7h`、CUP(8,末列)、`AB` |
| 15 | wrap-disabled-edge | `?7l`、CUP(10,末列)、`CD` |
| 16 | wrap-enabled-again-edge | `?7h`、CUP(12,末列)、`EF` |
| 17 | pending-edge-disable-before-next | CUP(14,末列)、`G`、`?7l`、`H` |
| 18 | disabled-edge-enable-before-next | `?7l`、CUP(16,末列)、`I`、`?7h`、`J` |
| 19 | main-margin-origin-before-alt | `?7h`、`2;5r`、`?6h`、CUP(2,4)、`M` |
| 20 | alternate-enter | `?1049h` |
| 21 | alternate-margin-origin | `3;6r`、`?6h`、CUP(2,4)、`A` |
| 22 | alternate-exit | `?1049l` |
| 23 | main-cup-after-alternate | CUP(1,6)、`R`，读取特定返回路径的 origin/margin 影响 |
| 24 | final-explicit-reset | `?6l`、`?7h`、`r`、CUP(1,1)、`DONE` |

这些是待回答的问题和确定输入，不是预先规定的原版答案。样式、完整网格、历史选择、复杂文字、resize、全套保存恢复、其他模式、固定 ConPTY 本地路径、不同 DPI、物理输入/实际呈现和性能均未由本项验收。

## 次数、期限与停止

每份计划最多一次原版尝试。false proposal 默认禁止执行，且不能创建 attempt marker 或目标目录。执行前重新核对源码 commit/逐文件 SHA、生成后的 launcher SHA、Python 文件和版本、Windows 版本，以及现有 REA 6.7.0 shim/package/entry SHA。正式尝试先用排他方式创建 `.attempted`，失败后保留，不重试或覆盖目录。该内部防重复记录不要求新的逐次用户确认；实际执行由父代理完成预审后推进。

原预算保持：peer 接收循环43秒、JS阶段检查41秒/计时器42秒、REA 捕获45秒、CLI 等待60秒。原版在 CLI 前已恢复，另有2秒启动间隔；**45/60秒不是整个进程或启动器的墙钟上限**，`whole_lifecycle_bound_seconds=null`。继承的清理含 peer join3秒、原版句柄等待5秒和 Job drain 自身边界，不能把这些局部预算拼成一个已保证的整程期限。挂起创建/分配失败仍沿原句柄回收分支；任何结构、身份、NAWS、协议、阶段、stderr/REA或清理失败保留原件并停止。

本次准备只运行 [纯 Python 合成测试](../../research/ui/test_origin_wrap_reference.py)：内存假连接覆盖完整实际接收循环、分包DSR、瞬时NAWS变化和重复回复；没有打开套接字、运行 Node/Qt/native/guest 或启动原版。宽松末条匹配的 red 记录有9项测试、5个失败子例；严格边界修复后最终普通/`-O`各12项通过。早先缺模块的初始 red 仅为脚手架检查，不能当作协议反例。

## 复现准备与后续执行

在本工作树使用已存在的 Python 和 REA 路径；不安装、升级或探测连接配置。以下命令中的 `<...>` 由父代理替换为已有私有绝对路径，所有计划和原件留在 `local/`。

```powershell
python -B -X utf8 -m unittest research.ui.test_origin_wrap_reference -v
python -B -O -X utf8 -m unittest research.ui.test_origin_wrap_reference -v
python -B -X utf8 research/ui/origin_wrap_reference.py --prepare --plan local/origin-wrap-proposal.json --source <固定官方包> --out local/origin-wrap-run-0 --rea <现有REA6.7.0-shim>
```

上述 prepare 默认 `execution_authorized=false`，不启动任何目标。审查通过后，在新计划路径加入 `--authorize-execution` 冻结同一源码和期限，再用 `--plan <新计划>`执行；不得将旧 false 提案改写为已执行或复用其 attempt。实际运行后先审计原始阶段、DSR、NAWS、profile/Job，再实现严格离线白名单导出并验证普通/`-O`一致；raw文档、PID/路径、REA原始产物、专有副本均不入 git。没有这一步就不公开原始结果或关闭本项。
