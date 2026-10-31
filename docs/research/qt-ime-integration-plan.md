# Qt IME 基础接线计划（#155）

状态：**初始计划历史记录**，描述源761bb2c执行前的test-only准备；下文“当前”仅指当时基线，不是最新实现状态。实施/实测由 [#155](https://github.com/JavaZeroo/termforge/issues/155)维护，后续原生计划见 [N1](qt-ime-native-observation-plan.md)。依据 [ADR-0008](../adr/0008-cpp-core-qt-widgets.md) 和 [R2 合同](../milestones/R2-local-terminal.md)。初始基点 main a0aed8a643a0a99b09e16c7836e5a4e3ad26daed 已包含 #147 的 Qt 键盘文本；旧原件保持不变。本页不宣告最新能力或验收通过。

## 已知事实与首个反例

当前 TerminalWidget 明确关闭 WA_InputMethodEnabled，没有重写 inputMethodEvent / inputMethodQuery。Qt 6.10 的 [QWidget 合同](https://doc.qt.io/qt-6.10/qwidget.html#inputMethodEvent)说明默认事件处理拒绝输入法事件；这只是源码和接口依据，**尚不是本切片的运行观察**。

test-only 源 58ad294aea7d01388e83720b70e467b4b55d82f5 注册 qt_ime_input，使用现有固定 ConPTY、run_smoke.py OwnedJob/GO 和独立 shell_input_client：

1. 等接收端独立输出 TF_KEY_READY，以公开 Qt 按键发送 L。
2. 发送带 TextFormat / Cursor 属性的公开 preedit zhongwen，保存前后实际自有 QImage、哈希、差异像素数和 query 值；这些诊断尚不作为首个失败前置。
3. 发送一次 IME commit：中文、NFD、U+1F600、字面 U+0008 和 U+0000，再用公开按键发送 R。
4. 接收端通过自己的 ReadConsoleW / Windows UTF-8 转换导出文件。完整字面预期为 **4ce4b8ade6968765cc81f09f9880080052（17B）**；preedit 不属于该字节流。只由 Session 发送该夹具的 EOT，待接收端自然结束及回收后，先保存观察再断言全文。
5. 当前实现会丢失 commit 是**待验证假设**；不提前把 WA_InputMethodEnabled 或图像检查失败写成“已取得独立接收红例”。

未延长原合同：阶段 gate5秒，既有监督器 communicate25秒、超时后5秒等待，CTest35秒；后续 drain 和 finally 清理各有原实现边界。它们不组成已证明的整程硬时限，更不是性能门槛。原件写入独立自有目录；图像仅属本项目控件离屏绘制，不是原版资源或物理呈现。

## 最小实现形状

只修改 Qt widget 的输入法与绘制接线，必要时复用 Model 的错误呈现通道增加单一 reject_input(reason) 领域入口。Parser/Screen 不保存 preedit，也不把终端屏幕当成可编辑输入行；ConPTY 和有界 InputQueue 的职责不变，不引入新组件、字体或测试钩子。

- **字面提交。** preedit 和 commit 各沿旧 ImeContext 的256个UTF-16单位工程上限，先严格校验完整 UTF-16，再按字面 UTF-8 编码。复用旧 ime.hpp 的 WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS) 语义，不调用会将 Backspace 转成 DEL 的 KeyboardEncoder.character。整个结果只调用一次有界 send，拒绝不发送前缀。该上限和非法输入策略属于当前 TermForge 工程政策，尚无 WindTerm 对照。
- **独立状态。** IME 不把自己的代理项交给 #147 的键盘编码器；无效或被拒绝的 IME 事件不改变已经持有的键盘半个标量。焦点/隐藏/关闭/不可用目标继续清理两种状态。旧 Win32 接线也是将 ImeMessage.committed 独立发送，不能因共享入口而套键盘控制码政策。
- **组合显示。** widget 只持有有界 preedit、格式范围和组合光标。按 [QInputMethodEvent 合同](https://doc.qt.io/qt-6.10/qinputmethodevent.html)至少实现 TextFormat 的前景/背景/下划线及 Cursor 位置/可见性/颜色，用 Qt 自有文字布局绘制，裁剪于终端可用区域。错误 UTF-16、越界/冲突属性明确拒绝并取消，不静默截断。
- **周边编辑请求。** 当前没有应用输入行/重转换合同，replacementStart或replacementLength非零明确拒绝，不向 child 注入猜测的退格/光标序列。周边文本和选择 query 不暴露屏幕正文作可编辑文本；计划返回空内容/索引或不支持值，并在验证中保留实际返回。
- **候选锚点。** ImEnabled 按实际目标可用性、焦点和可见性回答。ImCursorRectangle / ImAnchorRectangle、ImFont 使用当前 widget 逻辑坐标、模型光标和实际文字布局；output、resize、字体/DPR及焦点变化调用 [QInputMethod.update](https://doc.qt.io/qt-6.10/qinputmethod.html#update) 通知。Qt 逻辑矩形与 native 物理矩形分列，不将原 Win32 的12×26网格认定为 Qt 已等价。
- **取消与重入。** empty preedit 更新可取消组合但不发送文本。焦点转移先清理本地状态并依赖 Qt 自动重置输入上下文；若因本目标错误主动 reset，只在它仍是当前焦点时调用并先清理本地状态，不取消新目标或操作共享 OS 输入服务。
- **一次性发送。** 每次 commit 事件只在本 handler 发送一次，不再交给默认编辑处理或同时从 key text 重发。相同内容的另一次输入事件仍是新输入，不能用“内容去重”吞掉用户有意重复；真实 Windows 提交/字符事件是否重复由 native 证据核对。组合期间普通按键文本不能绕过 IME 进入 child。

Qt 接口要求是实现依据，未知 WindTerm 行为仍未知；没有因为文档或测试通过而批准重要行为差异。

## 后续红绿与停止条件

按 tracer bullet 接续，不先写完整假想测试矩阵：

| 顺序 | 公开动作和独立预期 |
|---|---|
| 首片 | 先编译/冻结/执行上面的17B接收红例一次，再实现字面提交使其通过；源、EXE、SDK/ConPTY/CRT、stdin GO、stdout/stderr、receiver和Job独立保留 |
| 组合 | 实际 QImage 的 preedit/格式/光标与取消后恢复，原始接收端证明 preedit 未发送；普通文本属性和 Cursor 显隐用可判读颜色/区域反例 |
| 拒绝 | 256/257单位、非法 UTF-16、replacement、无目标和实际发送失败；不通过改变等待或加生产队列钩子取绿。现容量 guard 的独立测试与 Qt 真实拒绝现场分列 |
| 上下文 | focus/hide/两标签/关闭/自然结束/失败目标；前后接收文件、查询、组合图像和回收均可核对，不根据空文件声称自然退出 |
| 几何 | output、resize、字体变化后的公开 query 和自有渲染；记录逻辑矩形/网格关系，不冒充跨DPI或实际上屏 |
| 完整配置 | 合入最新 main，复验原Qt键盘/cmd/resize，冻结源码/依赖/运行目录，运行一次必要完整矩阵并分开Qt和其他诊断、实际skip与旧失败 |
| 原生 | 源码和包/计划审查后，仅root在隔离Win11固定Microsoft拼音执行真实输入→Qt组合/结果→child字节，记录候选/焦点/清理；未满足则#155不能按完整原生条件结项 |

本次准备阶段只提交源码和计划，**编译/Qt执行/full/native均为0**。先等 root 的互斥负载租约；不改个人输入布局/桌面/剪贴板，不直接接管 Qt HWND 的 IMM，不终止 ctfmon 等共享服务，不执行未审核的 guest 写入。后续实际失败保留，不自动重试取绿或扩大原期限。

完整 Find/rename、同次八会话、CF、真实跨DPI、所有字素/glyph/fallback、物理呈现及性能仍由 #30/#31/R2 接续；只观察到合成 Qt 事件不能关闭真实固定拼音条件。

