# Alacritty 原生核心候选（R2-01 / #24）

本目录包含候选构建、状态适配、固定兼容补丁、C ABI 与核心成本实验。[ADR-0003](../../../docs/adr/0003-core-reference-and-reuse.md)已决定 R2 新能力复用候选 Term/Processor；C++ 拥有值适配已接入显式候选 `ShellWindow`，默认 CMake 保留自研回退，不把 headless 结果写成产品 TUI。

## 固定来源与许可

| 项目 | 当前固定值 | 来源 |
|---|---|---|
| `alacritty_terminal` | `0.26.1-dev+termforge.3`，Apache-2.0 | [上游 commit `5421470`](https://github.com/alacritty/alacritty/commit/542147067dd016fc5f49120ef0ba605a98a4e3a7)与[来源/补丁台账](PROVENANCE.md) |
| 本地补丁 | ED1/ED2、稳定 Row ID/换行元数据/淘汰、安全完整 Row swap、每缓冲区滚动区域 | `vendor/alacritty_terminal/src/`；所有文件哈希进入结果 manifest |
| 工具链 | Rust/cargo 1.92.0 | 本机与候选 CI 显式选择；当前锁定图最高声明 Rust 1.85 |
| 传递依赖 | 58 个注册表包 + 根适配器与固定 vendor 两个 path crate，共 60 个包 | `Cargo.lock` 与 `cargo metadata --locked --format-version 1` |

根 `Cargo.toml` 使用固定 path 依赖并关闭默认 serde feature。上游只复制 manifest、changelog、许可证和约 426 KiB 的 `src/`，不复制约 46 MiB 行为录制；内联测试保留。补丁不冒充 crates.io 原始 0.26.0，也不把第三方代码改标 MIT。候选二进制当前只用于本地/CI 实验，不进入 Alpha 或产品发行包；以后分发须完成全部依赖许可与通知审计。

选择 Term/Processor 是因为其实际网格、历史、备用屏、resize 和 `Event::PtyWrite` 能力及共同证据，不是语言或项目声誉。候选 PTY/event loop 未因此成为产品依赖。

## 干净构建与验证

从仓库根目录执行；输出目录必须不存在：

```powershell
rustup default 1.92.0
cargo build --manifest-path experiments/candidates/alacritty/Cargo.toml --release --locked
cargo test --manifest-path experiments/candidates/alacritty/Cargo.toml --locked --workspace
python experiments/candidates/alacritty/run.py --out local/candidate-alacritty-new
python -m unittest experiments.candidates.alacritty.test_candidate -v

# 可选 C++ 适配器；默认 CMake 仍不需要 Rust。
cmake -S . -B build/candidate-adapter -A x64 -DTERMFORGE_BUILD_CANDIDATE_ADAPTER=ON -DPython3_EXECUTABLE="$PWD/.venv/Scripts/python.exe"
cmake --build build/candidate-adapter --config Release
ctest --test-dir build/candidate-adapter -C Release --output-on-failure
```

Linux/macOS 使用相同 Cargo 命令和相应 CMake 配置。默认 trace 为 `target/release/termforge-alacritty-trace[.exe]`；可用 `TERMFORGE_ALACRITTY_TRACE` 显式选择另一构建，缺失文件会失败。候选 workflow 在 Windows/Ubuntu 构建并运行固定 crate 的 141 个单元测试、1 个 doc test 和 C++ 适配器。

## Trace、C ABI 与检查内容

`alacritty_trace` 的 CLI 与现有 `terminal_trace` 一致，输出光标、待换行、样式、屏幕格、历史格、模式位和相邻检查点的 `PtyWrite`。输入上限 1 MiB，累计投影上限 100 万格；Trace JSON 只用于正确性观测，不能作为产品热路径或性能排名。

`run.py` 固定检查 ASCII、中文、组合标记和 emoji 分块一致性，有界历史、备用屏恢复、真实回复、非法分块、缺失二进制和篡改结果负例。摘要记录版本、上游 commit、全部 vendor/适配器源码/二进制/输入哈希和限制。

`src/ffi.rs` 与 `include/termforge_candidate.h` 提供 staticlib C ABI；`experiments/core/candidate_terminal.{hpp,cpp}` 将其包装为现有 `Screen`/`Line` 拥有值。feed、事件、行/格文本都经调用方缓冲区返回，不跨 FFI 暴露 Rust 引用或异常。C++ 同步稳定 ID、版本、ending/next_id/wrap_columns、样式和历史淘汰；[接口与边界](../../core/CANDIDATE.md)另列。

## 当前结果与历史边界

- termforge.3 的[共同矩阵](../../differential/results/2026-10-26-candidate-common-v6/summary.json)为 95 次一致；[扩展能力](../../differential/results/2026-10-26-capabilities-v5/summary.json)分类不变；[fuzz](../../differential/results/2026-10-26-fuzz-v4/summary.json)为 2 次固定已知差异、6 次一致；[真实应用](../../differential/results/2026-10-26-real-applications-v6/summary.json)固定 3 次一致。[margin 验证](../../shell/results/2026-10-26-margin-state/validation.json)区分公开源 VT 的恢复与真实 ConPTY 的宿主限制；#41 的宿主 margin 和 #42 的 More 尾部 ECH/捕获时序仍未闭合，不是完整产品 TUI。
- termforge.2 的[共同矩阵](../../differential/results/2026-10-25-candidate-common-v5/summary.json)为 95 次、1011 检查点全部一致；旧 ED 差异不再以 `reference_difference` 放行。[扩展能力](../../differential/results/2026-10-25-capabilities-v4/summary.json)保持 15 次自研未支持、5 次一致、5 次 resize 差异；[真实应用](../../differential/results/2026-10-25-real-applications-v3/summary.json)3 次全部一致；[fuzz](../../differential/results/2026-10-25-fuzz-v3/summary.json)为 2 次固定已知差异、6 次一致。均不是完整语义接受或产品 TUI。
- C++ 适配器验证分块一致性、真实回复、备用屏/滚动区、ED、Unicode、resize、ID/版本、选择、搜索和 HistoryViewport。显式候选 `ShellWindow` 已运行两轮真实 Vim、60×12 resize、主屏恢复及经有界 InputQueue 的真实光标回复，并修复普通空格背景残留；证据见 [validation](../../core/results/2026-10-25-candidate-adapter/validation.json)。默认构建保留自研回退；#29 的限定 resize/历史切片已有验证，#36/#38 的真实回归、#41 的宿主 margin、完整 R1 回归和实际呈现仍未关闭。
- #25 的[逐项裁决](../../differential/decisions.md)保留旧 0.26.0 差异；#26 的[路线审查](../../../docs/research/2026-10-25-core-route-review.md)与 project/interactive 计量是原始 0.26.0 历史基线，termforge.1 是更早 headless 切片，均不覆盖 termforge.2。
- 默认/ANSI 16 色映射为现有数值；其他颜色以 `-2` 保留未投影。上游 `Cell::is_empty` 合并空格和空单元格，不能裁决显式尾空格。`replies` 是相邻检查点的新回复，不是累计队列。
- resize/reflow 合同归 #29，不能由候选行为静默替换。候选可构建不等于支持 [#23 平台台账](../../../research/windterm-2.7.0-platform-scope.md)的全部系统；Windows 7 非 ConPTY、macOS 10.13、glibc 2.17、x86 和 ARM 缺口继续归 #34/R4。#37 的限定 Windows Vim CI 已通过，其他 shell/生命周期回归由 #36/#38 保留。
