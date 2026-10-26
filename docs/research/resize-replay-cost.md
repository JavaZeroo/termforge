# 八会话的 resize 重放成本与关闭回复

接续 [UI sender fence](ui-paste-fence.md) 的 [#53](https://github.com/JavaZeroo/termforge/issues/53)。候选 `.4` × 显式官方 ConPTY `1.25.260930003`、flags24 在源码 `eae4b6a4ef3e044ebfebbbbfeccf9aec74cd7f0d` 上通过两轮原八会话 **data-only** 合同。修复保留原 3MB 汉字、768B checkpoint、16KiB sender 门槛、后台输出量、UI70秒和 fixture55秒期限。[固定身份与最终回归](../../experiments/shell/results/2026-10-26-replay-cost-and-close/validation.json)与[全部诊断样本](../../experiments/shell/results/2026-10-26-replay-cost-and-close/)分列，不把真实剪贴板/IME、其他构建或整个 R2 算作通过。

## 成本定位与有限优化

旧源 `8ff112e` 的两次真实八会话插桩仍在 stage45 失败，Job0、desktop 已回收。第一次把约64秒定位到 parser feed；第二次进一步分开内部阶段：parser feed 63.987秒、Rust feed 0.560秒、synchronize 63.420秒，其中 cell export 2.031秒、reconcile_replay 60.905秒。计数是主线程累计的嵌套 wall time，不能相加，也不是实际呈现或输入延迟。两次均关闭资源采样，仅作诊断；[完整计时、源码/二进制身份与原始记录哈希](../../experiments/shell/results/2026-10-26-replay-cost-and-close/ui-profiles.json)保留。

普通无 resize 的 headless F1 输入未复现高成本：[2,048,000B 的六样本](../../experiments/shell/results/2026-10-26-replay-cost-and-close/headless-ordinary.json)均满足逐行文字、光标和历史预期，未插桩三次 wall 为0.446–0.486秒。缩小到204,800B后，只在计时前加入一次同尺寸 `resize(17,74)`，未插桩两次从无 resize 的0.046/0.044秒变成0.929/0.969秒；六次最终 screen JSON 哈希全部相同。[全部样本与冻结输入](../../experiments/shell/results/2026-10-26-replay-cost-and-close/headless-resize.json)、[诊断驱动及计时边界](../../experiments/shell/results/2026-10-26-replay-cost-and-close/probe/README.md)可审查。

观察与源码共同支持一个有限原因：resize 保留了重放起始屏幕，后续每次同步的 `reconcile_replay` 会反复序列化同一行，查找 origin 时还会再次生成行键。`06bf6b5` 让同次同步生成的 expected/actual 键向量保留空行位置，并直接按索引复用；计数仍跳过空键。键内容、重复行处理、匹配顺序、行 ID/version 映射、重放生命期和历史策略保持原合同。缓存只在一次调用内存在。

这些样本来自共享 Windows 11 build26200 宿主，8逻辑核，MSVC19.42 Release x64；headless 只输入 producer 字节，没有 ConPTY 变换或 UI。短样本与插桩均有扰动，不据此计算产品提速比例，也不替代 [#54](https://github.com/JavaZeroo/termforge/issues/54) 的预冻结 `.3/.4` 正式 A/B、用户性能预算或 WindTerm 对标。

## 关闭期间仍发送回复的独立错误

只完成键复用后的原八会话在27.467秒完成后台消费，35.974秒完成粘贴关闭和后续输入，但退出时因 `terminal reply rejected by input queue` 返回2。额外诊断的两轮分别成功与失败；失败现场为 `closing=1, stopping=1, queued=0, failed=0, paste_busy=0`，回复是 `ESC[4;4R`。这些成功/失败均保留在[中间运行记录](../../experiments/shell/results/2026-10-26-replay-cost-and-close/intermediate-workflows.json)，一次成功不消除退出错误。

原因是关闭已停止输入队列，剩余输出中的光标位置查询仍经 `route_terminal_events` 发送回复。新增真实 ConPTY 回归先将查询留在 OutputQueue，明确要求尚未消费输出，再关闭并确认输入停止，最后才泵窗口消息。旧代码0.19秒即因输入拒绝失败；`eae4b6a` 仅在 `closing_` 时不再发送回复，两次新进程均实际观察查询、消费65B、解析标记文字、零输入拒绝并完成回收。输出仍解析、排空和发布；正常会话的回复失败仍严格报错。

[红绿身份](../../experiments/shell/results/2026-10-26-replay-cost-and-close/closing-reply.json)明确记录夹具变化：绿色版本增加了已解析标记与消费字节断言，未放宽旧条件。测试只注册于候选＋显式后端组合，不能据此声明默认/system 配置已验收。

## 最终结果与剩余边界

最终源码两轮八会话包含资源采样，后台完成时间分别为26.268、27.312秒，粘贴关闭后输入验证分别在34.675、35.808秒完成。每轮26个检查点、五轮6↔8会话、拒绝第9个会话、精确接收字节和18个会话关闭均通过，Job0、desktop已删除。两轮 cancel/close 的动作前均为 sent17391 / accepted18414 / held1023；取消接收18414B，关闭恰768B，in_flight0、retained15。close 的实际唤醒为 cancelled，最终状态为 stopped，二者分列。

同源码完整 CTest 为54/56，465.08秒：`candidate_shell_tui` 的六项中 Vim stage59 失败，其余五项通过；`shell` 聚合触及原180秒期限，已记录 resize/resize-close43、main第三轮7、ui第三轮52。聚合中单窗口粘贴两轮和 EOF/无GO反例均已完成并通过；负例的产品运行仍返回2、接收恰768B、Job0/desktopgone，未因清理转绿。未结束的 shell 项不能算通过。

内部 skip 也保留：可选 Rust 对比/real-app 工具未构建、托管实际 CF/IME 保护、原版失败注入 opt-in 及私有站点 OpenClipboard 权限失败。CTest 条目通过不表示其中被跳过的场景通过。#51/#52/#63、真实系统剪贴板/IME、实际呈现、长期资源、正式性能及产品核心/后端选型仍需各自完成。自动化使用自有非输入桌面，不要求操作用户窗口。

```powershell
cmake --build build/candidate --config Release
ctest --test-dir build/candidate -C Release --output-on-failure
```

复验前按贡献指南显式选择 `.4` 与固定官方后端，准备固定 Vim 的测试进程 PATH；结果同时记录二进制/依赖哈希。单独复验关闭查询可用 `ctest --test-dir build/candidate -C Release -R '^candidate_reply_close$' --output-on-failure`。完整回归的失败不会因局部修复通过而被忽略。
