# 缩宽后寻址的独立原版观察协议

这组工具继续 [固定正文的三阶段原版观察](results/2026-11-02-resize-state-reference/README.md)，用于 #29 与 #27 的寻址边界调查。旧三阶段工具和归档保持不变；新工具为 [resize_address_reference.py](resize_address_reference.py)、[控制器](resize_address_reference.js)和[严格离线导出器](export_resize_address_reference.py)。本文件定义准备与判读合同，不是原版运行结果。

## 五份独立计划

每个 case 必须创建一份新计划并启动一个新原版进程，每计划最多尝试一次。所有 case 都先写入同一份 `0123456789`、`ANCHOR`、无自动换行边缘 `uv` 与自动换行边缘 `wx` 正文，再执行以下四阶段。每阶段末尾恰有 `ESC [ 6 n` DSR 请求；只有阶段 2 具有额外动作。

| 阶段 | 窗口尺寸 | 必须观察到的 NAWS / capacity | 终端输入 | 观察用途 |
|---|---|---|---|---|
| 0 | 1360×749 | 91×20 | 固定原始正文 + DSR | 初始基准 |
| 1 | 1000×749 | 61×20 | 仅 DSR | 缩宽后、动作前基准 |
| 2 | 1000×749 | 61×20 | 下表唯一动作 + DSR | 当前尺寸下的寻址结果 |
| 3 | 1360×749 | 91×20 | 仅 DSR | 恢复宽度后的保留或擦除结果 |

| case | 阶段 2 的确切字节 | 用途 |
|---|---|---|
| `next-char` | `51`（`Q`） | 自然的缩宽后光标继续输出 |
| `carriage-return` | `0d51`（`CR Q`） | CR 后输出 |
| `line-feed` | `0a51`（`LF Q`） | LF 后输出 |
| `explicit-cup` | `1b5b31313b324851`（`CSI 11;2 H Q`） | 显式 CUP 的独立寻址控制 |
| `erase-line-start` | `1b5b314b`（`CSI 1 K`） | 从行首到当前光标的擦除观察，无后续 marker |

计划总量为 **5 个独立进程、最多 20 个正文观察阶段**。阶段 1 必须稳定、写出观察并获得 `ack-1`，peer 才能发阶段 2。显式 CUP 结果不可冒充自然的 resize 后光标；CR、LF、CUP 和擦除互不串联。协议 DSR、文档行列、`position`、caret API 与可见矩形分别保留，不假设它们采用同一坐标层。

## 停止条件与身份

初始网格精确为 91×20；只有阶段 1、3 允许有 resize 握手与新 NAWS，分别精确冻结为 61×20、91×20。初始和 resize 冻结要求连续 600 ms 网格稳定，单阶段观察要求连续 400 ms 快照稳定。QRect 使用 inclusive left/top/right/bottom。错误尺寸、改变行数、未武装的网格变化、过期 ready 事件计数、额外回复、错误 case / phase、缺少动作前 ack、正文或 marker 越界均失败，不自动改用其他宽度或重放计划。

沿用原隔离启动器的 41/42/43 秒内部期限、45 秒 REA 捕获和 60 秒 CLI 等待；这些值不是整个复制、启动、清理生命周期的硬上限。自有 Job 主动清理不能证明自然 GUI 退出。

`prepare_plan` 只检查并写计划，默认 `execution_authorized=false`，不启动目标。实际授权后的 `run_plan` 在启动前独占写入 `.attempted`；成功或失败都会保留该文件。同一计划不得重放，输出目录必须是新建的本工作树 `local/` 子目录，且与原版源目录不重叠。

运行源码身份由 `mode_state_reference.source_identity(root=..., source_files=...)` 生成。无 Git 的 VM 使用根目录 `reference-source.json`，schema 为 `termforge-mode-reference-source-v1`；字段仅为 schema、40 位基点提交、恰好匹配 `SOURCE_FILES` 的 10 源 SHA256 映射。基点提交与逐文件字节身份分开，不能声称未提交的新工具已经存在于该提交中。运行前后均校验源码，固定 EXE、原包清单、Python/Node/REA、模板、生成脚本及执行宿主也分别绑定。

## 公开导出边界

导出器先校验计划外部 SHA256，再复核执行原件、peer、provider、4 阶段原始观察、两个 resize 握手、动作前 ack、完整 4,047 文件分发与执行副本、合成 profile，以及精确执行源码和注入脚本。输出只选取已定义字段，记录源/原件哈希、固定合成输入、DSR、有限 marker/矩形/网格标量；不发布原始全文、资源、进程标识、私有路径或 profile。

正文白名单是固定字符串有限集合：前两阶段只允许旧协议的原始合成行；后两阶段只增加这些行在列 0…92 的一次 `Q` 替换/追加，或 EL1 前缀擦除。单个文档中的 `Q` 最多一个；擦除 case 始终不允许 `Q`。没有任意字符正则或无限填充例外。公开 `synthetic_lines_sha256` 绑定已校验的全文，但不输出全文。marker 的 label / 文档行列必须与全文逐项匹配；`position` 是有界原版 API 观察，不从字符串长度推断内部编号。

任何白名单外实际观察都应保留为失败原件，先调查并独立冻结修订，不能静默放宽发布范围。导出不自动判定 reflow、擦除或寻址策略，不代表完整 active grid、样式、ConPTY 本地 Shell、选择/复制、性能或 R2 验收。

## 离线验证入口

```powershell
python -B -X utf8 -m unittest research.ui.test_resize_address_reference research.ui.test_export_resize_address_reference -v
python -B -O -X utf8 -m unittest research.ui.test_resize_address_reference research.ui.test_export_resize_address_reference -v
node research/ui/test_resize_address_stage4.js research/ui/resize_address_reference.js
```

Python 测试覆盖真实 peer 的分块 DSR、确切四份输入、NAWS 边界、额外/过期事件、缺少动作前 ack、离线 source archive prepare、失败计划不可重放，以及五个 case 的完整合成导出和拒绝条件。Node 测试直接执行新控制器源码，检查稳定时间与四阶段排序。所有目标程序、网络连接与原生 UI 均由纯内存替身隔离；测试通过不是原版已运行或行为一致的证明。

完整导出夹具使用 5 个无可执行内容的合成模块文件，并仅在测试命名空间中替换清单数量/身份常量；生产导出器仍要求固定原包的 4,047 文件。该夹具证明严格导出链的校验路径，不是新原版包的实际校验结果。
