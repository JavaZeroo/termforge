# 证据索引

本页回答“结论来自哪里、如何重新验证”。当前R1限定总验收固定于`ce9b24d`；早期盘点基点`2dc8d6d`和整合验证`9b16acc`保留为各自历史证据。每条记录以自己的源码/二进制身份与范围为准，历史数据不拼成新提交的测量。

当前能力见[功能矩阵](FEATURES.md)，目标与路标见[总体计划](PLAN.md)。批次叙述不是当前状态清单。

## 当前实现的验收入口

| 结论 | 实现/命令入口 | 已有结果与限制 |
|---|---|---|
| R1完整文字工作流与总出口（限定） | [同次系统组合及独立审计](../experiments/shell/results/2026-10-24-r1-system-workflow/validation.json)、[同源码既有回归](../experiments/shell/results/2026-10-24-r1-system-workflow/regression.json)、[协议与复现](../experiments/shell/WORKFLOW.md) | `ce9b24d`；[主CI 40045678550](https://github.com/JavaZeroo/termforge/actions/runs/40045678550) Windows38/Ubuntu24，本地38/38且内部8项skip另列；[显式verify 40045686776](https://github.com/JavaZeroo/termforge/actions/runs/40045686776)实际八会话两fresh，各26主点/13系统点/28动作、409/512观测、18实例回收。独立核191文件、58源码/5二进制身份、B原件51→57→58及目标隔离、两次actual revision绑定滚动、8夹测/19阶段窗/5短驻留，21真实产物反例全拒、两个最终帧已查看；同源码E2两轮与旧12组24轮/五组IME另审。结合R1-01–07满足限定总出口；无EXE独立重哈希/源到二进制证明，main内部skip未知，presented_done=null，不构成日常版、长期无泄漏或WindTerm性能等价 |
| R1 八会话 E2 数据工作流与运行观测 | [协议/自动入口](../experiments/shell/WORKFLOW.md)、[固定源码审计](../experiments/shell/results/2026-10-24-r1-eight-sessions/validation.json)、[既有能力复验](../experiments/shell/results/2026-10-24-r1-eight-sessions/regression.json) | 固定源码与独立复核：`72fe231` / [主CI](https://github.com/JavaZeroo/termforge/actions/runs/40040980384)，本地及同源码hosted各两轮。本地35/35（内部8项skip），主CI Windows35/Ubuntu21；[同源码真实IME复验](https://github.com/JavaZeroo/termforge/actions/runs/40040994121)为五模式28检查点。验收范围为每轮26检查点、18会话身份、235/256有界观测，覆盖五路洪流、阅读/Find/输入/缩放/淘汰、5次8→6→8和合成粘贴取消/关闭；实际窗口创建、字节/日志、worker/Job及非输入desktop回收分别复核。此历史data-only批次不等于同次真实CF/IME；完整组合见本表最新总验收 |
| E2 launcher-tree 资源接缝（历史批次） | [复用采样器的adapter](../experiments/shell/workflow_resources.py)、[同一固定源码审计](../experiments/shell/results/2026-10-24-r1-eight-sessions/validation.json) | 首个实际launcher样本在GO前取得；原始采样与脱敏observations逐条比对，公开记录可离线重算同时样本内存/handles峰值和CPU，sampler另组。scope为launcher-tree，不是UI-root；短命后代可能漏采、unknown保留。该历史批次观测成本显著且有调度缺口，100ms仅为请求间隔；其phase_windows=null，不能追溯补出校准。新校准与同次工作流见最新行，均不证明低扰动、性能或长期无泄漏 |
| R1 组合验收基础与既有能力复验 | [夹具协议](../experiments/shell/WORKFLOW.md)、[固定源码数据审计](../experiments/shell/results/2026-10-24-r1-workflow-foundation/validation.json)、[动作/Paste复验](../experiments/shell/results/2026-10-24-r1-workflow-foundation/regression.json)、[真实IME复验](../experiments/shell/results/2026-10-24-r1-workflow-foundation/ime-regression.json) | `5235f17` / [主CI 40034110925](https://github.com/JavaZeroo/termforge/actions/runs/40034110925)：Windows32/Ubuntu19套件，五场景两轮的54个公开文件重算；[verify 40034133987](https://github.com/JavaZeroo/termforge/actions/runs/40034133987)复核旧五模式28检查点、11空前缀。此项夹具不算真实Paste/IME、八会话或完整E1；E2独立证据见本表，其后完整组合见本表最新总验收。私有transport、未上传EXE无法从公开产物独立重哈希；无呈现/性能结论 |
| R1 统一文字动作限定验收 | [逐项对照](research/r1-action-comparison.md)、[最终动作审计](../experiments/shell/results/2026-10-24-r1-actions-accepted/validation.json)、[合同覆盖复核](../experiments/shell/results/2026-10-24-r1-actions-accepted/contract-coverage.json)、[真实 IME 交叉](../research/ime/results/2026-10-24-product-ime-contexts/validation.json) | `6a165c8` / [主 CI 40029637769](https://github.com/JavaZeroo/termforge/actions/runs/40029637769)：local26/system36各两轮，Find实际CF/中文重选、默认Return、Paste v2四轮25021字节及取消/关闭前缀、日志/帧/Job0/desktop删除均独立重算；[IME verify 40029638833](https://github.com/JavaZeroo/termforge/actions/runs/40029638833)五模式28检查点、9组reading上下文和候选/字节/回收另审。二进制只有hosted记录哈希，未上传独立重哈希；单场景不代替合同覆盖复核或 #18 完整工作流 |
| 干净检出可构建和回归 | [贡献指南](../CONTRIBUTING.md)、[根 CMake](../CMakeLists.txt)、[CI workflow](../.github/workflows/build.yml) | `2dc8d6d` 对应 [Actions 39930814167](https://github.com/JavaZeroo/termforge/actions/runs/39930814167)，Windows 2025 / Ubuntu 24.04 已有成功记录；Linux 未构建 Windows 客户端 |
| R1 历史/键盘/粘贴窗口整合 | [源码、二进制、重复样本与跳过项](../experiments/shell/results/2026-10-23-r1-foundations/validation.json)、[运行入口](../experiments/shell/README.md) | `9b16acc` 对应实现的本地全量 19 个 CTest 套件通过；内部仍有 3 个跳过项。历史内容/原生拖动、键盘实际接收字节、粘贴途中取消/关闭各重复两轮；系统剪贴板与实际 IME 未据此验收，不是性能对照 |
| R1 系统剪贴板完整粘贴及有限复制 | [最终独立审核事实](../experiments/shell/results/2026-10-24-r1-clipboard-accepted/validation.json)、[Actions 40010995723](https://github.com/JavaZeroo/termforge/actions/runs/40010995723)、[原 1418 失败](../experiments/shell/results/2026-10-24-r1-system-copy/validation.json) | `6efe3e2`：正常系统粘贴和读取占用/超限/取消/关闭均两轮通过；独立核对基线/重试/前缀、无迟到/Escape 字节、其他会话和 Job0/desktop 删除。晚发布仍记录真实 1418，未冒充成功。复制每轮另有 22 阶段与真实非空 Ctrl+Shift+C 2499 字节往返；软硬行/尾空白/DPI 和 Copy 弹出菜单不在该证据内 |
| 选择与复制纯模块 | [按值文本接口](../experiments/core/text_document.hpp)、[选择模块](../experiments/core/text_selection.hpp)、[独立内容测试](../experiments/core/selection_test.cpp) | `selection_core` 验证稳定位置、宽/组合字符、正逆选区、软换行合并/硬 CRLF/尾空白、历史和版本/容量边界；纯模块结果不能单独代替实际鼠标、菜单、系统剪贴板或完整 #13 验收 |
| R1 查找完整限定验收 | [固定提交的 artifact 独立审计](../experiments/shell/results/2026-10-24-r1-find-accepted/validation.json)、[Actions 40010995723](https://github.com/JavaZeroo/termforge/actions/runs/40010995723) | `6efe3e2` 的普通 15 阶段与流式 10 阶段各两轮通过；逐字 raw、阶段日志、24 个负例、BMP 和双会话/Job0/desktop 删除已复核。持续输出策略为项目决策；不替代 #17/#18 或性能验收 |
| 选择字节与受控 DPI | [源码身份、真实窗口与保留失败](../experiments/shell/results/2026-10-24-r1-selection-bytes/validation.json)、[系统 CF 两轮审计](../experiments/shell/results/2026-10-24-r1-styled-system-copy/validation.json) | 完整擦除清除旧 crop 来源后，真实软换行可重新合并；c206324 / 40014166528 的带 SGR43 样式尾两空格、硬 CRLF、96→144→192→96 逐字通过。ConPTY 普通尾空格规范化为 EL 的红样本保留，不恢复上游已消除的来源信息。此项不替代实际跨显示器验收 |
| R1 选择复制最终限定验收 | [原始字节/日志/哈希与回收审计](../experiments/shell/results/2026-10-24-r1-selection-accepted/validation.json)、[错误画面独立复核](../experiments/shell/results/2026-10-24-r1-selection-accepted/visual-validation.json)、[Actions 40019418276](https://github.com/JavaZeroo/termforge/actions/runs/40019418276) | ac1efba：v5 Copy 两轮各12基础+28交互阶段，实际菜单14、真实OpenClipboard:5、选区/sentinel保持、ordinary释放/join及同入口重试2499 UTF-8字节。context两轮各16阶段/3会话，隐藏恢复、同id版本覆盖失效、原生EDIT Copy、带选区关闭后目标隔离、raw及Job0/desktop删除通过。本地28/28 CTest另有7个内部跳过，不算系统通过；该批未覆盖其后的#17/#18；全部WindTerm等价仍未验收 |
| 基础键盘与真实产品 IME | [四组产品场景独立审计](../research/ime/results/2026-10-24-product-ime-accepted/validation.json)、[显式 verify 40017974845](https://github.com/JavaZeroo/termforge/actions/runs/40017974845)、[键盘字节回归](../experiments/shell/results/2026-10-23-r1-foundations/validation.json) | 固定 154dc48：22 检查点、独立 raw/进度/帧、候选绝对锚定和移动、两区域/输出/resize/125% zoom/受控120 DPI/rename/关闭通过。8个正常receiver stop/join/exit0，关闭目标由Job取消 exit91且空接收，内外Job0；不写成全部exit0。原失败与data-only修复由[工具说明](../research/ime/README.md)保留，不替代真实跨屏/其他IME或 #17/#18 |
| 增量查找与原生控件 | [搜索模块](../experiments/core/text_search.hpp)、[独立内容测试](../experiments/core/search_test.cpp)、[查找控件](../experiments/shell/find_bar.hpp)、[真实控件测试](../experiments/shell/find_bar_test.cpp) | `search_core` 覆盖小预算持续输出、Unicode/软硬行、版本/淘汰与 query generation；`find_bar` 在自有非输入 desktop 检查 EDIT、按钮、键盘、焦点和回收。控件和模块测试不能代替完整会话查找 #15 验收 |
| 选择/查找窗口整合 | [最新边界与六轮窗口验证](../experiments/shell/results/2026-10-24-r1-window-boundaries/validation.json)、[前轮整合](../experiments/shell/results/2026-10-24-r1-selection-find/validation.json)、[自动回归](../experiments/shell/test_shell.py) | 本地全量 25/25 CTest 通过；选择每轮 33 阶段、普通查找每轮 15 阶段、持续输出查找每轮 10 阶段，各两轮。原生菜单、高亮与回收、局部导航、运行中替换/取消/关闭及原始接收字节有独立预期；该轮尚未覆盖系统入口、DPI/软折行/尾空白及产品 IME；后续证据见本表独立记录，原轮内部系统跳过项未算通过。保留先前负例夹具漏证据的失败 |
| 本地 shell、工作区与回收 | [操作和场景说明](../experiments/shell/README.md)、[多 shell 生命周期验证](../experiments/shell/results/2026-10-24-r2-shell-lifecycle/validation.json)、[主区分屏验证](../experiments/shell/results/2026-10-24-r2-main-split/validation.json)、[自动测试](../experiments/shell/test_shell.py) | cmd、PowerShell 5.1/7、Ubuntu WSL 的启动/目录/尺寸/正常退出及失败恢复；主区一个真实左右 split 两轮独立输出、隐藏后台、焦点和关闭证据。默认工作区复验通过；任意嵌套、拖放/搜索/颜色和全部运行时错误提示未验收 |
| 当前持续输出中断回归 | [#36](https://github.com/JavaZeroo/termforge/issues/36)、[ConPTY 输入机制研究](research/conpty-ctrl-c-input.md)、[原 R1 历史证据](../experiments/shell/results/2026-10-23-r1-foundations/validation.json) | 当前 Windows 11 标准构建的 39 个 CTest 自然执行结束，shell 套件 normal/resize 超时，37 项通过。ETW 确认 cmd 输入模式为 0x7 且后续命令未进入 cooked-read；同次插桩样本的洪流 ETX 写入后 `CtrlFlags=0`、`CtrlCSeen=0`，尚未进入 conhost 控制分派，但调试扰动和管道读完成缺口仍保留。未减少负载、延长超时或跳过失败，#36 保持 OPEN |
| 当前工作区几何与交互回归 | [#38](https://github.com/JavaZeroo/termforge/issues/38)、[默认全套记录](../experiments/shell/results/2026-10-25-workspace-state-v2/validation.json) | 含 v2 模型的默认 Release 40 个 CTest 为 39 通过；`workspace_state` 通过，但 shell 有 normal/resize 超时、ui-main 空错误、分屏矩形、重命名和会话行准备六类失败。私有 desktop/Job 原始样本保留，不能只说只有 #36，也不由 #33 模型通过抵消，#38 保持开放 |
| DPI 与会话缩放 | [R2 字体/显示合同](milestones/R2-font-display.md)、[真实跨屏验证](../experiments/shell/results/2026-10-24-r2-monitor-font/validation.json)、[受控 DPI 旧证据](../experiments/shell/results/2026-10-23-dpi-zoom/dpi.json) | 32 组 renderer 度量；受控 96/144/192。两台真实显示器均为 96 DPI，窗口往返两次保持 rename 焦点、隐藏/可见会话和网格，`distinct_dpi=false`；不同 DPI、物理输入和实际呈现仍未验收，`presented_done=null` |
| 工作区 v2 配置模型（#33 开发中） | [模型与平台验证](../experiments/shell/results/2026-10-25-workspace-state-v2/validation.json) | 有界版本化配置、Unicode 标题/路径、v1 迁移、损坏分类、非法 UTF 和原子写入在 Windows/Ubuntu 通过；干净 launcher 也生成并调用尺寸辅助程序。该记录限于模型切片；后续单会话接入另列，#33 保持开放 |
| 单会话工作区恢复（#33 开发中） | [核心与双进程验证](../experiments/shell/results/2026-10-25-workspace-restore-core/validation.json) | 真实 ui-edit/ui-restore 新进程恢复 cmd、Unicode 标题、默认窗口/布局，原有两轮测试通过；配置独立解码、启动写入抑制、关闭前快照及 Job 清理已核。该切片未覆盖多会话，后续验证另列，#33 保持开放 |
| 多会话工作区恢复（#33 开发中） | [两轮双进程与几何证据](../experiments/shell/results/2026-10-25-workspace-restore-multi/validation.json) | cmd 与 PowerShell 的独立目录、Unicode 标题、100%/150%/75% zoom、split 成员/底部焦点和非默认窗口恢复两轮通过；实际 cmd 目录、PowerShell 主版本、HWND/网格及恢复前后配置逐字段核对。修正模型错误的 split 焦点等值约束；真实 v1/空/损坏/写失败与最终关闭失败仍未验收，#33 保持开放 |
| 核心有限子集与恢复 | [核心支持合同](../experiments/core/README.md)、[Recovery 合同](../experiments/core/RECOVERY.md)、[resize 决策](adr/0005-physical-row-resize.md) | 手写状态预期、随机/逐字节拆包、未知序列与超长输入；不等于完整 VT、Unicode 或 TUI 认证 |
| 独立语义参考 | [差分入口](../experiments/differential/README.md)、[固定依赖](../experiments/differential/requirements.txt)、[2026-10-25 摘要](../experiments/differential/results/2026-10-25/summary.json) | pyte 0.8.2 / wcwidth 0.2.13；95 次运行、1011 检查点，70 次一致、25 次精确已知差异；六类裁决保留五项参考差异并修正一项自研实现，差异不算兼容通过，也不作为性能比较 |
| 外部原生核心候选 | [固定版本/许可与复现入口](../experiments/candidates/alacritty/README.md)、[本地 Windows 摘要](../experiments/candidates/alacritty/results/2026-10-24-windows/summary.json)、[构建验证](../experiments/candidates/alacritty/results/2026-10-24-windows/validation.json)、[Windows/Ubuntu CI](https://github.com/JavaZeroo/termforge/actions/runs/40117504346) | `alacritty_terminal 0.26.0` Apache-2.0；独立空目标目录 `--locked` 构建，2 个 Rust、3 组 headless、5 个 Python 检查通过，记录屏幕/历史/备用屏和真实回复；历史双平台 candidate workflow 已通过。当时无性能、真实应用或产品路线结论，后续重审另列 |
| R2 核心路线决定 | [ADR-0003](adr/0003-core-reference-and-reuse.md)、[平台/维护/接口审查](research/2026-10-25-core-route-review.md) | 决定 R2 新能力复用候选 Term/Processor，保留自有传输、呈现和自研差分回退；重审时旧锁定图 MSRV 1.88，ED1/ED2、上游未发布修复和稳定行身份是整合前置。Windows 7/旧 macOS/glibc、x86/ARM 未验收；重审时未接入产品；候选产品切片后续另列，当前 Windows CI Vim 失败由 #37 保留 |
| 自研与原生候选共同轨迹 | [差分说明](../experiments/differential/README.md)、[逐项裁决](../experiments/differential/decisions.md)、[2026-10-25 Windows 摘要](../experiments/differential/results/2026-10-25-candidate-common-v3/summary.json) | 95 次、1011 检查点，90 次一致、5 次已裁决参考差异；pyte 为 70 次一致、25 次保留差异。ED1 候选错误与 ED2 历史策略已分开裁决并锁定签名；`semantic_acceptance=false`，不用于性能或路线结论 |
| termforge.1 兼容与身份补丁 | [来源/补丁台账](../experiments/candidates/alacritty/PROVENANCE.md)、[v4 共同矩阵](../experiments/differential/results/2026-10-25-candidate-common-v4/summary.json)、[真实应用 v2](../experiments/differential/results/2026-10-25-real-applications-v2/summary.json)、[fuzz v2](../experiments/differential/results/2026-10-25-fuzz-v2/summary.json) | 固定 0.26.1-dev 上游资源修复及 ED1/ED2、Row ID/换行元数据、安全 swap；140 单元+1 doc、23 差分 Python 测试通过，共同 95 次与真实应用 3 次全部一致，fuzz 为 2 已知差异+6 一致。仅 headless 切片，C++ FFI 与产品能力未完成 |
| termforge.2 C++ 拥有值适配与候选产品接入 | [接口边界](../experiments/core/CANDIDATE.md)、[验证记录](../experiments/core/results/2026-10-25-candidate-adapter/validation.json)、[v5 共同矩阵](../experiments/differential/results/2026-10-25-candidate-common-v5/summary.json) | 142 单元+1 doc、24 差分、15 计量及 candidate_core/runtime_observation 通过；真实 Vim 两轮进入/编辑/60×12 resize/退出写盘、主屏恢复及真实光标回复经有界 InputQueue 验证。默认构建保留自研回退；完整候选 CTest 为 37/42，#27/#29/#36 和实际呈现未关闭；#37 的独立 Windows CI 验收另列 |
| 扩展核心能力轨迹 | [能力矩阵说明](../experiments/differential/README.md#扩展能力轨迹25)、[2026-10-25 摘要](../experiments/differential/results/2026-10-25-capabilities-v2/summary.json) | 备用屏/滚动区/查询各 5 次为自研明确未支持，Recovery 5 次与候选一致；resize 5 次为 `resize_difference`，双方实际 2×6，候选重排与自研物理裁剪不同。语义接受未覆盖 |
| 真实应用核心轨迹 | [真实应用说明](../experiments/differential/README.md#真实应用核心轨迹25)、[2026-10-25 摘要](../experiments/differential/results/2026-10-25-real-applications/summary.json) | Windows cmd、more.com、Vim 真实 ConPTY 进入/操作/退出和文件结果已核验；分页器与 Vim 回放一致，shell 仅有已裁决 ED2 差异。`presented_done=null`，不是产品 GUI/TUI 验收 |
| Vim CI 捕获握手（#37） | [本地三应用摘要](../experiments/differential/results/2026-10-25-real-applications-v4/summary.json)、[握手与负例验证](../experiments/differential/results/2026-10-25-real-applications-v4/validation.json) | 以 `[New]`、`-- INSERT --` 和实际文本作为状态握手，再有序 Escape/`:wq`；保留实际编辑器路径及失败阶段、原始输出和 Job 回收。真实应用 Python 4 项与 differential CTest 通过；本地默认全套 38/39 唯一失败仍为 #36，远程 CI 未通过前 #37 保持开放 |
| Vim 事件文件握手与 Windows CI（#37） | [v5 三应用摘要](../experiments/differential/results/2026-10-25-real-applications-v5/summary.json)、[事件与负例验证](../experiments/differential/results/2026-10-25-real-applications-v5/validation.json)、[CI 40190890251](https://github.com/JavaZeroo/termforge/actions/runs/40190890251) | 以 Vim 写入的 `VimEnter=READY`、`InsertEnter=INSERT` 文件握手，避免消息被重绘覆盖；`c284c86` 的 Windows Server 2025 产物确认 Vim 9.2、握手/文件/退出和3次轨迹一致，launcher 依赖也已生成尺寸辅助程序。真实应用、fuzz、project/interactive 全部自然通过，限定 CI 验收完成；不代替完整 shell、其他 TUI、实际呈现或 R2 验收 |
| 有界 fuzz 与最小回归 | [fuzz 说明](../experiments/differential/README.md#有界-fuzz-与固定回归25)、[运行摘要](../experiments/differential/results/2026-10-25-fuzz/summary.json)、[首次失败](../experiments/differential/results/2026-10-25-fuzz/first-failure.json) | seed 0–3 的 8 次 whole/byte 均有分类；原失败与最小例固定完整签名，改变输入负例通过。可持续扩大种子范围，未登记新差异仍失败；少量种子不是广泛兼容证明 |
| 核心 feed 与投影成本 | [计量说明](../experiments/measure/README.md)、[2026-10-25 project 原始批次](../experiments/measure/results/2026-10-25-core-compare/) | 12 组×2 个连续 1 秒交错样本；纯 feed CPU 中位数自研 42.61–44.42、候选 28.12–29.70 ns/字节，观察开启自研 50.89–52.82、候选 539.40–580.09。分配范围不同且无插桩对照未完成，不作路线或 WindTerm 性能结论 |
| 核心交互 feed 与投影成本 | [2026-10-25 interactive 原始批次](../experiments/measure/results/2026-10-25-core-interactive/) | 22,000 字节固定 ASCII 编辑回放，1/16/64/22000 分块，16 组×2 个连续 1 秒交错样本；纯 feed CPU 中位数自研 64.64–76.49、候选 62.82–90.22 ns/字节，观察开启自研 89.50–102.21、候选 1834.75–2041.90。不是键盘到呈现延迟或真实 TUI 验收，不作路线结论 |
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

# Windows E2 data-only八会话；驱动启用资源采样，需已有依赖及全量helper。
$env:TERMFORGE_TEST_ARTIFACTS = Join-Path (Get-Location) 'local/workflow-check'
python -m unittest experiments.shell.test_workflow_sessions -v
```

真实CF/IME组合只由指定一次性GitHub托管环境的显式`r1-ime.yml mode=verify`或`alpha-release.yml workflow_dispatch`运行；普通本机入口拒绝，不设置伪hosted环境标志。下载`r1-workflow-system-windows-2025`白名单产物后，`research.ime.workflow_system_checks.validate_workflow_system_public(case_directory)`可逐case离线重算实际GO/native流、raw内容、动作、滚动和校准资源；不依赖上次runner的verified布尔值。

回放/实验 runner 的输出目录必须不存在。根 CTest 指向当前构建；单独执行时必须显式选择二进制，不借旧产物证明新提交通过。测试失败保留合成原始输出及身份；日常用户会话不自动录制或上传。

## 首个 Alpha 发行物

[v0.1.0-alpha.1](https://github.com/JavaZeroo/termforge/releases/tag/v0.1.0-alpha.1)的[发行验证](releases/v0.1.0-alpha.1-validation.json)固定源码、annotated tag、三个发布资产的重新下载哈希、/MT依赖审计、hosted门槛和Windows 11同一ZIP复验。它交付R1体验包，未验收R2开发版、长期稳定或WindTerm性能等价。原始hosted合成证据保留在对应Actions白名单产物，保留期30天。

## WindTerm 目标与逆向事实

| 证据 | 来源/复现入口 | 可以支持的判断 |
|---|---|---|
| 固定功能和发行目标 | [固定 README](https://github.com/kingToolbox/WindTerm/blob/a8336c1d1e3dd1a981500dae8ef9eb42066de95f/README.md)、[2.7.0 release](https://github.com/kingToolbox/WindTerm/releases/tag/2.7.0)、[平台范围台账](../research/windterm-2.7.0-platform-scope.md)、[发行包校验流程](../benchmarks/tools/WINDTERM-LAB.md) | 官方功能声明与包身份；#23 已用固定源码、release API 和资产元数据固定四类发行平台/架构及声明下限，保留安装说明差异和未知系统。声明仍需运行时验收，不跟随未来 master 自动扩大范围 |
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
