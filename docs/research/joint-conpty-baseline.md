# 候选 .4 与显式官方 ConPTY 的联合基线（#55）

本记录固定后续 #51–#53 的共同复现输入，不宣布后端兼容性或 R2 通过。[验证清单](../../experiments/conpty/results/2026-10-26-joint-backend/validation.json)是逐配置结果与身份的入口；旧 [.3 记录](../../experiments/conpty/results/2026-10-26-reviewed-backend/validation.json)不改写。

## 固定源码与配置

联合源码 `3e5b96d0e0854b691b6dd41821ab10b6124b83eb` 合并 main `3d6d44d3b3e9a9a3e41da98e8711e9f19a211fd8` 和 Draft #50 `e2702af0e03cf6f78c0f433820df7edf64146b87`。产品代码自动合并；两份索引冲突保留 main 的文档结构。此后证据提交不改变这一产品源码身份。

| 独立构建 | 核心 | Session 后端与 flags | 定位 |
|---|---|---|---|
| `joint-default` | 自研 C++ | system / 0 | 默认对照，无 Rust 构建依赖 |
| `joint-system` | `0.26.1-dev+termforge.4` | system / 0 | 主线候选对照 |
| `candidate` | 同一 `.4` | 固定官方 `1.25.260930003` / `0x18` | 显式实验联合配置 |

通用 margin A/B 探针仍显式使用 flags 0；实际 GUI margin、Unicode/glyph 和 Backend 生命期使用配置表中的 Session 策略。两类结果不得互换。后端双文件、锁文件、C++/Rust 产物、源码 blob 与本机换行后的 SHA 分别记录；未将二进制或私有运输流提交到仓库。

## 重建与重放

固定官方同包文件的来源、双 SHA 和完整 MIT 见 [third_party/conpty](../../third_party/conpty/README.md)。以下使用已核对的本地目录，不下载、安装或修改全局 Terminal。三个输出目录独立；候选 Rust 静态库在同一源码目录产生。

```powershell
cmake -S . -B build/joint-default -A x64 -DTERMFORGE_BUILD_CANDIDATE_ADAPTER=OFF -DTERMFORGE_CONPTY_BACKEND_DIR=
cmake --build build/joint-default --config Release --parallel 4
cmake -S . -B build/joint-system -A x64 -DTERMFORGE_BUILD_CANDIDATE_ADAPTER=ON -DTERMFORGE_CONPTY_BACKEND_DIR=
cmake --build build/joint-system --config Release --parallel 4
cmake -S . -B build/candidate -A x64 -DTERMFORGE_BUILD_CANDIDATE_ADAPTER=ON -DTERMFORGE_CONPTY_BACKEND_DIR=<absolute-reviewed-directory>
cmake --build build/candidate --config Release --parallel 4
cargo build --release --locked --bins --manifest-path experiments/candidates/alacritty/Cargo.toml
cargo test --release --locked --manifest-path experiments/candidates/alacritty/Cargo.toml
ctest --test-dir build/joint-default -C Release --output-on-failure
ctest --test-dir build/joint-system -C Release --output-on-failure
ctest --test-dir build/candidate -C Release --output-on-failure
```

需在 PATH 中显式提供固定 Vim；本批使用 Git for Windows 附带的 Vim，身份与版本输出哈希见清单。Python 依赖、构建入口遵循[贡献指南](../../CONTRIBUTING.md)。仅修改当前进程环境：`PYTHONUTF8=1`，`TERMFORGE_BIN_DIR` 指向所选 `bin/Release`；官方配置加 `TERMFORGE_EXPECT_CONPTY_BACKEND=reviewed-side-by-side-1.25.260930003`，系统配置不设置此变量。

- #51：`experiments.shell.run.execute(exe, "resize", fresh_out, timeout=40)`，或 `ShellTests.test_resize_child_dimensions_and_interrupt`。原内部25秒期限、精确行/位置/重放断言不变。
- #52 GUI：同入口 `candidate-tui`，40秒外层期限；`CandidateTuiTests.test_real_fullscreen_editor_and_cursor_reply` 保留事件文件与精确写盘合同。native 路线用 `experiments/differential/real_app_matrix.py::run_capture`，保留60秒监督期限、shell/More/editor 完整流程。
- #53：同入口 `ui-paste`，40秒外层期限；原3MB、768-byte receiver gate、16KiB门槛与实际 cancel/close 合同不变。提前 completed 仍拒绝。
- 源175B：`python -m experiments.conpty.margin_run --backend system --candidate-trace <fixed-trace.exe> --output <fresh-out>`，再用固定 `conpty.dll` 绝对路径替换 `system`。此探针的 flags 0 与产品 flags 24 分列。

真实会话均由原监督器的自有 Job、GO 和非输入 desktop 管理；native capture 不创建 GUI desktop。`fresh_out` 必须不存在，失败、原始 transport、屏幕和异常原文留在忽略的本地目录。公开清单采用白名单，不含私有路径、PID、desktop 名或原始屏幕。

## 联合配置的已确认状态

原场景各两次：resize 为一红一绿（红在 stage43），GUI Vim 一绿一红（红在 stage59）；native shell/More/editor 一绿一红（红在 new-buffer）。3MB 粘贴两次在 receiver gate 前完成，仍不满足未完成期取消条件。成功轮不能关闭旧失败，间歇性本身也不证明时序根因。六次 GUI 的外层 Job 均归零、desktop 删除；两次 native capture 均 Job0，无监督超时。

同源175B A/B 保留两网格与 vt-on/off：system 的 vt-on 末端 margin 两尺寸均错，固定官方两尺寸均正确；所有8次 child37、EOF109、Job0。原生 CHAR_INFO 的 margin 缺口继续由 #41/#56 处理。没有通过末端成功豁免原生合同。

完整 CTest、内部 skip、真实 Unicode/glyph、API 生命期/拒绝以及额外失败按[清单](../../experiments/conpty/results/2026-10-26-joint-backend/validation.json)分配置记录。共享 Windows 宿主同时运行多组测试；这些是正确性诊断，不能作为空闲环境性能或稳定性结论。失败不被后续成功覆盖；未建立因果关系的失败继续保留。

## 接续边界

#51 先记录 host 实际 resize、核心本地 resize 和运输 offset，验证残尾的因果；#52 分别核对 GUI/独立 native 的应用事件与输入；#53 在保持负载/原阈值的前提下建立确定性 sender 栅栏，再验真实取消与关闭。#54 成本和 #56 原生路径独立推进。

本基线可包含真实红项；它只提供可重建的共同输入。Draft #50、#47、#41、#34 与 R2 仍有各自未完成条件。完整 R1/IME/八会话、真实不同 DPI、长时资源、最终包和 WindTerm 性能等价均未由本基线验收，`presented_done=null`。
