# WindTerm 2.7.0：origin/wrap 与滚动区域的有限直接观察

**一次直达终端观察完成，未实现或验收 TermForge 模式。** 固定91×25网格下取得25阶段、25条bare DSR和25份Qt文档快照，可用于自主C++的下一项有限实现。完整活动网格、样式、所有模式交叉和本地ConPTY路径仍未由本项验证；#27/#41/#56/R2保持开放。

输入、次数和期限先按 [#151计划](windterm-origin-wrap-plan.md)固定，再由根代理执行唯一原版尝试。[完整白名单数据](../../research/ui/results/2026-10-29-origin-wrap-reference/validation.json)和[独立发布审计](../../research/ui/results/2026-10-29-origin-wrap-reference/publication-audit.json)保留实际来源，公开内容只有原创合成字节、回复、合成文档及脱敏元数据。

## 来源与完整性

- 原版执行源：`72e661d964b771eae226e3bc301ed213c5e55ce8`。官方2.7.0 x64 EXE为 `7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0`；4,047文件清单为 `e459adf2f12adb6a1b7f55e66a9da1c22794185e66e848057d21e0c60d669203`。副本和原官方源逐文件复核均相同。
- 唯一正式计划SHA256：`0bb60918f4236c36349d9b87f2cc4c0c71c5147a89a12ddeff1db7b8d6959dc6`。marker保留，原版运行次数1；未重试。独立非输入WindowStation/Desktop、单个合成Telnet profile及自有Job复核通过。
- 完整REA Evidence：`ev_9f414d87b06374e853efc468a6ed115f754c869745e0a75b554d17274fa6247d`。30个send恰为1个ready、3个readiness、25个phase和1个final；单个script，阶段/末态与保存文件逐项相同，无截断、脚本error或两层cleanup_error。provider subject/environment/version均为null，不由这些字段推断环境；启动器另有独立来源验证。
- NAWS完整事件为 `[91,24]→[91,25]`；UI稳定后在第二事件冻结91×25，25次capacity均为25，冻结后没有新NAWS。实际合成输入334B，加25个DSR请求100B，共发送434B；收到157B，恰好25条bare DSR，无额外应用字节。每个阶段均有独立ack，TCP分块未预设。
- probe模板和运行脚本均重建一致。Windows默认写入将189个LF精确转换为CRLF；实际probe SHA256为 `3800ba21b6b4a5e91e8ecc4b75f428d2cb659d8412b1d4323a162852d5e1f51e`。旧launcher的 `fixture_sha256` 是未执行的通用fixture元数据标签；本项真实peer/JS由正式计划的8源SHA独立绑定，不能把旧标签当成本项输入来源。
- Node/Python/REA当前文件、Python/Windows版本与执行前计划相同，8项执行源码当前字节及72e git blob均与计划相同。这是前置计划与本次离线复核的一致性，不声称全生命期连续监控文件。原版Node进程版本没有被捕获；准备时纯V8测试的v22.21.1不转记为原版运行证据。

## 可支持的有限行为

下表位置均为**原版主动发送的DSR行、列**。开启origin后的行号在本样例表现为区域相对坐标；文档行号、caret API与DSR是不同端点，不能互相代替。

| 阶段 | 确定输入与直接观察 | 可用边界 |
|---|---|---|
| 0–3 | 新会话Home写`FT`回复(1,3)；显式`?6l/?7h/r`重置后(1,1)；`2;5r`后仍(1,1)；CUP(1,3)写`D`后(1,4) | 新会话本次入口及显式控制状态，不外推所有默认设置 |
| 4–8 | `?6h`后(1,1)，CUP(1,3)写`O`后(1,4)，但`O`的文档位置比此前`D`低一行；CUU99+CUB99得到(1,1)，CUD99得到(4,1)；`?6l`后(1,1) | 支持`2;5r`下origin偏移和上下界的固定输入；不能由相同DSR推断同一物理位置 |
| 9–10 | 区域第2–5行的`R0,R1,R2,R3`，在第5行CRLF写`NW`后变为`R1,R2,R3,NW`，回复(5,3)；区域外`TP/BT`保持原位置 | 只对该四行区域、CRLF及ASCII样例证明内部滚动与外部保持 |
| 11–13 | 在区域上方第1行LF写`UP`得到(2,3)；在区域下方第25行LF写`DN`得到(25,3)，中间`R2,R3,NW`未滚动；`r`后(1,1) | 两次区域外LF有各自预期覆写，不能继续声称所有哨兵从未变化 |
| 14 | wrap开启，在第8行末列一次发送`AB`，回复(9,2)；文档为90空格后连续`AB` | 文档软折行投影不是两个活动屏物理行数组 |
| 15–16 | wrap关闭后第10行末列`CD`只留下`D`，回复(10,91)；再开启，第12行末列`EF`得到(13,2)，文档保留`EF` | 仅约束所发送完整序列，不推定内部pending字段 |
| 17 | 末列`G`紧接`?7l`和`H`，最后只留`H`，回复(14,91) | 中间没有插入DSR |
| 18 | wrap关闭时末列`I`紧接`?7h`和`J`，文档保留`IJ`，回复(17,2) | 中间没有插入DSR；不能按一般规范猜测未观测状态 |
| 19–22 | 主屏`2;5r/?6h`、CUP(2,4)写`M`后(2,5)；1049进入后(1,1)；备用`3;6r/?6h`写`A`后(2,5)；退出后主屏完整文档、caret API和DSR均恢复到阶段19 | 主备用两边都设origin开启，无法区分origin位是否分属各屏；返回后的margin下界未测 |
| 23–24 | 返回后CUP(1,6)写`R`，`R`出现在原`UP`行，回复(1,7)；最终显式reset、Home写`DONE`后(1,5) | 支持此返回路径的主margin上界恢复；不代表全套保存恢复已完成 |

本次 `caretColumn` 始终为0，虽然DSR列在1–91之间变化，所以不把该API当作终端列坐标。`lineText`包含保留文档：备用期间仍出现主屏旧文字，部分软折行文字长度92超过91列。它没有提供完整active grid、全部空白格或逐格样式。

下一C++切片可直接锁定上述固定输入、DSR及独立文字预期，再验证行身份、历史隔离和分块；未取证的非法/退化margin、更多origin/wrap/resize交叉、每屏origin位与下界恢复不能计为原版匹配。来源观察与TermForge容器/状态策略分别记录，重要差异仍按ADR-0006提交用户确认。

## 回收与期限

最终Job为 `active0/total3/terminated0`，stderr为空，受控profile实际仍为一个loopback Telnet会话。**启动器主动终止原版窗口**，该Job计数不是自然GUI退出证明。Frida CLI正常返回也不等于所有进程自然结束。

外层wrapper记录2026-10-28 21:23:17.0405260Z至21:24:13.3076732Z，观察wall约56.267147秒；它包括本次准备、执行和收尾，不是吞吐或呈现指标。原43秒peer、41/42秒JS、45秒REA捕获、60秒CLI等待各属局部期限；原版在CLI前已恢复且有单独2秒间隔，**没有把这些期限拼成整程45/60秒硬上限**，`whole_lifecycle_bound_seconds=null`。

三个false提案原字节仍保留，均未执行、无自身attempt。初版提案与正式计划共用一个预留out；该目录现已由正式计划创建，所以此时 `output_present=true`、`output_shared_with_authorized_plan=true`。另两份false的out仍不存在。这不改写此前准备时三者均无out的事实，也不能把目录存在当成false提案执行过。

## 离线发布验证与剩余范围

离线实现源 `fb1a40a2b6efea4ae5cf528607cd1d853e6ecd4e` 独立于原版执行源72e；其中已整合main `7e896cb75283fd0d984c936500ba8d1de4ceaf46`，8执行源逐项无差异，没有因此重新执行原版。新增CTest只注册纯Python观察/出口测试，不执行原版或纯Node fixture。

[严格出口](../../research/ui/export_origin_wrap_reference.py)逐层重建白名单，不转发原始terminal/provider/Job对象。5项新增出口测试覆盖私密文字、字符串caret、错身份、缺阶段、额外回复、未回收Job、截断和script error；未知terminal/Job字段不转发。普通/`-O`的真实公共CLI拒绝私密文本且不创建输出目录，相关纯测试总计各21项通过；未跑完整本地矩阵。

实际raw分别用普通和`-O`导出，41,269B逐字节相同，SHA256 `9774e25bab9f5f751368e19f915e5d1ec8f4332be606070f15b5ffb33ce5b675`。公开前再独立核对25份文档/DSR投影、8源和43份原件；绝对路径、PID/TID、station/session/script ID及原始Evidence/profile/资源均未进入公开数据。这一离线阶段没有新增原版、REA、Node、Qt/native或guest运行。

本项是有限原版合同输入，`semantic_acceptance=false`、`presented_done=null`。完整模式、固定ConPTY本地运输、真实TUI、所有字体/复杂字、DPI、持续稳定及性能仍按各责任issue验收。没有因此修复旧CI事件、批准性能容差或宣布R2完成。

## 初次 Windows CI 红与夹具路径修正

发布HEAD01a979d的 [push/default Windows](https://github.com/JavaZeroo/termforge/actions/runs/40774903422/job/123121533676)为59/60、292.34s，仅新增 `windterm_origin_wrap_reference` 失败；[PR/default Windows](https://github.com/JavaZeroo/termforge/actions/runs/40774909044/job/123121553707)为58/60、283.98s，除该测试外还有独立的owned-desktop收尾事件，由#38另行接续。原job和artifact保留；未重跑旧run，本修正不解释或修复独立desktop事件。artifact12853994007 / SHA `1861c7fee6a58c9eb96898ec52eac77e4f2becc7073c005788e6f0a8c9ad2960`、artifact12854726855 / SHA `05392812a79a0b38d48384b47905c85c9f46285cbedcf0b4afa2275550c9efbb`由根代理下载保留，本项未重复下载。

已用实际存在的子目录后接 `..` 构造同一目录的不同拼写，复现直接调用的 `Provider source/lifecycle differs or is incomplete`，以及公共CLI在私密文本门槛前遇到 `Runtime probe bytes differ`。前者是夹具目录未规范化、provider路径却resolve后比较；后者是夹具将未规范化目录嵌入probe，而真实公共入口先resolve，再重建probe。**已证明的是该夹具缺陷；旧CI未确定TEMP的具体拼写，不能据此声称它实际使用8.3或哪一种别名。**

反例内容提交 `9d36d4efa02a70ab6d7585d50f1e42df32ebedc6`；最小修正 `74d4cadd9fa90fd6b6a1de6c61e96f8369e67951` 仅将夹具目录改为 `(root / "capture").resolve()`，在生成probe和source元数据前与真实launcher一致。生产exporter、source/path/lifecycle校验、189行LF→CRLF及私密文字白名单均未改。两个新增回归直接驱动真实capture/publicCLI：red为1个Provider错误和普通/-O两个CLI失配；green相关纯测试普通/-O各23项通过，别名下有效记录通过，私密文本仍在正确白名单门槛拒绝且不创建输出。

[路径修正审计](../../research/ui/results/2026-10-29-origin-wrap-reference/fixture-path-validation.json)单列本次反例、源码、日志和派生出口。原件43项、原版执行源72e、三个false及旧公开 `9774e25…` 原字节保持。修正后的实际raw普通/-O重导仍相同，新SHA `6ad6820884d4f94b9a42fd1a669253c72df38512b9f8dadb461250e3425e4557`；与旧公开文件只差 `offline_export_source_commit` 由fb1a40a变为74d4cad，其余投影和字节完全相同。这是调用HEAD元数据变化，不是新观测、数据修正或新原版运行。新source的自动CI及最终审查另行核对；没有用本地green覆盖旧CI红。
