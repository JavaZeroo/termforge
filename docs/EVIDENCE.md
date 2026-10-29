# 证据索引

本页回答“结论来自哪里、如何复验”。当前能力见[功能矩阵](FEATURES.md)，路标见[总体计划](PLAN.md)，R2 验收见[出口检查表](milestones/R2-local-terminal.md)，执行状态以 [GitHub 任务入口](TRACKING.md)为准。源码/二进制身份、原始计数、失败阶段和负例保留在各份原记录，本页按能力与调查分类，不维护逐批开发日志。

每条证据仅支持其固定配置，不能把历史成功、局部模块、未合并分支拼成当前产品验收：

产品方向和确认依据见 [ADR-0006](adr/0006-confirmed-project-direction.md)。Windows PTY 路线已由用户确认采用[随包固定微软 ConPTY](adr/0007-bundled-microsoft-conpty.md)，正式核心继续自主 C++，当前 GUI 采用 [Qt Widgets](adr/0008-cpp-core-qt-widgets.md)；新组合的整合与验收仍未完成。历史 Alacritty 与 Win32 比较及限定合同保留事实身份，不转记为新产品组合能力或重要差异的批准；关键性能容差仍待可靠基线后与用户明确。

| 配置 | 证据定位 |
|---|---|
| 自主 C++ 主屏 ASCII DECAWM | [有限合同与红绿](../experiments/core/results/2026-10-29-cpp-decawm/README.md)、[固定验证身份](../experiments/core/results/2026-10-29-cpp-decawm/validation.json)：抽取原版14–18阶段79B，strict/recovery各235个字面前缀分块用例；初次定向8/9的缺构建依赖失败、补建后的core单项31测试通过分列。跨1049/宽字/resize、origin/margin、GUI/固定ConPTY运输和完整TUI均不由本项验收 |
| 自主 C++ 行结构发布与精确身份查询 | [首片合同与固定夹具](../experiments/core/results/2026-10-29-order-query/README.md)、[验证身份](../experiments/core/results/2026-10-29-order-query/validation.json)：`[1,3,4,5,7,6]` 的精确 rank（7→4、6→5、2 缺失）、文档序比较与强制 Find 导航、结构 revision 边界、发布拒绝与光标夹取；筛选的 8 项核心 CTest 与本地候选 `candidate_core` 通过，首个推送版本的 CI 红（回放保留历史仍为上一帧宽度）与按物理行策略适配后的绿分列，完整默认 Release 构建 exit0。未封住公开 lines/history，运行中跨重排的增量 Find、区域 LF、Qt/固定 ConPTY 与性能均未验收 |
| 自主 C++ 限定 1049 协议 | [固定结果](../experiments/core/results/2026-10-29-cpp-1049/README.md)、[验证身份](../experiments/core/results/2026-10-29-cpp-1049/validation.json)：文字/光标/颜色及 pending-wrap、重复进入与读者失效；55项中Qt1项分列，13条内部skip及旧负例判据失败保留。未验收非颜色属性/全网格背景、其他模式、真实TUI/IME/DPI、呈现或性能 |
| 自主 C++ 双缓冲存储与文档 epoch 基础 | [固定结果](../experiments/core/results/2026-10-29-buffer-state-r2/README.md)、[验证身份](../experiments/core/results/2026-10-29-buffer-state-r2/validation.json)：拥有状态、共享 ID、隐藏帧 resize 回滚与跨缓冲句柄失效；Qt/candidate 关闭配置的完整 50 项及 12 条内部 skip 分列。未接 private CSI，不是原版模式、Qt 产品路线或完整 #27 验收 |
| Qt 基础 Unicode 文本字节 | [红绿与联合验证](../experiments/shell/results/2026-10-29-qt-unicode-input/README.md)、[固定联合身份](../experiments/shell/results/2026-10-29-qt-unicode-input/validation.json)：公开Qt事件到独立ReadConsoleW接收端，整/分事件代理项、焦点/隐藏/关闭、1024B整包与NUL、两目标隔离。2b6d源码一次61项分列Qt3/其余58，13内部skip、旧红例与27原件保留；不是实际IME、字素/字形、原生Unicode窗口、DPI或性能验收 |
| Qt cmd 标签与退出前回收 | [固定结果](../experiments/shell/results/2026-10-28-qt-tabs/README.md)、[验证身份](../experiments/shell/results/2026-10-28-qt-tabs/validation.json)：双 cmd 隔离、失败页、关闭与四个实际事件循环边界；Qt 1 项和其他 49 项诊断及内部跳过分列，旧中断轮不拼接 |
| Qt 基础动态网格与真实 child 尺寸 | [固定结果](../experiments/shell/results/2026-10-29-qt-resize/README.md)、[原冻结与查询](../experiments/shell/results/2026-10-29-qt-resize/validation.json)、[平台区域反例与夹具修订](../experiments/shell/results/2026-10-29-qt-resize/platform-area-validation.json)、[0ff 隔离原生补充](../experiments/shell/results/2026-10-29-qt-resize/native-validation.json)：GUI/core、child、错误和字体度量红绿；678 原生极窄失败及 135 条 child 回复保留；0ff 新离屏 57 项与冻结包一次原生成功分列，原生保留全部 40 条 CSBI、实际顶层区域 120px→11 列及 root/Job 回收。两次离屏均分开 Qt 2 项、其余 55 项诊断及 13 条内部 skip；退出竞态旧尺寸保留；原生窗口仅 DPI 96，不是原版 reflow、真实不同 DPI、物理呈现或旧 system 失败修复 |
| Qt app-local 固定微软 CRT 与隔离 Windows 加载 | [固定结果](../experiments/shell/results/2026-10-29-qt-crt-deploy/README.md)、[完整运行时与验证身份](../experiments/shell/results/2026-10-29-qt-crt-deploy/validation.json)：原 `cf8` 52 文件首次加载失败与仅追加 CRT/原许可后的成功分列；新源码构建、原生窗口、有限 cmd/关闭、offscreen 及 Job 清理独立记录。不是完整发行、跨 DPI/IME 或物理呈现验收 |
| Qt 正式入口的独立 hosted 构建 | [固定结果](../experiments/shell/results/2026-10-28-qt-cpp-ci/README.md)、[验证身份](../experiments/shell/results/2026-10-28-qt-cpp-ci/validation.json)：固定 SDK、VS 2026 hosted 与真实 cmd 的 offscreen / OwnedJob 检查；原依赖换行和 generator 失败保留。没有原生上屏、完整部署哈希或产品包验收 |
| 自主 C++ + Qt Widgets + 固定微软 ConPTY 正式目标 | [ADR-0007](adr/0007-bundled-microsoft-conpty.md) / [ADR-0008](adr/0008-cpp-core-qt-widgets.md) 记录用户确认；新组合的实现与验证另行留证，不引用旧默认或 Alacritty 通过结果代替 |
| 既有默认/Alpha：自主 C++ + 系统 ConPTY + Win32 | R1 限定总验收和有限 VT/Unicode 子集；与当前 Qt/固定后端目标组合分列 |
| Alacritty 历史参考：`termforge.4` + 系统 ConPTY | [固定来源/补丁台账](../experiments/candidates/alacritty/PROVENANCE.md)、[适配边界](../experiments/core/CANDIDATE.md)、[#39 字素合同](research/streaming-graphemes.md)；结果仅证明其固定配置，不转为自主 C++ 能力 |
| PR #50 历史联合基线：`termforge.4` + 固定官方 ConPTY | [Draft PR](https://github.com/JavaZeroo/termforge/pull/50) / [#47](https://github.com/JavaZeroo/termforge/issues/47)；[联合基线](research/joint-conpty-baseline.md)已固定，产品回归未全绿；该基线当时的 resize/工作区失败由 #51/#63 后续在原实验分支限定闭合，#52 GUI Vim 及新组合完整验收仍未完成。[#53 的未完成粘贴、负例及原八会话证据](https://github.com/JavaZeroo/termforge/blob/848cb0e621a291f732cc0e046d0b2fab6a77d962/docs/research/resize-replay-cost.md)已按限定合同整合到实验父分支；不包含同次真实 CF/IME 或完整官方矩阵验收 |

R1 固定总出口的源码为 `ce9b24d`，早期 `2dc8d6d` / `9b16acc` 仍分别只代表原记录。源 VT、原生控制台 CHAR_INFO、transport、末端状态、自有 DIB、绘制提交和真实呈现是不同检查点；未知端点保持未知。

原版模式依据见 [1049 直达终端观察](research/windterm-alternate-reference-2026-10-28.md)与[验证身份](../research/ui/results/2026-10-28-alternate-reference/validation.json)：26 阶段精确输入、DSR 和文档投影相互绑定，原版重复进入的光标行为与通用幂等假设不同；样式、完整 active grid 和本地 Shell 运输继续保留未知。

[#151 origin/wrap/margin有限观察](research/windterm-origin-wrap-reference-2026-10-29.md)取得一次固定91×25直达终端的25阶段、25条bareDSR及25文档快照，[数据与发布审计](../research/ui/results/2026-10-29-origin-wrap-reference/README.md)保留完整NAWS、源/回复/文档分层、Node与源码身份、主动终止和旧false输出路径别名。严格出口普通/-O逐字节相同；初次Windows夹具CI红、真实路径别名反例及仅fixture规范化的修正另列，原公开字节保留，不推定CI使用8.3。它只为自主C++下一切片提供有限依据，不是完整active grid、产品模式、固定ConPTY或性能验收。

[#146 SGR1 固定样本调查](../research/ui/results/2026-10-29-sgr1-reference/README.md)取得五阶段/五 grab、28 对 H/space 完整有序像素签名；SGR0/SGR1 控制在采样条件下均不可分，进入/重复进入/返回无唯一分类。内部字重、SGR1 是否受字体/profile 偏好影响及全部非颜色属性仍未知；没有据此更改 C++ 策略或验收 #123/#126/#27。

原版输出自动化见 [cmd 正确性工具](../benchmarks/tools/WINDTERM-CMD.md)及[固定结果](../benchmarks/tools/results/2026-10-28-windterm-cmd/README.md)：小 ASCII 全文、生产事件和 Job 回收已有无人值守闭环，路径元字符修复及各执行源码分列。它没有取得 C0、完整历史、消费/真实呈现端点或正式性能基线。

[#133 设置读取诊断](../benchmarks/tools/results/2026-10-29-c0-settings-reference/README.md)在独立受控配置差异下核对 536B 全文，并分别读到 producer console / 原版 view 窗口 120×40、document capacity 23；QWidget 字体仅为元数据，28 条 dot-key 设置仍不可读。完整 C0、glyph/字体请求有效性、10000 行历史、autocomplete、呈现及性能继续未知。

显式官方后端的旧 `.3` 实现及独立生命期/双 SHA 拒绝记录见[实验合同](https://github.com/JavaZeroo/termforge/blob/e0b4d67e50bda6fe08cc02b43a475137133310b3/docs/research/experimental-conpty-backend.md)与[冻结验证](https://github.com/JavaZeroo/termforge/blob/e0b4d67e50bda6fe08cc02b43a475137133310b3/experiments/conpty/results/2026-10-26-reviewed-backend/validation.json)。联合源码 `3e5b96d` 的[三配置清单](../experiments/conpty/results/2026-10-26-joint-backend/validation.json)分别记录默认43/45、候选/system 48/48、显式官方46/48（shell套件超时，未完成项仍未知）；原resize/Vim各一红一绿、粘贴两红。新恢复/worker/工作区失败由 #61–#63 承接，旧失败不因其他轮成功变成通过。

后续合并推送的两个独立失败见[原件审计](../experiments/shell/results/2026-10-26-postmerge-ci/validation.json)：main `098f9ab` 工作区第二轮保存 stage2 [#72](https://github.com/JavaZeroo/termforge/issues/72)，实验分支源码 `af563ba` 的 681 高度选择场景 [#73](https://github.com/JavaZeroo/termforge/issues/73)。两次 CI 实际均为默认自研核心/system；return2、Job0、desktop 已删除，原生退出/EOF 与逐运行 EXE 绑定仍未知。它们不等同已修 #61/#66，另一批 PR 成功不能覆盖这些失败。

## 产品能力的限定验收

新路线的[首个 Qt/C++ 会话证据](../experiments/shell/results/2026-10-28-qt-cpp-entry/README.md)绑定 `0fd35e2`：完整 CTest 47/47，其中 Qt 新入口 1 项、旧 system 诊断 46 项，内部 12 条跳过记录分列。固定 cmd 的输入/750 正文墨迹、自然退出、启动错误与人工关闭有实证；这不迁移旧 Win32/Alacritty 能力，也不是完整 R2、原生窗口呈现或性能验收。

| 范围 | 结果与复现入口 | 可以支持的结论与限制 |
|---|---|---|
| R1 完整文字工作流（限定总出口） | [同次系统组合及独立审计](../experiments/shell/results/2026-10-24-r1-system-workflow/validation.json)、[同源码既有回归](../experiments/shell/results/2026-10-24-r1-system-workflow/regression.json)、[协议与复现](../experiments/shell/WORKFLOW.md)、[主CI 40045678550](https://github.com/JavaZeroo/termforge/actions/runs/40045678550)、[显式verify 40045686776](https://github.com/JavaZeroo/termforge/actions/runs/40045686776) | 固定源码 ce9b24d 的同次八会话 Copy→Paste→真实拼音、后台负载、滚动状态/提交端点及回收，满足 R1 限定总出口。内部 skip 与无独立 EXE 重哈希缺口保留；presented_done=null。 |
| R1 统一文字动作限定验收 | [逐项对照](research/r1-action-comparison.md)、[最终动作审计](../experiments/shell/results/2026-10-24-r1-actions-accepted/validation.json)、[合同覆盖复核](../experiments/shell/results/2026-10-24-r1-actions-accepted/contract-coverage.json)、[真实 IME 交叉](../research/ime/results/2026-10-24-product-ime-contexts/validation.json)、[主 CI 40029637769](https://github.com/JavaZeroo/termforge/actions/runs/40029637769)、[IME verify 40029638833](https://github.com/JavaZeroo/termforge/actions/runs/40029638833) | 实际菜单/快捷键、Find 的真实 CF、中文重选、默认 Return、Paste 和上下文/回收按合同核对。不能由单项动作替代同次组合。 |
| R1 系统剪贴板完整粘贴及有限复制 | [最终独立审核事实](../experiments/shell/results/2026-10-24-r1-clipboard-accepted/validation.json)、[Actions 40010995723](https://github.com/JavaZeroo/termforge/actions/runs/40010995723)、[原 1418 失败](../experiments/shell/results/2026-10-24-r1-system-copy/validation.json) | 正常系统粘贴及占用/超限/取消/关闭均有重复证据；晚发布 1418 原失败保留。最终选择复制及组合需引用各自证据。 |
| R1 选择复制最终限定验收 | [原始字节/日志/哈希与回收审计](../experiments/shell/results/2026-10-24-r1-selection-accepted/validation.json)、[错误画面独立复核](../experiments/shell/results/2026-10-24-r1-selection-accepted/visual-validation.json)、[Actions 40019418276](https://github.com/JavaZeroo/termforge/actions/runs/40019418276) | 实际 Copy 菜单、占用反馈/保持/重试、原生 EDIT、版本覆盖失效及关闭后目标隔离已限定验收；不等于全部 WindTerm 行为。 |
| R1 查找完整限定验收 | [固定提交的 artifact 独立审计](../experiments/shell/results/2026-10-24-r1-find-accepted/validation.json)、[Actions 40010995723](https://github.com/JavaZeroo/termforge/actions/runs/40010995723) | 固定场景的普通/流式查找、原始字节、日志、负例和回收已审计；不是正则、大历史或性能验收。 |
| 基础键盘与真实产品 IME | [四组产品场景独立审计](../research/ime/results/2026-10-24-product-ime-accepted/validation.json)、[显式 verify 40017974845](https://github.com/JavaZeroo/termforge/actions/runs/40017974845)、[键盘字节回归](../experiments/shell/results/2026-10-23-r1-foundations/validation.json)、[工具说明](../research/ime/README.md) | 固定 Microsoft 拼音的真实候选、组合/提交、区域/输出/resize/zoom/控件和关闭已审计；正常退出与 Job 取消分列，不推广到所有 IME 或真实异 DPI。 |
| 本地 shell、工作区与回收 | [操作和场景说明](../experiments/shell/README.md)、[多 shell 生命周期验证](../experiments/shell/results/2026-10-24-r2-shell-lifecycle/validation.json)、[主区分屏验证](../experiments/shell/results/2026-10-24-r2-main-split/validation.json)、[自动测试](../experiments/shell/test_shell.py) | 固定 cmd、PowerShell 5.1/7、Ubuntu WSL 的启动/目录/尺寸/自然退出；一个主区 split 的输出、后台/焦点/关闭。任意嵌套、拖放及全部错误场景未验收。 |
| DPI 与会话缩放 | [R2 字体/显示合同](milestones/R2-font-display.md)、[真实跨屏验证](../experiments/shell/results/2026-10-24-r2-monitor-font/validation.json)、[隔离 Windows 11 环境](research/r2-win11-display-environment.md)、[受控 DPI 旧证据](../experiments/shell/results/2026-10-23-dpi-zoom/dpi.json) | 旧产品证据为受控 96/144/192 与真实同96-DPI跨屏。隔离 VM 另有实际96/120-DPI输出、独立Win32可见窗口两轮往返及guest请求deadline/Job回收；不代替正式Qt产品、物理输入或实际呈现验收。 |
| 多会话工作区恢复（#33 限定切片） | [两轮双进程与几何证据](../experiments/shell/results/2026-10-25-workspace-restore-multi/validation.json) | 两轮双进程恢复 cmd/PowerShell、目录、标题、zoom、split 与非默认窗口；逐字段/实际子进程核对。 |
| 工作区迁移与回退保护（#33 限定切片） | [两轮恢复与配置证据](../experiments/shell/results/2026-10-25-workspace-recovery-guards/validation.json) | v1 延迟写入/迁移、空配置、损坏回退及未知 v3 原文保持；与多会话/写失败证据共同构成 #33 限定验收。 |
| 工作区写入与关闭故障（#33 限定切片） | [两轮故障与重试验证](../experiments/shell/results/2026-10-25-workspace-write-failure/validation.json) | 原子替换失败保持原文，重试保存；关闭失败保留窗口，显式放弃后才回收。放弃不写成持久化成功，#38/R2 仍独立验收。 |
| R2 流式字素与固定 emoji（#39） | [合同](research/streaming-graphemes.md)、[红/绿及实际场景验证](../experiments/shell/results/2026-10-26-streaming-graphemes/validation.json) | 已合并 #39：.4 的固定 ZWJ/keycap 原子格/选择/绘制和单列 resize。766 个规范用例只验证分段 helper；64 标量超限按明确无损分片，不宣称完整字素布局或完整 Unicode/IME。 |
| 真实备用屏切换（#27 限定切片） | [五阶段原生快照与候选验证](../experiments/shell/results/2026-10-26-alternate-screen/validation.json)、[候选接口](../experiments/core/CANDIDATE.md) | 真实 1049h/1049l 五阶段的源 VT、CHAR_INFO、样式/光标恢复及候选历史隔离；origin/wrap、margin/resize 交叉与 #27 整体验收另做。 |
| 局部滚动区域（#27 限定切片） | [原生阶段快照、独立 transport 重放与候选验证](../experiments/shell/results/2026-10-26-scroll-region/validation.json)、[候选接口](../experiments/core/CANDIDATE.md) | 真实 DECSTBM 局部滚动的源、原生快照与 transport 独立重放；区域外保持/历史隔离通过，不要求控制字节原样回显。 |
| ConPTY resize 重放、历史与 Find/选区（#29） | [限定合同](adr/0005-physical-row-resize.md)、[重放与历史验证](../experiments/core/results/2026-10-26-resize-replay/validation.json)、[候选接口](../experiments/core/CANDIDATE.md) | 固定 861B resize 重放的行身份、Find/选区、连续同步、历史合并/清空与上限；不等于所有 resize/reflow、真实 DPI 或 WindTerm 运行时。 |
| R2 复杂标量序列输入（#30 限定切片） | [真实键盘接收验证](../experiments/shell/results/2026-10-26-unicode-keyboard/validation.json)、[场景与复现](../experiments/shell/README.md) | 固定 ZWJ/keycap 通过 WM_CHAR/ConPTY/ReadConsoleW 精确接收；只证明输入保真，不证明绘制、字素宽度或完整应用键盘模式。 |
| 自有窗口绘制正确性 | [呈现探针](../experiments/presentation/README.md)、[共享 renderer](../experiments/presentation/render.hpp)、[度量测试](../experiments/presentation/renderer_metrics_test.cpp) | 自有 DIB 的 retained/full RGB、字体/光标/缺字负例与覆盖；BitBlt/GdiFlush 提交不等于实际上屏，presented_done=null。 |
| 进程生命周期和平台边界 | [ConPTY 探针](../experiments/conpty/README.md)、[跨环境记录](research/conpty-platform-check-2026-10-22.md)、[旧平台探针](../.github/workflows/conpty-compat.yml) | 受控后代、取消/超时和短批次回收；Windows 11/Server 2025 有固定记录，Server 2022 稳态句柄门槛仍失败。长期无泄漏未知。 |

## R2 未闭调查与已修正的夹具

| 范围 | 结果与复现入口 | 可以支持的结论与限制 |
|---|---|---|
| 行存储封存与结构单一发布（#169 第二片） | [固定结果](../experiments/core/results/2026-10-29-sealed-row-storage/README.md)、[验证身份](../experiments/core/results/2026-10-29-sealed-row-storage/validation.json) | `Screen::lines/history` 私有，只读口 + 23 文件/363 处迁移；Find overlay 改文档 rank；默认 Release 构建 exit 0、核心 8/8、扩展快测 20/20、candidate 源 /W4 编译 0。无新 VT/重排行为；运行中跨重排 Search、Qt 与候选运行由宿主作业复核。 |
| 默认核心缩小与重绘交错（#156） | [固定四-case机制诊断](../experiments/shell/results/2026-10-29-resize-repaint-witness/README.md)、[脱敏验证](../experiments/shell/results/2026-10-29-resize-repaint-witness/validation.json) | A/C保留2499B，B/D在第五段重绘首次失效且行分配路径不同；只见证机制，原CI切点/根因未知，未修生产。witness结束后Job仍active1，发起一次终止后归零；成员身份/逐句柄退出未知，不称全树自然退出。 |
| 系统 ConPTY resize 后的客户端尺寸回复（#145 未关闭） | [有界重复采样](../experiments/conpty/results/2026-10-29-resize-reply-r145/README.md)、[固定验证身份](../experiments/conpty/results/2026-10-29-resize-reply-r145/validation.json) | 新增 tag 化探针：hosted 2000 次交替 resize + 500 次全新会话、本机 200 + 100，均无陈旧回复，buffer/window 无分歧，resize VT 全部先于回复，回复中位 8.7–16.1ms。只否定“resize 后立即查询存在必然竞态”；原两条 CI 失败、原始探针/5 秒等待与 #145/#38 状态不变。 |
| 原生命周期探针重复与控制台测试后采样（#145 未关闭） | [固定结果](../experiments/conpty/results/2026-10-29-probe-repeat-r145/README.md)、[验证身份](../experiments/conpty/results/2026-10-29-probe-repeat-r145/validation.json) | 隔离重复 40×4=160 会话 0 红；先跑 conpty_ctrl_c/conpty_margin 后采样 20×4=80 会话，1 红但 resize 回复正确（resized=1、120x40、exit 37），原因是会话后线程基线 4→6 超过 1s 结算窗口。旧尺寸回复仍未复现；该线程稳态红归 #38 族，不并入 #145 根因。 |
| 持续 cmd 输出与原始 Ctrl-C 回归（#36 未关闭） | [受监督入口](../experiments/conpty/README.md#持续-cmd-输出与-ctrlc-独立回归)、[三次 fresh 公开记录及本地字节复核](../experiments/conpty/results/2026-10-26-ctrl-c-retest/validation.json) | 原百万次输出、单次 ETX、五秒期限和退出 37 保留；三次 fresh 原始字节/Job0 本地重算。后续成功未解释历史失败，未改产品 Ctrl-C 路径。 |
| 持续输出中断的原失败记录 | [#36](https://github.com/JavaZeroo/termforge/issues/36)、[ConPTY 输入机制研究](research/conpty-ctrl-c-input.md)、[原 R1 历史证据](../experiments/shell/results/2026-10-23-r1-foundations/validation.json) | 原 Windows 11 回归中的 normal/resize 超时与 ETW/插桩观察；调试扰动及管道读完成缺口保留。不是当前全套结果，后续未复现不能抹去此失败。 |
| controller B 正常结束后 cmd 退出未验收（#44） | [原失败脱敏记录](../experiments/shell/results/2026-10-26-controller-exit/validation.json)、[新退出诊断及有界重放](../experiments/shell/results/2026-10-26-controller-exit-observation/validation.json)、[关闭合同](https://github.com/JavaZeroo/termforge/issues/44) | 原 child 收完 32B、退出 0/join/Job0；cmd 只留下 close 后 0xC000013A，close 前未知。新观察分列输入、wait、pre-close 与清理；多轮未复现不构成历史修复，继续阻塞 #38。 |
| 原 10 秒纯夹具超时的阶段保留（#40 未关闭） | [限定合同](research/inventory-timeout-checkpoints.md)、[正常与受控超时对照](../research/ime/results/2026-10-26-inventory-timeout-checkpoints/validation.json) | 保持原 10 秒期限；超时后保留有界阶段、原异常和回收。受控挂起对照已通过，但历史 CI 的阶段/根因仍未知，#40 不因此关闭。 |
| Node 纯夹具阶段与失败判定（#144 未关闭） | [工具合同](research/node-contract-deadline.md)、[固定三 case 与身份](../research/ui/results/2026-10-29-node-contract-deadline/README.md) | 固定02aac源码的一次正式批次：正常passed0/23cp、受控挂起timed_out99/6cp、受控异常failed1/11cp，均根回收/Job0；CLI0只表示预定义结果齐全。协议尾、版本漂移及离线出口白名单P2的纯反例分列；出口普通/-O各15项纯测试，原件和旧公开字节保留。旧CI根因/整程硬界仍null，不接原CI执行入口，新增CTest仅注册纯数据回归，不关闭#144/#38。 |
| 纯 AST 夹具的生命周期记录 | [#40](https://github.com/JavaZeroo/termforge/issues/40)、[计时、进度与回收验证](../research/ime/results/2026-10-26-inventory-lifecycle/validation.json)、[专用 CI 40253727745](https://github.com/JavaZeroo/termforge/actions/runs/40253727745) | 旧 10 秒 AST 夹具及专用 CI 记录启动、AST 和 Job0；原超时根因未确定。诊断增强见 #40 阶段保留，不由一次 CI 成功结项。 |
| 原生 margin 逐字段合同与修复候选（#56 仍开放） | [合同、源码与路径](research/conpty-native-margin-path.md)、[独立 oracle](../experiments/conpty/margin_contract.py)、[固定运行](../experiments/conpty/results/2026-10-26-native-margin-path/validation.json) | 四阶段文字/格属性/光标独立判据；系统与官方原生 final 仍红，官方80列两次原5秒生命周期失败保留。共享单 margin 源码支持小 MIT fork 候选；patch 仅可应用，未编译、未运行、不计修复。 |
| ConPTY 系统/固定官方后端的同源 margin A/B（#41、#47 未关闭） | [合同与第一方来源](research/conpty-margin-backends.md)、[固定 175B 与 16 case 结果](../experiments/conpty/results/2026-10-26-margin-backends/validation.json) | 相同 175B、两网格/两 mode/两 fresh：系统末端 margin 错，固定官方后端的末端正确；CHAR_INFO 仍错。禁用 VT 由窄屏反例拒绝；仅诊断可行性，不证明 PR #50 或 main 联合整合通过。 |
| 实验输入 worker 持块与停止（#62） | [sender fence 合同](research/sender-fence.md)、[固定源码与实际字段](../experiments/shell/results/2026-10-26-sender-fence/validation.json) | `519a3f3` 的系统/官方各两进程、每次 stop/cancel/竞争，共 12 条实际 worker 记录，均 join/Job0；另有各一次原 focus 单测和各 2/2 纯 CTest。明确修正旧 never-read/output-first 不能保证 16KiB 的夹具假设，同 EXE 旧顺序仍红。实现仅在 PR #50 实验整合，main 未包含；probe 未链接候选 VT，纯 EOF 为模拟，#53 UI/3MB/768B/八会话及完整官方矩阵未验收。 |
| Native Vim 捕获目录隔离 | [原因与复验](research/vim-fixture-isolation.md)、[固定原件审计](../experiments/conpty/results/2026-10-26-vim-fixture-isolation/validation.json) | 实验分支同旧 EXE 的兄弟运行一绿一红；修正新目录隔离后完整模块9/9。仅闭合 #52 native 夹具原因；GUI stage59 仍开放，未知与受扰动探针分列。 |
| 工作区几何与交互的固定记录 | [#38](https://github.com/JavaZeroo/termforge/issues/38)、[工作区几何与输出验证](../experiments/shell/results/2026-10-26-workspace-geometry/validation.json)、[resize 限定证据](../experiments/core/results/2026-10-26-resize-replay/validation.json) | 固定 27f376d 的客户区/布局/输出、默认/候选恢复及窄屏记录；其完整回归仍有 #36/#38 红项，不代表当前所有配置或 R2 通过。 |
| 短窗口选区夹具与 hosted 四 shell 前置（#38） | [固定锚点回归](../experiments/shell/test_shell.py)、[准备入口](../tools/prepare_hosted_shells.py)、[夹具收尾合同](research/hosted-wsl-fixture-cleanup.md)、[云端结果与剩余阻塞](https://github.com/JavaZeroo/termforge/issues/38) | 短窗口保留相同选择断言；仅指定一次性 hosted VM 准备固定 Ubuntu。夹具强制收尾与产品自然退出分列，未知归属/回收仍失败。 |
| 工作区恢复的运输完成端点（#38 夹具修正） | [限定合同](research/workspace-restore-transport-oracle.md)、[原失败及默认/候选验证](../experiments/shell/results/2026-10-26-workspace-transport-oracle/validation.json) | OSC 标题中的重复标记与 column-one CUP 误导旧端点；新判定保留完整 UTF-8、期限、负载及恢复断言。仅修夹具，不宣称修复产品恢复。 |
| 工作区恢复的命令编辑前缀（#61） | [BS 因果反例及复验](../experiments/shell/results/2026-10-26-workspace-restore-bs/validation.json)、[运输完成合同](research/workspace-restore-transport-oracle.md) | 原失败中的 PowerShell 命令重绘 BS 污染当前记录；真实 LF 后仍须完整输出与新提示符，回显/拼接/非法封装拒绝保留。仅修完成 oracle，原 30/40 秒与产品断言不变。 |
| 工作区保存的空记录重绘（#72） | [原 122 B 因果回归与三配置结果](../experiments/shell/results/2026-10-27-workspace-empty-cr/validation.json)、[运输完成合同](research/workspace-restore-transport-oracle.md) | 两次 CI 失败记录逐字节相同；空且未编辑的记录在输出前允许裸 CR，仍须完整独立标记与新提示符。默认/system、候选/system、候选/官方各两轮保存/恢复/钳制共 18 次通过；最终完整回归、跳过和未闭合项独立记录。 |
| WSL 原生尺寸程序返回边界（#80） | [机制、反例与复验](research/wsl-size-return.md)、[各阶段固定记录](../experiments/shell/results/2026-10-27-wsl-size-return/validation.json) | 尺寸输出后提前发送 exit 的受控 stage6 红例；实际事件握手修前红、修后与原两轮通过。默认/system 完整 46/46，12 个内部跳过单列；全 shell 提示符方案的 cmd stage5 失败保留。原 hosted 样本同因未知，其他配置、实际呈现及 #81 准备超时未由此验收。 |
| hosted 准备子命令边界（#81） | [白名单与观测合同](research/hosted-shell-command-boundaries.md)、[原边界身份](../experiments/shell/results/2026-10-27-hosted-command-boundaries/validation.json)、[#158输出后验身份](../experiments/shell/results/2026-10-29-hosted-output-boundary/validation.json) | 调用/返回/超时与worker部分记录保留；新增worker/Job回收后有界输出计数和完整marker布尔，未知不当0，原命令/预算/oracle不变。历史23项含真实隔离Job，新切片普通/-O各32项仅纯/mock，身份分列；旧输出未留存及旧超时根因仍未知，不重跑取样。 |
| 选择复制夹具错误边界（#85） | [观测、控制与保护合同](research/selection-clipboard-boundaries.md)、[原失败及固定回归](../experiments/shell/results/2026-10-27-selection-clipboard-boundaries/validation.json) | 失败后记录固定阶段、读写目的、原错误及打开窗口/数据所有者类别，明确非原子；清理异常不覆盖主错误。默认/system 本地 46/46，13 个内部跳过；真实 CF 控制需 hosted 验证，原 OpenClipboard 5 根因仍未知，不作修复宣称。 |
| 候选文字观测与回显端点（#38 夹具修正） | [同源码前后对照及反事实](../experiments/shell/results/2026-10-26-native-observation/validation.json)、[标准文本投影](../experiments/core/text_projection.hpp) | 观测改用标准文本投影，回显完成后才全选；原断言和负例保留。修正夹具时序/投影，不宣称产品选择策略已变或 R2 全绿。 |
| 拒绝负例的读取前停止（#46 夹具修正） | [原记录、反事实及 fresh 验证](../experiments/shell/results/2026-10-26-receiver-stop/validation.json)、[公开空数据夹具](../experiments/shell/fixtures/rejected-order-before-read.json) | 旧 oracle 错误拒绝“首读前已停止”；同一空数据/零读原件红→绿，正常读取约束保留。限定夹具修正，不解释 #44 的退出。 |
| 候选 GUI 合法空标题（#45） | [真实 GUI 前后对照](../experiments/shell/results/2026-10-26-empty-title/validation.json)、[转换入口](../experiments/presentation/render.hpp)、[持续回归](../experiments/shell/candidate_title_test.cpp) | 真实空 OSC 标题使旧 UTF 转换抛异常；修后两 fresh GUI 正常，非法非空反例保留。不回填旧 normal-1 未知异常的根因。 |
| More 尾部 ECH 与捕获时序（#42 限定修复） | [合同及来源](research/more-capture-ech.md)、[修复前后与同输入对照](../experiments/differential/results/2026-10-26-more-ech/validation.json)、[历史触发记录](../experiments/shell/results/2026-10-26-margin-state/validation.json) | Recovery ECH 缺失和旧捕获过早分开修正，同原件 whole/byte 红→绿；完整 More 提示、单 q/自然退出/EOF/Job0 独立核对。候选 pending-wrap 差异仍保留。 |

## 核心路线、参考与历史验证

| 范围 | 结果与复现入口 | 可以支持的结论与限制 |
|---|---|---|
| 构建与 CI 入口 | [贡献指南](../CONTRIBUTING.md)、[根 CMake](../CMakeLists.txt)、[CI workflow](../.github/workflows/build.yml)、[Actions 39930814167](https://github.com/JavaZeroo/termforge/actions/runs/39930814167) | 历史 2dc8d6d 的 Windows/Ubuntu 构建记录；当前入口由根 CMake/CI 定义。Linux 核心 CI 不代表 Linux 客户端。 |
| 核心有限子集与恢复 | [核心支持合同](../experiments/core/README.md)、[Recovery 合同](../experiments/core/RECOVERY.md)、[resize 决策](adr/0005-physical-row-resize.md) | 自研有限 VT 子集、恢复与物理行裁剪合同；手写预期和拆包不能替代完整 VT/Unicode/TUI 认证。 |
| R2 候选比较与历史路线决定 | [ADR-0003](adr/0003-core-reference-and-reuse.md)、[平台/维护/接口审查](research/2026-10-25-core-route-review.md) | 历史 Term/Processor 实验及传输/呈现/自研差分证据保留。按 ADR-0006，Alacritty 当前为待评估候选，正式核心选择由 #34 提交用户确认；历史平台/维护评估不等于客户端支持或产品选型批准。 |
| 初始外部核心可行性（历史） | [固定版本/许可与复现入口](../experiments/candidates/alacritty/README.md)、[本地 Windows 摘要](../experiments/candidates/alacritty/results/2026-10-24-windows/summary.json)、[构建验证](../experiments/candidates/alacritty/results/2026-10-24-windows/validation.json)、[Windows/Ubuntu CI](https://github.com/JavaZeroo/termforge/actions/runs/40117504346) | 初始 crates.io 0.26.0 的固定来源、许可、构建及 headless 可行性；不是当前补丁版或产品路线验收。 |
| 独立语义参考 | [差分入口](../experiments/differential/README.md)、[固定依赖](../experiments/differential/requirements.txt)、[2026-10-25 摘要](../experiments/differential/results/2026-10-25/summary.json) | 固定 pyte/wcwidth 的共同轨迹、精确裁决与负例；参考差异不算兼容通过，不用于性能比较。 |
| 初始共同轨迹（历史） | [差分说明](../experiments/differential/README.md)、[逐项裁决](../experiments/differential/decisions.md)、[2026-10-25 Windows 摘要](../experiments/differential/results/2026-10-25-candidate-common-v3/summary.json) | 旧候选共同矩阵及精确参考差异；semantic_acceptance=false，保持原版本身份。 |
| termforge.1 兼容与身份补丁 | [来源/补丁台账](../experiments/candidates/alacritty/PROVENANCE.md)、[v4 共同矩阵](../experiments/differential/results/2026-10-25-candidate-common-v4/summary.json)、[真实应用 v2](../experiments/differential/results/2026-10-25-real-applications-v2/summary.json)、[fuzz v2](../experiments/differential/results/2026-10-25-fuzz-v2/summary.json) | 历史 .1 的上游资源、ED1/ED2 和行元数据补丁，headless 共同/真实应用/fuzz 结果；不转写为当前 .4 产品验收。 |
| termforge.2 C++ 拥有值适配与候选产品接入 | [接口边界](../experiments/core/CANDIDATE.md)、[验证记录](../experiments/core/results/2026-10-25-candidate-adapter/validation.json)、[v5 共同矩阵](../experiments/differential/results/2026-10-25-candidate-common-v5/summary.json) | 历史 .2 拥有值 C++/FFI 接入、Vim/回复与 v5 矩阵；当时完整回归红项保留，当前 .4 的新增范围由字素合同证明。 |
| 每缓冲区 margin 与宿主差异（#27/#41） | [源 VT、原生快照与候选验证](../experiments/shell/results/2026-10-26-margin-state/validation.json)、[termforge.3 共同矩阵](../experiments/differential/results/2026-10-26-candidate-common-v6/summary.json)、[候选 CI 40261449120](https://github.com/JavaZeroo/termforge/actions/runs/40261449120) | 历史 .3 的每缓冲区源 feed 恢复/resize/RIS 通过；真实系统 ConPTY margin 未恢复，margins_per_buffer_verified=false。后端 A/B 不回写此原结果。 |
| 扩展核心能力轨迹 | [能力矩阵说明](../experiments/differential/README.md#扩展能力轨迹25)、[2026-10-25 摘要](../experiments/differential/results/2026-10-25-capabilities-v2/summary.json) | 旧能力矩阵明确自研不支持的备用屏/滚动区/查询，以及候选重排与物理裁剪差异；未覆盖的语义不算接受。 |
| 真实应用核心轨迹 | [真实应用说明](../experiments/differential/README.md#真实应用核心轨迹25)、[2026-10-25 摘要](../experiments/differential/results/2026-10-25-real-applications/summary.json) | 真实 cmd/More/Vim 的 ConPTY/文件结果与核心回放；presented_done=null，不替代产品 GUI/TUI 验收。 |
| Vim 屏幕消息握手（历史） | [本地三应用摘要](../experiments/differential/results/2026-10-25-real-applications-v4/summary.json)、[握手与负例验证](../experiments/differential/results/2026-10-25-real-applications-v4/validation.json) | 旧屏幕消息握手方案的本地结果及失败阶段/原始输出保留；后续事件文件握手替代它，不将旧成功推广为当前 CI。 |
| Vim 事件文件握手与 Windows CI（#37） | [v5 三应用摘要](../experiments/differential/results/2026-10-25-real-applications-v5/summary.json)、[事件与负例验证](../experiments/differential/results/2026-10-25-real-applications-v5/validation.json)、[CI 40190890251](https://github.com/JavaZeroo/termforge/actions/runs/40190890251) | Vim 自写 READY/INSERT 事件及尺寸/文件/自然退出握手，固定 c284c86 的 Windows CI 已完成 #37 限定验收；不代替其他 TUI 或新后端验证。 |
| 有界 fuzz 与最小回归 | [fuzz 说明](../experiments/differential/README.md#有界-fuzz-与固定回归25)、[运行摘要](../experiments/differential/results/2026-10-25-fuzz/summary.json)、[首次失败](../experiments/differential/results/2026-10-25-fuzz/first-failure.json) | 固定种子 whole/byte、首个失败/最小例的完整签名与改变输入负例；未登记差异仍失败，少量种子不代表广泛兼容。 |

## 较早的模块与组合验证

| 范围 | 结果与复现入口 | 可以支持的结论与限制 |
|---|---|---|
| R1 基础窗口整合（历史） | [源码、二进制、重复样本与跳过项](../experiments/shell/results/2026-10-23-r1-foundations/validation.json)、[运行入口](../experiments/shell/README.md) | R1 基础历史、实际键盘接收和合成粘贴取消/关闭的历史窗口整合；内部系统 skip 不计为真实剪贴板/IME 验收。 |
| 选择与复制纯模块 | [按值文本接口](../experiments/core/text_document.hpp)、[选择模块](../experiments/core/text_selection.hpp)、[独立内容测试](../experiments/core/selection_test.cpp) | 纯模块验证稳定位置、宽/组合字符、软硬行、尾空白、历史与版本；不能替代实际鼠标、菜单和系统 CF。 |
| 增量查找与原生控件 | [搜索模块](../experiments/core/text_search.hpp)、[独立内容测试](../experiments/core/search_test.cpp)、[查找控件](../experiments/shell/find_bar.hpp)、[真实控件测试](../experiments/shell/find_bar_test.cpp) | 独立搜索模块与原生 Find 控件的 Unicode/预算/版本/焦点和回收；完整会话行为引用查找验收。 |
| 选择/查找窗口整合（历史） | [最新边界与六轮窗口验证](../experiments/shell/results/2026-10-24-r1-window-boundaries/validation.json)、[前轮整合](../experiments/shell/results/2026-10-24-r1-selection-find/validation.json)、[自动回归](../experiments/shell/test_shell.py) | 历史窗口整合与边界记录；原先漏证据的负例保留。后续系统入口、DPI 与 IME 的结论引用各自独立验收。 |
| 选择字节与受控 DPI | [源码身份、真实窗口与保留失败](../experiments/shell/results/2026-10-24-r1-selection-bytes/validation.json)、[系统 CF 两轮审计](../experiments/shell/results/2026-10-24-r1-styled-system-copy/validation.json) | 样式尾空格、软硬行和受控 DPI 的精确 CF 字节已核对；普通尾空格被 ConPTY 规范化的红样本保留，不恢复已丢信息，也不代表真实异 DPI 跨屏。 |
| 选择字节的双会话阶段等待（#66） | [独立阶段合同](research/selection-session-tick-gate.md)、[原 hosted 计数及本地复验](../experiments/shell/results/2026-10-26-selection-tick-gate/validation.json)、[同实现 hosted CF 复核](../experiments/shell/results/2026-10-26-selection-tick-gate/hosted-validation.json) | 原 reverse 只满足 main 的三 tick，bottom 实际仅两 tick；修正场景等待双方独立进度，保留 Python oracle、九阶段、CF 保护及原期限。本地数据与真实 hosted CF 各两轮分列；后者的源字节/CF/重绘/回收已独立复核，逐运行 EXE 哈希缺口保留。 |
| R1 组合夹具基础 | [夹具协议](../experiments/shell/WORKFLOW.md)、[固定源码数据审计](../experiments/shell/results/2026-10-24-r1-workflow-foundation/validation.json)、[动作/Paste复验](../experiments/shell/results/2026-10-24-r1-workflow-foundation/regression.json)、[真实IME复验](../experiments/shell/results/2026-10-24-r1-workflow-foundation/ime-regression.json)、[主CI 40034110925](https://github.com/JavaZeroo/termforge/actions/runs/40034110925)、[verify 40034133987](https://github.com/JavaZeroo/termforge/actions/runs/40034133987) | 组合夹具的数据、快照与既有回归基础；不单独验收真实 Paste/IME、八会话或 R1 总出口。 |
| R1 八会话数据工作流 | [协议/自动入口](../experiments/shell/WORKFLOW.md)、[固定源码审计](../experiments/shell/results/2026-10-24-r1-eight-sessions/validation.json)、[既有能力复验](../experiments/shell/results/2026-10-24-r1-eight-sessions/regression.json)、[主CI](https://github.com/JavaZeroo/termforge/actions/runs/40040980384)、[同源码真实IME复验](https://github.com/JavaZeroo/termforge/actions/runs/40040994121) | 固定源码 72fe231 的八会话数据工作流、容量/后台/阅读/查找/淘汰/回收已独立复核；这是 data-only 记录，同次真实 CF/IME 以 R1 总出口为准。 |
| 历史 launcher-tree 资源观察 | [复用采样器的adapter](../experiments/shell/workflow_resources.py)、[同一固定源码审计](../experiments/shell/results/2026-10-24-r1-eight-sessions/validation.json) | 历史 launcher-tree 资源记录可离线重算；不是 UI-root 归因。观测成本/调度缺口显著，phase_windows=null，不追溯补造校准或低扰动结论。 |
| 工作区 v2 配置模型（#33 限定切片） | [模型与平台验证](../experiments/shell/results/2026-10-25-workspace-state-v2/validation.json) | 有界 v2、v1 迁移、UTF/损坏分类与原子写模型；只是 #33 的模型层证据。 |
| 单会话工作区恢复（#33 限定切片） | [核心与双进程验证](../experiments/shell/results/2026-10-25-workspace-restore-core/validation.json) | 真实新进程恢复单 cmd、标题和布局，核对启动抑制、快照与 Job；多会话范围见相应记录。 |
| 复杂文字与窄屏 ConPTY 尾部 | [源/控制台/布局验证](../experiments/shell/results/2026-10-26-unicode-workflow/validation.json)、[只读调查入口](../research/README.md) | 固定源字节、CHAR_INFO、运输尾部和独立布局核对；保留分解原文，不把 pyte NFC 当原文。完整 Unicode/IME 与 #30 仍独立。 |
| 原文/glyph v1（历史） | [真实控制台与绘制验证](../experiments/shell/results/2026-10-26-unicode-renderer/validation.json)、[字体/显示合同](milestones/R2-font-display.md)、[#39](https://github.com/JavaZeroo/termforge/issues/39) | 历史 v1 核对原文、原生替换投影、单元格和 DWrite 网格适配；当时 ZWJ/keycap 分格结果保留，不改写为聚类通过。#39 的 v2 结果另列。 |

## 复验规则与入口

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
| 五阶段 raw H/space 输出参考（#129） | [固定采集合同](research/windterm-raster-raw-capture-plan.md)、[执行与独立审计](research/windterm-raster-raw-reference-2026-10-29.md)、[脱敏结果](../research/ui/results/2026-10-29-raster-raw-reference/validation.json) | `f6d9a97` 的唯一授权执行取得 5 grab、17 组完整 H/space；phase2 双 H 的 16/16 墨迹并列保留 null，未中断采集。phase1/3 原样 H 唯一匹配同帧 SGR0，phase4 匹配同帧 SGR31;44 的完整有序签名；仅为固定样本输出，不证明内部 pen、完整 grid、presented、local-shell 或性能。旧 #126 0 grab 和旧 #129 3 grab partial 均独立保留，父项未关闭。 |

主对照为官方 Windows x64 2.7.0：ZIP SHA-256 `b05c3c7371340ff1302967247ffcd3a3ba27b237f72010e9157086143c453e50`，EXE SHA-256 `7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0`。最初本机安装的 EXE 身份不同；其历史静态报告不得冒充官方包结果。其他目标平台仍需各自固定包身份。

公开仓库只保留原创探针、事实描述和审核过的摘要。原始反编译、复制发行物、个人配置、原始插桩结果保留在 ignored `local/`；证据 ID 是本地索引，不是可访问的公共附件。规则见[来源与隐私](PROVENANCE.md)。

## 性能证据与尚未完成的预算

| 工作 | 结果/复现入口 | 当前结论边界 |
|---|---|---|
| 官方 WindTerm 生产端烟测 | [摘要](../benchmarks/results/2026-10-21-windterm-smoke/summary.json)、[基准规范](../benchmarks/PROTOCOL.md) | 7,732 字节写出、人工见到结束标记；没有全内容正确性、消费/呈现时间和正式重复批次，不是 C0 性能基线 |
| 接收批量与通知合并 | [实验协议](../experiments/receive/README.md)、[120 次实验报告](research/receive-experiment-2026-10-22.md) | 原创固定输入、字节顺序、队列边界与背压；机制实验不等于 WindTerm 路径或端到端收益 |
| 连续 CPU 区间与校准 | [计量协议和命令](../experiments/measure/README.md)、[批次 A](../experiments/measure/results/2026-10-23-v2-a/manifest.json)、[批次 B](../experiments/measure/results/2026-10-23-v2-b/manifest.json) | 48 个核心样本/4 次校准；尚缺解码/状态/历史独立归因、Unicode/大历史、无插桩对照和更严格环境控制 |
| 自主 C++ recovery 核心双构建准备 | [固定协议](../experiments/measure/RECOVERY-PREPARE.md)、[实际结果](../experiments/measure/results/2026-10-29-recovery-core-prepare/README.md)、[脱敏身份](../experiments/measure/results/2026-10-29-recovery-core-prepare/validation.json) | 同源 recovery 包装/无本工具包装的实际对象和命令核验；一次38进程含24短正确性/2identity/12拒绝，相关14项回归，25源/历史与18构建产物前后核对。旧strict/v2/calibration分列，不是Qt/ConPTY热路径、正式性能批次或数值预算 |
| 输入到状态/提交 | [shell 端点定义](../experiments/shell/README.md)、[呈现端点定义](../experiments/presentation/README.md) | 现有脚本中断/后续命令时段与绘制提交有明确名称；不能当单键延迟或真实呈现，带完整画面检查的样本不是低扰动性能样本 |

| 范围 | 结果与复现入口 | 可以支持的结论与限制 |
|---|---|---|
| 初始候选 project feed/投影成本 | [计量说明](../experiments/measure/README.md)、[2026-10-25 project 原始批次](../experiments/measure/results/2026-10-25-core-compare/) | 旧候选的 1 秒交错 project 样本，分离 feed/观察成本；分配范围与无插桩缺口保留，不作当前 .4 或 WindTerm 性能结论。 |
| 初始候选 interactive feed/投影成本 | [2026-10-25 interactive 原始批次](../experiments/measure/results/2026-10-25-core-interactive/) | 旧候选的固定 ASCII 编辑回放与交错样本；不是键盘到呈现延迟，也不能用于验收新字素实现的预算。 |

| 当前成本调查 | 结果与合同 | 结论边界 |
|---|---|---|