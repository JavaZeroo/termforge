# Alacritty 原生核心候选（R2-01 / #24）

本目录包含候选构建、状态适配、固定兼容补丁与核心成本实验。[ADR-0003](../../../docs/adr/0003-core-reference-and-reuse.md)已决定 R2 新能力复用候选 Term/Processor；当前尚未完成 C++ 产品 FFI 或默认 shell 替换，不启动 GUI，不把 headless 结果写成产品 TUI。

## 固定来源与许可

| 项目 | 当前固定值 | 来源 |
|---|---|---|
| `alacritty_terminal` | `0.26.1-dev+termforge.1`，Apache-2.0 | [上游 commit `5421470`](https://github.com/alacritty/alacritty/commit/542147067dd016fc5f49120ef0ba605a98a4e3a7)与[来源/补丁台账](PROVENANCE.md) |
| 本地补丁 | ED1、ED2、稳定 Row ID/换行元数据、安全完整 Row swap | `vendor/alacritty_terminal/src/`；所有文件哈希进入结果 manifest |
| 工具链 | Rust/cargo 1.92.0 | 本机与候选 CI 显式选择；当前锁定图最高声明 Rust 1.85 |
| 传递依赖 | 59 个注册表包 + 固定 path crate，共 60 个包 | `Cargo.lock` 与 `cargo metadata --locked --format-version 1` |

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
```

Linux/macOS 使用相同命令。默认二进制为 `target/release/termforge-alacritty-trace[.exe]`；可用 `TERMFORGE_ALACRITTY_TRACE` 显式选择另一构建，缺失文件会失败，不回退到旧产物。主 CMake/CTest 不需要 Rust；候选自己的 workflow 在 Windows/Ubuntu 干净检出上构建并运行固定 crate 的 140 个单元测试和 doc test。

## Trace 与检查内容

`alacritty_trace` 的 CLI 与现有 `terminal_trace` 一致：

```text
alacritty_trace input.bin rows columns history chunks.txt
```

每个检查点输出 `offset`、光标、待换行、当前样式、屏幕格、历史格、模式位和该间隔内的 `PtyWrite` 回复。输入上限 1 MiB，累计投影上限 100 万格，子进程测试限时 20 秒；非法尺寸、分块和不完整覆盖返回非零。Trace 序列化只用于正确性观测，不能拿它的耗时做核心性能排名。

`run.py` 固定检查 ASCII、中文、组合标记和 emoji 分块一致性，有界历史、备用屏恢复、真实回复、非法分块、缺失二进制和篡改结果负例。摘要记录当前版本、上游 commit、适配器与全部 vendor 源码/二进制/输入哈希、平台、检查点和限制。

## 当前结果与历史边界

- termforge.1 的[共同矩阵](../../differential/results/2026-10-25-candidate-common-v4/summary.json)为 95 次、1011 检查点全部一致；旧 0.26.0 的 ED1/ED2 差异已修复，不再以 `reference_difference` 放行。[扩展能力](../../differential/results/2026-10-25-capabilities-v3/summary.json)保持 15 次自研未支持、5 次一致、5 次 resize 差异；[真实 cmd/more.com/Vim 核心轨迹](../../differential/results/2026-10-25-real-applications-v2/summary.json)3 次全部一致；[fuzz](../../differential/results/2026-10-25-fuzz-v2/summary.json)为 2 次固定已知差异、6 次一致。均不是产品 GUI/TUI 或完整语义接受。
- #25 的[逐项裁决](../../differential/decisions.md)保留旧 0.26.0 的 90 次一致、5 次已裁决参考差异及修复关系；#26 的[路线审查](../../../docs/research/2026-10-25-core-route-review.md)与 project/interactive 计量也是原始 0.26.0 历史基线，不能改写成 termforge.1 的同版本性能结论。
- 默认/ANSI 16 色映射为现有数值；其他颜色以 `-2` 保留未投影。上游 `Cell::is_empty` 合并空格和空单元格，不能裁决显式尾空格。`replies` 是相邻检查点的新回复，不是累计队列。
- 稳定 Row ID 和换行元数据为后续拥有值适配提供基础；C++ FFI、产品 damage/快照、真实输入队列和 R1 系统整合仍未完成。resize/reflow 合同归 #29，不能由候选行为静默替换。
- 候选可构建不等于支持 [#23 平台台账](../../../research/windterm-2.7.0-platform-scope.md)的全部系统。Windows 7 非 ConPTY、macOS 10.13、glibc 2.17、x86 和 ARM 缺口继续归 #34/R4；Windows CI 的 Vim 捕获问题由 #37 单独保留。
