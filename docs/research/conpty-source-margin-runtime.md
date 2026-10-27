# 同源 ConPTY 原生 margin 运行对照

固定 Microsoft Terminal `dc8ae365847d41f62d1ebca93815bb00959bc12f` 的两组重建产物，在原始 175 字节、两种网格和原期限下完成一次预定对照：未修补版复现原生 final 错误，应用 margin 状态补丁后四阶段原生文字、逐格字符/属性和光标全部符合原独立预期。实际运输经候选核心回放，两版末端画面均正确；VT-off 反例在两版之间保持一致。

这提供了原用例的同源因果证据。它没有覆盖样式变化、历史、resize 交叉及完整产品合同，不能关闭 [#56](https://github.com/JavaZeroo/termforge/issues/56) 或 [#41](https://github.com/JavaZeroo/termforge/issues/41)，也不决定 #34 的产品后端。原系统/官方包失败与新源码构建结果分开保存。

## 来源与构建身份

[PR #96](https://github.com/JavaZeroo/termforge/pull/96) 的 [v2 构建 40449292015](https://github.com/JavaZeroo/termforge/actions/runs/40449292015) 使用同一固定源码、依赖和工具链：

- Terminal 提交如上，vcpkg baseline 为 `927f62e4b8838bd7e441e9c45103a16ffd75007e`；原 NuGet/vcpkg 版本和 feature 不变。
- 实际 CI checkout 为 `ad857d448dfe64c0c11d10254043a8b893e7546a`，VS18 / MSVC 14.51.36231 / SDK 26100；详细工具和文件身份见[构建摘要](../../experiments/conpty/results/2026-10-27-source-margin-runtime/build-summary.json)。
- 补丁 SHA-256 为 `dc27377e399bd0217a0b76e16cc89f087c1b781929e4c958ad30c3a475322b81`。仅测试补丁时 focused 测试明确失败一次、退出 1；随后保存 baseline EXE/DLL，再应用两个生产文件的补丁，focused 1/1、完整 adapter 412/412、零跳过，最后保存 patched EXE/DLL。
- 两组都有独立 `variant.json`，绑定生产补丁是否应用、测试状态、双文件 SHA 与大小；共享 summary 的完整 adapter 通过只属于 patched。baseline 没有执行完整 adapter。

| 文件 | baseline SHA-256 / 字节 | patched SHA-256 / 字节 |
|---|---|---|
| OpenConsole.exe | `fabdc934d9755c405fe848251905e8c0e70a2bf7702b8ed7ccd104eec6591588` / 1146880 | `1c8f071f6e4779ec297fb041af4eaea39be29b6920cef625ab91d1cbc20c79cb` / 1147392 |
| conpty.dll | `9d165745cce4b29a5818a45c23f68e962b3a3140c2e00566bd17fee970fdd9d1` / 108544 | 与 baseline 逐字相同 |

原始 summary SHA 为 `8132695b32162ff22e61d2de166a31fad508f0b63cbb46de8d1612bd197e1ded`，两个 pair 和 summary artifact 中相同。artifact `12710186210`（summary）、`12710186222`（baseline）、`12710146241`（patched）的 ZIP SHA、来源和内部文件均已核对，记录在[验证清单](../../experiments/conpty/results/2026-10-27-source-margin-runtime/validation.json)。入库构建摘要是规范化 UTF-8/LF 的解析内容；原始摘要字节 SHA 不冒充入库文件 SHA。

这些是带 Microsoft MIT LICENSE/NOTICE 的短期实验产物，不是正式包。没有把二进制、私人路径、原始提取或 WindTerm 文件加入源码库。artifact 保留期为 14 天；之后可按固定构建入口重建，但新文件身份须重新审核，不宣称逐字节可复现。

## 隔离入口与身份拒绝

`TERMFORGE_BUILD_SOURCE_CONPTY_EXPERIMENT` 默认 OFF，本次使用 `conpty_source_margin_probe` 与 `conpty_source_backend_probe`；后续[补充对照](conpty-source-margin-controls.md)另增加 `conpty_source_margin_controls`。只有这三个独立目标编译 source factory，不能与产品 `TERMFORGE_CONPTY_BACKEND_DIR` 同时启用；默认及官方 factory 仍严格接受各自原身份。产品 bundle、默认后端和 PR #50 的固定官方配置均未切换。

source factory 只允许表中两个 SHA 对，分别报告 `source-dc8-r56-baseline-run40449292015` / `source-dc8-r56-patched-run40449292015`。Python 入口另核对原始 summary、patch、精确 variant 字段与类型、文件大小和 SHA；布尔值不能以 0/1 代替。DLL 与 EXE 在加载期间保持只读共享句柄；create/resize/close 使用同一模块，模块存活至会话和子进程回收之后。

独立 API/身份验证实际执行 15 个原生场景及 8 个清单拒绝检查：两版 identity、create/resize/自然退出/EOF/卸载；官方入口拒绝两版 source，source 拒绝官方、相对路径、缺失和未知文件；分别篡改 DLL/EXE；交换 variant、数字冒充布尔、修改 summary 或文件字节均拒绝。两版 DLL 本来相同，不虚构“混用相同 DLL”的可识别差异。另有系统 API 生命周期、原官方/系统 loader 14/14 和原 margin/oracle/cleanup 13/13 回归。

13/13 与 14/14 的计数和耗时来自当时命令输出，未单独保存 stdout 日志；验证清单明确标记这一可复核限制。8个margin样本和15个API/身份场景的原始记录另外完整保存，不借用两项会话输出代替。

## 原始 175B 的运行结果

本机 Windows 11 x64 build 26200，独立探针使用 VS17 / MSVC 19.42.34433 构建；候选 trace 为 `termforge.5`、Rust 1.92.0。运行时代码为 `fdee94fe94d48e83f5a61f191bddfc812d43a2f8`、工作树干净；探针、trace、参与源文件的 SHA 保存在验证清单。API/身份检查脚本字节绑定后续提交 `08ec4708afa347d87b584def73a3dba5021f7870`，不把它写成前一轮运行时源码。

源 SHA 为 `01878a5d54c343007309abb89faeb90c89262a8b55e420c5f267342ed9b27e59`，175 字节与四阶段保持原样；原生预期来自[独立 oracle](../../experiments/conpty/margin_contract.py)，没有通过候选 parser 生成。每种 variant × 网格 × VT mode 各一次，共 8 次，没有自动重试或失败后延长预算。

| 网格与模式 | baseline | patched | 生命周期 |
|---|---|---|---|
| 80×24、VT-on | before/alt/restored 全部正确；final 文字/格错误、光标正确，出现 `REGION_CIXED` | 四阶段文字、格/属性、光标全部正确；主屏区域恢复 | 均自然退出 37、EOF 109、Job0，无 force |
| 12×7、VT-on | 同一 final 错误；保留结束标记换行 | 四阶段全部正确，结束标记仍分两行 | 同上 |
| 80×24、VT-off | 两版完整原生格 SHA、文本、光标和末端文本一致 | 不把 VT-off 当成一般修复方案 | 同上 |
| 12×7、VT-off | 同上，原窄屏反例保留 | 同上 | 同上 |

VT-on 两版的实际运输经同一候选 trace 回放后，主屏末端文本均正确、alternate=false；说明 baseline 原生错误不能由末端正确掩盖。这里没有测末端逐阶段光标。原生属性检查仅覆盖原 source 未设置 SGR 时的初始属性保持，不外推为显式样式恢复。

所有 8 次保留原 5 秒 child wait、15 秒外部期限与共享 5 秒 cleanup；creation flags 为 0，外 Job 最终 active0/total4/terminated0，未强制回收。另一个 API 生命周期探针使用原 20 秒外部期限，source flags24 的 create/resize/close 检查不能与 flags0 margin 拼成完整产品结果。

原件保存在 `local/r56-native-runtime/original-matrix-1` 和 `backend-check-2`；公开清单只保留合成结果与审核字段。首轮 margin 测试调用漏设 `TERMFORGE_BIN_DIR` 导致两个 setup error，修正调用后原 13 项通过；首次新增身份检查误要求 Job total=1，实际拒绝样本 total2/active0，已改为保留实际总数并严格检查回收。两项失败均保留，不修改原用例或期限。

## 复验与后续边界

先按[构建入口](../../experiments/conpty/upstream/README.md)取得并审核上述固定产物，在全新输出目录执行：

```powershell
cmake -S . -B local/source-margin-build -A x64 -DTERMFORGE_BUILD_SOURCE_CONPTY_EXPERIMENT=ON -DTERMFORGE_BUILD_CANDIDATE_ADAPTER=OFF -DTERMFORGE_CONPTY_BACKEND_DIR= -DBUILD_TESTING=OFF
cmake --build local/source-margin-build --config Release --target conpty_backend_probe conpty_source_backend_probe conpty_source_margin_probe --parallel 2
cargo +1.92.0 build --manifest-path experiments/candidates/alacritty/Cargo.toml --locked --release --bin termforge-alacritty-trace
python -m experiments.conpty.source_margin_run --baseline local/source-baseline --patched local/source-patched --probe local/source-margin-build/bin/Release/conpty_source_margin_probe.exe --candidate-trace experiments/candidates/alacritty/target/release/termforge-alacritty-trace.exe --output local/source-margin-new
python -m experiments.conpty.source_backend_check --baseline local/source-baseline --patched local/source-patched --official local/official-conpty-1.25 --probe-directory local/source-margin-build/bin/Release --output local/source-backend-new
```

无备用屏、重复 h/l、默认 margin 的后续 12 次[原生补充对照](conpty-source-margin-controls.md)已单独记录，不更改或扩充本批 175B 结果。非零光标和显式样式、历史隔离、resize、查询及 API 写入仍待真实交叉；之后还需实际产品联合源码、支持平台、资源与维护/升级验证，并由 #34 提出正式方案供用户确认。

独立的 [#97 candidate-scroll 退出超时](https://github.com/JavaZeroo/termforge/issues/97)发生于 parent `5c073e4` 的 candidate/system push CI：正确末帧已经产生，但原 20 秒期限未自然结束。它不是本 175B 输入，同 HEAD 的其他 CI 成功不能覆盖该失败；此处的源构建修补也不构成其因果修复。原官方 80 列 5 秒失败、Vim #52、完整 R2、WindTerm 行为等价、性能与实际呈现均没有由本实验验收。
