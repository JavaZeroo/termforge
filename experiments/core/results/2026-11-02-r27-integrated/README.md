# #27 模式修正的统一验证

本次修正区域自动换行的续接 ID、RIS/ED3 清空历史后的结构通知、每屏 origin 保存及每次 1049 进入的 origin/六项属性复位。详见[区域与读者](../2026-11-02-r27-mode-cross/README.md)、[每屏 origin](../2026-11-02-origin-frame/README.md)、[重复进入](../2026-11-02-1049-reentry/README.md)、[六属性](../2026-11-02-1049-attributes/README.md)。原版输入/观察与核心工程测试分开，不从模拟模型推导原版未知。

最终同一组核心源码下，Release 核心 **15/15**，正式 Qt/固定 ConPTY **4/4**，离线 Python 取证/导出守卫 **88/88** 通过；后续仅修订 resize 导出器白名单，并另跑它的普通/优化模式测试。精确源码、二进制及本地日志 SHA 见 [validation.json](validation.json)。构建只用一个 worker；Qt 6.10.3、ConPTY 1.25.260930003、CRT 14.51.36247.0。核心与 Qt 分别使用自己的构建目录，不能把 Qt OFF 的核心测试计为 GUI 验收。测试秒数仅作执行记录，不是性能测量。

随后整合主线 `3ec11c4` 的 #303 极窄 resize 测试，生产源码未再改变；新增 `resize_edges_core` 单独构建并通过，内部 7/7。该增量检查与上面的 15 项分轮保存，不虚构为同一次运行。

再整合主线 `3b17e92` 的 #304 延迟换行测试，生产源码仍未改变；新增 `pending_wrap_core` 单独构建并通过 1/1。这是另一次增量检查，不改变历史报告中的测试集合。

远程 `04b31ab` 的 [Windows 总回归](https://github.com/JavaZeroo/termforge/actions/runs/41330092575) 曾失败：74 项 CTest 中 72 项通过，模式/属性两组的 6 个 Python 用例因临时 ROOT 未规范化而触发目录包含校验；同提交的 Linux 总回归与 [Qt/固定 ConPTY](https://github.com/JavaZeroo/termforge/actions/runs/41330092581) 分别通过。Windows 日志没有记录临时目录的实际别名，不能声称它直接证明了 8.3 路径；本机已用真实 8.3 路径及等价目录写法复现同型错误。

`adec6ae` 仅修复 unittest 夹具的 ROOT 规范化，冻结的原版运行工具及生产目录校验保持不变。模式组普通/`-O` 各 46/46，属性组强制真实 8.3 临时路径下普通/`-O` 各 25/25 通过；越界路径仍被拒绝。旧失败及修复验证的日志身份见 `validation.json`；远程后续状态由 [#27](https://github.com/JavaZeroo/termforge/issues/27) 记录，这些本地结果不代替远程重跑。

Qt 四项为启动/会话、Unicode 输入、基础尺寸及新增终端模式。新增模式测试通过公开 Qt 数字键触发自有 child，核对主屏、备用屏、重复进入、返回、DECAWM 的五阶段原生网格、末端状态和有限背景色。固定源包含宽字/结合字，child 正常退出码 37，自有 Job `active=0,total=4,terminated=0`。这不含第六阶段正文 resize、真实物理输入或完整样式呈现。

**正文 resize 诊断仍失败。** 同一新二进制显式 `--resize` 执行六阶段后，原生 B/CD 已重排，自主核心仍裁剪；退出码 1，未超时，Job 已清空。没有弱化原断言、使用 `WILL_FAIL` 或把清理成功计为功能通过。默认模式回归与未完成的 resize 诊断按实际覆盖分列；诊断可通过 `TERMFORGE_ENABLE_RESIZE_REPRO=ON` 注册，或直接运行：

```powershell
python src/qt/run_smoke.py <qt_terminal_modes_test.exe> --resize
```

前两次六阶段运行及实际运输字节见[正式路径失败记录](../../../shell/results/2026-11-02-qt-terminal-modes/README.md)。#27 的 resize 交叉、完整网格/粗体/复位等未知仍保留，#29 的逻辑行/历史/选择/Find 不由上述测试关闭。没有新增性能门槛，不宣称 #27 或 R2 完成。
