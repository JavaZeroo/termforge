# R2 原生核心路线重审（#26）

调查与决定日期：2026-10-25。固定候选为 `alacritty_terminal 0.26.0`，对照为当前自研 C++ 核心。本文记录 [#26](https://github.com/JavaZeroo/termforge/issues/26) 的平台、维护、接口和路线证据；[ADR-0003](../adr/0003-core-reference-and-reuse.md) 保存决定，[FEATURES](../FEATURES.md) 保存实际能力。没有启动 WindTerm、GUI 或截图探针，也没有把 headless 轨迹写成产品呈现。

## 决定

**R2 新终端能力复用候选的 `Term` / `Processor`，不再沿自研核心扩展完整 VT/TUI。** TermForge 保留自有传输、队列、会话生命周期、历史/选择/搜索行为合同和呈现快照；候选的 PTY/event loop 不因此成为产品依赖。现有 C++ 核心保留为 R1 回退与独立差分参考，直到候选产品整合通过相同回归。

这是核心依赖路线决定，不是“候选已接入产品”或“完整 VT/TUI 已验收”。#27–#30 可以按此路线实施；#34 决定最终产品接口，#35 仍需完整 Windows 开发版验收。

选择依据是实际行为覆盖、维护边界和共同计量，而不是 Rust/C++ 语言标签：

- 共同矩阵为 95 次、1011 检查点，90 次一致、5 次精确锁定的候选参考差异；[ED1/ED2 裁决](../../experiments/differential/decisions.md) 不把差异算作兼容通过。
- [扩展能力](../../experiments/differential/results/2026-10-25-capabilities-v2/summary.json) 已实际提供备用屏、滚动区、回复和 resize；自研对应多项明确未支持。[真实 cmd/more.com/Vim 核心轨迹](../../experiments/differential/results/2026-10-25-real-applications/summary.json)与[有界 fuzz/最小回归](../../experiments/differential/results/2026-10-25-fuzz/summary.json)均可复验，但不代表产品 GUI/TUI。
- [project](../../experiments/measure/results/2026-10-25-core-compare/)与 [interactive](../../experiments/measure/results/2026-10-25-core-interactive/)原始批次分开 feed 和观测。纯 feed CPU 中位数分别为自研 42.61–44.42 / 候选 28.12–29.70 ns/字节，以及自研 64.64–76.49 / 候选 62.82–90.22 ns/字节。候选全量投影/JSON 观察成本显著，不能进入产品热路径，也不能据此作性能排名。
- 候选已有 `Term::damage` / `reset_damage` 和 `EventListener`，具备做增量呈现与回复适配的实际接口；自研完整模式、回复和兼容维护则需本项目逐项承担。

## 平台差距

平台目标来自 [#23 冻结台账](../../research/windterm-2.7.0-platform-scope.md)。“工具链有目标”“核心能编译”“PTY 能运行”“完整客户端支持”是四个不同结论。

| 范围 | 固定来源事实 | 当前证据与缺口 |
|---|---|---|
| Windows 7 x86/x64 | Rust 1.92 普通 MSVC 目标要求 Windows 10/Server 2016；专用 `i686/x86_64-win7-windows-msvc` 为 Tier 3，不提供预编译 std，须自行 build-std（[普通 MSVC](https://doc.rust-lang.org/1.92.0/rustc/platform-support/windows-msvc.html)、[Win7 MSVC](https://doc.rust-lang.org/1.92.0/rustc/platform-support/win7-windows-msvc.html)） | 没有候选 Win7 目标、依赖或运行验证。保持明确缺口，不宣称支持或证明绝对不可移植 |
| Windows PTY | 固定候选 Windows 模块只选择 ConPTY；先尝试 `conpty.dll`，否则调用系统 `CreatePseudoConsole`（[固定模块](https://github.com/alacritty/alacritty/blob/94e7c8874e526b1e67b349d9ba30ddf81669119e/alacritty_terminal/src/tty/windows/mod.rs)、[固定 ConPTY](https://github.com/alacritty/alacritty/blob/94e7c8874e526b1e67b349d9ba30ddf81669119e/alacritty_terminal/src/tty/windows/conpty.rs)）。微软 API 下限为 Windows 10 1809/Server 2019（[Microsoft](https://learn.microsoft.com/en-us/windows/console/createpseudoconsole)） | 没有内建 WinPTY/Windows 7 后端。本路线不复用候选 PTY，保留自有传输，未来非 ConPTY 后端须独立抽象和验收；headless 成功不能代替 PTY 验收 |
| macOS 10.13 x64 | Rust 1.92 `x86_64-apple-darwin` 基线为 macOS 10.12，部署目标可控制（[Rust Apple](https://doc.rust-lang.org/1.92.0/rustc/platform-support/apple-darwin.html)）；候选 manifest 未声明更高下限 | 工具链不排除 10.13，但没有构建、SDK/链接审计、PTY 或真实 10.13 测试 |
| Linux glibc 2.17 x64 | Rust 1.92 的 `x86_64-unknown-linux-gnu` 基线包括 glibc 2.17+（[固定平台表](https://github.com/rust-lang/rust/blob/1.92.0/src/doc/rustc/src/platform-support.md)）；候选有 Unix PTY 实现 | Ubuntu 24.04 CI 不证明 glibc 2.17。仍需旧 sysroot 构建、ELF `GLIBC_*` 审计和真实 PTY 生命周期测试 |
| x86 与 ARM | Rust 提供部分相关目标；普通 i686 Windows 不等于 Win7，Apple ARM 从 macOS 11 起 | 没有候选 x86/ARM 构建或运行证据。保持未验证，不能把 target 存在写成候选支持 |

实际观察只覆盖本地 Windows 11、Windows Server 2025 与 Ubuntu 24.04 headless 实验。[历史双平台 CI 40117504346](https://github.com/JavaZeroo/termforge/actions/runs/40117504346)通过；当前 [40155572785](https://github.com/JavaZeroo/termforge/actions/runs/40155572785) 的 Ubuntu 通过、Windows 在真实 Vim 捕获失败，由 [#37](https://github.com/JavaZeroo/termforge/issues/37)单独跟踪。没有 macOS、旧 Windows、glibc 2.17、x86 或 ARM 产品验收。

## 版本与维护路径

- crates.io 固定 [0.26.0 元数据](https://crates.io/api/v1/crates/alacritty_terminal/0.26.0)：2026-04-06 发布、Apache-2.0、未 yanked、crate SHA-256 `bda177466b9524d59f1b12f0dd30b68696788e9992a7e959021c4a0ed96fcf59`。上游 tag 对应 commit [`94e7c887`](https://github.com/alacritty/alacritty/commit/94e7c8874e526b1e67b349d9ba30ddf81669119e)。
- 顶层 crate 声明 Rust 1.85，但当前 `Cargo.lock` 的 [`home 0.5.12`](https://crates.io/api/v1/crates/home/0.5.12)要求 Rust 1.88；因此本次锁定图的声明下限是 **1.88**，实际验证工具链固定 1.92.0。升级必须审查整个依赖图。
- 审查时 0.26.0 是最新发布版，但上游仍活跃。[固定上游 0.26.1-dev changelog](https://github.com/alacritty/alacritty/blob/542147067dd016fc5f49120ef0ba605a98a4e3a7/alacritty_terminal/CHANGELOG.md)已有 PTY non-blocking panic、ViMotion、**逐格零宽字符内存无界**及零列 Row unsoundness 修复。不得把 latest release 当成包含全部修复；产品整合前必须等待含修复的发布或选择固定回溯补丁并重新跑全部语义/资源回归。
- [上游贡献政策](https://github.com/alacritty/alacritty/blob/94e7c8874e526b1e67b349d9ba30ddf81669119e/CONTRIBUTING.md)要求 PR、测试、MSRV 和显著变更 changelog。没有推断支持 SLA、固定发布周期或回溯保证。

具体兼容修复不是“改投影文字”：

1. **ED1：** [最小差分](../../experiments/differential/results/2026-10-25-erase-display/)与规范裁决证明候选错误。固定源码 [`Term::clear_screen`](https://github.com/alacritty/alacritty/blob/94e7c8874e526b1e67b349d9ba30ddf81669119e/alacritty_terminal/src/term/mod.rs) 的 `ClearMode::Above` 只在 `cursor.line > 1` 时清理前序行，需形成最小上游回归与修复；产品不能保留该已知错误。
2. **ED2：** 同一路径 `ClearMode::All` 在非备用屏调用 `clear_viewport`，会把可见行迁入历史；TermForge 已裁决保留“清屏但不新增/清空历史”。需争取可配置上游行为，或以固定许可兼容补丁维持项目策略，并用原复合轨迹、ED2 最小例及真实 shell 回放验证。
3. 需要本地补丁时，固定精确 Git commit、保留 Apache-2.0 与来源、记录相对发布版的 diff；不得把修改后的源码继续伪称未修改的 crates.io 0.26.0。每次 rebase/升级重跑共同/扩展/真实应用/fuzz、完整 R1 和成本探针。
4. 升级时审查 changelog、tag→commit、整个 lockfile 的 MSRV/yank/许可/API 变化，以及 released 与 unreleased 修复。CI 的 Windows 真实应用失败不得跳过或改成警告。

## transport / presentation 接口影响

- **传输保持自有：** `OutputQueue` 的 8192 字节块和 4 ms/65536 字节 UI tick 预算只作为当前实现事实，不硬编码进核心。适配器接收字节并驱动 `Processor`；每批后排干 `EventListener`。`PtyWrite` 必须进入现有有界 `InputQueue`，拒绝/失败可见，不能绕过背压直接写 ConPTY。Title、clipboard、bell、wakeup 等事件由对应产品合同处理。
- **线程与所有权：** 当前 Parser/Snapshot/Renderer 仅由 UI 线程拥有。初版候选适配同样限制在会话所属线程；C++/Rust 边界使用拥有所有权的值和显式错误，不跨 FFI 暴露 Rust 引用、异常或并发可变网格。
- **呈现不走 JSON：** trace/projection 仅是测试观测。产品适配使用 `damage` 和值快照，保留现有 retained 绘制、dirty revision、IME caret 与实际提交观察；观察开/关不是无插桩 A/B。
- **历史身份是硬合同：** 当前选择、搜索和阅读锚点依赖稳定 `line_id`、`version`、物理列、软硬换行与淘汰语义（`TextDocument` / `HistoryViewport`）。候选网格和 damage 不直接提供本项目的全部身份合同，适配器必须显式维护并测试，不能把行号当稳定 ID。
- **resize 不静默换策略：** 候选会 reflow，自研当前是物理行裁剪。#29 必须先定义逻辑行、历史与身份行为；在合同确定前不得用候选替换当前 resize 后宣称等价。
- **R1 回退：** 自研核心与现有测试保留，候选产品接入只有在相同选择/复制/粘贴/查找/IME/多会话/资源回归通过后才替换默认会话。失败触发路线重审，不静默改回大规模自研 VT 扩张。

## 重审条件与未完成项

出现以下任一情况须重审：兼容补丁无法在上游或固定 fork 中可维护；稳定行身份/增量投影无法满足选择搜索合同；真实产品成本或资源有界性不达标；最低平台出现不可接受的硬缺口；上游维护/许可发生不利变化。

本审查不完成产品 FFI、补丁、真实 TUI、复杂 Unicode、reflow、不同 DPI、跨平台客户端、无插桩性能对照或 WindTerm 性能等价。它们仍归对应 issues；R2/#7 保持开放。
