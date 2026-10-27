# 源码 ConPTY：主屏与重复切屏的原生对照

同源 margin 补丁在三个补充场景中符合预先固定的结果：主屏单独滚动保持正确；新建或再次进入备用屏使用默认全屏滚动区域；返回主屏和重复离开备用屏后恢复原来的 `2;3r`。未修补版在后两个场景出现具体预期错误，修补版通过全部原生阶段。实际运输的最终主屏在两版中均正确，不能用末端正确掩盖原生错误。

这是 [#56](https://github.com/JavaZeroo/termforge/issues/56) 的独立补充实验。预期依据是既有 R2 每缓冲区 margin 合同和固定上游状态路径，运行前已记录在 issue；WindTerm 2.7.0 对这些场景的实际行为仍未知。本结果不验收 WindTerm 等价、不选择产品后端，也不关闭 #56/#41。

## 固定输入与观测边界

沿用[原 175B 对照](conpty-source-margin-runtime.md)的同源 baseline/patched：Terminal `dc8ae365847d41f62d1ebca93815bb00959bc12f`、构建 run `40449292015` 和已审核的双文件 SHA、variant/summary/patch receipt。本次未修改上游补丁或重新构建上游。

三个场景各在 `80×24`、`12×7` 下运行 baseline/patched 一次，共 12 次，只开 VT；无自动重试。输入由独立 controls 探针发送，原 175B、四阶段 probe/oracle 和既有期限不变。新场景使用 `CTRL_DONE`，字节数与原负载分列。

| 场景 | 80×24 / 12×7 字节数 | 运行前固定的原生预期 | 未修补版实际结果 | 修补版实际结果 |
|---|---:|---|---|---|
| `main-only` | 99 / 99 | 无备用屏；主屏 `2;3r` 滚动，区域外不变 | before/final 均正确 | before/final 均正确 |
| `default-alt` | 150 / 149 | 主屏区域不传给新备用屏；物理底部换行应全屏滚动；返回后仍为主屏区域 | alt 未全屏滚动：保留 `ALT_TOP`，`ALT_LAST` 被覆盖；其余阶段正确 | before/alt/restored/final 全部正确 |
| `repeat-alt` | 221 / 220 | alt1 的 `4;5r` 不传给再次创建的 alt2；两次离开后主屏仍为 `2;3r` | alt2 同样未全屏滚动；final 出现 `REGION_CIXED`；其余阶段正确 | before/alt1/alt2/restored/final 全部正确 |

每个阶段分别检查 `ReadConsoleOutputCharacterW` 文本、完整 `CHAR_INFO` 字符/属性、光标及 viewport top。预期为字面值，不使用候选 parser 生成；baseline 必须符合上述具体旧状态，异常、生命周期失败或其他文字错误均不计为有效反例。没有设置 SGR，属性检查只证明初始属性保持。

12 次都自然退出 37、读端 EOF 109，creation flags=0，内层子进程清零，外层 Job 为 active0/total4/terminated0，无强制取消。每次仍为 5 秒 child wait、15 秒外期限、共享 5 秒 cleanup；离线候选回放另由既有 trace 入口管理。两版实际运输的最终文字和 alternate=false 均符合预期；没有观测末端逐阶段光标、属性或实际呈现。

## 源码、构建与验证身份

- 本地实际运行源码 `edec26bb3c4d04858a667a96debf69c139476172`，工作树干净；参与文件 SHA、探针、trace、输入、运输、每阶段捕获及监督记录 SHA 见[脱敏清单](../../experiments/conpty/results/2026-10-27-source-margin-controls/validation.json)。
- Windows 11 x64 build 26200；VS17 / MSVC 19.42.34433 / SDK 26100，Release x64。controls 探针 SHA `a71c94795806a40062ace7788ee69cfbac0cd8a274765dce6b28aa6d7f33c7ee`；`.5` trace SHA `5941d4569d0ae146eb005f12ecfb15014bc06c18ee48d1c9460824ab65df93d3`。
- 新旧 margin 判据与 GO/回收回归 17/17，通过耗时 0.375 秒；原 loader/API/清理回归 14/14、零跳过，1.020 秒。后者复用前一批相同源码的原探针 EXE，运行本次修改后的 Python supervisor；不冒充重新编译原探针。两个实际 stdout 日志均保存在本地并记录 SHA，时长仅为测试诊断。
- `TERMFORGE_BUILD_SOURCE_CONPTY_EXPERIMENT` 仍默认 OFF；新增 `conpty_source_margin_controls` 独立目标。仅 supervisor 增加可选参数和期限，既有 API 调用默认仍为 20 秒。纯 controls 判据纳入 Windows/Linux CTest，不依赖 native DLL；原生矩阵需显式运行。

私有原件位于 `local/r56-margin-controls/matrix-1`；公开清单仅含合成场景结果、必要身份和文件哈希，不包含绝对路径、原生二进制、原始运输或用户状态。

## 复验与剩余范围

先按[源码构建说明](../../experiments/conpty/upstream/README.md)取得并审核固定 run 的 baseline/patched。使用全新的输出目录：

```powershell
cmake -S . -B local/source-controls-build -A x64 -DTERMFORGE_BUILD_SOURCE_CONPTY_EXPERIMENT=ON -DTERMFORGE_BUILD_CANDIDATE_ADAPTER=OFF -DTERMFORGE_CONPTY_BACKEND_DIR= -DBUILD_TESTING=OFF
cmake --build local/source-controls-build --config Release --target conpty_source_margin_controls --parallel 2
cargo +1.92.0 build --manifest-path experiments/candidates/alacritty/Cargo.toml --locked --release --bin termforge-alacritty-trace
python -m experiments.conpty.source_margin_controls --baseline local/source-baseline --patched local/source-patched --probe local/source-controls-build/bin/Release/conpty_source_margin_controls.exe --candidate-trace experiments/candidates/alacritty/target/release/termforge-alacritty-trace.exe --output local/source-controls-new
python -m unittest experiments.conpty.test_margin_controls -v
```

本次只补无备用屏、默认备用屏区域、重复 h/l 的真实交叉。显式样式与非零光标的后续[独立对照](conpty-source-margin-styles.md)记录原生通过及末端空白前景色差异，不回写本批结果。历史隔离/压力、resize、查询/API 写入、完整产品工作流、性能及维护升级仍未验证；原 175B 的 VT-off 证据属于前一批，不能合并成同批更大矩阵。原系统/官方失败、#52、#97 和 #99 的独立失败继续保留。正式方案仍由 #34 提交证据及取舍，经用户确认后生效。
