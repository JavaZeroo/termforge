# 候选 C++ 终端适配器

`CandidateTerminal` 是 #27 的拥有值适配层，不是自研核心的替代品声明。它把固定 `0.26.1-dev+termforge.4` 的 `Term/Processor` 接到现有 `Screen`/`Line`；默认 CMake 保留自研核心且不需要 Rust，候选产品由 `TERMFORGE_BUILD_CANDIDATE_ADAPTER=ON` 显式启用。

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
