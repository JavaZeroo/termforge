# Alacritty 固定核心与候选适配工具

本目录包含候选构建、状态适配、固定兼容补丁、C ABI 与核心成本实验。[ADR-0003](../../../docs/adr/0003-core-reference-and-reuse.md)及 [ADR-0006](../../../docs/adr/0006-confirmed-project-direction.md)保留 Alacritty 为待评估候选，正式核心选型待原版对标和用户确认；C++ 拥有值适配接入显式候选 `ShellWindow`，默认 CMake 保留自研回退。能力统一见[功能矩阵](../../../docs/FEATURES.md)，联合验收和依赖见 [R2 合同](../../../docs/milestones/R2-local-terminal.md)，本说明不维护逐批进度。

## 固定来源与许可

| 项目 | 当前固定值 | 来源 |
|---|---|---|
| `alacritty_terminal` | `0.26.1-dev+termforge.4`，Apache-2.0 | [上游 commit `5421470`](https://github.com/alacritty/alacritty/commit/542147067dd016fc5f49120ef0ba605a98a4e3a7)与[来源/补丁台账](PROVENANCE.md) |
| 本地补丁 | ED1/ED2、稳定 Row ID/换行元数据/淘汰、安全完整 Row swap、每缓冲区滚动区域、流式字素与明确剪裁 | `vendor/alacritty_terminal/src/`；所有文件哈希进入结果 manifest |
| 工具链 | Rust/cargo 1.92.0 | 本机与候选 CI 显式选择；当前锁定图最高声明 Rust 1.85 |
| 传递依赖 | 59 个注册表包 + 根适配器与固定 vendor 两个 path crate，共 61 个包 | `Cargo.lock` 与 `cargo metadata --locked --format-version 1` |

根 `Cargo.toml` 使用固定 path 依赖并关闭默认 serde feature。上游只复制 manifest、changelog、许可证和约 426 KiB 的 `src/`，不复制约 46 MiB 行为录制；内联测试保留。补丁不冒充 crates.io 原始 0.26.0，也不把第三方代码改标 MIT。候选二进制当前只用于本地/CI 实验，不进入 Alpha 或产品发行包；以后分发须完成全部依赖许可与通知审计。

选择 Term/Processor 是因为其实际网格、历史、备用屏、resize 和 `Event::PtyWrite` 能力及共同证据，不是语言或项目声誉。候选 PTY/event loop 未因此成为产品依赖。

## 干净构建与验证

从仓库根目录执行；输出目录必须不存在：

```powershell
cargo +1.92.0 build --manifest-path experiments/candidates/alacritty/Cargo.toml --release --locked
cargo +1.92.0 test --manifest-path experiments/candidates/alacritty/Cargo.toml --locked --workspace
python experiments/candidates/alacritty/run.py --out local/candidate-alacritty-new
python -m unittest experiments.candidates.alacritty.test_candidate -v

# 可选 C++ 适配器；默认 CMake 仍不需要 Rust。
cmake -S . -B build/candidate-adapter -A x64 -DTERMFORGE_BUILD_CANDIDATE_ADAPTER=ON -DPython3_EXECUTABLE="$PWD/.venv/Scripts/python.exe"
cmake --build build/candidate-adapter --config Release
ctest --test-dir build/candidate-adapter -C Release --output-on-failure
```

Linux/macOS 使用相同 Cargo 命令和相应 CMake 配置；工具链须预先存在，命令中的 `+1.92.0` 不改变全局默认。默认 trace 为 `target/release/termforge-alacritty-trace[.exe]`；可用 `TERMFORGE_ALACRITTY_TRACE` 显式选择另一构建，缺失文件会失败。候选 workflow 在 Windows/Ubuntu 构建并运行固定 crate 的单元测试与 doc test 和 C++ 适配器。

## Trace、C ABI 与检查内容

`alacritty_trace` 的 CLI 与现有 `terminal_trace` 一致，输出光标、待换行、样式、屏幕格、历史格、模式位和相邻检查点的 `PtyWrite`。输入上限 1 MiB，累计投影上限 100 万格；Trace JSON 只用于正确性观测，不能作为产品热路径或性能排名。

`run.py` 固定检查 ASCII、中文、组合标记和 emoji 分块一致性，有界历史、备用屏恢复、真实回复、非法分块、缺失二进制和篡改结果负例。摘要记录版本、上游 commit、全部 vendor/适配器源码/二进制/输入哈希和限制。

`src/ffi.rs` 与 `include/termforge_candidate.h` 提供 staticlib C ABI；`experiments/core/candidate_terminal.{hpp,cpp}` 将其包装为现有 `Screen`/`Line` 拥有值。feed、事件、行/格文本都经调用方缓冲区返回，不跨 FFI 暴露 Rust 引用或异常。C++ 同步稳定 ID、版本、ending/next_id/wrap_columns、样式和历史淘汰；[接口与边界](../../core/CANDIDATE.md)另列。

## 语义与验收边界

- [流式字素合同](../../../docs/research/streaming-graphemes.md)定义 Unicode 17 分段/宽度、首样式、固定 ZWJ/keycap、无损有界分片、单列 resize 和实际 DWrite 整形。默认自研仍为固定 Unicode 15。固定样本的[红/绿与实际场景验证](../../shell/results/2026-10-26-streaming-graphemes/validation.json)不扩展为全部文字系统或 WindTerm 运行时等价；吞吐成本由 [#54](https://github.com/JavaZeroo/termforge/issues/54)独立裁决。
- C++ 适配器验证分块一致性、真实回复、备用屏/滚动区、ED、Unicode、resize、ID/版本、选择、搜索和 HistoryViewport。核心模型、真实 ConPTY、实际 GUI 与系统输入是不同层，不能用其中一层代替完整 [#27](https://github.com/JavaZeroo/termforge/issues/27) / [#28](https://github.com/JavaZeroo/termforge/issues/28)验收。
- [margin 研究](../../../docs/research/conpty-margin-backends.md)区分公开源 VT、宿主原生控制台与末端状态；[More 合同](../../../docs/research/more-capture-ech.md)分开捕获完成、默认 ECH 修复与候选 pending-wrap 差异，不把旧失败或差异观察改判为通过。
- 默认/ANSI 16 色映射为现有数值；其他颜色以 `-2` 保留未投影。上游 `Cell::is_empty` 合并空格和空单元格，不能裁决显式尾空格。`replies` 是相邻检查点的新回复，不是累计队列。
- resize/reflow 合同归 [#29](https://github.com/JavaZeroo/termforge/issues/29)，不能由候选行为静默替换。候选可构建不等于支持[平台台账](../../../research/windterm-2.7.0-platform-scope.md)的全部系统；产品 GUI/平台与长期接口决定归 [#34](https://github.com/JavaZeroo/termforge/issues/34)。

固定补丁的逐版来源在 [PROVENANCE](PROVENANCE.md)，共同矩阵、能力、真实应用与 fuzz 的机制及历史结果索引在[差分说明](../../differential/README.md)，跨模块审核结果统一见[证据地图](../../../docs/EVIDENCE.md)。旧 0.26.0 与 termforge.1/.2/.3 结果保留原版本，不覆盖后续补丁，也不拼接成性能对照。
