# R54：新 hosted VM 的 CPU 分量诊断

本次按[预声明合同](../../R54-CPU-HOSTED.md)完成一次六位置诊断，全部输出正确、退出0，完整子进程/runner/全机计量可复核。**它没有执行空闲资格筛选或正式成本批次，不构成性能验收。**

## 来源与执行身份

- 源码 `4772f4f937c4f0f220be1630592f84bed3a591c8`，树 `86e2342d16acd101fe182793d20aef3f1f017633`；与审查树一致。
- [run 40512310499](https://github.com/JavaZeroo/termforge/actions/runs/40512310499)，job `122217071656`，attempt1，创建push分支 `perf/grapheme-cpu-hosted-r54-entry3`。提供者元数据、作业日志及归档身份相符。
- 实际映像 `windows-2025-vs2026` / `20261021.278.1`；Windows Server 2025 build26100、UBR33650、24H2，AMD EPYC 7763，guest4逻辑/2物理CPU，Python3.12.10、psutil7.2.2。
- Rust1.92.0、MSVC14.51.36231 x64；Release、locked/offline、no-default-features、jobs1，独立target。构建前31项准备测试通过（14.042s），两次构建和四次短正确性通过后才进入诊断。

| 核心 | 固定来源 | 本次新EXE SHA-256 | 字节数 |
|---|---|---|---:|
| `.3` | `65988f5f20f62294ff52fcac5b5b81e563f93f3b` | `5f31a54e5543eca286e98a10b31aae060b0a52f2ccbcfb4bfc1d7983d6428c03` | 523264 |
| `.4` | `3d6d44d3b3e9a9a3e41da98e8711e9f19a211fd8` | `45a15862e2bd8c8ce93073923b68f23bc6f3f848aa147ed1ce6340135e214207` | 580096 |

80/84个跟踪来源文件、原锁文件与两处共同harness覆盖均核对。Git blob和实际Windows检出换行不同的文件，逐个用完整SHA验证准确的LF→CRLF转换；没有修改证据来匹配仓库字节。4份共同harness/校验入口及164份原文件共168次来源核对，其中115份对应CRLF检出、53份直接匹配blob。它是独立构建，不沿用此前EXE身份。

## 实际六位置

固定interactive 22,000B、chunk1、observe0，内部CPU/墙钟至少2秒，顺序 `.3,.4,.4,.3,.3,.4`；单进程90秒、清理5秒。未重跑或补样，原完整oracle不变。

| sequence | 核心 | 自身区间外CPU/s | runner CPU/s | 全机未归属余额/s | 余额占全机总CPU区间 |
|---:|---|---:|---:|---:|---:|
| 0 | `.3` | 0.093750 | 0 | 0.500000 | 5.263158% |
| 1 | `.4` | 0.062500 | 0.015625 | 0.281250 | 3.333333% |
| 2 | `.4` | 0.078125 | 0 | 0.328125 | 3.888889% |
| 3 | `.3` | 0.078125 | 0 | 0.328125 | 3.860294% |
| 4 | `.3` | 0.062500 | 0 | 0.296875 | 3.492647% |
| 5 | `.4` | 0.078125 | 0 | 0.312500 | 3.703704% |

余额为全机busy减完整子进程CPU及runner同区间CPU，允许正负；可能包括其他guest活动、计数粒度和非原子端点。sequence0即使扣除自身完整工作，余额仍超过原5%环境护栏的数值；本诊断不应用该护栏判通过。结果仅支持“补齐自身计量不能消除这个新环境的全部余额”，不将余额归因某一进程，不解释旧hosted第24位置失败，也不证明物理宿主排他或频率稳定。

没有计算吞吐比、没有改原v1两批各112位置及5%护栏，也没有接受产品性能容差。`accounting_complete`仅表示诊断记录完整；`performance_gate=not_evaluated`，正式成本验收样本为0。

## 保留的入口缺口

首次source `eed0ab7` 的 [40509157581](https://github.com/JavaZeroo/termforge/actions/runs/40509157581)在31项准备测试中的路径边界比较失败，实际构建/测量0。夹具根路径未规范化；本地8.3别名复现该缺陷，但旧job实际临时路径形式未记录。只规范化测试ROOT，生产路径边界未放宽。首次四条普通CI也在Windows的同一测试失败，Linux成功；元数据与错误摘录留在 [validation.json](validation.json)。

修订 `c14adb9a` 的四条普通CI及setup2默认CI均通过；在已发布source创建setup2时，仅出现无路径过滤的默认回归40511533857，没有CPU或候选workflow run。CPU入口随后去掉paths过滤，并单独预声明entry3；创建事件与attempt1限制保持。没有删除重建旧分支、重跑旧run或用新观察覆盖失败。触发解释及其未知范围见合同。

## 归档和复核

审核后的[evidence.zip](evidence.zip)与提供者产物字节相同：123388B，SHA-256 `263a3bf8e229b89f1e443b12e522daf69d30de403affa0dc49fe674c0dd8f241`，artifact `12738430576`。75文件中74份有原始/公开哈希清单，24份sanity/诊断identity/观察流保持原字节；元数据移除子PID、长短路径改别名，不含checkout、缓存、进程清单或WindTerm私有产物。

完整构建日志、来源清单、原始计数与输出均在归档；[validation.json](validation.json)整理实际身份和结论。运行以下只读复核，不启动编译或测量，需包含固定来源提交的Git历史：

```powershell
python experiments/measure/results/2026-10-27-r54-cpu-hosted/verify.py
```

复核包含所有公开文件哈希、24份原字节流、来源及构建绑定、四次sanity、六次完整输出oracle和全部CPU等式。本地measurement40/40（19.436s），入口增量4/4（0.297s）；实际hosted测试与上述本地测试身份分别保留。

后续仍需受控完整A/B、allocator/projection扰动与组件归因、可靠WindTerm基线及用户确认的逐项预算。#54、#34和R2保持未完成，产品核心与后端继续作为候选。
