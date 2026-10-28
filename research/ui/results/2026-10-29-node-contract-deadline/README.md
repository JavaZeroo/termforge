# Node 纯夹具：一次有界三 case 诊断

[#144](https://github.com/JavaZeroo/termforge/issues/144) 的新诊断工具已完成一次正式、预先冻结的本地批次。**正常项通过，受控挂起仍为超时，受控异常仍为失败。** CLI exit0 仅表示三个预定义诊断结果都观察到，不表示三项均通过，更不表示旧 CI 超时已修复。#144/#38/R2 保持开放。

## 执行身份

执行源码为 `02aac45ca6e8eaffcbca1d3ef1a80d243159ec46`；授权计划 SHA256 为 `87a0518c23b65f050d3991557b6b58aca334c8d80dc939fab601d3fae01f0fc8`。计划与输出目录只使用一次，exclusive attempt marker 保留。源码、三份 case、CLI 日志/退出记录及 marker 的实际字节身份见 [validation.json](validation.json)。原件留私有 `local/`，公开不含 stderr 栈、本地绝对路径、PID 或原始 checkpoint 对象。

实际 Windows 版本字符串为 `10.0.26200`，Python `3.12.10`，Node `v22.21.1` / x64 / win32；Python/Node EXE 哈希、Python 完整版本文本哈希和五项实际源码哈希单列。它们与计划、各子观察及离线当前文件一致。这仅是已记录的版本/文件身份，不是完整机器或环境指纹，也不补写旧 CI 的未知 Node 身份。

发布分支已整合 main `4d7bd8a7c822135b1fdee5935262af3205ee565f`；五项执行源码在合并前后字节不变。整合和发布提交不是新的执行身份。

## 实际结果

| case | 子状态 / 退出码 | checkpoint | 最后阶段 | 强制结束 Job | 结束 Job active/total/terminated |
|---|---|---|---|---|---|
| normal | passed / 0 | 23，完整 | final | 否 | 0/2/0 |
| hang-vm-helpers | timed_out / 99 | 6，前缀 | vm-helpers-start | 是 | 0/2/0 |
| throw-before-grab | failed / 1 | 11，前缀 | case-start:grab | 否 | 0/2/0 |

三项在 GO 前均为 Job 1/1/0，根进程归属由保留句柄实际核对；根已回收、Job active=0。超时处为 Job 2/2/0，原10秒预算产生 `TimeoutExpired`；清理使用独立共享5秒预算。子异常由 stderr 中的受控标记和 exit1 分别保留，不能因清理成功改判为通过。离线审计只重算已保存记录及源代码守卫，没有重新读取已经销毁的 Job。

Node 首次 checkpoint 位于内建 fs 加载之后；此前的启动区间仍不能细分。宿主计时与 Node 单调时钟不作跨时钟性能推断。同步进程创建、准备和哈希没有整程硬界，`whole_lifecycle_bound_seconds=null`；计时仅为诊断记录，观测开销未校准。旧 CI 原因仍为 `null`。

## 来源与审计分列

| 记录 | 实际源码 | 边界 |
|---|---|---|
| 原 CI 失败 | `4388302` | Node 原10秒超时，无阶段；不由本次本地结果解释 |
| 早期受控进程测试 | `5ddecd4` | 私有历史原件保留，不转记为新源码批次；不补实际宿主版本字段 |
| 协议尾部 P2 | red `66a0391` → green `3b74a94` | 完整23条后非法/多余记录不得通过；普通/-O各8项纯测试 |
| 版本漂移 P2 | red `cebc530` → green `b809706` | marker前拒绝 Windows/Python 版本漂移；普通/-O各3项纯计划测试 |
| 本次正式三 case | `02aac45` | 唯一新授权批次，结果如上；没有再次运行 Node 或重试 CI |

另一次独立 stdlib 审计将三个 case 与 CLI 行逐字段比较，并按原写出格式重建字节，全部一致；重算有序阶段、PID绑定、时钟、源码/运行库、计划及 Job 字段，发现0项。24份旧原件和三个未执行 false 提案逐一保持，公开只列不透明原件 ID、长度和 SHA256。普通与 `python -O` 的[白名单导出器](../../export_node_contract_deadline.py)输出逐字节一致，公开 JSON SHA256 为 `db97e95aa6d56a9bb44553178f788c6f7c33fff9e52441ef343ea63821f98a3d`。

这是[有界诊断工具](../../../../docs/research/node-contract-deadline.md)的实现与验证，不接入原 Python/CTest 入口，不关闭 #144/#38，也不支持产品、真实 Qt/原版行为、性能或长期稳定验收。普通 observer 为 no-op，原合成输入/断言保留；`raster_style_reference.js`、原 `test_raster_style_reference.py` 入口和 `experiments/conpty/run.py` 与 `4388302` 字节一致，三项哈希另列。
