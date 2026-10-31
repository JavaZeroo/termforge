# R54：独立 hosted CPU 分量诊断

本次延续[六位置CPU边界合同](R54-CPU-DIAGNOSTIC.md)，回答在一个新GitHub-hosted Windows VM中，自身区间外CPU、runner CPU与全机余额分别多大。共享工作站的[一次实际诊断](results/2026-10-27-r54-cpu-boundary/README.md)已显示这些分量可以分开记录，但其活动水平不支持成本验收；旧hosted第24位置的原因仍未知。

## 固定执行

以 PR #106 的 `beeb64496b2e2898e8619fa5e9c39f490a3891c4` 为基点。新入口为[工作流](../../.github/workflows/r54-cpu.yml)，仅在分支`perf/grapheme-cpu-hosted-r54`首次创建push执行；拒绝attempt2及后续重跑。后续代码/导出器/证据修正不自动触发替代观察。原 `r54-cost.yml`、两批各112位置、原5%护栏及其失败记录不变。

一个新的`windows-2025` VM依次准备Python3.12、psutil7.2.2、Rust1.92.0，执行合同测试，然后[复建并审计](r54_build.py)原固定 `.3/.4` 及四次短正确性。仍是MSVC x64、Release、locked/offline、no-default-features、jobs1、独立target及两个共同harness覆盖；80/84来源文件、原锁文件、真实编译/链接日志和EXE哈希保持可追溯。它是新构建，不借用本地或上次hosted二进制身份。

构建/测试结束后只执行一次 `.3,.4,.4,.3,.3,.4`，全部为原interactive 22,000B、chunk1、observe0、内部CPU/墙钟至少2秒。使用未经修改的`r54_cpu_diagnostic`完整子进程/runner/全机计量，原完整输出oracle和来源前后审计继续执行。单子进程90秒、清理5秒；来源、输出、计数或退出不成立时立即停止，不补样。构建期限600秒、job30分钟；构建失败不进入诊断。

本次不跑空闲资格预检，不计算A/B成本比或吞吐回归，不接受或调整产品性能容差。`accounting_complete`只表示六份诊断记录完整。全机余额保留正负，可能包含其他guest活动、计数粒度和采样边界；不把它称为某个外部进程的精确CPU，也不能补造旧hosted缺失的全生命周期计数。两台宿主的绝对数值不能直接用于性能比较。

## 证据和公开范围

保留GitHub事件/run/attempt/commit、实际runner/映像、构建来源、host OS/CPU、原始计数及全部观察；分析时从提供者作业元数据独立核对身份。没有修改电源、优先级、亲和性或后台服务的步骤。物理宿主排他性与频率仍未知。

发布器的显式`cpu-boundary`模式只包含本次构建和六位置诊断的固定白名单，与原成本批次分开。它在任何staging之前核对本次raw schema/run/attempt/source，失败和不完整流也保留；测量、sanity及诊断入口identity的stdout/stderr字节不改；bundle内的构建identity日志保留完整内容并执行路径前缀替换。元数据中的实际长/短/转义路径改为别名，子PID及进程清单不公开；清单同时列原始/公开SHA和大小。绝不递归上传checkout、Cargo缓存或任意目录。新分支身份和schema不会被原默认发布模式接受。

只有下载并审核实际产物、源码/构建、全部原字节oracle和CPU等式之后，才将观察写入 #54。若此诊断支持改进后续环境计量，须另行冻结新协议与完整样本计划；不倒改原v1，也不以诊断完成代替受控A/B、allocator/projection及组件归因、预算提案和用户确认。
