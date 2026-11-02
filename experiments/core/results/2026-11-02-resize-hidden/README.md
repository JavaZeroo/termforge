# 隐藏主屏缩窄后的正文、寻址与显示回放

本切片在 `afdcf59f942f2b7cd3607ad7ab9bd8be9dc907dd` 修正 `resize_columns_preserving` 对隐藏 live 帧的处理。[固定字面回归](../../resize_hidden_test.cpp)从 **12/32 阶段通过、1200 条断言中 420 条失败**，变为 **32/32、1200 条断言零失败**。另有 [owner 工程检查](../../retained_columns_test.cpp)通过。这里没有继承其他提交的整体核心或 Qt 回归结果，也没有接入或验收正式 Qt 的 resize 路径。

## 原版证据与比较范围

依据是 [hidden-primary 独立补充审计](../../../../research/ui/results/2026-11-02-resize-frame-reference/hidden-primary-supplement.json)，SHA-256 `1fde75cb4f841177ab868377b8621624afa0383601b994dd511c0f138d2a6974`。该原版进程采集了 8 阶段，但冻结 runtime 的正文白名单审计失败；保持原录制不变后，新增一个有限候选 `61 个空格 + x` 的独立离线审计通过，新增原版运行次数为 0。不能把它表述为原 runtime 审计通过。

测试将同一原版 8 阶段录制按 strict/recovery × 整块/逐字节执行四次，合计 32 个核心回放阶段；这不是 32 次原版运行。初始输入为 98 字节，SHA-256 `7b74dd7b4c304ca21a70e78a6558efeabeffdd24e56ad32ea2e8a3ed58910441`。真实发送 `CSI ?1049h/l`，尺寸事务调用 `resize_columns_preserving`；不在 resize 或返回观察前补 CUP、更换光标或重写正文。

| 阶段 | 动作与比较 | 字面约束 |
|---|---|---|
| 0 | 主屏 91 列，写入公共正文，再设 margin 2..5、origin ON、MP | DSR `2;12`；完整主屏正文、源 marker 位置及显示位置 |
| 1、2 | 进入备用屏写 AP；保持备用屏活动缩到 61 列 | 两阶段 DSR 均 `2;12`；AP 显示行列 `(1,9)`，原像素矩形起点 `(219,18)`；仅比较可见 marker，不比较原版 Document 中隐藏主屏或额外空行的索引 |
| 3 | 仅发送 `CSI ?1049l` 返回主屏 | DSR `3;12`；旧 v/w 消失；正文逻辑行 7 恰有 61 个空格，逻辑行 9 恰为 `61 个空格 + x`；x 源位置 `(9,61)`、offset 152，显示位置 `(10,0)` |
| 4、5、6 | 返回状态、显式 origin ON、显式 origin OFF 三组 CUP 控制 | DSR 均 `20;3`；PS/PI/PO 在源及显示首行，BS/BI/BO 在源行 18、显示行 19；完整正文与 marker 集合逐阶段检查 |
| 7 | 控制后恢复到 91 列 | DSR `20;3`；v/w 不复活；x 源位置仍 `(9,61)`、offset 154，显示位置为 `(9,61)`；BO 显示行 18 |

主屏阶段 0、3..7 比较完整逻辑正文及每行长度，并逐项检查 marker 的零基源行、列、全局字符 offset。唯一忽略项是正文数组末端的空字符串容量：原版 Document 和固定容量 Screen 的未写尾部行数不同。**不删除空格，不删除中间空行，不修正索引**；行 7 的 61 个空格以及 x 前的 61 个空格必须精确保留。备用屏阶段 1/2 按声明只比较 DSR 与可见显示 marker，不引入 `+1` 特判。

全部 8 阶段的 DSR 与显示 marker 均独立比较。显示预期来自起点 `(147,0)`、单元 `8×18` 的原版矩形，不从源坐标或协议光标反推；没有由这些 marker 推广出完整 active grid、空白样式或像素相同的结论。三组控制重合也不能证明原版私有 origin 位关闭。阶段 7 已受显式 OFF 控制影响，不是再次验证未经修改的返回状态。

## 实现与工程边界

活动帧继续保留源正文供显式显示投影使用；隐藏 live 行则在列宽事务中适配新协议宽度。缩窄隐藏行时保留行身份与有效 soft link，截短超出新宽度的 Cell、收敛 `wrap_columns` 并递增内容版本，同时保留有意义的空白 extent。隐藏帧协议列被限制到新边界，deferred wrap 与结合字符附着清除，滚动区域复位到全屏。工程表示不需要关闭 origin 位来匹配本组不可区分的控制。

两帧所需的新 live 存储先准备完毕再发布；history 不扫描、不复制、不改写。owner 检查补充核对隐藏内容的 ID/version、内容长度、仍存 Cell 的样式和增宽后已删除内容不复活。这些是工程不变量，不是额外原版 oracle，也未进行分配失败注入或性能测量。

原版观察只确定返回时的结果，不证明原版在哪个内部步骤裁尾；把处理放在列宽事务内是 TermForge 的独立实现。隐藏帧仅增宽、完整样式/Unicode 行为、高度变化、任意尺寸、滚动/历史淘汰、其他帧序列及所有编辑动作仍未由本组原版样本验收。默认 `Screen::resize` 和正式 Qt 接线也不由本次显式入口成功覆盖。

## RED、GREEN 与固定身份

| 记录 | 结果 | 身份边界 |
|---|---|---|
| 原始 RED | 12/32 阶段通过；420/1200 断言失败 | 生产基线 `9b678860f9388aae38d172aeda3d948b2552b2ad`；当时生产核心未修改，新增测试/CMake target 在工作树；没有完整 pre-run manifest，旧二进制未保留，身份为 `null` |
| 修复后 GREEN | 32/32；0/1200 | `afdcf59`；测试 SHA 与 RED 相同，没有修改字面预期 |
| 归档时独立重执行 | hidden 32/32、0/1200，exit 0；owner 三组工程场景通过，exit 0 | 两个已构建 Release 二进制分别执行，运行前后哈希一致；源码清单在此时采集，与 `afdcf59` 的 Git blob 逐字节相同 |

冻结测试 SHA-256 为 `b84b9d68de9e9f8959831fdd31fbabd676dabfa12a8b43e204097add7ddfdd65`。源码清单包含 CMake、core 下全部 12 个头文件和本次两个测试源，共 15 项；这是本轮 GREEN 的身份快照，不冒充 RED 的 pre-run 清单。运行后 binary SHA 为：

- hidden：`ed321e426d565adcbbc1c6b75c5b9fd9af830ec588d74c138fc537a17d4f9f98`
- owner：`7345e8deaa9a69f86e1ac6b44f6f444d8a97359de5bac0d8d70e53a230035ce7`

完整清单、两次实际重执行的 stdout/exit code、四份本地日志的大小与 SHA 见 [validation.json](validation.json)。日志为 `local/r29-retention/resize-hidden-{red,green,red-build,green-build}.log`；含本地构建路径的原日志不复制到公开目录。独立核对原始录制 hash 与补充审计 provenance 一致，8 阶段合成正文 canonical JSON hash 全部匹配；测试输入、DSR、6 份主屏正文及字面位置也通过离线转录核验。

构建为 Windows x64、Visual Studio 17 2022、MSVC `19.42.34433.0`、SDK `10.0.26100.0`、Release，Qt OFF、候选适配器 OFF。以下是后续复现入口，不代表本报告已执行整体核心/Qt 回归：

```powershell
cmake --build local/r2-core-build --config Release --target resize_hidden_test retained_columns_test --parallel 1
& local/r2-core-build/bin/Release/resize_hidden_test.exe
& local/r2-core-build/bin/Release/retained_columns_test.exe
```

本次不形成 ConPTY 联合验收、性能等价或 R2 完成声明。
