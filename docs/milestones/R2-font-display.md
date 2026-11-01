# R2 字体、网格与真实显示验收合同

按 [ADR-0009 决策 4](../adr/0009-r2-scope-decisions.md)，R2 只验收**单显示器 96/144 DPI**。原 [#31](https://github.com/JavaZeroo/termforge/issues/31#issuecomment-6553824433)已按范围决定收口；正式 Qt 产品的单屏渲染与声明交互由 [#35](https://github.com/JavaZeroo/termforge/issues/35)在最终包复验，真实跨屏迁移由 [#297](https://github.com/JavaZeroo/termforge/issues/297)在 R4 承接。复杂 Unicode/IME 继续由 #30 验收。

本合同区分工作区字体、终端绘制字体、网格和真实显示环境；受控消息、真实窗口跨屏、实际呈现是不同证据层级。旧 Win32/DirectWrite 的 12×26 网格、18 DIP、缩字适配及缺字体失败策略不自动成为 Qt 产品策略。按 [ADR-0006](../adr/0006-confirmed-project-direction.md)，重要字体/几何/交互差异仍需原版对照和用户确认；已有度量和回归只证明各自固定配置。

## R2 正式 Qt 产品的单屏合同

- 在声明的 Windows 环境分别记录实际单屏 96/144 DPI、窗口/终端区域、Qt 字体与网格度量、源码/依赖/二进制身份；受控消息中的请求值不代替实际 DPI。
- 核对文字、光标、裁剪、IME caret 与鼠标命中使用一致的坐标换算，记录可见和隐藏会话、焦点、输入法及已声明编辑控件；未实现的交互保持明确缺口，不由探针结果推断完成。
- DPI 或字体变化后分别核对窗口、核心、transport 完成和真实 child 尺寸；resize/历史仍按 #29，不在显示合同中改写物理裁剪或 reflow 策略。
- 字体资产来源、许可、缺失处理及实际 glyph/fallback 按 Qt 绘制路径验证；请求的 family、控件字体、旧 GDI/DWrite 字体身份不能代替实际 glyph 字体。
- 最终包在声明交互范围内复验。#31 的关闭仅解除真实跨屏前置，不意味着单屏 Qt 完整流程已通过；单屏未验部分由 #35 明确收口，#30 的保留输入任务不被豁免。

当前 Qt 网格计算见 [Qt 会话入口](../../src/qt/README.md)。下面保留旧 Win32 配置的固定合同与证据，用于回归和比较，不将其成功转记为 Qt 验收。

## 既有 Win32 配置：字体来源与失败策略

- 工作区 UI 使用独立获取的 Roboto Mono Regular，9pt，SIL OFL 1.1；固定上游提交、原始字节和 SHA-256 见[字体来源](../../assets/fonts/README.md)。通过 `AddFontResourceExW(..., FR_PRIVATE, ...)` 仅对进程加载，缺失、注册失败或实际 `GetTextFaceW` 不是 Roboto Mono 时明确失败，不静默替换。`renderer_metrics` 独立复核实际 GDI face、em 高度与 `TEXTMETRIC.tmHeight`；96/120/144/192 DPI 的 em 为 12/15/18/24，行高为 16/20/24/32，不能把二者混为“字符高度”。
- 旧 Win32 终端绘制使用系统 Consolas，DirectWrite `CreateTextFormat` 与 GDI `CreateFontW` 是独立路径。系统字体不分发；Microsoft 系统字体可用性、实际 fallback 和缺字必须由实际绘制路径证明。
- DirectWrite `GlyphAudit` 读取实际被绘制的 `IDWriteTextLayout`，记录 glyph run 的字体身份、glyph 数和 glyph index 0。控件字体、请求的 family 和最终 glyph 字体不能混为一谈。固定 ZWJ/合格 keycap 按[流式字素合同](../research/streaming-graphemes.md)另要求整段为 1 个 Segoe UI Emoji glyph；keycap 整段指定该系统字体，避免数字与方框被两个不同 fallback 字体拆开。
- 不复制 WindTerm 字体、Nerd Font 或主题资源。新增可携带字体或 fallback 资产必须另行审核许可。

## 既有 Win32 配置：固定网格

- 该 Win32 配置在 96 DPI / 100% zoom 下使用 **12×26 物理像素**单元格，Consolas 字号为 18 DIP；这是旧原型的固定网格设计，不是从实际 glyph advance 推导的度量，也不宣称与 WindTerm 像素等价。
- 超过当前一格/两格物理宽度的 DWrite 文字布局按可用宽度缩小字号，不扩大网格；布局缓存区分单元格宽度，并在调整后重新检查实际 glyph 和最终宽度。布局宽度指标不等于逐像素过冲或上屏验收。该规则不决定复杂字素如何聚类或应占几格；首批 ZWJ/keycap 合同由 [#39](https://github.com/JavaZeroo/termforge/issues/39)限定验收，完整输入整合由 #30 继续负责。
- DPI 与 50%–200% zoom 通过同一整数缩放产生单元格尺寸；x/y 布局、脏区、裁剪、光标、IME caret 和鼠标命中共用这些整数，避免不同子系统各自舍入。
- 行列由终端 HWND 客户区扣除原生滚动条后除以单元格尺寸得到；DPI/zoom 改变后，窗口、核心与 ConPTY 尺寸必须同步。物理行裁剪/reflow 仍遵循 ADR-0005 和 #29，不在本合同暗中改变。
- `renderer_metrics` 必须继续覆盖 GDI/DirectWrite、96/120/144/192 DPI 和 50/100/125/200% zoom，检查实际 glyph、缺字、dirty/full 图像和网格边界；另用不含 `assets/` 的独立进程验证私有 UI 字体缺失时明确失败。固定网格通过不代表复杂字素或字体 fallback 完整。

## 证据层级与 R4 跨屏合同

以下跨屏要求保留给 #297，不作为 R2 的单屏前置。受控消息和既有 Win32 测试仍有各自回归价值。

1. **受控 DPI 消息。** `ui-dpi` 验证 96→144→192→96 的布局、焦点、编辑框、命中、隐藏标签和子进程尺寸。它不修改系统显示设置，`real_monitor_transition=false`。
2. **真实窗口跨屏。** `ui-monitors` 使用实际 `EnumDisplayMonitors`、`MonitorFromWindow` 和 `SetWindowPos` 在至少两台显示器间往返。必须记录真实矩形、窗口 DPI、rename 焦点/内容、可见与隐藏会话、网格和 dirty/full 图像，并满足 `real_monitor_transition=true`。
3. **不同 DPI。** 只有两台目标显示器的实际 DPI 不同时，`distinct_dpi=true` 才能成立。相同 DPI 的真实跨屏是独立有限结果，不能覆盖 Windows 显示缩放变化。
4. **实际呈现和物理输入。** 非输入 desktop 的 HWND、DIB 和消息证据不支持 `presented_done`；该字段保持 null。物理键鼠、用户实际可见画面和真实 IME 跨屏需各自指定环境，不能由本合同的消息测试替代。

## 验收与未知保留

- R4 的真实跨屏证据必须能拒绝伪造的阶段、窗口位置、DPI、可见性和 `distinct_dpi`；少于两台显示器是该合同的环境缺口，不是通过，也不阻塞 R2 单屏验收。
- 相同 DPI 的真实跨屏结果只证明该限定路径，不覆盖不同 DPI。#31 按用户范围决定关闭不能回填任何未观测结果，剩余跨屏合同由 #297 保留。
- 已建立 [Windows 11 x64 隔离测试环境](../research/r2-win11-display-environment.md)，固定记录中 Windows 两个输出为 96/120 DPI，独立窗口两轮往返读值一致。这只完成 #131 环境基础，不替代 R2 单屏 96/144 的 Qt 产品流程，也不替代 #297 完整跨屏交互。
- 任何声明支持的 Windows 显示环境都记录系统版本、监视器矩形、实际 DPI、源码/二进制身份和重复结果。不得通过修改用户系统显示设置来制造无人值守验收。
