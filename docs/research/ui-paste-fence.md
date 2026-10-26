# 未完成 UI 粘贴的确定性取消与关闭

对应 [#53](https://github.com/JavaZeroo/termforge/issues/53)，属于 [#50](https://github.com/JavaZeroo/termforge/pull/50) 的实验后端整合。实现源 `a15eb7f67315fb53c9d66b9c5a4424e13511a311` 的单窗口两轮取消/关闭及控制故障检查通过，**完整八会话仍失败，#53 保持开放**。[固定源码、二进制、原始记录哈希与逐字段结果](../../experiments/shell/results/2026-10-26-ui-paste-fence/validation.json)分列原 red、针对性验证与整体验证，不能互相替代。

## 原失败与修改

在 `.4` 候选与官方 ConPTY `1.25.260930003`、Console width flags24 的联合构建上，原场景两次被正确拒绝：一次到动作前已 `sent=accepted=total=3000000`，另一次在 receiver gate 前就 completed。这说明原夹具不能稳定观察未完成期，不能据此认定无栅栏的产品取消失效。

`ShellWindow` 现在允许自有测试预配置单次 generation 与绝对期限。真实 `PasteTransfer` 创建后、`InputQueue::begin_paste` 唤醒 writer 前完成 fence 绑定；普通产品请求仍默认关闭。writer 沿原取块、实际发送、写确认和释放路径工作，达到原 16KiB 门槛后只持住一个尚未提交的真实块。

先只增加栅栏、保持原 UI 直接 `transfer.cancel()` 时，场景在原 35 秒期限失败；改为真实 Escape 调用 `input_.cancel_paste()` 后，同一测试两轮通过。该入口通知等待中的 writer，允许已领取块完成真实发送；取消前没有成功 GO。关闭仍走既有 cancel、stop、Session 回收路径。

原 100 万个汉字、3,000,000B、768B 接收检查点和 flood 不变。只有同时观察到 held、实际文件 checkpoint、实际输出 flood 和另一会话继续响应，才执行动作。单窗口的另一会话在 held 后执行独立 echo；八会话检查另一后台会话在 held 后继续消费输出。两者的响应证据不同，不称为同一种输入延迟测量。

## 控制协议和证据

测试控制端通过真实匿名 Win32 管道发送 generation/GO、读取应答；两个端点在同一进程，不是外部监督进程断连验证。UI 正例先验证错误 generation 被拒绝且仍 held，随后执行真实 Escape 或关闭。纯协议测试另覆盖未 held、匹配 GO、重复 GO、八条请求/应答积压及第九条拒绝。

EOF 负例实际关闭写端，并由 `ReadFile` 确认 `ERROR_BROKEN_PIPE`；无 GO 负例使用单独的 2 秒故障期限。两种真实 UI 场景均保持失败返回 2，writer 释放 payload，receiver 仍恰为 768B；Job0 与 desktop 回收不会转化为成功粘贴。正例原期限仍为 UI 35 秒、外层 40 秒、receiver gate 15 秒；八会话仍为 UI 70 秒和 fixture 55 秒。

`CreatePipe` 的请求大小只是建议，因此构造时通过 `GetNamedPipeInfo` 检查实际 incoming server buffer 足以容纳整个 128B 请求／192B 应答预算，不能直接把 4096 请求值当作实测容量。见 Microsoft 的 [CreatePipe](https://learn.microsoft.com/en-us/windows/win32/api/namedpipeapi/nf-namedpipeapi-createpipe) 与 [GetNamedPipeInfo](https://learn.microsoft.com/en-us/windows/win32/api/namedpipeapi/nf-namedpipeapi-getnamedpipeinfo) 合同。

关闭可能先由 cancel 唤醒，再由 stop 改变最终状态。现在在 CV 返回时固定实际退出原因，并分别记录 `exit` 与 `final_state`；最新两轮 close 为 `exit=cancelled / final_state=stopped`。旧 `62226af` 记录的 `exit=stopped` 只是最终状态，不作为实际唤醒原因证据。

| 动作 | 动作前 sent / accepted / held bytes | 动作后与独立 receiver |
|---|---|---|
| Escape，两轮 | 17391 / 18414 / 1023 | cancelled；sent=accepted=received=18414；in_flight0、retained15 |
| 关闭，两轮 | 17391 / 18414 / 1023 | receiver 恰为768；in_flight0、retained15；已提交块仍按真实计数记录 |

## 未完成的组合出口

两次带采样八会话都完成了上述两项粘贴动作，但后台整体消费完成分别约为 51.158、52.546 秒；close 前分别约为 61.806、63.096 秒。前台 B fixture 已按原 55 秒期限退出，最后一个 `!` 无法收到，UI 在 stage45 期限失败。原始状态、字节和回收结果均保留。

同一最终代码关闭资源采样的诊断仍失败：后台完成 50.262 秒、close 前 60.948 秒。因此不能把采样器认定为这一失败的唯一原因；该单变量运行也不替代带采样组合验收。候选适配、消息循环和宿主负载成本仍需分别定位，不延长原期限或减少后台负载。

`62226af` 的完整 CTest 为52/55：Vim 不在当时 PATH、八会话 stage45、shell 聚合180秒期限分别保留；shell 中另有 resize43、main7、ui52 的失败，不归因于粘贴修复。最终唤醒原因与容量修正后，3/3 针对性 CTest、两次完整单窗口正例及 EOF/无 GO 负例通过。随后仅在测试进程 PATH 补齐固定 Vim9.1，原入口再次在 stage59 失败，resize 命令进入插入文本；这与环境准备失败分列，#52 继续开放。其他配置、托管系统剪贴板/IME、呈现与 WindTerm 性能等价均未由这些结果验收。

## 复验

按贡献指南准备依赖，显式选择候选及固定官方后端后：

```powershell
cmake --build build/candidate --config Release --target termforge_shell shell_test_launcher sender_fence_test sender_fence_pipe_test
ctest --test-dir build/candidate -C Release -R '^(sender_fence|sender_fence_pipe|paste_fence_evidence)$' --output-on-failure
$env:TERMFORGE_BIN_DIR = (Resolve-Path build/candidate/bin/Release).Path
python -m unittest experiments.shell.test_shell.ShellTests.test_paste_confirmation_and_hidden_target_preserve_visible_focus experiments.shell.test_shell.ShellTests.test_paste_control_eof_remains_failure_after_cleanup experiments.shell.test_shell.ShellTests.test_paste_missing_go_remains_failure_after_cleanup -v
python -m unittest experiments.shell.test_workflow_sessions -v
```

最后一项保留真实失败出口。测试使用自有非输入桌面，不要求操作用户窗口；完整 R2 与产品后端选择仍由各自合同约束。
