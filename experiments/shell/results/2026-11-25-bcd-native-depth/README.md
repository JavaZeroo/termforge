# #29 B/C/D 原生缓冲深度对照

四阶段均发送[原版 Telnet 对照](../../../../research/ui/results/2026-11-24-resize-bcd-reference/README.md)中的同一组 VT 字节。`80×24 → 60×12` 后先读原生网格，再发送 `CUP 12;1` 和 `RESIZED`，最后恢复 `80×24`。单元格坐标零基。 [逐阶段记录](validation.json)

| 阶段 | 固定 ConPTY 缓冲深度对照 | WindTerm 随附 WinPTY 的 Windows 原生控制台 |
|---|---|---|
| 初始 | 子进程将缓冲设为 `80×3000`，返回成功。视口为 `80×24` | 缓冲 `80×3000`，视口 `80×24` |
| 第一次 resize 后、写入前 | 缓冲和视口均为 `60×12`，原 `D` 在 `(11,0)` | 缓冲 `60×3000`，视口 `60×12`，原 `CD` 相邻，位于行 10 |
| 固定 CUP 写入后 | `RESIZED` 从 `(11,0)` 写入，覆盖原 `D` | `RESIZED` 从 `(11,0)` 写入，旧 `CD` 仍在行 10 |
| 恢复尺寸后 | 缓冲和视口均为 `80×24`，`D` 仍缺 | 缓冲 `80×3000`，视口 `80×24`，`D(10,0)` 与 `RESIZED(11,0)` 均可读回 |

固定 ConPTY 对照使用 Windows hosted runner、Qt offscreen 和原有公开 Qt resize 入口。只在初始正文写入前调用一次 `SetConsoleScreenBufferSize`。四阶段的应用 DSR、Qt 保留源和显示行仍与既有 Telnet 对照一致。子进程退出码 37，内外 Job 均已回收。 [运行记录](https://github.com/JavaZeroo/termforge/actions/runs/45538929015) · [原包对照](../2026-11-25-qt-bcd-comparison/README.md)

WinPTY 对照在 Windows 11 x64 QEMU 来宾内使用 WindTerm 2.7.0 随附的 DLL 和 agent。直接调用默认标志的 WinPTY API，通过独立阶段信号驱动自有子进程，并读取系统控制台网格。首轮完整采到四阶段，agent 在控制连接仍打开时等待，监督器验证回收。复验先核对子进程自然退出码 37，再由持有者释放 agent，Job 内活动进程为 0。文件哈希和原生行在验证记录中。 [探针](../../../../research/ui/winpty_bcd_native.py)

固定 ConPTY 的 [resize 链路](https://github.com/microsoft/terminal/blob/dc8ae365847d41f62d1ebca93815bb00959bc12f/src/host/outputStream.cpp#L375-L425) 先更新原生缓冲信息，再更新视口。两处视口更新都能将缓冲调整至视口尺寸。 [`SetViewportSize`](https://github.com/microsoft/terminal/blob/dc8ae365847d41f62d1ebca93815bb00959bc12f/src/host/screenInfo.cpp#L605-L622) · [`SetConsoleWindowInfoImpl`](https://github.com/microsoft/terminal/blob/dc8ae365847d41f62d1ebca93815bb00959bc12f/src/host/getset.cpp#L849-L902)

同源候选保持缓冲深度后，原 `D` 留在视口下方，并于恢复尺寸后重新可读。候选的原生绝对寻址和 Qt 源行仍有差异，旧六阶段 `--resize` 仍在第 6 阶段失败。 [同源对照](same-source-ab.md)
