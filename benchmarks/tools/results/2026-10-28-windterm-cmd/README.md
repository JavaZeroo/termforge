# 原版 WindTerm 本地 cmd：有界自动正确性诊断

#119 的首个自动切片，基于 main `2903290`。工具及其运行边界见 [WINDTERM-CMD.md](../../WINDTERM-CMD.md)。本页只记录固定结果；#3/#4 的实际呈现、低扰动校准和正式基线仍未完成。

最终执行源码 `windterm_cmd.py` SHA-256 为 `bd4a67f5df26c32a13c5d9f0296bf2347719a3b9024d0607afe81f4655e00f61`。在 Windows 11 上使用既有 REA 6.7.0 公共 CLI，启动固定官方 2.7.0 EXE 和经完整内容清单校验的发行物副本。没有改动发行物、用户配置、输入桌面、剪贴板或显示设置。

| 检查点 | 最终实际结果 |
|---|---|
| 合成数据 | 新 run_id 的 14 行 ASCII，536 B，字节 SHA 与独立 manifest 一致 |
| 本地链路 | 原版合成 Shell 会话 → 本地 cmd → Python producer；本地控制台 VT 启用成功 |
| 文本正确性 | `visibleLineCount=15`，逐条 `lineText(i)` 恰为全部 14 条独立预期及 1 条空白尾行；不是只检查 marker |
| 配置可读项 | producer 控制台窗口80×24、缓冲80×3000；JS `screenLineCapacity=23`；控件矩形 `[0,0,905,428]`、QWidget 字体 Roboto Mono 9pt / pixel_size -1 |
| 身份与回收 | run_id/target_pid 绑定 producer、探针和 cleanup；自有 Job active=0；profile 仍只有一个合成会话 |
| 未观测端点 | consumer_done=null；presented_done=null；performance_gate=not_evaluated |

控制台尺寸、JS 文档容量与 QWidget 几何属于各自读取点，原样分列；没有将它们解释为同一网格、完整 C0 验证或真实像素呈现。控件字体也不证明 QPainter、实际 glyph fallback 或字体文件身份。

首次工具源码曾完成同样的小负载诊断，其原件与执行源码保存在 `local/windterm-cmd/initial-20261028`，没有覆盖。随后离线审查修正异步 TerminateProcess 的等待，以及 cleanup 身份和摘要白名单/原始记录哈希绑定；只在新源码、新 run_id 和新目录 `local/windterm-cmd/final-20261028` 执行了一次已预声明的最终验证。两次均为独立正确性诊断，没有合并成性能样本或重跑旧手工批次。

新工具10项边界测试及既有隔离准备3项通过，独立 JS 语法检查通过。测试覆盖 marker-only、缺失中间/首行、多余内容、空格改变、错误运行/源码/PID、截断、probe错误、重复快照、缺失 producer、配置白名单、异常终止仍回收以及非零 Job/profile 变化拒绝。独立离线脚本重新生成 oracle、核对 payload/生产事件/完整15行/身份/六份原始记录哈希和清理等式，全部通过；没有再次启动目标。

[validation.json](validation.json) 仅公开人工审查过的白名单摘要、设置和来源哈希，不含 PID、宿主路径、原始生产计数器、画面、复制发行物或 profile。原始 REA evidence 和本地数据均保留在忽略目录。

这是有插桩的小 ASCII 文本读取链路，尚不覆盖滚动历史、Unicode、ANSI样式、输入响应、真实呈现、观测扰动或正式 T01。没有计算吞吐/时延、提出数值容差或声明原版性能等价。
