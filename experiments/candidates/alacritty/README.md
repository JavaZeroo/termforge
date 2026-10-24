# Alacritty 原生核心候选（R2-01 / #24）

本目录是 [#20](https://github.com/JavaZeroo/termforge/issues/20) 的实际候选构建与状态适配实验，不是产品核心选型结论，也不改变现有 C++ 核心的默认构建。这里不测量性能，不启动 GUI，不扩大 R2 的 VT/TUI 产品实现。

## 固定来源与许可

| 项目 | 固定值 | 一手来源 |
|---|---|---|
| `alacritty_terminal` | `0.26.0`，Apache-2.0，Rust >= 1.85 | [crates.io 版本元数据](https://crates.io/api/v1/crates/alacritty_terminal/0.26.0)、[固定 crate 源码](https://crates.io/api/v1/crates/alacritty_terminal/0.26.0/download)、[上游仓库](https://github.com/alacritty/alacritty) |
| crate SHA-256 | `bda177466b9524d59f1b12f0dd30b68696788e9992a7e959021c4a0ed96fcf59` | 同一 crates.io 元数据的 `checksum` |
| 适配器工具链 | Rust/cargo 1.92.0 | 本机与候选 CI 显式选择；不宣称这是最低验证版本 |
| 传递依赖 | `Cargo.lock` 固定 59 个依赖包；`cargo metadata` 共 60 个包（含适配器） | `cargo metadata --locked --format-version 1` 核对精确版本、来源和 registry 校验值 |

`Cargo.toml` 用 `=0.26.0` 固定候选并关闭默认 serde feature；`Cargo.lock` 固定实际传递依赖。未 vendoring 上游源码，也未把第三方代码改标 MIT。候选二进制当前只用于本地/CI 实验，不进入 Alpha 或产品发行包；若以后分发，必须同时完成全部依赖许可与通知审计。

选择这个候选是因为它能直接提供无窗口终端网格、光标/样式、历史、备用屏、resize 和 `Event::PtyWrite` 回复事件。选择依据不包括 Rust 语言、Alacritty 的声誉或其整体性能声明。

## 干净构建与验证

从仓库根目录执行；输出目录必须不存在：

```powershell
rustup default 1.92.0
cargo build --manifest-path experiments/candidates/alacritty/Cargo.toml --release --locked
cargo test --manifest-path experiments/candidates/alacritty/Cargo.toml --locked
python experiments/candidates/alacritty/run.py --out local/candidate-alacritty-new
python -m unittest experiments.candidates.alacritty.test_candidate -v
```

Linux/macOS 使用相同命令。默认二进制为 `target/release/termforge-alacritty-trace[.exe]`；可用 `TERMFORGE_ALACRITTY_TRACE` 显式选择另一构建，缺失文件会失败，不回退到旧产物。主 CMake/CTest 不需要 Rust；候选自己的 workflow 在 Windows/Ubuntu 干净检出上构建。

## Trace 与检查内容

`alacritty_trace` 的 CLI 与现有 `terminal_trace` 一致：

```text
alacritty_trace input.bin rows columns history chunks.txt
```

每个检查点输出 `offset`、光标、待换行、当前样式、屏幕格、历史格、模式位和该间隔内的 `PtyWrite` 回复。输入上限 1 MiB，累计投影上限 100 万格，子进程测试限时 20 秒；非法尺寸、分块和不完整覆盖返回非零。Trace 序列化只用于正确性观测，不能拿它的耗时做核心性能排名。

`run.py` 固定检查：

- ASCII、中文、组合标记和 emoji 在整块/逐字节输入后的状态一致；
- 有界历史、备用屏退出后的主屏恢复，以及光标查询产生的真实回复；
- 历史容量和屏幕行内容；
- 测试另验证非法分块、缺失二进制和结果被改变时不能判为通过。

`summary.json` 记录候选版本、许可、crate/适配器源码/二进制/输入哈希、平台、检查点和限制。2026-10-24 本地 Windows 干净构建的[结果摘要](results/2026-10-24-windows/summary.json)和[验证边界](results/2026-10-24-windows/validation.json)已审核公开；Ubuntu workflow 尚未运行。CI 只上传这些合成白名单记录，不上传候选二进制或个人配置。

## 适配边界与后续

- 默认/ANSI 16 色映射为现有数值；其他颜色暂以 `-2` 保留“未投影”，不能当成默认色或完整样式通过。
- 上游 `Cell::is_empty` 将空格和空单元格合并；本投影不用于裁决显式尾空格语义。宽字符 spacer、零宽字符和 NFC 差异须在 #25 单独比较。
- `replies` 是相邻检查点之间的新回复，不是最终状态里的累计队列；汇总时按检查点顺序收集。
- 备用屏/查询的一个固定轨迹不证明完整 TUI、所有 VT 模式、真实 ConPTY 生命周期或产品 UI 兼容。
- 候选可构建不等于支持 [#23 平台台账](../../../research/windterm-2.7.0-platform-scope.md)的全部系统。Windows 7 的非 ConPTY 路径、macOS 10.13、Linux glibc 2.17、x86 和 ARM 缺口在 #26/#34 单独判断。
- #25 已完成[共同轨迹与逐项裁决](../../differential/decisions.md)：95 次、1011 检查点，90 次一致、5 次已裁决参考差异；ED1 候选错误与 ED2 历史策略已固定签名，执行通过不等于语义接受。真实应用和 fuzz 比较仍未完成；#26 才测量成本并重审 ADR-0003。
