# #54 首次 hosted 固定批次：环境护栏停止

固定源码 `f734d29bc4f8a120eac59da3acac3169d49231b2` 在 [GitHub run 40490861301](https://github.com/JavaZeroo/termforge/actions/runs/40490861301) 的首次 `windows-2025` 作业中完成两份构建、来源审计及4次短正确性检查。原协议、两个种子、各112位置、2秒CPU/墙钟下限、5%环境诊断护栏均未改变。

第一批的十个空闲区间为1.154%–3.125%，通过预检。随后执行24个位置：23个记录有效，第24个（sequence 23，candidate/interactive/chunk 1）输出正确且退出0，但未归属CPU活动上界估计为5.410447761%，超过5%护栏，故停止。原始数据全部保留，没有续跑、补样、删除慢样本或启动第二批。

该位置全机总CPU区间为8.375秒，busy为2.484375秒，内层测量进程CPU为2.03125秒，余额0.453125秒。余额包含内层区间以外的准备、oracle、启动和runner工作，不能直接称为其他进程的后台CPU，更不能据此定位具体进程。临时VM的逐进程身份未导出，不能追溯补造归因。

**完整批次数为0，不生成配对成本摘要，不从这个前缀计算或验收吞吐回归。** `performance_gate=not_evaluated`；5%不是用户批准的产品性能容差。物理宿主独占、邻居VM、频率稳定、输入/传输/呈现及WindTerm等价仍未测。

- [validation.json](validation.json)：源码/树、构建/二进制、宿主、停止位置、限制及同源码回归CI。
- [evidence.zip](evidence.zip)：审阅后的164,608B公开副本，SHA-256 `9ade502f03419f55295b4e9f4df9dc136c5c4d50ace02d41417fea1de6952917`。内含131个文件：130份白名单证据和`publication.json`；56份合成stdout/stderr原字节保留。全部24次输出的正确性和完整字段已独立复查。
- 提供者原归档165,170B、SHA-256 `6a2713dd96d86ce66e2462a142d46b72e915ccc4cfd45bd3b42c5e7486c3cef5`保存在local。产物复审发现两份构建日志各有10处未处理的8.3短profile前缀；未发现凭据。公开副本仅补做这些前缀的别名替换，并更新清单，保留转换前/后哈希和`post_publication_review`；成本数据、顺序和停止状态未改，不冒充原ZIP字节。
- 复查副本的来源、固定白名单、长/短路径及秘密字段后，未发现逐进程名称/PID、凭据或未处理的workspace/profile前缀。旧作业确实使用旧导出器；后续实现修正不回填旧运行身份，也不触发补跑成本批次。

测量源码对应的默认PR/push回归均通过（Windows46/46、Linux29/29）；候选两条CI也通过（measurement Windows40/40，Linux39通过/1个Windows专属跳过）。当时相关本地measurement31/31；后续短路径修正另有失败反例、实际Windows别名查询及32/32回归。普通CI与成本作业分开，不用CI成功抵消成本批次失败。日志哈希指向本地保留的UTF-8/LF解码文本，并非提供者日志ZIP原件。

归档复查命令不会执行成本负载或修改数据：

```powershell
python experiments/measure/results/2026-10-27-r54-hosted/verify.py
```

下一项调查由[#54](https://github.com/JavaZeroo/termforge/issues/54)接续：另行预声明有界诊断，观测整个测量子进程及runner的CPU，区分自身区间外工作与外部guest活动；本次失败和v1规则保持。扰动/组件归因、完整受控批次及用户预算确认仍未完成。
