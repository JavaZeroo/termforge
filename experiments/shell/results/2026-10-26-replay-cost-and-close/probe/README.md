# 成本诊断的固定计时边界

本目录归档 `8ff112e785073dde91a74142cf1a3000fa240007` 上实际使用的诊断源与插桩差异，不参与产品或 CI 构建。原始 EXE、屏幕、运输记录留在本地；脱敏后的全部样本、原始文件哈希及构建身份见上级 JSON。

- `ordinary_probe.cpp` 和 `resize_probe.cpp` 是实际 headless 驱动，分别消费 2,048,000B 和 204,800B；最终状态按独立的逐行字面值、历史长度、淘汰数及光标验证。
- 合成输入每行由 `f'TF_R108_F1_{row:05d} 中文 e\u0301'.encode('utf-8').ljust(62, b'.') + b'\r\n'` 生成；分别连接 32,000 / 3,200 行。其字节哈希、17×74、history100、chunk8192 与完整样本次序已记录。
- headless 构建将驱动与固定源码的 `experiments/core/candidate_terminal.cpp`（或应用 `headless-instrumentation.patch` 的副本）链接到同一固定 `.4` Rust Release 库；MSVC `/W4 /utf-8`、C++17，链接 `ws2_32 userenv ntdll`。构造和可选同尺寸 resize 在计时前，循环 feed 计时，finish 和正确性校验在计时后。每个样本是新的进程。
- headless phases 0–5 依次是 Rust feed、scan_replay、synchronize、event drain、cell export、reconcile 及后续同步收尾。phase5 包含后续工作，不能直接称为纯 reconcile。UI 第二次插桩才把 reconcile、history merge 和 versions/copy 分开。
- UI 第一批使用 `ui-instrumentation.patch` 的 phases0–9；第二批再使用 `ui-core-instrumentation.patch` 增加 phases10–16。`ui_timing.hpp` 为最终17槽版本；第一批仅10槽。UI 计时由主线程累计，析构写到 `TERMFORGE_DIAGNOSTIC_PHASE_OUT`；原场景仍由自有 Job/desktop 与原期限监督。
- UI patch 的 include 位置已从本机绝对路径改成仓库相对标识，移植时需令其可由 include 搜索路径解析；各 patch 已规范化换行和行末空白。这些是归档差异，不是当前分支的可直接应用补丁。JSON 中源码与 EXE 哈希对应实际运行的本地版本。

嵌套 wall time 不相加。采样很短，宿主有其他负载，插桩有扰动；这些是定位原因的诊断，不是 #54 的正式 A/B、性能预算或实际呈现验收。修复后回归使用未插桩的产品代码。
