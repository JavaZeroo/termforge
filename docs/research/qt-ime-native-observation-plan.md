# Qt IME 原生观察 N1（#155，待执行）

状态：**提案/待root快审，实际native运行次数0，完整CTest次数0**。它只接续 #155，不改变 [R2](../milestones/R2-local-terminal.md)、原版优先规则或性能容差。公开事件 focused 原件位于本切片私有工作目录；只在源码、运行包和本计划经审查后由 root 独占隔离 Windows 11 VM 执行。

## 固定入口与来源

入口为 qt_ime_input_test.exe --native-ime-observer <new-absolute-output-directory>。从既有 stdin 精确 GO+LF、OwnedJob 检查之后创建 QApplication；native分支立即进入观察器，不调用默认测试数组的 QInputMethodEvent/QKeyEvent/QTest 注入函数。它只接受 windows 平台插件，拒绝 offscreen。显式 --offscreen-ime-observer-lifecycle 复用同一个观察器、实际SessionTabs/ConPTY与两个接收端，仅检查正常/取消应用生命周期，绝不替代原生IME。两个独立lifecycle CTest各保留25秒supervisor、5秒共享清理及35秒CTest上限；旧focused的25/35秒和20秒controlled-child默认保持不变。

N1 使用现有正式 SessionTabs 预建两个目标，各有自己的固定ConPTY和独立 ReadConsoleW 接收端，分别保存 native-a.bin / native-b.bin。child由同一EXE的 --ime-output-child 分支运行；先检查已有Job归属，不创建Qt GUI或接管输入法。输入文字与控制事件独立，停止前须由root确认预期原始字节已落盘。

N1 源码配置单独固定为 observer45秒、controlled child50秒；建议root自有监督55秒、清理5秒、交互请求70秒。上述均是本次原生诊断的待审核执行预算，不是性能门槛或旧合同的延长。root须把实际外层脚本、期限、stdin、源/EXE/完整运行时清单/ZIP哈希与唯一输出路径绑定后才启动。最大实际原生运行 **1次**，失败/超时不自动重试、不替换原件。编译失败、尚未启动和真正执行失败分别计数。

运行目录的manifest列40个非清单文件；加runtime-manifest.json为41文件包，以实际清单为准，不为了凑“40”删文件。Qt6.10.3、固定ConPTY1.25.260930003、app-local CRT14.51保持已有身份。最终完整回归须在原生观察及必要修正之后，对最终源码只执行必要一次。

## root执行前预检

- 只使用自有VM/交互session及新建空结果子目录，不改个人Windows的显示、布局、输入法或剪贴板；不终止ctfmon等共享服务，不直接接管Qt HWND的IMM context。连接、账户、PID/HWND与原始图像仅私有保存。
- 在实际目标输入线程核对HKL、语言0804、启用的Microsoft拼音provider/profile及系统版本。既有 HostedPinyin 的身份为 provider 81D4E9C9-1D3B-41BC-9E6C-4B40BF79E35E / profile FA550B04-5AD7-411F-A5AC-CA038EC515D7；只能复用身份和已有OS驱动方法，不能伪造hosted环境绕过旧runner守卫。实际VM身份和版本仍须独立记录。
- 原生窗口标题固定 TermForge Qt IME native observer。ready.json 仅在两个child控制ready与两个模型真正读到TF_QT_IME_READY后产生；包含Qt平台、locale、实际焦点、DPR、逻辑tab矩形和查询。locale/HKL本身不证明转换模式或IME已工作。
- root保留根句柄，核对它属于精确自有Job并在GO前验证；锁定实际窗口PID/线程/前台/焦点后才SendInput。外部驱动仅使用虚拟键或scan code（含实际鼠标tab点击），**禁止KEYEVENTF_UNICODE、WM_IME_COMPOSITION伪造或手造Qt输入事件**。这是synthetic OS input，不是物理键盘。
- native-state.json每次50ms观察器poll在实际Qt GUI线程只读采样GetActiveProfile(GUID_TFCAT_TIP_KEYBOARD)、该线程HKL、Win32焦点及两个Qt目标状态。记录HRESULT、profile type/language/CLSID/GUID/flags和tick；S_FALSE/失败/过期采样不作已激活证明。只借用并Release该线程实际焦点窗口的HIMC读取open/conversion/requested candidate form，不创建、关联、更换或设置context。固定拼音、语言0804、active flag、实际焦点/当前tab、HKL及中文open/native转换状态须同时匹配，缺项fail-before-input，不用controller profile或已安装名单补齐。
- 只读诊断在测试EXE新增Windows SDK imm32/ole32/uuid链接，正式产品入口依赖不变。CoInitializeEx仅S_OK/S_FALSE持有并配对释放自身引用；RPC_E_CHANGED_MODE沿当前已初始化apartment查询但不CoUninitialize Qt的引用；不创建/激活ThreadMgr或ActivateProfile。
- root驱动脚本、实际provider选择与Chinese转换状态、每步发出的Win32输入和观测条件先冻结。本文件不猜测未知provider状态，也不把“看起来启用了中文”当成已取证。

## 预声明动作与判据

| 动作 | 原生证据和预期 |
|---|---|
| A组合开始，尚不提交 | 实际OS虚拟键进入固定拼音，a-events.json取得非空preedit和TextFormat/Cursor；A/B raw保持空。root必须关联真实候选窗口、当前目标/焦点和Qt锚点，不能由自有QImage冒充候选窗或实际上屏 |
| OS Escape取消 | Qt组合清理、候选/焦点变化可追溯；未提交文字不出现在raw中。至少保留一个只组合/取消而自然结束的B空文件，区别于强制取消进程 |
| A用固定拼音输入nihao并确认 | 观察真实provider产生的commit UTF16，独立A接收精确“你好”UTF8 e4bda0e5a5bd一次；B不增加。两个相同的独立commit仍应各自发送一次，不按内容或时间去重 |
| A再组合，真实Ctrl-C与随后Escape | 记录ShortcutOverride/KeyPress/KeyRelease的真实键、modifiers、scan/virtual key以及IME取消/结果。若Ctrl-C确实到达widget，沿已有中断合同只产生一次03；若provider未交给widget，只记录实际链与未知，不虚构已测试03或原版一致。Escape作为单独取消动作保留，不据它回填Ctrl-C效果 |
| 实际tab切换到B/返回A | 使用ready中逻辑tab矩形和实际DPI/Win32客户区变换定位，核对真实Qt焦点与每目标事件/raw，不把未完成组合串入另一个目标。B可仅做组合+Escape而不commit，以保留取消空文件 |
| 组合期间输出/窗口尺寸/字体 | 可用output.request触发独立child输出、root对自有native窗口的实际resize、font.request触发明确诊断字体API动作；记录Qt query/公开cursor信号、实际候选窗与焦点。字体API不是OS按键，不声称原Win32的12×26或目标glyph已等价 |
| 收尾 | root先核对已声明raw及事件，再创建finish.request；两个child通过独立控制事件结束，SessionTabs实际完成回调结束Qt事件循环，再生成complete.json。正常要求natural exit0、EOF、inner Job0和cleanup verified；root核对根正常退出、外Job0、空请求队列及实际回收。cancel.request只代表诊断取消，close/cleanup完成后CLI1/observer_complete=false；45秒绝对期限在finish后仍有效，期限先于证据I/O检查，达到后退出失败，不无界等callback。强杀/超时/缺失证据不能称通过 |

候选窗口可能属于OS输入服务，不以“PID等于产品进程”代替关联；root应保存实际类/owner或UIA关系、provider身份、焦点和几何变化。Qt逻辑矩形与Win32物理矩形分列，不自行设未经确认的像素容差或从相同DPI推导不同DPI验收。无法可靠识别候选或真实输入来源就保留缺口，不用模拟补齐。

## 观察器能证明和不能证明的事

a-events.json / b-events.json 是widget处理前的公开事件观察，不把进入时accepted标志解释为处理成功；commit由独立raw校验。spontaneous标志不是OS来源证明，需外部Win32驱动与native分支无合成输入的来源链。queued-frame记录input sequence、后续事件数和coalesced_input_updates，合并帧不冒充该事件精确瞬时状态。

原始IME文本最多256单位、属性最多258、每目标事件最多128；超过会显式标evidence_gap并失败收尾，不截断后称完整。自有QImage只表示产品控件渲染，presented_done保持null。observer_complete/CLI0仅表示观察器与两接收端正常完成；native_os_ime_acceptance保持null，真正IME功能结论由root对来源、candidate/焦点、事件、原始接收及回收独立审核。

留证失败仍立即停止，不重试或忽略。首个失败的 `evidence_write_failure` 保留 target、JSON open/write/commit 或 PNG save 操作、仅本地 basename、Qt FileError/errorString、JSON 预期/实际 bytes 或 PNG writer error；后续成功或失败不覆盖首错。JSON 必须一次写入完整序列化长度。最终 state 在两个目标 snapshot 后读取该上下文，成功写出的 about-to-quit/complete 带首错；complete 文件自身写失败时，结构化 stdout 保留同一上下文并返回失败，不能声称该文件存在。错误字符串删除私有目录，不公开原生截图或路径。

`qt_ime_evidence_write` 为独立 console seam：实际 QSaveFile 正常/open/无 DELETE sharing 的目标句柄造成的 commit 失败，受控短写/-1 不重试不 commit，PNG writer 失败，以及 sticky/final JSON 传播。它不创建 GUI、receiver、IME 输入或 VM。共享锁用例只证明该失败能被诊断；PS reader 的共享模式与原生观察失败之间的因果仍是待验证 hypothesis，不据本地合成反例认定。

当前258属性guard由256单位最多256个非空格式范围+Cursor+Selection推导；零length格式不占格式存储，过量no-op提前拒绝。它与256单位/非法UTF16拒绝都是TermForge工程边界，非WindTerm已对照。Ctrl-C显式分支不被composition普通文本guard吞掉，provider实际取消政策仍待这次原生观察。

只读API语义依据：[GetActiveProfile](https://learn.microsoft.com/en-us/windows/win32/api/msctf/nf-msctf-itfinputprocessorprofilemgr-getactiveprofile)、[TF_INPUTPROCESSORPROFILE](https://learn.microsoft.com/en-us/windows/win32/api/msctf/ns-msctf-tf_inputprocessorprofile)、[CoInitializeEx](https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-coinitializeex)、[CoUninitialize](https://learn.microsoft.com/en-us/windows/win32/api/combaseapi/nf-combaseapi-couninitialize)。API查询不等于真实候选可见或物理键盘来源，仍须外部链证据。

若本次发现OS时序/焦点/commit差异，先保留失败、定位并明确修正，再对新最终源做完整回归；不得先full再默认为不同源码仍已验收。原版未知、完整Find/rename/同次八会话、CF、所有字素/glyph/fallback、真实跨DPI和性能仍由#30/#31/R2接续。

