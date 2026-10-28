# 自主 C++ recovery 核心：双构建计量准备

本切片由 [#152](https://github.com/JavaZeroo/termforge/issues/152)承接，仅建立正确性和构建身份。正式路线见 ADR-0007/0008；性能容差尚未由用户确认。本工具不包含 Qt 事件调度、ConPTY、绘制或实际呈现，不称产品热路径基线或 WindTerm 性能等价。

## 构建与协议

三个可执行文件共享 `measure.cpp`。既有 `core_measure` 保留 strict、全局 allocator 包装、schema v2 及原 calibration；历史文件不修改。两个新增目标如下，源、编译/链接策略和核心参数相同；实际命令的目标路径/产物名自然不同，额外代码开关仅为 allocator 宏：

| 目标 | Parser / 初始屏幕 / history | 本工具自定义 new/delete |
|---|---|---|
| `core_recovery_measure` | recovery / 24×80 / 100 | 编译在内 |
| `core_recovery_measure_noalloc` | recovery / 24×80 / 100 | 不定义 |

这三个参数对应正式 Qt 的初始化，不代表包含 Qt 实际执行路径。新目标只接受 `project` / `interactive`；直接 state 路径未纳入新协议。`--identity` 先用非法 UTF-8 后接 ASCII 的独立小样例，实际核对 replacement、后继字符和 recovery 诊断；不是只返回一个模式标签。样例在计量区间外执行。

新协议为 `termforge-recovery-core-measure-v1`，明确记录 mode、网格、history、包装状态、route/chunk、负载及 observer 语义。旧 `calibrate` 在两个新目标均明确拒绝，不能用旧 runner 自动校准它们。

- observe=0：构造/前轮析构、feed、状态/历史维护与区间门槛；没有每轮投影。
- observe=1：还包括每轮完整投影与哈希检查。包装构建同时计数普通 new；无包装构建的分配指标始终 `null`，不能解释为零分配。
- 两种构建仍有计时、门槛、独立最终 oracle 和输出代码。关闭本工具 allocator 包装不证明整个 CRT 或产品没有插桩。

## 固定负载与区间

| route | 固定源字节 | SHA256 | 最终状态 FNV-1a64 |
|---|---|---|---|
| project | `(x×76 + CRLF)×1000`，78000B | `02ff60ca95c13ab3e41909685634ee6a800310dc39a07a98ac6965b0c57f2b14` | `5455070173120736870` |
| interactive | `(abcdefgh ESC[D ESC[D ESC[D Z BS Q CRLF)×1000`，22000B | `405d574c15d33d81e17745173dac95ed8c0082884e9692c71fbc67b5a588c148` | `8292075482910706113` |

复用已有独立逐格、宽度、样式、光标、历史数量/淘汰预期及完整状态摘要。预热和最后一轮检查不移入计量区间。区间仍是一次 CPU 结束减开始、每8轮墙钟门槛及至少约250ms的 CPU 查询间隔；不累加短轮次、不估算扣除观察开销。最后一次析构在区间外，实际计入 `rounds-1` 次析构。输入准备、recovery 身份小样例、oracle 和序列化在区间外。

原 v2 与新协议不得拼接样本或汇总。正确性与共享核心代码可复用，旧 strict/包装数据不转记为 recovery 数据；parse/state 两路径也不相减解释净 parser 成本。`receive_seconds` 和 `presented_done` 始终显式 `null`。

## 预声明的单次短 sanity

主代理释放构建/负载租约后，才运行此固定批次。此处记录执行前计划，不表示已经执行：

```powershell
# 使用明确的 Release 构建输出；三个目标的实际编译参数和对象另外留证。
cmake --build <build> --config Release --target core_measure core_recovery_measure core_recovery_measure_noalloc
python experiments/measure/recovery_sanity.py --wrapped <wrapped.exe> --noalloc <noalloc.exe> --out <new-local-directory>
```

`recovery_sanity.py` 没有重复、随机调度或性能汇总参数。固定为以下 **38 个核心进程**，每个最多一次：

1. 两个目标各一次 `--identity`，先全部通过才开始负载。
2. 每目标 project 的 chunk=1/4096/65536、interactive 的 chunk=1/16/22000，均各 observe=0/1、CPU floor=0.1s，共24次短正确性样本。
3. 每目标各拒绝一次 calibration、parse、state、chunk=0、observe=2、NaN duration，共12次反例。

floor=0.1s沿用既有短 sanity 参数，不是性能容差。核心内部连续区间上限仍60s，每个直接核心子进程外层期限90s；整批无独立硬时限，不将单进程期限称整程上限。不自动重试、补样、改负载或覆盖旧目录；首个身份/退出/字节/JSON/oracle/来源失败即停止。原始 stdout/stderr 先按字节保存，再解析和判定，保留失败及部分输出。此纯核心工具不产生子树，也不宣称原生 GUI/Job 验收。

旧 strict/v2/calibration 按现有 measurement 回归独立验证；其结果与新38进程批次分列。普通 CTest 仅新增纯协议测试，不自动执行新 sanity 或正式成本批次。

## 实际构建来源审计

构建前冻结 Git commit、CMake/三个核心头/测量源/校验器/执行器哈希，以及实际 generator、Release、架构、CRT、编译器/链接器路径、版本与可执行文件哈希。保留两份目标的完整真实编译/链接参数；仅本工具 wrapper 宏不同，不能依据目标名假定配置相同。

保留实际 `.obj` 与 EXE 的大小/SHA256。Windows 使用同一工具链的 `dumpbin /symbols <实际目标对象>`，核对全局普通/数组 `operator new` 的定义：包装对象应有本源定义，无包装对象不得有本源定义，外部 CRT 引用另列；不能只检查 `--identity` 的自报字段。对象路径从实际构建日志/项目文件取得，不猜路径。原始符号和绝对路径只留 local。

批次对源和两EXE逐次前后重算，完整批次后再核对全部冻结输入；对象/编译器身份由外层构建审计绑定，不由 runner 冒充已验证。公开前审查来源和隐私，仅发布合成负载与脱敏事实。正式空闲环境、重复测量、Qt调度/运输/绘制归因、原版对照和预算提案均另行准备。
