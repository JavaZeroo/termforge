# 工作区恢复的独立输出判定

`ui-workspace-restore` 的 stage 1 必须同时观察到 cmd 当前目录、PowerShell 当前目录、PowerShell 主版本及底部 cmd 标记，再观察各自返回固定 `TF>` 提示符。这里验证固定测试命令实际完成；它不证明标记最终仍独占屏幕某行，也不借用被测终端核心的解析结果。

## 已复现的观测错误

旧判定对完整 transport 使用 `rfind(marker)`。一份真实失败中，cmd 已独立输出 `TF_CMD_DIR=restore-cmd 目录\r\n`，后续完整 OSC 0 标题又包含相同标记。最后一次匹配后的字节是 BEL，导致谓词永远为假，尽管后面已有新 `TF>`。其余三个谓词均为真。原始失败文件 SHA-256 为 `af1a2c513b3891ce75acccca6ec2125ab46ab2fc2b312d0a05fd8cd40fa425bc`，保留原件，不回填结果；独立 C++ helper 重放其原始字节时四个谓词均成立。

仅剥离控制序列并要求 LF 仍不充分：新的真实默认构建运行中，cmd 的完整标记后直接出现 `ESC [ 6 ; 1 H`、显示光标序列和 `TF>`，没有运输层 CRLF。这份新失败（SHA-256 `339ca19fc01a0a188325fd6b4b594538cfdf6535e9b1e17ca398b6fa4472c8a5`）同样保留；最小反例先失败，再增加受限的 column-one CUP 记录边界。它不假设起始游标行号，也不声称 CUP 移到了新行。

[#61](https://github.com/JavaZeroo/termforge/issues/61) 的两份默认/system 新失败具有不同原因：PowerShell 命令编辑重绘在实际输出之前带一个裸 BS（`0x08`）。旧 oracle 因它直接拒绝整个运输，即使随后已有真正 CRLF、完整目录/版本输出和新提示符。用原字节独立重放，两个 PowerShell 条件均为 false，而 cmd/底部条件为 true；仅移除前缀 BS 的诊断输入使旧条件转为 true，证实拒绝分支。修正不模拟退格或删除回显，而是把该前缀记录标记为不可信，直到真正 LF 才允许后续独立输出；[限定复验](../../experiments/shell/results/2026-10-26-workspace-restore-bs/validation.json)保留原始失败及各配置身份。这是测试完成端点修正，不是产品输出丢失或恢复功能缺陷的修复。

## 限定 oracle 合同

- 独立扫描 7-bit ESC 形式的完整 OSC（BEL 或 ST 终止）和 CSI。OSC 内容不参与正文匹配，UTF-8 的所有高位正文 bytes 原样保留，包括 `目` 中的 `0x9b`。
- 标记必须匹配完整正文记录，前后不可附加命令、其它正文或任意子串。LF 结束记录；显式正行号、第 1 列 CUP/HVP 仅可结束完整标记或提示符，不能清除命令前缀或拼接分散的标记片段。其它非空内容遇 CUP 保持未知，直到真实 LF；SGR 和其它 CSI 不会凭空结束标记。
- 在首次匹配输出之前，命令编辑中的 BS 只会污染当前记录；CUP 不能解除该污染，也不把退格两侧文字拼起来。真实 LF 后可验证新的完整记录。已经匹配输出后的 BS 仍拒绝，不能借编辑过的提示符证明命令完成。
- CRLF 的 CR 不改变正文。裸 CR 仅在已观察到记录边界、当前记录为空且未受 BS 污染、尚未匹配输出时允许经过；它不清除文字、不结束记录，也不替代之后的完整标记和新提示符。初始裸 CR、非空或受编辑污染记录中的裸 CR、已匹配输出后的裸 CR，以及其它裸控制字符仍拒绝。
- 该记录之后必须出现独立的 `TF>` 或 `TF> ` 记录，才证明 shell 返回；输入命令回显、标题内标记、旧提示符和输出行前缀都不满足条件。
- 完整 CSI 仅按语法封装剥离，不解释或验收其执行语义；其中受限 column-one CUP 的记录边界另按上条处理。不完整或格式错误的封装、未知 ESC 引导形式、DCS 等其它控制字符串保持未证明状态。这只覆盖固定持久化命令已输出和返回提示符的运输证据，不扩展为第二个通用 VT 核心；产品屏幕及恢复断言仍独立保留。
- 保留原有 8192-byte 观测上限、30 秒总期限、真实目录/版本、返回提示符、恢复成员/标题/缩放/分屏/几何及尺寸检查和真实 Job/desktop 回收断言。

`transport_completion_test` 独立覆盖标题重复遮蔽、BEL/ST、SGR、CUP、UTF-8、命令编辑前缀 BS、空记录裸 CR，以及命令回显、CR/BS 拼接或提示符编辑、OSC/DCS payload、不完整控制、缺失或旧提示符等反例。它修正的是测试 oracle，不构成产品恢复故障修复或完整 R2 验收。

```powershell
ctest --test-dir build/default -C Release -R '^transport_completion$' --output-on-failure
$env:TERMFORGE_BIN_DIR = "$PWD/build/default/bin/Release"
$env:PYTHONUTF8 = '1'
python -m unittest experiments.shell.test_shell.ShellTests.test_workspace_v2_multiple_shells_split_and_geometry_restore -v
```

候选构建使用相同测试命令和原场景两轮保存/新进程恢复，将 `TERMFORGE_BIN_DIR` 指向显式候选构建。两个构建使用私有非输入 desktop 和自有 Job；无须接触用户剪贴板或输入法。

此前固定源码的[公开复核摘要](../../experiments/shell/results/2026-10-26-workspace-transport-oracle/validation.json)保留 OSC/CUP 原失败、当时默认/候选的两轮保存/恢复/钳制结果，不回写为 #61 通过。新 BS 反例、默认/候选各配置的实际结果以 [#61 复验身份](../../experiments/shell/results/2026-10-26-workspace-restore-bs/validation.json)为准。原始运输数据保留本地；局部方法通过不代替合并后 CI 或完整候选回归。
