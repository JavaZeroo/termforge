# 候选 C++ 终端适配器

按 [ADR-0006](../../docs/adr/0006-confirmed-project-direction.md)，用户确认保留本候选代码继续评估，尚未确认最终采用。下述实现、来源与回归结果继续保留；与 WindTerm 2.7.0 的重要差异、性能容差及正式选型分别提交用户确认。

`CandidateTerminal` 是 #27 的拥有值适配层，不是自研核心的替代品声明。它把固定 `0.26.1-dev+termforge.9` 的 `Term/Processor` 接到现有 `Screen`/`Line`；默认 CMake 保留自研核心且不需要 Rust，候选产品由 `TERMFORGE_BUILD_CANDIDATE_ADAPTER=ON` 显式启用。

`.7` 已修复原生 resize 的硬/软边界传递及缩宽容量淘汰计数，结果保留在[工作检查点](results/2026-10-28-reflow-metadata/README.md)。`.8` 增加一次 resize 来源映射，当前 `.9` 扩展到一次真实 feed，来源见[补丁台账](../candidates/alacritty/PROVENANCE.md#termforge9-补丁)。C++ 长行 resize/repaint 的整段复制回归仍失败：旧历史行与原生片段重复，来源归属和统一视图映射仍由 #29 继续处理。这些原生修复不构成候选完整正确性通过。

## 一次 resize 的来源映射

`tf_terminal_resize` 导出主屏和备用屏各自的旧行、目标行和被移除片段，不因当前激活哪个屏幕而改变两者名称。每个稀疏区间描述旧行 ID、旧物理列、目标物理列和长度；硬换行边界独立保存旧行 ID，因此空硬行也有身份。新补 padding/leading spacer 不假造旧来源；单列裁剪宽字保留 lead cell 来源，逻辑宽度仍属于原始 cell。移除原因区分容量、光标重定位、底部高度裁剪和非 reflow 列裁剪，缺失 padding/spacer 不等于逻辑文本丢失证明。

映射只描述同步 resize 本身。C ABI 返回拥有值并校验 epoch；下一次成功 feed（包括空输入）、finish、历史淘汰或 resize 使旧映射失效，非法尺寸和未通过前缀校验的淘汰请求保持原映射。后续写入可能复用原生行 ID，调用方不能用本映射推断写入后的来源。普通 Rust `Term::resize` 不启用追踪；`resize_with_map` 结束时清理所有存储槽中的临时来源，包括非活动缓存槽。

原生淘汰数累计实际超出历史容量的物理行，包括一次多行滚动中移出容量的原可见行。扩高时仅为补空行而执行的内部滚动不计淘汰；显式清空、物理行合并及非容量裁剪仍分别处理。删除片段数量不是通用的逻辑文本损失计数。

`candidate_resize_map_test` 从 C++ 调用真实静态库，验证字段布局、容量片段、映射生命周期、主/备用屏命名和空硬行。Rust FFI 测试覆盖拆分、相同文字的不同来源合并、宽字 spacer 和高度缩小的来源移除。当前 C++ 历史合并尚未消费此映射，后续覆盖写入、源文本归属、复制/Find 和视口坐标仍需实现与完整验证；该接口不裁决原版历史覆盖策略，也未测量新增来源表的资源成本。

## 一次 feed 的来源映射

`tf_terminal_feed_mapped` 是显式来源捕获入口；普通 `tf_terminal_feed` 不捕获。记录结构沿用 `TfResize*` 命名与物理坐标，只通过 `tf_terminal_feed_*` 查询读取 feed 结果。实际写入或擦除使旧来源消失，文字相同也不保留旧身份；移动保留来源，待续字素不会因捕获而被封闭。一次操作仅有一种映射，resize/feed 查询不能互用，均受共享 epoch 和后续成功修改的失效规则约束。

feed 的 `removed_rows` 只记录过程中的移除事件。它不是完整擦除日志或最终保留政策；同一 feed 中 ED3/RIS 可能出现在移除之前或之后，最终 `history_clear_epoch` 不提供事件顺序。消费方必须避免复活清空前的来源，不能先清空再无条件追加整份 removed 列表。最终目标来源、清空与外部历史的保留规则仍须由上层一致处理。

## 构建与验证

```powershell
cargo +1.92.0 build --manifest-path experiments/candidates/alacritty/Cargo.toml --release --locked --lib
cmake -S . -B build/candidate-adapter -A x64 -DTERMFORGE_BUILD_CANDIDATE_ADAPTER=ON -DPython3_EXECUTABLE="$PWD/.venv/Scripts/python.exe"
cmake --build build/candidate-adapter --config Release
ctest --test-dir build/candidate-adapter -C Release --output-on-failure
```

Linux 使用相应 CMake 配置。候选 workflow 在 Windows/Ubuntu 构建 `candidate_core_test`；Windows 的真实 shell 场景使用已有私有非输入桌面、自有 Job 和受控消息，不操作用户桌面。

## 边界

- Rust `staticlib` 只暴露 opaque handle、C 结构体和调用方缓冲区；`CandidateTerminal` 负责析构、错误转换和值拷贝，不暴露 Rust 引用、异常或并发可变网格。
- `feed` 以 1 MiB 为单次上限；`resize` 有独立尺寸检查；`finish` 保留当前候选 trace 的 no-op 语义，不宣称刷新不完整序列。
- 每次 feed 同步网格并排干事件。候选 `ShellWindow` 将 `PtyWrite` 排入现有有界 `InputQueue`，不直接写 ConPTY；本切片仅保留标题状态、处理 Bell 及已明确忽略的事件，未声明支持的事件不会被静默当成成功。
- C++ 同步历史/可见行、稳定 ID、逐行版本、样式、光标、软硬换行、next_id/wrap_columns 和实际淘汰数。普通 reset 保留 ID，滚动复用行更新 ID；初始 Storage 方向也显式检查。
- `marks` 是 lead 后附加标量数，复杂字素中可含独立宽度非零的标量，并非 Unicode Mark 类计数。clip、fragment、leading_spacer 与饱和 overflowcounter 在 C ABI 和拥有值中显式保留；leading padding 仍绘制背景，只有文本投影跳过它，真实写入的空格不被 trim。
- 普通空单元格保留一个可绘制背景格，只有宽字符 spacer 是零宽。未写格与显式空格的语义仍受上游 `is_empty` 限制，不能由当前投影裁决；真彩色等非 16 色样式仍为 `-2`。
- 不走 JSON。Rust/C++ 文本由调用方拥有，未做增量 FFI 或性能验收；候选 damage 的产品化仍属后续。
- `screen.resize` 与候选 `Term::resize` 同步尺寸；默认自研回退继续采用 ADR-0005 的物理行裁剪，候选不把它称为完整 reflow。真实 ConPTY 重放可能先由上游把历史拉入可见区再覆盖，候选在完整重放后合并有界历史，按 ID 和完整行键去重，保留 Find/选区所需的稳定身份，且不让冲突产生重复 ID。连续同步缓存原始 FFI ID 与最终身份的映射，仅在需要重放的未知旧 ID 时回退到完整键匹配，不重复扫描全部历史。真实淘汰不恢复，显式清空不复活；普通空白行、未决 reflow、备用屏/滚动区全语义不在此保证内。合同与验证见 [resize/历史记录](results/2026-10-26-resize-replay/validation.json)。

回放行键与后续缓存匹配同时保留文字的起始列，只忽略末尾未写 padding。相同 `ABC` 从第 0 列移到第 2 列（或反向）不能沿用旧版本供选择/Find 使用；原选区失效，Find 重扫后命中实际列。独立[列位置回归](results/2026-10-27-replay-coordinates/README.md)覆盖回放中/回放后、整段/逐字节和原位对照。

等待回放时冻结最近一份完整的 resize 后状态，旧尺寸快照只用于完成时匹配身份。解析器实际执行主屏 ED3（含 `03J`）或 RIS 后，`.5` 的清空代次经 C ABI 释放旧回放/缓存，不等 `?25h` 才显示清空；ED2 不清历史，清空不计为淘汰。[快照与清空回归](results/2026-10-27-resize-snapshot/README.md)记录固定可见网格和清空边界。跨宽度历史行的显示仍有行宽不一致反例，完整历史合同未完成。[原版文本接口观察](../../docs/research/windterm-resize-text.md)仅证明固定序列的文本保留和投影恢复，不验证本项目清空或冻结策略。

`.6` 将主屏缓存、原生身份映射与淘汰端点一起保存在拥有值快照中，跨备用屏切换挂起/恢复；空原生历史不再推断为显式清空。容量淘汰同时失效旧回放中的缓存行；仍驻留 Rust 的旧行经 C ABI 核对整个最旧前缀的 ID 后从实际历史删除，原生与缓存独有行的淘汰各计一次。ED3/RIS 的解析器清空代次继续优先。相关[缓存与淘汰回归](results/2026-10-28-history-cache/README.md)覆盖透明输入、主/备用屏切换、回放中断、清空和实际淘汰；它不验证完整 reflow 或混宽历史的绘制。

## 真实产品场景

`candidate_shell_tui` 在同一 `ShellWindow` 和 ConPTY 路径上连续执行两轮真实 Vim：进入全屏编辑、插入文字、resize 到 60×12、退出写盘、确认主屏 shell 返回。本机 Vim 输出未发送 `1049/1047/47`，所以此场景不计作备用屏验收；1049 的固定核心用例与真实 Vim 证据分开。固定自动命令写入 `candidate-vim.vim` 并以短 `-S` 命令加载，避免交互式 cmd 拆分长启动命令。启动和插入由 Vim 写入的真实 `VimEnter`/`InsertEnter` 事件文件确认，避免 `echon` 消息被重绘覆盖；随后通过插入模式的 `Ctrl-O` 调用 Vim 命令，实际保存 `&columns/&lines` 并检查 60×12，再以 `Ctrl-O:wq` 写盘退出。不把 ConPTY 重绘或 `ResizePseudoConsole` 返回当作 Vim 已就绪。本路径不声称验证 Vim 的 Escape 行为，独立控制台 ESC 接收另测。`-W` 日志核对插入文本与 `:wq` 顺序，其中可能含 Vim 内部特殊键，不能冒充纯键盘字节。随后用独立 `shell_query_client.exe` 发出真实 `CSI 6n`，保存终端经 `InputQueue` 返回的光标报告，并用 `shell_size_client.exe` 核对子进程尺寸。另有 resize 后独立 ESC 接收检查。

`candidate-alt` 通过真实辅助进程执行两次 1049h/1049l，在五个阶段保存源 VT 字节、子控制台 `CHAR_INFO`、文本、样式和光标。两轮[限定验证](../shell/results/2026-10-26-alternate-screen/validation.json)要求原生主屏恢复与进入前一致，候选主屏文字/样式和最终光标恢复，ALT_ONE/ALT_TWO 不进入主历史。快照按实际控制台文字/样式有界同步，不把早期快照或旧核心用例冒充产品结果。origin/wrap、独立 margin、异常序列及 resize 交叉仍由 #27 保留，#28 不因此解除阻塞。

`candidate-scroll` 通过真实辅助进程设置 DECSTBM 2;3r 并在区域底部连续换行，保存 before、region、final 的真实控制台状态。两轮[限定验证](../shell/results/2026-10-26-scroll-region/validation.json)要求区域外文字保持、C/D 位于正确区域、A/B 不混入主历史，光标与原生最终状态一致。ConPTY 的 transport 可能采用不同重绘方式，因此独立使用固定源 VT、原生阶段快照及 `pyte==0.8.2` 重放，不要求原样回显控制序列。origin/wrap、主/备用屏各自 margin 和 resize 交叉仍未验收。

`candidate-margins` 固定主屏 `2;3r`、备用屏 `4;5r`，保存源 VT、四阶段真实控制台快照和 transport。[限定证据](../shell/results/2026-10-26-margin-state/validation.json)分别检查公开 feed 的主屏 margin 恢复、resize/RIS 边界与真实 ConPTY 的宿主差异。源 VT 的整段/逐字节候选回归通过；该批固定系统后端只恢复主屏内容/样式/光标，未恢复主屏 margin，最终出现 `REGION_CIXED`，由 [#41](https://github.com/JavaZeroo/termforge/issues/41) 保留。该次观察返回 `margins_per_buffer_verified=false`，不是能力通过；源 LF 保持列与 transport 的 CRLF 分别核对，未修改输入或注入恢复命令。系统/官方后端 A/B 的不同端点见[后端调查](../../docs/research/conpty-margin-backends.md)。

`candidate-unicode` v2 使用真实辅助进程输出固定分解组合、单 emoji、ZWJ 和 keycap，保存源字节、真实控制台 `CHAR_INFO`、ConPTY transport、候选单元格及实际 DWrite glyph。按[流式字素合同](../../docs/research/streaming-graphemes.md)，两阶段检查 80×24→100×30 的子控制台尺寸确认、单 lead/两格宽度、产品拖尾选择复制、实际 1 glyph/Segoe UI Emoji 与网格适配。旧 [v1 原文/glyph 证据](../shell/results/2026-10-26-unicode-renderer/validation.json)保留原始分格观测；它不覆盖 v2，也不改写为聚类通过。当前 v2 验证限定首批四样本，不是完整 Unicode/IME、所有脚本或 WindTerm 运行时等价。

原有 `runtime_observation` 也在候选适配构建运行，保留滚动后的逐行内容、retained/full RGB 一致、no-op 状态端点及会话清理断言。所有此类检查只使用自有窗口的 DIB，不截图，`presented_done` 仍未知。

## 当前验证与限制

`candidate_core_test` 覆盖 whole/逐字节、Unicode/组合标记、备用屏恢复、真实光标回复、空载荷事件、稳定 ID/版本/淘汰、选择/搜索/HistoryViewport、软硬换行、滚动区、ED1/ED2、resize 和错误参数。新增真实 ConPTY 重放的唯一行身份、Find 增量收敛、整篇/局部选区、连续同步、历史合并/清空及超限释放检查。证据见[适配验证](results/2026-10-25-candidate-adapter/validation.json)和[resize/历史验证](results/2026-10-26-resize-replay/validation.json)。

各验证记录只证明其固定源码与构建配置；旧失败保留在原记录，不随新版本改判。当前能力统一见 [FEATURES](../../docs/FEATURES.md)，跨模块证据见 [EVIDENCE](../../docs/EVIDENCE.md)，联合配置和剩余验收见 [R2 交付路线](../../docs/milestones/R2-local-terminal.md)。本适配说明不维护动态 issue 状态或把核心结果外推为完整 R1/R2、真实显示或性能通过。
