# Node 纯夹具在 hosted windows-2025 的耗时分布（#144）

本片回答 [#144](https://github.com/JavaZeroo/termforge/issues/144) 的一个可测子问题：那条**纯 Node 夹具**在这台 CI 镜像上到底要多久，10 秒固定预算有多少余量。

## 方法

dispatch-only 作业 `windows-2025-node-contract-timing`（[工作流](../../../../.github/workflows/conpty-compat.yml)），只做测量、不跑原版、不改任何夹具或期限：

- 命令与失败测试完全一致：`node research/ui/test_raster_native_contract.js`；
- **顺序 60 次** + **20 批 × 4 并发**（共 140 次），每次新进程，10 秒超时，记录耗时与超时；
- 记录 Node 版本与平台，产物 JSON 上传。

## 结果（run [40864960451](https://github.com/JavaZeroo/termforge/actions/runs/40864960451)，Node v22.23.4）

| 指标 | 值 |
|---|---|
| 顺序 p50 / p90 | 0.063 s / 0.078 s |
| 顺序最大 | **1.469 s（第 1 次，冷启动）** |
| 顺序 min（warm） | 0.062 s |
| 并发批次最大 | 0.125 s |
| 超时次数 | **0 / 140** |

第 1 次 1.469 s 是冷启动（首次执行 node.exe + 首次读夹具源文件），随后稳定在 ~60–80 ms；并发的 4 路也没有把单次推高。

## 解释

- 10 秒预算相对 warm 耗时约有 **130 倍余量**，相对冷启动也有约 7 倍余量；
- 因此已记录的两次 CI 失败（PR141 与 #178 的 push 运行，均 `timed out after 10 seconds`）**不能用夹具自身的计算成本解释**；
- 现象更像宿主级停顿（首次执行时的安全软件扫描、VM 调度/存储短暂阻塞）导致 Node 进程在 10 秒内没有完成，但本批在空闲镜像上无法复现该停顿，根因仍为 unknown。

## 边界与不做

- 只测空闲 windows-2025 镜像；未在“62 项 CTest 同时段”、其它作业并跑或安全软件扫描中的状态下测量。
- 未修改夹具、未延长 10 秒预算、未重跑原失败；本批不关闭 #144/#38。
- 记录的是诊断耗时，不是性能预算或容差。
