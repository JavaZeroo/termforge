# ConPTY 每屏 margin：源控制台与末端 VT 的边界

[#41](https://github.com/JavaZeroo/termforge/issues/41) 的固定源仍是 175 字节，SHA-256 `01878a5d54c343007309abb89faeb90c89262a8b55e420c5f267342ed9b27e59`：主屏设置 `2;3r`，备用屏设置 `4;5r`，`1049l` 后在主屏滚动。**本机系统 ConPTY 的源控制台已没有恢复主屏 margin；固定官方 side-by-side ConPTY 则能把原 DECSTBM 交给末端核心，使末端恢复正确。** 这是继续整合的可行路径，不是已完成的产品修复。

## 三个不同检查点

- **应用写出的源 VT**：不修改原 fixture、不补发恢复命令、不改预期。
- **源控制台 CHAR_INFO**：四阶段独立读取文字、属性、光标，保留原生 API 实际结果。
- **ConPTY 输出与末端**：持续读到 EOF、正常退出后关闭；用固定候选核心处理实际 transport，再核对精确末屏。

旧系统后端的 `REGION_CIXED` 同时出现在源控制台和末端，候选仅忠实显示收到的内容。重新修复候选的每缓冲区 margin 无法还原 transport 已丢失的源命令。Microsoft [Scrolling Region 文档](https://learn.microsoft.com/en-us/windows/console/console-virtual-terminal-sequences#scrolling-region) 声明每个缓冲区维护自身区域；这一声明不能替代具体版本的运行结果。

## 同源 A/B 与证伪

同一探针、源字节和四阶段 capture，仅改变后端、输出 mode、合法网格大小。每个后端运行 `vt-on`/`vt-off` × `80×24`/`12×7`，两次 fresh 矩阵均完成；所有成功探针都要求 child 自然退出 37、管道 EOF 109、内外自有 Job 归零、无强制进程或读线程取消。结果与源码身份见[白名单记录](../../experiments/conpty/results/2026-10-26-margin-backends/validation.json)。

| 分支及可证伪预测 | 观察 | 裁决 |
|---|---|---|
| 系统标准后端，源控制台已丢 margin，末端也应错误 | 两尺寸 `vt-on` 均出现 `REGION_CIXED`，原 DECSTBM 未保留 | 不是末端绘制或候选状态恢复造成 |
| 关闭 `ENABLE_VIRTUAL_TERMINAL_PROCESSING` 可否当 raw 输出后端 | 80×24 的末端表面正确，但原生格已把 ESC 当字面内容；12×7 丢失 `BOTTOM_FIXED` 和完整结束标记 | 拒绝；不是可维护的 raw 路径 |
| 旧 passthrough flag 是否仍是受支持路径 | 第一方已删除该实现；当前头文件将 `0x08` 用作字素宽度策略 | 拒绝旧 flag，未在产品中试用或启用 |
| 固定官方新 ConPTY 可否保留源 DECSTBM | 两尺寸 `vt-on` 均保留 `2;3r`/`4;5r`；候选末端恢复 `REGION_C`、`REGION_D`、`BOTTOM_FIXED` | 继续实验整合，不能宣称 native API 状态也已修复 |

`SetConsoleMode` 控制应用所在的 console buffer；它不是外部宿主的通用旁路协议。其[官方合同](https://learn.microsoft.com/en-us/windows/console/setconsolemode)还明确 mode 属于各 buffer。关闭处理的局部成功不能证明依赖原生 console API、readback 或会重新设置 mode 的 shell/TUI 也正确。

## 为什么新后端不同

Microsoft 在 [PR #16954](https://github.com/microsoft/terminal/pull/16954)（合入 `2050416997b237361e0c5979bb1cad88d0544673`）于 2024-03-28 删除旧 VT passthrough，原因包括 resize 不同步等不足。[固定 1.24 头文件](https://github.com/microsoft/terminal/blob/7ff11ee107c984e236960ec948b450ccbc10187b/src/winconpty/winconpty.h#L50-L59) 的 `0x08` 是 `PSEUDOCONSOLE_GLYPH_WIDTH_GRAPHEMES`。不要照搬过时的 `PSEUDOCONSOLE_PASSTHROUGH_MODE=0x8`。

新的 [PR #17510](https://github.com/microsoft/terminal/pull/17510)（`450eec48de252a3a8d270bade847755ecbeb5a75`）将 Console API 的输出同步转为 VT，并把应用 VT 交给末端；它也明确记录 `ENABLE_VIRTUAL_TERMINAL_PROCESSING` 开启、`DISABLE_NEWLINE_AUTO_RETURN` 关闭时的 LF→CRLF。实际 transport 符合这个限制，**不是源字节完全原样转发**。本轮不引用其性能数字作为 TermForge 性能结果。

固定包是 Microsoft 发布的 [Microsoft.Windows.Console.ConPTY 1.25.260930003](https://www.nuget.org/packages/Microsoft.Windows.Console.ConPTY/1.25.260930003)，对应 [Terminal v1.25.2733.0](https://github.com/microsoft/terminal/releases/tag/v1.25.2733.0)、源码提交 `dc8ae365847d41f62d1ebca93815bb00959bc12f`，MIT。下载对象 1,750,533 字节，SHA-256 `6d6f8b008c655d814c498d8205d4012dcd4d0b6dfc1096461d0a185d1e3caf03`；只提取同包 x64 `conpty.dll`/`OpenConsole.exe`，两个文件的 Authenticode 均为 `Valid`，文件身份在记录中保留。没有安装包、修改系统组件或替换默认后端。

## 复现与剩余工作

按根构建指南编译 `conpty_margin_probe`，使用已有显式候选 trace 路径及全新的输出目录：

```powershell
cmake --build build --config Release --target conpty_margin_probe
python -m experiments.conpty.margin_run --candidate-trace <candidate-trace.exe> --output local/margin-system --require-margins
python -m experiments.conpty.margin_run --backend <absolute-package-directory>/conpty.dll --candidate-trace <candidate-trace.exe> --output local/margin-official --require-margins
ctest --test-dir build -C Release -R '^conpty_margin$' --output-on-failure
```

本机系统命令返回 1，官方固定后端返回 0；返回值只约束 `vt-on` 的两个固定末端结果。`vt-off` 是保留的反例，正常退出不等于终端语义正确。监督器将 transport、native capture 和错误原文保留在 `_private`，只发布 `summary.json`；它不下载或安装后端，固定 DLL/EXE 哈希不匹配立即拒绝。原生入口必须由监督器先加入自有 Job 再发送 GO。

下一步是显式实验 backend 接口、匹配包部署与来源、同 API 族 create/resize/close、模块生命期，以及实际 GUI 的查询/resize/cmd/PowerShell/More/Vim/关闭和 R1 关键回归。源控制台仍出现 margin 差异，依赖原生 API 的应用可能与末端不同步，必须单独处理。#41、#27、#28、#35 和 R2 保持开放；本轮没有截图、用户输入桌面、实际呈现时间、WindTerm 运行等价或性能结论。
