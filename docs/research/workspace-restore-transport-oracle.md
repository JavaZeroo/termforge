# 工作区恢复的独立输出判定

`ui-workspace-restore` 的 stage 1 必须同时观察到 cmd 当前目录、PowerShell 当前目录、PowerShell 主版本及底部 cmd 标记，再观察各自返回固定 `TF>` 提示符。这里验证固定测试命令实际完成；它不证明标记最终仍独占屏幕某行，也不借用被测终端核心的解析结果。

## 已复现的观测错误

旧判定对完整 transport 使用 `rfind(marker)`。一份真实失败中，cmd 已独立输出 `TF_CMD_DIR=restore-cmd 目录\r\n`，后续完整 OSC 0 标题又包含相同标记。最后一次匹配后的字节是 BEL，导致谓词永远为假，尽管后面已有新 `TF>`。其余三个谓词均为真。原始失败文件 SHA-256 为 `af1a2c513b3891ce75acccca6ec2125ab46ab2fc2b312d0a05fd8cd40fa425bc`，保留原件，不回填结果；独立 C++ helper 重放其原始字节时四个谓词均成立。

仅剥离控制序列并要求 LF 仍不充分：新的真实默认构建运行中，cmd 的完整标记后直接出现 `ESC [ 6 ; 1 H`、显示光标序列和 `TF>`，没有运输层 CRLF。这份新失败（SHA-256 `339ca19fc01a0a188325fd6b4b594538cfdf6535e9b1e17ca398b6fa4472c8a5`）同样保留；最小反例先失败，再增加受限的 column-one CUP 记录边界。它不假设起始游标行号，也不声称 CUP 移到了新行。

## 限定 oracle 合同

- 独立扫描 7-bit ESC 形式的完整 OSC（BEL 或 ST 终止）和 CSI。OSC 内容不参与正文匹配，UTF-8 的所有高位正文 bytes 原样保留，包括 `目` 中的 `0x9b`。
- 标记必须匹配完整正文记录，前后不可附加命令、其它正文或任意子串。LF 结束记录；显式正行号、第 1 列 CUP/HVP 仅可结束完整标记或提示符，不能清除命令前缀或拼接分散的标记片段。其它非空内容遇 CUP 保持未知，直到真实 LF；SGR 和其它 CSI 不会凭空结束标记。
- 该记录之后必须出现独立的 `TF>` 或 `TF> ` 记录，才证明 shell 返回；输入命令回显、标题内标记、旧提示符和输出行前缀都不满足条件。
- 不完整、未知或格式错误的控制序列保持未证明状态，包括 DCS；不通过忽略未知字节来获得成功。这只覆盖固定持久化命令的运输输出，不扩展为第二个通用 VT 核心。
- 保留原有 8192-byte 观测上限、30 秒总期限、真实目录/版本、返回提示符、恢复成员/标题/缩放/分屏/几何及尺寸检查和真实 Job/desktop 回收断言。

`transport_completion_test` 独立覆盖标题重复遮蔽、BEL/ST、SGR、CUP、UTF-8 以及命令回显、OSC/DCS payload、不完整控制、缺失或旧提示符等反例。它修正的是测试 oracle，不构成产品恢复故障修复或完整 R2 验收。

```powershell
ctest --test-dir build/default -C Release -R '^transport_completion$' --output-on-failure
$env:TERMFORGE_BIN_DIR = "$PWD/build/default/bin/Release"
$env:PYTHONUTF8 = '1'
python -m unittest experiments.shell.test_shell.ShellTests.test_workspace_v2_multiple_shells_split_and_geometry_restore -v
```

候选构建使用相同测试命令和原场景两轮保存/新进程恢复，将 `TERMFORGE_BIN_DIR` 指向显式候选构建。两个构建使用私有非输入 desktop 和自有 Job；无须接触用户剪贴板或输入法。

本地最终验证：默认与候选 Release 构建均通过独立 helper 回归和上述原测试方法；每种构建完成两轮保存、两轮新进程恢复和两轮超宽窗口裁剪恢复。原始两份失败字节分别经 C++ helper 重放，四个条件均成立。[公开复核摘要](../../experiments/shell/results/2026-10-26-workspace-transport-oracle/validation.json)记录源文件及输入身份、旧谓词、反例和 fresh 样本哈希；原始运输数据保留本地。此处不代替合并后 CI 或完整候选回归。
