# 选择复制夹具的原生错误边界（#85）

## 问题与来源

PR83 的 [Windows CI 40377249027](https://github.com/JavaZeroo/termforge/actions/runs/40377249027/job/121763560184) 使用默认自研核心/system，源码 `79352f2b36f9f2a8828368ced089f6e2a6fda078`、合并 checkout `939866f1cfd7ccc96fb9fdee0c6a493b6ebd3c07`。完整 CTest 45/46、265.77s；选择字节场景的第一轮真实系统复制返回 `OpenClipboard: 5`。准备五步和四 shell 已通过，不将此失败归入 #81。

原 artifact12677990045（4714691B，SHA256 `7575a5d82fec271625affe62a19a5f21edf97ae2a6bcd6b005f070070bcfa28f`）已本地核对并保留。system-0 只有 forward/reverse 两条完整进度，均为预期的合成文字；没有第三条 dpi-96 或最终结果，system-1 未运行。退出2、非超时、Job active0/total11/terminated0，桌面已删除。

**原根因未知。** 现有证据无法区分 stage6 的 sentinel 写入和 stage7 的独立读回，不能由错误5推断具体占用者或访问上下文。后续控制成功或正常复制通过均不能补写原事件原因。[#85](https://github.com/JavaZeroo/termforge/issues/85) 持续追踪；本次交付是诊断和控制验证。

## 失败后的观测合同

`scenario_selection_bytes` 只在原实际 CF 调用抛出 `ClipboardError` 后记录：固定 stage、`sentinel-write`/`readback`、`OpenClipboard`/其他剪贴板操作、原错误码，以及打开窗口与数据所有者的独立类别。类别仅为未报告、无法判断、工作区窗口、同进程其他窗口、其他进程窗口，不输出 HWND/PID、标题、路径、其他窗口文本或剪贴板内容。

记录标为 `after-error-nonatomic`。两次 Win32 查询不是同一时刻的快照，也不是失败瞬间的归因；未报告打开窗口不证明没有占用。诊断采用临时文件再替换，写入失败不替换原错误。

原错误处理还可能遇到自有 owner 清理异常。`rethrow_clipboard_failure` 保留主异常的类型和内容，清理异常只输出固定诊断；即使诊断流再次失败，仍重抛主异常。实际 owner 的 release/finish 合同不放宽：控制用例必须取得成功释放记录、线程退出和所有原生/worker错误为0，否则测试失败。

## 受控占用与个人环境保护

默认关闭的 `TERMFORGE_TEST_SELECTION_CLIPBOARD_HOLD` 只有 `sentinel-write`/`readback` 两个值，且仅在原 hosted system 场景允许。它复用已有 `ClipboardScenarioOwner(false)`，由自有线程/窗口通过事件确认真实占用，在 stage6 写或 stage7 读时产生错误。测试通过原 `execute`、真实窗口和原 CF 调用入口执行，要求保留错误5/退出2、对应阶段、同进程类别、释放记录及 Job/桌面回收。

个人环境仍由 Python 与 native 双重 guard 拒绝真实系统 CF；不伪造 hosted 标志，不修改个人剪贴板。原 queued Ctrl+Shift+C、反向选择、受控 DPI、空选区、输入隔离、九阶段与两轮、30s/40s 期限均不变。没有增加自动重试、sleep 或改变产品占用反馈。

## 验证与限制

固定源码、构建身份、原失败、纯清理反例及本地回归见[验证清单](../../experiments/shell/results/2026-10-27-selection-clipboard-boundaries/validation.json)。纯反例通过抽取原无保护清理顺序验证主错误覆盖；这是异常处理边界测试，不是对原 hosted 占用原因的复现。最终测试校验异常类型、值和文本，不以异常对象地址作为合同。

新工作树首次完整回归缺少已声明的 Python 依赖，41/46、234.70s，五个失败保留；后续使用独立 venv 安装仓库既有固定依赖。初次抽取测试曾使用异常对象地址，不能作为修复证据，已另存并排除；有效反例与最终测试使用相同类型/值合同。

固定 `0774d33b82745444e5629d14c758a09e2dd562bf` 的默认/system 本地定向检查 2/2、3.57s；完整 CTest 46/46、285.53s，13 个内部 skip 单列。选择模块 10 项中 8 项通过、2 项真实 CF 跳过。JUnit 的成功输出有截断，内部 skip 和模块细节由完整 LastTest 日志核对，不能从截断摘要猜测。

本地真实系统 CF 按保护规则跳过。真实占用控制与原两轮系统复制须在一次性 hosted VM 验证；结果由 #85 对应 PR 分列，不把本地跳过计为验收。候选/官方后端、实际呈现、WindTerm 等价及性能均未由此验收。
