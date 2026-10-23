# 证据索引

本页回答“结论来自哪里、如何重新验证”。工作区已有能力的盘点基点为 `2dc8d6d`（2026-10-23），R1 整合的新增验证对应 `9b16acc`。各条记录以自己的源码/二进制身份和范围为准；历史数据不拼成当前版本的新测量。

当前能力见[功能矩阵](FEATURES.md)，目标与路标见[总体计划](PLAN.md)。批次叙述不是当前状态清单。

## 当前实现的验收入口

| 结论 | 实现/命令入口 | 已有结果与限制 |
|---|---|---|
| 干净检出可构建和回归 | [贡献指南](../CONTRIBUTING.md)、[根 CMake](../CMakeLists.txt)、[CI workflow](../.github/workflows/build.yml) | `2dc8d6d` 对应 [Actions 39930814167](https://github.com/JavaZeroo/termforge/actions/runs/39930814167)，Windows 2025 / Ubuntu 24.04 已有成功记录；Linux 未构建 Windows 客户端 |
| R1 历史/键盘/粘贴窗口整合 | [源码、二进制、重复样本与跳过项](../experiments/shell/results/2026-10-23-r1-foundations/validation.json)、[运行入口](../experiments/shell/README.md) | `9b16acc` 对应实现的本地全量 19 个 CTest 套件通过；内部仍有 3 个跳过项。历史内容/原生拖动、键盘实际接收字节、粘贴途中取消/关闭各重复两轮；系统剪贴板与实际 IME 未据此验收，不是性能对照 |
| R1 系统剪贴板集成与剩余失败 | [前轮延迟渲染事实](../experiments/shell/results/2026-10-24-r1-clipboard-render/validation.json)、[此前就绪等待失败](../experiments/shell/results/2026-10-24-r1-clipboard-diagnostic/validation.json)、[Actions 40005747353](https://github.com/JavaZeroo/termforge/actions/runs/40005747353) | `98303f3`：实际系统复制两轮逐字往返通过，正常系统粘贴及底层套件通过；读取取消后的延迟 owner 明确报 `SetClipboardData-render / 1418`。Windows 23/24 CTest、总 run 失败，关闭场景未执行；错误阶段已定位，仍须区分已退出 requester 的晚发布结果与真正 fixture/产品错误，#14 未完成 |
| 真实 IME 环境门槛 | [成功盘点事实](../research/ime/results/2026-10-24-hosted-inventory-fixed/validation.json)、[Actions 40000823473](https://github.com/JavaZeroo/termforge/actions/runs/40000823473)、[先前失败](../research/ime/results/2026-10-24-hosted-inventory/validation.json) | `cc377d4`：六项只读查询全部完成，程序版本/哈希固定，六个 worker/Job 回收；机器 TIP 已注册，当前用户注册键/语言列表未发现固定 TIP，中文 Basic 仍 NotPresent。四个 verify 步骤均跳过，本轮只证明盘点；现有 provider 用户激活及真实组合/候选/提交仍须验收 |
| 现有 IME provider 激活实验 | [失败读回事实](../research/ime/results/2026-10-24-hosted-activation-readback/validation.json)、[显式 verify 40005751477](https://github.com/JavaZeroo/termforge/actions/runs/40005751477) | `98303f3`：盘点通过，固定用户语言设置返回后，读回投影生成 null 字段并在第五阶段失败，真实探针跳过。非枚举 Generic.List 的纯夹具复现了读回/混合语言过滤错误；显式枚举修复的 28 项本地测试通过，仍待 hosted 复验，不能据旧 null 报告认定实际 TIP 设置丢失 |
| 选择与复制纯模块 | [按值文本接口](../experiments/core/text_document.hpp)、[选择模块](../experiments/core/text_selection.hpp)、[独立内容测试](../experiments/core/selection_test.cpp) | `selection_core` 验证稳定位置、宽/组合字符、正逆选区、软换行合并/硬 CRLF/尾空白、历史和版本/容量边界；这不等于实际鼠标、菜单、系统剪贴板或完整 #13 验收 |
| 增量查找与原生控件 | [搜索模块](../experiments/core/text_search.hpp)、[独立内容测试](../experiments/core/search_test.cpp)、[查找控件](../experiments/shell/find_bar.hpp)、[真实控件测试](../experiments/shell/find_bar_test.cpp) | `search_core` 覆盖小预算持续输出、Unicode/软硬行、版本/淘汰与 query generation；`find_bar` 在自有非输入 desktop 检查 EDIT、按钮、键盘、焦点和回收。控件和模块测试不能代替完整会话查找 #15 验收 |
| 选择/查找窗口整合 | [精简身份与重复验证](../experiments/shell/results/2026-10-24-r1-selection-find/validation.json)、[窗口回归](../experiments/shell/test_shell.py) | 本地全量 24/24 CTest 套件通过；选择 12 阶段、查找 15 阶段各两轮，原生历史滚动同时回归。系统 UI 剪贴板、私有 station 剪贴板及 opt-in 参考注错存在内部跳过项，真实 IME 未运行；不据此关闭 #13–#18 或声明性能等价 |
| 本地 shell、工作区与回收 | [操作和场景说明](../experiments/shell/README.md)、[自动测试](../experiments/shell/test_shell.py) | 真实 cmd、独立非输入 desktop、自有 Job、输出/关闭/resize/双区域焦点等；输入为脚本消息，不是物理键盘延迟实验 |
| DPI 与会话缩放 | [审查和验证记录](archive/reviews/2026-10-23-ui-dpi-zoom.md)、[源/EXE 身份及回收](../experiments/shell/results/2026-10-23-dpi-zoom/validation.json)、[几何记录](../experiments/shell/results/2026-10-23-dpi-zoom/dpi.json) | 全量 CTest 14/14，最后边缘修复后 shell 11 项通过；32 组 renderer 度量组合。受控 DPI 96/144/192，系统窗口 DPI 为 96；真实跨屏未测 |
| 核心有限子集与恢复 | [核心支持合同](../experiments/core/README.md)、[Recovery 合同](../experiments/core/RECOVERY.md)、[resize 决策](adr/0005-physical-row-resize.md) | 手写状态预期、随机/逐字节拆包、未知序列与超长输入；不等于完整 VT、Unicode 或 TUI 认证 |
| 独立语义参考 | [差分入口](../experiments/differential/README.md)、[固定依赖](../experiments/differential/requirements.txt)、[结果摘要](../experiments/differential/results/2026-10-22/summary.json) | pyte 0.8.2 / wcwidth 0.2.13；95 次运行、1011 检查点，65 次共同子集一致、30 次精确已知差异；差异不算兼容通过，也不作为性能比较 |
| 进程生命周期和平台边界 | [ConPTY 探针](../experiments/conpty/README.md)、[跨环境记录](research/conpty-platform-check-2026-10-22.md)、[旧平台探针](../.github/workflows/conpty-compat.yml) | 受控子进程/孙进程、取消/超时、短批次资源回收；Windows 11 / Server 2025 有验证，Server 2022 稳态句柄门槛仍失败；长时泄漏未验收 |
| 自有窗口绘制正确性 | [呈现探针](../experiments/presentation/README.md)、[共享 renderer](../experiments/presentation/render.hpp)、[度量测试](../experiments/presentation/renderer_metrics_test.cpp) | retained/full RGB 一致、光标/字体/缺字负例及背景覆盖；DIB 与 BitBlt/GdiFlush 提交不等于实际上屏，`presented_done=null` |

常规复验从仓库根目录执行，依赖安装见贡献指南：

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure

# Windows 定向 shell 回归；根构建需包含全部 helper。
$env:TERMFORGE_BIN_DIR = (Resolve-Path build/bin/Release).Path
python -m unittest experiments.shell.test_shell -v
```

回放/实验 runner 的输出目录必须不存在。根 CTest 指向当前构建；单独执行时必须显式选择二进制，不借旧产物证明新提交通过。测试失败保留合成原始输出及身份；日常用户会话不自动录制或上传。

## WindTerm 目标与逆向事实

| 证据 | 来源/复现入口 | 可以支持的判断 |
|---|---|---|
| 固定功能和发行目标 | [固定 README](https://github.com/kingToolbox/WindTerm/blob/a8336c1d1e3dd1a981500dae8ef9eb42066de95f/README.md)、[2.7.0 release](https://github.com/kingToolbox/WindTerm/releases/tag/2.7.0)、[发行包校验流程](../benchmarks/tools/WINDTERM-LAB.md) | 官方功能声明与包身份；声明仍需运行时验收，不跟随未来 master 自动扩大范围 |
| 已公开/未公开边界 | [源码与发行物调查](research/windterm-audit.md) | 固定公开源码主要是组件/修改，无法据此构建完整 WindTerm；静态依赖与符号不证明运行时实现或授权 |
| 接收路径静态线索 | [接收路径报告](research/windterm-headless-2026-10-22.md)、[headless 工具入口](../research/README.md) | 指定路径 64 KiB ReadFile 请求上限、空→非空通知条件；不能推出完整线程/背压策略或性能收益 |
| 工作区结构/几何 | [首轮 UI 报告](research/windterm-ui-2026-10-23.md)、[主标签/字体报告](research/windterm-ui-typography-2026-10-23.md)、[探针与准备守卫](../research/ui/README.md) | 发行资源、定向二进制和隔离运行时交叉核对；默认缩放两次及 Qt 1.5 倍一次。旧首启自动导入和错误状态样本已明确排除，不能混作最终纯合成证据 |
| 可公开的 UI 精简结果 | [默认样本 1](research/ui-typography/normal-01.json)、[默认样本 2](research/ui-typography/normal-02.json)、[Qt 1.5 倍样本](research/ui-typography/scale-150.json) | 白名单矩形、字体、DPI 和身份；不是完整像素、逐字形 fallback、真实跨屏或性能证明 |
| 基础文字交互参考合同 | [行为合同](research/windterm-r1-text-contract.md)、[hosted 事实摘要](research/r1-text/hosted-39960732305.json)、[Actions 39960732305](https://github.com/JavaZeroo/termforge/actions/runs/39960732305) | `c386494` 的 text/menu-wrap 各两轮及 repeat 门槛通过：历史/选择/查找、键盘及实际 Select All/Find/Copy/Paste 菜单、精确 CF 软折行/CRLF/尾空白与严格粘贴基线均有证据；四轮 Job 清空。满足 #11 限定参考合同，不代表产品功能等价或性能验收 |

主对照为官方 Windows x64 2.7.0：ZIP SHA-256 `b05c3c7371340ff1302967247ffcd3a3ba27b237f72010e9157086143c453e50`，EXE SHA-256 `7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0`。最初本机安装的 EXE 身份不同；其历史静态报告不得冒充官方包结果。其他目标平台仍需各自固定包身份。

公开仓库只保留原创探针、事实描述和审核过的摘要。原始反编译、复制发行物、个人配置、原始插桩结果保留在 ignored `local/`；证据 ID 是本地索引，不是可访问的公共附件。规则见[来源与隐私](PROVENANCE.md)。

## 性能证据：已有工作与尚不能下的结论

| 工作 | 结果/复现入口 | 当前结论边界 |
|---|---|---|
| 官方 WindTerm 生产端烟测 | [摘要](../benchmarks/results/2026-10-21-windterm-smoke/summary.json)、[基准规范](../benchmarks/PROTOCOL.md) | 7,732 字节写出、人工见到结束标记；没有全内容正确性、消费/呈现时间和正式重复批次，不是 C0 性能基线 |
| 接收批量与通知合并 | [实验协议](../experiments/receive/README.md)、[120 次实验报告](research/receive-experiment-2026-10-22.md) | 原创固定输入、字节顺序、队列边界与背压；机制实验不等于 WindTerm 路径或端到端收益 |
| 连续 CPU 区间与校准 | [计量协议和命令](../experiments/measure/README.md)、[批次 A](../experiments/measure/results/2026-10-23-v2-a/manifest.json)、[批次 B](../experiments/measure/results/2026-10-23-v2-b/manifest.json) | 48 个核心样本/4 次校准；尚缺解码/状态/历史独立归因、Unicode/大历史、无插桩对照和更严格环境控制 |
| 输入到状态/提交 | [shell 端点定义](../experiments/shell/README.md)、[呈现端点定义](../experiments/presentation/README.md) | 现有脚本中断/后续命令时段与绘制提交有明确名称；不能当单键延迟或真实呈现，带完整画面检查的样本不是低扰动性能样本 |

目前没有足够证据判断 TermForge 距离 WindTerm 快或慢多少。正式结论必须同时提供负载/配置/环境身份、正确性、完成端点、观测开销、重复样本与限制。未知字段保持未知；历史校准未完成不转成要求用户反复手动运行的前置。执行规则见 [ADR-0002](adr/0002-automation-first-investigation.md)，核心选型重审见 [ADR-0003](adr/0003-core-reference-and-reuse.md)。
