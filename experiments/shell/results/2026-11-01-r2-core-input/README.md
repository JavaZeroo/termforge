# R2 行顺序、显示映射与 Qt 输入首片

本记录对应 [#27](https://github.com/JavaZeroo/termforge/issues/27)、[#29](https://github.com/JavaZeroo/termforge/issues/29)、[#30](https://github.com/JavaZeroo/termforge/issues/30) 的限定实现。三个任务均继续开放。基点 `a8d6160a2bf326963d4880f9d74fec619dadeb30`；代码由 `f99b6ab`（行顺序）、`87c536e`（映射）、`f20fe0b1c65e20a5df99363f4d68d2f9d9ee4f93`（Qt 接线）组成，最后一个提交是联合验证的代码身份。后续文档提交不改变这组被测源码。

## 修复与交付

局部滚动区底部 LF 会在仍保留的区域外行之前分配新行身份。旧实现仍声明身份单调递增，使二分定位漏掉实际保留的行；IL/DL 还遗漏了结构 revision。现在由 Screen 发布真实顺序状态，选择和增量 Find 能继续定位原有行。测试复用 [#151 的原版 91×25 阶段输入与字面 DSR](../../../../docs/research/windterm-origin-wrap-reference-2026-10-29.md)，在 strict/recovery、整段/逐字节下核对原有 25 条回复、区域滚动读者与 1049 往返；IL/DL 使用独立字面行序、选择和 Find 预期。本轮没有新增 WindTerm 运行，也没有裁决未知原版模式。

`ViewMapping::project` 旧实现把等宽 span 的部分源选区扩大到整个显示 span。现在保留实际交集，仍由 `TextDocument::normalize` 扩展宽字边界；不等宽 span 沿既有不可拆显示原子合同。`HistoryViewport::snapshot` 同次发布 cells 和拥有值的 mapping，覆盖历史页、正反范围、宽字/结合字、增宽协调、真实裁剪、淘汰和旧 epoch/version 拒绝。它仍是物理行模型，不引入 reflow 或正文恢复。

正式 Qt 入口新增无修饰 Home/End/Insert/Delete/PageUp/PageDown，复用既有键表；Home/End 使用当前 DECCKM 状态。独立 `ReadConsoleW` 接收端精确核对正常模式 31 字节，其中包含代理项在导航键两侧的替换与顺序。Shift+Insert 等修饰导航仍不接管，Ctrl 字母除 C 的既有边界保持。普通模式字节通过不代表应用模式或完整编辑器键盘验收。

Qt 基础鼠标拖选通过同一显示页 mapping 命中、通过 TextSelection 保存稳定范围，并在自己的显示副本高亮。按下取格起点、拖动取最近边界，沿既有 R1 工程策略；失焦、隐藏或丢失捕获终止拖动，正文变动按既有精确协调策略保留或失效选区。`qt_cpp_shell` 用真实 cmd 计算出的 `750`，通过公开 Qt 鼠标事件分别正反选择中间一格：独立图像像素检查要求只高亮该格，且核心样式未被改写，空点击清除选区。测试不移动宿主鼠标，也不访问个人剪贴板。

## 实际验证

Windows 11 x64 build 26200，MSVC 19.42.34433、Windows SDK 10.0.26100.0、Release、Python 3.12.10。Qt 6.10.3、ConPTY 1.25.260930003、app-local CRT 14.51.36247.0 均由已有 manifest 固定并校验；Alacritty 关闭。Qt 使用 offscreen 平台和外层自有 Job，真实子进程及固定 ConPTY 仍参与。完整源/产物/日志哈希、各项结果与监督输出见 [validation.json](validation.json)。

| 验证面 | 实际失败与修后结果 |
|---|---|
| 行顺序 | RED 两项 CTest 均失败：区域行不可达、IL/DL 漏 revision；修后 2/2，邻近核心另一次 9/9 |
| 显示映射 | RED 1/1 失败：部分选择变整行；修后 history/selection/view_mapping 3/3 |
| Qt 导航 | RED 期望 31B、实际只有 `f09f9880`；修后 `qt_unicode_input` 1/1，内部 6 case 通过 |
| Qt 拖选 | RED 1/1 失败：中间格没有对应高亮；修后 `qt_cpp_shell` 1/1，含原有标签输入、退出和回收 |
| 最终代码联合检查 | 一次 21 项中 20 通过、1 失败，24.77s；失败为聚焦构建列表漏掉既有 `receive.exe`，不是产品断言失败 |
| 构建准备补齐 | 仅补建 `receive`，不改源码/预期/期限；单独重跑 `core` 1/1，内部 31/31，5.85s |

联合检查包含 14 个核心原生测试、keyboard、core、resize_replay_contract、recovery 和当前全部 3 项 Qt CTest；未运行整个仓库测试集。三个 Qt 项在同一次联合执行均通过：子进程正常回收，Job active=0、terminated=0，无超时。原失败保留，不能把两次执行改写为“一次 21/21”。测试耗时仅描述执行，不是终端性能指标。

构建保留既有 `getenv`/`fopen`、`QApplication::setActiveWindow` 弃用警告和 `edit_csi_test` 的局部变量遮蔽警告；`windeployqt` 在明确禁用翻译的部署中报告翻译工具不可用。未关闭警告或放宽测试以获得通过。

## 复验与边界

依照 [Qt 构建说明](../../../../src/qt/README.md)准备依赖并配置当前源码；完整构建可避免漏掉测试辅助程序：

```powershell
cmake --build build/qt-cpp --config Release --parallel 1
ctest --test-dir build/qt-cpp -C Release -R '^((resize|history|selection|search|search_reorder|buffer_state|view_mapping|alternate_mode|decawm_mode|private_mode|decom_origin|reply|edit_csi|order_query)_core|core|resize_replay_contract|recovery|keyboard|qt_cpp_(shell|resize)|qt_unicode_input)$' --output-on-failure
```

Qt 本片只有当前可见物理页的基础拖选高亮；选词/选行、历史浏览/自动滚动、系统复制/粘贴和 Find 尚未迁移。映射不得跨会话复用。实际字形/字素、物理键鼠、原生 IME、真实不同 DPI、WindTerm 行为等价和性能均未由本片验收，`presented_done=null`。

#27 后续仍需独立验证局部区域自动换行的连续行关联、ED3/RIS 清历史的顺序通知，以及备用屏 RIS 的目标行为；它们是源码排查线索，本片没有改变其策略。完整模式交叉、resize/reflow 和输入工作流按各任务继续，最终包由 #35 统一复验。
