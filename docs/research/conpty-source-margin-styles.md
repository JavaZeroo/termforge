# 源码 ConPTY：样式、光标恢复与空白格差异

固定同源 margin 补丁在本次五阶段原生检查中全部通过：备用屏退出后，主屏光标和红字蓝底属性恢复，后续滚动仍使用主屏 `2;3r`。未修补版前四阶段正确，最后一阶段出现预先固定的旧区域错误。不过，两版实际 transport 的候选末端都有滚动空白格的前景色元数据差异，**完整末端属性比较仍失败**。文字、背景色、光标和当前绘制属性通过，不代表完整合同通过。

这是 [#56](https://github.com/JavaZeroo/termforge/issues/56) 在[三个原生 controls](conpty-source-margin-controls.md)之后的独立实验。WindTerm 2.7.0 对该场景的实际行为尚未观测；本记录不选择产品后端，不批准候选核心策略，也不关闭 #56/#41。

## 运行前固定的场景

沿用[原 175B 实验](conpty-source-margin-runtime.md)已核验的 Terminal `dc8ae365847d41f62d1ebca93815bb00959bc12f` baseline/patched、构建 run `40449292015` 及双文件/补丁/summary receipt；上游源码与补丁未修改。独立 `--styled-cursor` 入口只执行一个 211B 场景，在 `80×24`、`12×7` 下分别运行两版，每格一次，共四次，无重试。原 175B 与默认三个 controls 保持原入口。

| 阶段 | 固定操作与原生判据 |
|---|---|
| before | 写入区域内外标记，主屏 `2;3r`，SGR `31;44`，光标第 3 行第 6 列 |
| alt | 进入备用屏，在 `4;5r` 内以 SGR `32;45` 滚动；光标第 5 行第 8 列 |
| restored | 离开备用屏，原主屏内容及第 3 行第 6 列光标恢复 |
| after-write | 直接写 `M`，原生第 3 行第 6 格为红字蓝底，光标前进一格 |
| final | 回到主屏区域滚动，保留区域外内容；`REGION_C`、`REGION_D` 及其尾部空白为红字蓝底；第 6 行写 `SGR_DONE` |

全文、完整 `CHAR_INFO` 字符/属性、光标和 viewport 分别使用独立字面值检查。原生红字蓝底为 `0x14`、绿字紫底为 `0x52`；末端 trace 的 ANSI 索引另用 `fg=1/bg=4`。baseline 必须匹配具体旧状态，异常或任意失败不计为有效反例。

## 实际结果与判据修正

| 观测 | 两个尺寸的 baseline | 两个尺寸的 patched |
|---|---|---|
| 原生前四阶段 | 全字段通过 | 全字段通过 |
| 原生 final | 文本/格属性失败，符合旧区域预测：保留 `REGIOM_B` 并出现 `REGION_CIXED`；光标/viewport 通过 | 全字段通过 |
| 实际 transport 末端 final | 文字、主屏、光标及 current_style 通过；完整单元格比较失败 | 相同 |
| 生命周期 | 自然退出 37、EOF 109、flags=0、outer Job active0/total4/terminated0，无 force | 相同 |

所有样本仍使用 5 秒 child wait、15 秒外期限、共享 5 秒 cleanup。四份源字节均为 211B / SHA `51d0f55717ff8fbe5e82040967edf2f9399a9b893fd8e8cb282f94c143919b62`；实际运输均为 254B / SHA `b584cd146f447c1e78e51d74e6d1c38c6a93d860d738a0afacdf959c8327d9bd`。未执行 GUI 产品流程或呈现测量。

首次判据把未触碰的默认空格错误地写为 `text=" " / width=1`，而已有 [trace 编码](../../experiments/candidates/alacritty/src/main.rs)对默认空单元使用 `text="" / width=0`，有背景色的空白才保留空格与宽度 1。离线审计发现这一测试错误，新增反例先失败，再修正编码；原四次运行、原 summary、原退出 1 结果全部保留，没有重跑原生程序。

修正后重新读取同四份捕获，80 列每次排除 1731 格、12 列每次排除 31 格的错误序列化预期；**显式前景/背景的预期没有放宽**。剩余差异只在零基第 1、2 行、第 8 列至行尾：

- 80 列每次 144 格，12 列每次 8 格。
- 文字均为一个空格、width=1、背景色均为 4、bold=false。
- 期望显式前景色 1，实际为默认前景色 -1。写出的彩色文字及 current_style 仍为 1/4。

原始与修正后的完整比较都为失败。不能将默认空单元的判据错误算为产品缺陷，也不能将剩余空白前景色差异直接认定为用户可见错误。

固定 Alacritty 上游 `542147067dd016fc5f49120ef0ba605a98a4e3a7` 的 [`Cell::reset`](https://github.com/alacritty/alacritty/blob/542147067dd016fc5f49120ef0ba605a98a4e3a7/alacritty_terminal/src/term/cell.rs)只保留 template 的背景色，其余字段取默认值；当前 `.5` 候选保持该逻辑，滚动清行调用这条路径。这解释了所捕获的候选元数据结果。该源码观察不证明 WindTerm 的行为，也不构成改变产品合同或修改候选核心的批准。

## 身份、验证与复验

[脱敏清单](../../experiments/conpty/results/2026-10-27-source-margin-styles/validation.json)保留原运行身份、原始 summary 哈希、四份捕获身份、离线修正来源及两种失败结论。

- 实际原生运行源码 `485c3e253e88a2d83f72326c0ff9185b94d87269`，当时工作树干净；离线判据修正源码 `6e52e6957de10c14c570d3b7b83992c585fcc3a9`，不冒充重新运行。
- Windows 11 x64 build 26200；VS17 / MSVC 19.42.34433 / SDK 26100，Release x64。独立探针 SHA `ca19645b65aea8ac74b56d1e59837cb7514c3acf5932d24112411725b92206d7`；复用前批 `.5` trace SHA `5941d4569d0ae146eb005f12ecfb15014bc06c18ee48d1c9460824ab65df93d3`。上游 pair 是 hosted VS18 构建，两者身份分列。
- 初始纯判据 7/7；空单元编码反例 1 次失败后，修正判据 7/7、0.069 秒，包含错误前景/背景、光标、current_style、文字、宽度和主屏状态的拒绝。时长仅为测试诊断。
- 前批 12 份 controls 捕获离线复查通过，未增加原生执行。此次完整 styled 矩阵仍失败，不能用纯测试成功替代。
- 私有原件位于 `local/r56-style-controls/matrix-1`；公开清单只有合成输入/输出、结果、来源和哈希，不含二进制、用户状态或绝对路径。

按[前批构建与来源说明](conpty-source-margin-controls.md#复验与剩余范围)生成 controls 探针和候选 trace，并核验固定 baseline/patched；使用新输出目录：

```powershell
python -m experiments.conpty.source_margin_controls --styled-cursor --baseline local/source-baseline --patched local/source-patched --probe local/source-controls-build/bin/Release/conpty_source_margin_controls.exe --candidate-trace experiments/candidates/alacritty/target/release/termforge-alacritty-trace.exe --output local/source-styles-new
python -m unittest experiments.conpty.test_margin_controls -v
```

保留失败判据，当前同配置预计矩阵退出 1；不得通过重试取绿。其他 SGR/真彩色、终端中间阶段、历史隔离/压力、resize、查询/API、完整产品、性能、升级维护及 WindTerm 对照仍待完成。`TERMFORGE_BUILD_SOURCE_CONPTY_EXPERIMENT` 默认 OFF，正式方案仍由 #34 按证据提案及用户确认决定。
