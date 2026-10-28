# C++ recovery 核心双构建：限定准备结果

[#152](https://github.com/JavaZeroo/termforge/issues/152)的实现源为 `7721985399a14667d564811029cd8c93308bcb36`，基点 `05f0065f4eb19ce5a76065d18f2247bcc731e285`。交付同源码 recovery 包装/无自定义 allocator 包装两个目标、独立协议、严格正确性校验和固定短 sanity；既有 strict `core_measure` 的 v2/calibration 保留。[协议、固定输入及执行前计划](../../RECOVERY-PREPARE.md)，[脱敏完整记录](validation.json)。

## 实际验证与来源

- 一次 Visual Studio 17 2022 x64 Release 构建三个目标成功，MSVC19.42.34433.0、`/MD /O2 /Ob2 /std:c++17`；三个构建均0警告/0错误。两份 recovery 的真实 compile/link tlog，在仅归一目标路径/产物名及 wrapper 宏后相同；没有依据目标名推测配置。
- `dumpbin /symbols` 核对实际对象：旧 strict 和新 recovery 包装对象有普通/数组 `operator new` 的本源定义；noalloc 对象均无本源定义，普通 `new` 是外部引用，数组 `new` 未出现。它仅证明本工具包装有无，不说明 CRT 或产品整体无插桩。编译器、linker、dumpbin、对象、EXE、命令和符号输出均保存身份。
- 旧 strict 的 `--identity` 仅执行一次，exit2、stdout空，为新入口缺失的真实CLI红例；未改变旧CLI。新协议的普通5项纯测试通过；两次早期公开 seam 红例分别是尚无模块及尚无 sample validator，日志原件保留，未伪称旧性能退化。
- 冻结 plan SHA256 `e26f714c6c5a97962c1be72c573e373398edddb48ca54b0ab73a5d9fe9217ddc` 的**唯一38进程批次通过**：两个实际 recovery 身份小样例、24个固定正确性样本、12个预期 CLI 拒绝。project chunk1/4096/65536、interactive chunk1/16/22000、每目标均observe0/1及CPU floor0.1s。没有重试、补样或改变原件。
- 全部样本的独立格位、宽度/样式、光标、100行历史、淘汰数和最终摘要通过。observe1还执行逐轮投影/哈希；包装＋observe1真实计数，无包装两种observe均为null。身份小样例实际处理非法UTF-8及后继ASCII，拒绝只更换标签的strict构建。
- 新协议与旧 `test_measure` 相关回归同次 **14/14、3.723s**；旧strict/v2/实际calibration、坏JSON/退出/超时保留及新协议拒绝分列，没有运行完整default/Qt矩阵。测试耗时不是核心性能指标。
- 执行源7文件与18份历史结果前后逐字节一致；18项实际构建产物/工具/命令日志后核对一致。单批raw manifest SHA256 `4a978f34bae06be0f761e8a8b9bfa9cdf151c6e331cc7b8e573098f3d462ba22`。全部76份新stdout/stderr流先保存原字节再验证；公开仅保留字节身份及固定正确性投影，区间/进程/构建原件留local。

这是共享 Windows11 build26200/UBR9711 宿主的构建和短正确性准备，不是受控性能样本。CPU floor0.1s沿用短sanity参数，不是用户批准的性能门槛；每进程外90s、内部区间60s，整批独立硬上限null，预算未放宽。

新工具仍包含区间门槛、准备、oracle和输出代码；observe1包含真实投影成本。初始recovery/24×80/history100对应Qt初始化，仅复用该核心参数；本工具不包含Qt事件循环、10ms drain、ConPTY、输入延迟、绘制或呈现。`receive_seconds/presented_done=null`，正式性能样本0，`performance_gate=not_evaluated`。未计算吞吐排名/回归百分比，未验收#54/#34、WindTerm等价或R2；未来正式空闲批次及数值容差另行声明并由用户确认。
