# 候选 C++ 终端适配器

`CandidateTerminal` 是 #27 的拥有值适配层，不是自研核心的替代品声明。它把固定 `0.26.1-dev+termforge.2` 的 `Term/Processor` 接到现有 `Screen`/`Line`；默认 CMake 保留自研核心且不需要 Rust，候选产品由 `TERMFORGE_BUILD_CANDIDATE_ADAPTER=ON` 显式启用。

## 构建与验证

```powershell
cargo build --manifest-path experiments/candidates/alacritty/Cargo.toml --release --locked --lib
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
- 普通空单元格保留一个可绘制背景格，只有宽字符 spacer 是零宽。未写格与显式空格的语义仍受上游 `is_empty` 限制，不能由当前投影裁决；真彩色等非 16 色样式仍为 `-2`。
- 不走 JSON。Rust/C++ 文本由调用方拥有，未做增量 FFI 或性能验收；候选 damage 的产品化仍属后续。
- `screen.resize` 与候选 `Term::resize` 同步尺寸，但 reflow 合同归 #29；独立测试不替代最终 resize 策略裁决。真实 ConPTY 多次重放已使未换行内容的 ID 改变并令 Find/选区失效，原始字节和复现见 [#29 证据](https://github.com/JavaZeroo/termforge/issues/29#issuecomment-6395700469)；不能将固定小尺寸 resize 或 headless ID 检查当作该合同已通过。

## 真实产品场景

`candidate_shell_tui` 在同一 `ShellWindow` 和 ConPTY 路径上连续执行两轮真实 Vim：进入全屏编辑、插入文字、resize 到 60×12、退出写盘、确认主屏 shell 返回。本机 Vim 输出未发送 `1049/1047/47`，所以此场景不计作备用屏验收；1049 的固定核心用例与真实 Vim 证据分开。固定自动命令写入 `candidate-vim.vim` 并以短 `-S` 命令加载，避免交互式 cmd 拆分长启动命令。启动和插入由 Vim 写入的真实 `VimEnter`/`InsertEnter` 事件文件确认，避免 `echon` 消息被重绘覆盖；随后通过插入模式的 `Ctrl-O` 调用 Vim 命令，实际保存 `&columns/&lines` 并检查 60×12，再以 `Ctrl-O:wq` 写盘退出。不把 ConPTY 重绘或 `ResizePseudoConsole` 返回当作 Vim 已就绪。本路径不声称验证 Vim 的 Escape 行为，独立控制台 ESC 接收另测。`-W` 日志核对插入文本与 `:wq` 顺序，其中可能含 Vim 内部特殊键，不能冒充纯键盘字节。随后用独立 `shell_query_client.exe` 发出真实 `CSI 6n`，保存终端经 `InputQueue` 返回的光标报告，并用 `shell_size_client.exe` 核对子进程尺寸。另有 resize 后独立 ESC 接收检查。

原有 `runtime_observation` 也在候选适配构建运行，保留滚动后的逐行内容、retained/full RGB 一致、no-op 状态端点及会话清理断言。所有此类检查只使用自有窗口的 DIB，不截图，`presented_done` 仍未知。

## 当前验证与限制

`candidate_core_test` 覆盖 whole/逐字节、Unicode/组合标记、备用屏恢复、真实光标回复、空载荷事件、稳定 ID/版本/淘汰、选择/搜索/HistoryViewport、软硬换行、滚动区、ED1/ED2、resize 和错误参数。证据见 [validation](results/2026-10-25-candidate-adapter/validation.json)。

候选整合不表示 #27 已关闭：完整候选 CTest 自然结束为 36/41；`text_actions`、`workflow_sessions`、`selection_bytes`、`selection_context` 有行身份/保真失败，`shell` 也超时，不能只归因于 #36。#28 的更多真实 TUI、#29 的 resize/重放身份合同、#37 的 Windows CI 捕获和最终呈现/性能验收均保持独立开放。
