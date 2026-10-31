# WindTerm 2.7.0：样式、光标与滚动区域的原版观察

原版的一个固定本地 Shell 样本，在同一 211B 输入之后，原生捕获和 UI 文字投影都出现了 `REGION_CIXED` 残尾。这与[源码 ConPTY 未修补版](conpty-source-margin-styles.md)的最终文字相同，区别于修补版恢复主屏滚动区域后的结果。按 [ADR-0006](../adr/0006-confirmed-project-direction.md)，该修补路线现在有具体的原版行为差异；重要差异或原版缺陷修复须经用户确认，不能直接选入产品。

本记录是 [#56](https://github.com/JavaZeroo/termforge/issues/56) 的一次有界参考观察，不判定全部终端行为，也不定位 WindTerm 内部的根因。原版末端颜色属性仍未取得，不能与候选核心的空白格前景色差异合并为同一结论。

## 固定身份与执行边界

- 官方 WindTerm 2.7.0 Windows x64：EXE SHA `7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0`，4,047 文件清单 SHA `e459adf2f12adb6a1b7f55e66a9da1c22794185e66e848057d21e0c60d669203`，由[既有准备守卫](../../research/ui/launch.py)核对。cmd 横幅为 Windows `10.0.26200.9711`，与此前 Vim 参考样本的 OS 修订号分列。
- 自有夹具源码 `dc5c631ffdf83dfbdfcb8b359137e0d87212ac6d`；复用原 controls EXE，SHA `ca19645b65aea8ac74b56d1e59837cb7514c3acf5932d24112411725b92206d7`。只调用其原 `--client styled-cursor`，未使用源码 ConPTY host，也未改写五阶段输入或 capture helper。
- 新建一个合成 cmd、独立 HOME/profile、非输入 window station 和自有 Job。REA CLI 固定 6.7.0，原版及驱动身份、实际调用和证据 ID 见[白名单记录](../../research/ui/results/2026-10-27-styled-native-reference/validation.json)。不接触个人剪贴板、显示设置、用户配置或日常 WindTerm 进程。
- 执行前声明最多一次。UI 请求 1230×887 后，实际 WinPTY `set_size(80,24)`、UI capacity=24 和原生 viewport=80×24 相互核对；未使用预留的两次几何校准。原生等待 5 秒、失败清理 5 秒；独立 wrapper 最多保留最终内容 8 秒供 UI 读取。REA 30 秒、外监督 45 秒。实际仅运行一次，无重试。

## 五阶段的原生结果

源文件精确为原 211B，SHA `51d0f55717ff8fbe5e82040967edf2f9399a9b893fd8e8cb282f94c143919b62`。每阶段同时捕获全文、完整 `CHAR_INFO` 字符/属性、绝对光标和 viewport top。

| 阶段 | viewport top | 绝对光标（零基行、列） | 相对窗口光标 | 内容与属性 |
|---|---:|---|---|---|
| before | 4 | 6,5 | 2,5 | `TOP_FIXED / REGION_A / REGION_B / BOTTOM_FIXED`，初始属性 7 |
| alt | 0 | 4,7 | 4,7 | `ALT_OUTSIDE`、第 4/5 行 `ALT_C / ALT_D`；两行完整属性 `0x52` |
| restored | 4 | 6,5 | 2,5 | 原主屏内容恢复，属性保持 |
| after-write | 4 | 6,6 | 2,6 | 第 3 行变为 `REGIOM_B`，写入的 M 为 `0x14` |
| final | 4 | 9,8 | 5,8 | 保留 `REGION_A / REGIOM_B`，出现 `REGION_CIXED / REGION_D / SGR_DONE`；后三行前 8 格为 `0x14` |

wrapper 在运行前后读到主缓冲区均为 80×3000、可见窗口均为 80×24；开始窗口 `[0,0,79,23]`，结束为 `[0,4,79,27]`。before 阶段本身已经位于 top=4，不能拿 wrapper 启动前的 top=0 回填五阶段记录。

直接套用原 ConPTY 判据时，绝对光标和 top 的区别使 before/restored/after-write 也失败，final 另有文字和属性差异；**原判据整体保持失败**。仅为解释来源，将光标行减去各自 top、将 top 投影为零的离线诊断，与未修补版五阶段预测全部吻合。文字、字符和属性没有修改。这是跨缓冲区布局的解释，不是产品验收，也没有改写原判据。

最终六行的差别可直接比较：

| 本次原版原生捕获及 UI 文字尾部 | 同源修补版原生 final |
|---|---|
| `TOP_FIXED` | `TOP_FIXED` |
| `REGION_A` | `REGION_C` |
| `REGIOM_B` | `REGION_D` |
| `REGION_CIXED` | `BOTTOM_FIXED` |
| `REGION_D` | 空行 |
| `SGR_DONE` | `SGR_DONE` |

UI 结果来自收到 `SGR_DONE` 后、释放 wrapper 等待前的 `activeText.lineText`，只公开其中六行合成文字。它不是截图、像素呈现、完整 screen-state 或历史隔离证明。没有捕获该次 WinPTY 到末端的运输字节，因此残尾在哪个原版组件产生仍未定位。

## 颜色、回收与后续决定

本次只枚举了已暴露的样式/光标接口名，没有读取末端逐格颜色或终端光标值；这些字段仍为 `null`。原生 `CHAR_INFO` 的完整颜色不能代替末端样式。该路径最终没有产生修补版所预期的两整行红字蓝底空白，因此也不能据此回答候选 `.5` 的 144 格空白前景色是否符合原版。

原生客户端自然退出 37，shell 后续也观察到自然退出 37。输出模式实测为 3→7，初始代码页为 936；wrapper 对模式和代码页的恢复 API 均返回成功，未再查询恢复后的值。外层仍按原启动器主动回收自有进程，Job active0/total11/terminated0；该计数不证明所有成员自然退出。配置检查仍为一个合成会话。

REA 无消息截断、无脚本错误或清理错误；provider 的 subject/environment 为 null，捕获时 state=running。包身份由本地守卫另行核验，停止后的自有回收另列，不补造 provider 已认证身份或自然退出的结论。

私有计划、驱动、五阶段捕获和插桩原件位于 `local/r56-windterm-styles/reference-0` 及其上级目录。白名单导出重新核对脚本、清单、EXE、原生文件、source 和 UI 合成尾部，无新增执行；公开摘要不含发行物、原始插桩、绝对路径或用户状态。私有驱动没有发布，因此本记录不是公开的一键复现包。

当前可确认的是一个具体原版行为：本地 Shell 路径在该输入下保留旧区域的文字结果。后续在 #56/#34 明确记录“保持该原版行为”与“修正主屏区域恢复”的产品取舍及影响，遵守原版优先和用户确认权限；实验补丁继续保留为候选，不将新观察改写成自动采用修复的依据。其他原版场景、末端颜色、历史/resize/API、完整 R2 和性能仍未验收。
