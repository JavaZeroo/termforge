# #135：固定 1049 的 Alacritty 参考差异与严格诊断门槛

本项只适配参考比较工具和台账，没有修改自主 C++、Alacritty 实现、既有输入、负载、期限或旧证据。原 `alternate_screen` 固定例已被新核心支持，旧 `supported=false` 分类过时；实际四项中间差异继续可见，不能为了 CI 改核心去追随 Alacritty。

## 保留初始失败与来源

[PR CI 40737560783](https://github.com/JavaZeroo/termforge/actions/runs/40737560783) 和 [push CI 40737501428](https://github.com/JavaZeroo/termforge/actions/runs/40737501428) 的 Windows/Linux 均在 `Compare extended capability traces` 失败，head 为 `c9f2eac`。PR 的实际 checkout 为 `eaf2cf43897a8ab45be3436db5ba6753d22e7dc4`（merge c9 into main5f7373f），已从两份 job 日志核对。push checkout 未单独取证，保留 null。

两份真实 PR artifact ZIP SHA 为 Windows `1f4851b3b8842cee652bcd7917c7dc350e3ca89497be8206a5b0b2d5a5396a1a`、Linux `daa39be3b256548c6305b6c800530deb9bb4b031306e64db633fbb90b3ed79b7`。原 25 runs 统计均为 **10 core_unsupported / 4 difference / 6 match / 5 resize_difference**，没有改写成初始通过。

[hosted-observations.json](hosted-observations.json)保留原摘要、双方真实 binary SHA、35 项原 source SHA、原各计划 report/input/chunks/JSONL SHA。Linux 35 项与 git blob 原字节一致；Windows 5 项一致、30 项精确对应同一 git 内容的 LF→CRLF 字节形式，分别保留实际 SHA，没有把它们伪装为同一文件字节。两平台的完整有序共同投影签名则逐项相同。旧原件留 local，不改摘要、不使用 source identity 未核的旧本地 candidate trace 作 pinned 依据。

## 有限原版依据与四项台账

原固定输入为 `A\r\nB ESC[?1049h ALT ESC[?1049l Z`，24B，3×12，history=3；输入 SHA 为 `6ae16cedd522763cbbefccf3424d3195350d2d149d06a1e815e52ab73f96fe76`。自研进入后为零基 row0/col0，Alacritty 继承主屏 row1/col1。双方最终屏幕相同，不代表中间轨迹一致。

原版 [#118](../../../../docs/research/windterm-alternate-reference-2026-10-28.md)的首次/重复/重入 `1049h` 均归一基 `(1,1)`。该观察支持自研已经实现的归首格方向；它是独立 Telnet 合成输入，不证明本例全部格子、完整属性、本地 shell 或其他模式都等价。没有新增 WindTerm 运行，也没有借参考台账批准原版行为偏差。

| 计划 | 检查点数（双方） | 首差异 offset / 字段 | 分类 |
|---|---:|---|---|
| byte | 25 | 12 / `.screen.row`，core0 / candidate1 | reference_difference |
| seed:42 | 6 | 14 / 同上 | reference_difference |
| seed:20261022 | 4 | 12 / 同上 | reference_difference |
| seed:8675309 | 4 | 13 / 同上 | reference_difference |
| whole | 2 | 无 | match |

台账精确匹配 case/plan、输入/尺寸/历史、首差异、双方最终状态，以及双方**完整有序 canonical 轨迹 SHA 和检查点数**。哈希编码为 sort_keys、ensure_ascii=False、紧凑 UTF-8 JSON 的完整数组；数组包含所有 offset、屏幕、历史、光标及当前样式。沿用既有 NFC 和空白表示投影，mode_bits/replies 仍不在本比较范围，没有新增语义归一化。

## 实际红→绿及最终针对性验证

先以独立字面状态模型重建 24B 的全部检查点，字面 SHA 与两 OS 原件相同。旧首差异+最终哈希条目会错误授权，完整新条目当时反而不匹配，两项实际红保留在会话验证中；补入全轨迹 SHA/检查点数后转绿。固定首差异与最终状态、仅改变任一侧后续检查点也必须为 difference。

另有缺投影字段可成为相同比较的反例，以及缺初始/中间/最终/resize 检查点的拒绝；字段、样式与完整计划 offset 先验校验后转绿。缺 manifest 字段、截断、未列 seed:99 都不能复用已知规则；公共 CLI 的未列 seed 返回 exit1，未删除输入或放宽差异。

最终诊断源为 **`1e5cc7ad83007e44fdcdd7f0f847afdfc453dd03`**。公共 trace-data/registry 6 项普通及 `python -O` 通过；固定该提交的合同+能力 **11 项全部通过、0 skip**，包括公共 CLI 正/负例，日志 SHA 与源码见[validation.json](validation.json)。候选 workflow 的原 common-test 步骤选择 `test_candidate*.py`，包含这六项新门槛，原负载和期限未改。[最终能力摘要](capability-summary.json)为 25 runs / 118 checkpoints：10 core_unsupported、4 reference_difference、6 match、5 resize_difference，`semantic_acceptance=false`。支持标志只针对该固定 1049 输入，不意味着 47/1047/1048 或全部备用屏模式已支持。

前置工作区 checkpoint 另外运行了 common 4 项（95 match）、fuzz 4 项（8 runs：6 match/2 known_failure）、Windows cmd/more/Vim 诊断 8 项（3 match、真实退出/文件/Job 边界通过）。其 compare tool SHA `3156f71393675d4efccf8415c8191cfe5f1e823c1b25d1bce00e2fbabe23f8d4` 与最终显式 input-length 校验/格式整理前不同，commit 身份为 null，单独列在 JSON。对应[前置源码档案](source-checkpoint-3156.py)由已记录修改恢复，完整字节 SHA 与三个原摘要完全相同；它不是当前入口。不能冒称最终1e5重跑全部应用矩阵。全部本地 exec 已监督到结束，没有跨代理移交运行会话。

本地 candidate 是在当前未修改的 c9 Rust/vendor 来源上新建 `cargo +1.92.0 build --release --locked`：实际 SHA `cd77d300f37f8d2ae8f5d9570f7544a4c65b7bc06c79a8f92169201a32d92686`，工具链和全部来源哈希单列。core trace SHA `3f8bda9d6b1443d02265089ed560941ab8c226cd0bd2900cbadea3812a55d081` 对应旧完整验证中的同一二进制；这些本地身份不替代 hosted 的四个 binary SHA，也不是实验后端维护或性能样本。

## 不转移验收与未知

原生产核心 `40bee8e`、完整验证源 `575bd282b7f61fc4c47da0cad7739b0c46d8c1f2` 的 **55/55 与 13 条内部 skip**、原 52/54 失败和全部输入清单仍按[原证据](../../../core/results/2026-10-29-cpp-1049/README.md)保存，文件 SHA 未变。本次没有重跑完整 55 项，不能把诊断新 head 冒称该完整运行源。

两 OS 旧原件在新诊断源码下的离线重分类不等于一次新的 hosted native CI。后续 PR/push CI 及双轴审查由当前 head 独立跟踪，不提前写绿。`semantic_acceptance=false`、性能未测；完整终端模式、原版全部 grid/背景/bold、GUI TUI/IME/真实 DPI、呈现、原生 margin 与 R2 总出口均保持未验收。
