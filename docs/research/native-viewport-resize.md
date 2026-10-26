# 显式官方后端的原生 viewport resize

本项处理 [#51](https://github.com/JavaZeroo/termforge/issues/51) 的连续 GUI resize 残尾。固定原基线为 `e0b4d67`、候选 `termforge.4`、官方 ConPTY `1.25.260930003`、Console glyph width flags `0x18`；修复为同一候选的 `.5`。默认 system 后端和正式核心选择不由此项决定。

## 观察与原因

原场景末屏出现 `TF_SIZE_100x28OW`，原断言要求整行恰为 `TF_SIZE_100x28`。原生 `ReadConsoleOutputW` 捕获在同一次失败中显示该行正确，候选屏幕却保留 `OW`。原运输与 resize 时序可以逐步重现失败；移除中间两次请求仍出现同样终态，所以不能仅归因于请求合并或遗漏 host resize。

候选的通用 reflow 会在增高或增宽合并行时把历史拉回可见区，并可能为了保持光标行而删掉底部行。原生控制台没有终端已经保留的历史；随后由 host 发送的绝对坐标更新仍针对其原生可见区。两套布局先分歧、再接受局部覆盖，造成旧文字残留。六组独立原生样本分别覆盖增高、可见软行合并、光标所在软行合并、历史/可见区续行边界和部分合并后的底行保留。

| 初始→目标（列×行） | 关注点 | 原生末屏（省略行尾填充） | 原生光标（0 起算） |
|---|---|---|---|
| 8×3→8×6 | 增高不拉回 H0/H1 | TOP / MID / BOT / 空 / 空 / 空 | 2,3 |
| 8×3→16×3 | 合并后不拉回 H1 | 123456789 / P> / 空 | 1,2 |
| 8×3→16×3 | 光标位于被合并行 | H2 / 123456789 / 空 | 1,9 |
| 8×3→5×3 | 历史续行不能改变原生可见区 | 90123 / 45678 / 90 | 2,2 |
| 8×4→10×4 | 部分合并不能丢 BOTTOM | H1 / ABCDEFGHIJ / KLMNO / BOTTOM | 1,9 |
| 8×3→16×3 | 增宽不跨历史边界合并 | 9012345678901234 / 567890 / 空 | 1,6 |

这些是 ASCII 合成用例的原生快照，不是由候选算法计算的预期。原生采样会影响时序；微型探针不回答 CPR，固定 host 的有界等待亦影响时序。因此本记录不作为吞吐、交互延迟或原版性能证据。

## 实现边界

新增显式 `native_viewport` 策略，仅由官方后端的候选窗口选择。历史和可见区分别 reflow，增高/合并产生的空行置于可见区底部；保留光标、底行、历史身份和有界淘汰语义。跨边界续行保留逻辑换行元数据，以独立标记区分布局填充与文本；任何可变单元格访问都会使该标记失效。保留原跨度内的尾空格，保证完整复制及后续 resize 不增删空格。默认 `reflow_history`、系统候选和默认自研核心保持各自合同。

本项目独立实现这些修改，保留第三方许可与版本身份；不是恢复出的 WindTerm 源码，也不是确认 WindTerm 运行时一致。完整 resize/选择/Find、备用屏与模式交叉仍归 #29/#27，正式选型归 #34。

## 可复现入口

先按[显式官方后端说明](experimental-conpty-backend.md)构建候选配置。以下输出目录须不存在，后端目录必须是经过双 SHA 校验的固定包；探针不会下载或回退。

```powershell
cmake --build build/candidate --config Release --target conpty_resize_native_probe candidate_resize_replay candidate_core_test
python -m experiments.conpty.resize_native_run --exe build/candidate/bin/Release/conpty_resize_native_probe.exe --replay build/candidate/bin/Release/candidate_resize_replay.exe --backend build/candidate/bin/Release/conpty-backend --out local/resize-native-new
ctest --test-dir build/candidate -C Release -R '^candidate_(core|resize_replay)$' --output-on-failure
```

探针先等待监督器 GO 和自有 Job，再启动私有控制台子进程。每组保存源 VT、前后原生 UTF-16、真实 transport、resize plan、两策略末端状态、二进制/源码哈希和清理状态。child 必须确认真实新尺寸、自然退出 37；父级检查 EOF、无强制取消、无溢出和 Job 清空。内层等待最多 5 秒、外层 20 秒，失败不扩大预算。回归中的预期来自上表，默认策略对照的差异单列。

## 验证与限制

[固定验证记录](../../experiments/conpty/results/2026-10-27-native-viewport-resize/validation.json)区分原 `.4` 真失败、正式 `.5` 与早期临时探针。原 GUI 的精确 marker、Unicode、位置/历史、child ack、Job/desktop 回收断言及原期限保持；未删除 `OW`、改成子串、清屏或关闭 reflow。

诊断中有两组结果不进入因果证据：只保留错误字符串的缩减最终删掉全部 resize，不能作为此缺陷的最小复现；另一次用保留旧 mtime 的文件复制恢复源码，增量构建复用了旧修复二进制。后者已按实际二进制哈希排除，重新写入源码、实际编译后恢复了原 stage43 red。公开记录保留这些排除原因。

六组合成探针可公开复现；原 GUI 原件保留本地，只发布审核后的状态、哈希和固定断言结果。完整 R1/R2、实际呈现、真实 CF/IME 组合与 WindTerm 对照不从本项推导，`presented_done` 保持 null。其他真实失败由对应 issue 继续跟踪。

最终产品源 `92eac12` 的显式官方配置完整 CTest 为 54/56、411.59 秒，另有 12 项内部 skip；失败为 Vim stage59（#52）和第二轮工作区恢复 stage1（#88）。同次八会话工作流通过，但此前两次编号半行失败仍由 #87 跟踪。原 resize、resize-close 和 Unicode 检查通过只验收本项限定修复，不覆盖这些独立失败。
