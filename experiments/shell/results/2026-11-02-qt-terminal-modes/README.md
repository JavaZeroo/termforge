# Qt / 固定 ConPTY 模式链路：resize 仍失败

本片为 [#27](https://github.com/JavaZeroo/termforge/issues/27) 与 [#29](https://github.com/JavaZeroo/termforge/issues/29) 提供实际失败证据，**完整六阶段不是通过的验收**。公开 Qt 键事件驱动自有子进程，子进程向固定 Microsoft ConPTY 写合成 VT；独立命名管道返回原生网格，`SessionModel` / `TerminalWidget` 观察核心与受控图像。两个 Qt 运行的前五阶段通过，80×24 缩为 60×12 后，原生网格重排而自主核心仍物理裁剪，文字位置不同步。当前源码将五阶段模式回归与六阶段 resize 诊断分为两个明确入口，历史失败保持不变。

完整白名单阶段、源十六进制、观察值、分轮身份和日志哈希见 [validation.json](validation.json)。上述两轮 Qt 运行没有执行 WindTerm；后续独立原版样本见文末，不能将其与这两轮合并为一次通过的验收。

## 配置与分轮身份

测试位于 [terminal_modes_test.cpp](../../../../src/qt/terminal_modes_test.cpp)，自有子进程与原生回执位于 [terminal_modes_fixture.hpp](../../../../src/qt/terminal_modes_fixture.hpp)。配置为 Windows x64、Visual Studio 2022 Release、Qt 6.10.3 offscreen、随包 ConPTY 1.25.260930003、app-local CRT 14.51.36247.0；固定后端按既有身份守卫加载，creation flags 为 `0x18`。没有候选核心或系统 ConPTY 混入结果。

两轮基于 `f72c5c4f68dd7eb579ad1e523762333b135aa074` 加当时工作树修改。首次源码/EXE 没有单独快照，后续构建已覆盖产物，其精确哈希保持 **null**。不能从诊断轮代码逆推首次身份。诊断轮保存了构建前六个选定源码 SHA 和运行 EXE SHA `10d10790f9de7364e7f4fc60775a221c7436e1a03a907c081711cf5439471cea`；这不是整个工作树的冻结清单。

| 运行 | 结果与边界 |
|---|---|
| 首次 Qt | 6 阶段执行完，前 5 阶段通过，第 6 阶段失败；0.42s，无超时，外层 Job `active0/total4/terminated0`。首次断言错误地期待原生末行只有 `RESIZED`，忽略原生 reflow 后仍有 `CD` 尾文；同一原件已经显示原生/core 不同位置 |
| Qt + 独立运输诊断 | 更正原生字面预期后，原生预期通过，**core 同步断言继续失败**；0.58s，无超时，外层 Job `active0/total6/terminated0`。其中增加的 direct-transport 会话独立运行，不冒称捕获了前一个 Qt 会话的字节 |

首次同一次 CTest 调用还包含独立 `view_mapping_core` 失败，不计入此 Qt 能力结果。两轮没有拼成一次通过；耗时只记录测试执行，不能用作性能指标。自有 child 正常退出码为 37，监督器 25 秒期限未改变；测试内部整体 18 秒、单阶段 2.5 秒。

## 模式回归与 resize 诊断入口

| 入口 | 阶段及结束条件 | 声明边界 |
|---|---|---|
| `qt_terminal_modes_test`，CTest `qt_terminal_modes` | 原样执行阶段 1–5，再通过公开 Qt 键发送 `x`；child 只在指定第五阶段接受正常退出，仍核对退出码 37、EOF 与自有进程回收 | 用于主/备用屏、重复进入、恢复、固定宽字/结合字、颜色与 DECAWM 边界的 CI 回归；拆分后的独立构建与五阶段通过记录见[统一验证](../../../core/results/2026-11-02-r27-integrated/README.md)，不倒填旧六阶段运行身份 |
| `qt_terminal_modes_test --resize`，显式诊断 CTest `qt_terminal_modes_resize_repro` | 执行原样阶段 1–6，保留完整原生与核心正文位置断言；child 到第六阶段后才接受 `x` | 保留正文 resize 失败的可复现入口；仅在显式诊断配置注册，失败仍返回非零，不设置 `WILL_FAIL` 或以 marker 前缀替代全文比较 |

两项使用同一自有 child、同一固定 ConPTY、同一阶段源与字面断言。默认入口不请求第六阶段的 60×12 resize；输出明确记录 `scenario`、`stages` 与 `resize_exercised`。五阶段回归不能关闭 #27 的 resize 交叉或 #29，也不将正文失配从它们的验收合同移除。

后续同一组修正后源码的独立[统一验证](../../../core/results/2026-11-02-r27-integrated/README.md)记录核心 15/15、Qt 4/4 通过，其中默认模式入口完成五阶段，child 退出码 37、自有 Job `active=0,total=4,terminated=0`。同一新二进制显式执行 `--resize` 仍以退出码 1 报告 B/CD 正文位置失配，清理后 Job 为 0；完整身份见该记录的 `validation.json`。这是新一轮结果，不覆盖本报告前两轮的源码、EXE 或失败身份。

通过现有监督器运行，保留启动前 Job 门禁与 25 秒期限：

```powershell
python src/qt/run_smoke.py local/r2-qt-build/qt-modes-bin/Release/qt_terminal_modes_test.exe
python src/qt/run_smoke.py local/r2-qt-build/qt-modes-bin/Release/qt_terminal_modes_test.exe --resize
```

只有第二项允许设置 `TERMFORGE_MODES_TRANSPORT_DIAGNOSTIC=1` 来增加独立运输会话；该环境变量不会悄悄扩展默认五阶段。开启后仍保留原 Qt 失败，且附加会话不冒称捕获了 Qt SessionModel 的字节。

## 确定源与三个观察端点

所有阶段由公开 Qt 数字键逐个触发，不直接给核心补喂控制序列。每阶段先核对独立 native ack 的 child/阶段身份，再等源末尾 `S1`…`S6` 到达核心；数据、原生回执和 UI 观察各有角色。

1. 主屏写 `MAIN`、`BASE`、`中é`，`MAIN` 使用 SGR 31/44。
2. `1049h` 进入备用屏，写 `ALT`、不同颜色的 `SIDE`。
3. 再次 `1049h`，写 `NEW`，检查旧备用内容清空。
4. `1049l` 回主屏，在保存位置写 `!`，检查主屏正文、颜色、宽字/结合字保留。
5. 第 8 行第 80 列，在 DECAWM 关闭时写 `AB`；再开启，在第 10 行第 80 列写 `CD`。前者只留末列 `B`，后者的 `D` 到下一行首格。
6. 通过公开 Qt `resize()` 将区域改为 60×12，独立 child 核对实际尺寸；只在第 12 行写 `RESIZED`，最后更新 `S6`。没有清行或重画 `B/CD`。

`GetConsoleScreenBufferInfo` / `ReadConsoleOutputW` 直接读取当前 `CONOUT$`，不是从核心回填。JSON 的 `native_utf8_hex` 是按 `CHAR_INFO` 格拼接的 UTF-16 转码，可能包含宽字 lead/tail 的重复值，**不是规范化的字素投影**。结合字正文另外按核心独立字面 UTF-8 核对，不由原生格 API 推断完整 Unicode 支持。

颜色只核对原生/核心 SGR 31/44 索引及 Qt 抓取图中的一个蓝色背景点。不据此声明所有属性、真实字形、物理桌面呈现或完整样式验收；`presented_done=null`。

## 缩窄后的实际差异

以下行列均为零基。原生 reflow 将旧的 80 列长行按 60 列重新安排；第 8 行末列 `B` 所在行扩成两行，使后面的 `CD` 也下移。`RESIZED` 只覆盖 7 格，没有清除该行已有尾文。新的预期分别检查原生布局和产品同步，不使用 `starts_with("RESIZED")` 掩盖尾文。

| 位置 | 原生实际/字面预期 | Qt 核心实际 |
|---|---|---|
| row 7 | 空 | 空 |
| row 8 | 19 个空格 + `B` | 空 |
| row 9 | 空 | 空 |
| row 10 | 空 | `D` 留在旧位置 |
| row 11 | `RESIZED` + 12 个空格 + `CD` | 只有 `RESIZED` |

原生尺寸与核心尺寸均为 60×12，二者光标均为 `(0,2)`；`MAIN`、`BASE`、宽字/结合字及颜色继续保留。因而“尺寸请求完成”和“新 marker 到达”不能证明旧正文已经同步。

## 独立运输确证

诊断使用同一 child 源和固定后端，另外创建自有 `tf_conpty::Session`，通过它既有的 `output()` 接口保留实际运输字节，再按记录的 resize 边界回放到独立 Parser。这个会话不使用 Qt SessionModel；它解释同源机制，不能替代前一 Qt 会话的逐字节捕获。捕获有 64KiB 上限，实际只有 **265B**。

- offset 0–22 是启动控制；前五阶段的合成源完整出现在运输中。
- resize 边界位于 offset **217**。随后 26B 为第六阶段原始 **22B**，加 `ESC[6n`。
- 回复查询后又收到 `ESC[1;3H` 共 6B；正常结束时另有 16B 关闭模式序列。
- resize 边界之后没有 `B/CD` 重排、旧 `D` 清理或整屏重绘字节。独立 Parser 产生与 Qt 相同的错误位置。

因此在本固定路径中，问题不是末端 parser 忽略了已有重排数据：ConPTY 原生网格做了 reflow，而产品核心自行 resize 时仍采用物理裁剪。#29 仍须结合独立原版的分层观测明确正式核心 resize 合同，再完成实现与联合验证。此结论不外推所有原生 API、margin、历史、尺寸或运输时序。已知 margin 丢失没有被补注入绕过。

当前 `--resize` 入口保留失败。`TERMFORGE_MODES_TRANSPORT_DIAGNOSTIC=1` 只增加独立运输记录，原 Qt 同步断言仍会失败。生产 Qt 没有加入输出观察接口。

## 原版观察：历史准备与后续实测

`validation.json` 中的 `resize_probe_preparation` 保留本报告最初准备时的身份：原版尚未执行，计划窗口为 `1360×860 → 1000×860 → 1360×860`，Python 普通/`-O` 各 10 项和纯 V8 6 项仅为当时离线验证。这些历史值没有被后续结果覆盖，也不表示原版当前仍为零次运行。

后续完成独立 [WindTerm 2.7.0 resize 研究](../../../../research/ui/results/2026-11-02-resize-state-reference/README.md)及其[严格白名单导出](../../../../research/ui/results/2026-11-02-resize-state-reference/strict-export.json)。两次新计划分别因 QRect 解码错误、实际高度受限而在正文阶段前失败，各记录 0 阶段。第三个新计划固定 `1360×749 → 1000×749 → 1360×749`，实际网格为 `91×20 → 61×20 → 91×20`，完成 3 阶段；旧失败原件与源码身份保留，没有重放旧计划。

成功样本仅在第一阶段写一次正文，缩窄/恢复后只请求 DSR，没有 CUP、清屏或重画。逻辑文档中的 `v/w/x` 位置保持，可见矩形随宽度重排并恢复原位；三次 DSR 均为 `(11,2)`。文档 index、显示坐标和协议光标仍是不同观察层，不能把文档数组当作完整 active grid，或从光标未变推断正文没有重排。

原版样本使用直接 Telnet，且只变宽度、保持 20 行；本报告的 Qt / 固定 ConPTY 失败则同时改为 `60×12`，两者输入、运输、尺寸与源码身份独立。新原版证据没有把旧 Qt 正文失配改写为通过，也不完成 #29 的协议写入、显示投影、选择/复制和历史行为合同。探针的完整 NAWS 事件、每阶段精确窗口检查、一次性计划、9 源归档身份与发布边界详见该研究报告。
