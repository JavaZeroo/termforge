# R2 字体、网格与真实显示验收合同

对应 [#31](https://github.com/JavaZeroo/termforge/issues/31)。本合同区分工作区字体、终端绘制字体、固定网格和真实显示环境；受控消息、真实窗口跨屏、实际呈现是不同证据层级。复杂 Unicode/IME 由 #30 验收，产品后端决定由 #34 负责。

## 字体来源与失败策略

- 工作区 UI 使用独立获取的 Roboto Mono Regular，9pt，SIL OFL 1.1；固定上游提交、原始字节和 SHA-256 见[字体来源](../../assets/fonts/README.md)。通过 `AddFontResourceExW(..., FR_PRIVATE, ...)` 仅对进程加载，缺失、注册失败或实际 `GetTextFaceW` 不是 Roboto Mono 时明确失败，不静默替换。`renderer_metrics` 独立复核实际 GDI face、em 高度与 `TEXTMETRIC.tmHeight`；96/120/144/192 DPI 的 em 为 12/15/18/24，行高为 16/20/24/32，不能把二者混为“字符高度”。
- 终端绘制当前使用系统 Consolas，DirectWrite `CreateTextFormat` 与 GDI `CreateFontW` 是独立路径。系统字体不分发；Microsoft 系统字体可用性、实际 fallback 和缺字必须由实际绘制路径证明。
- DirectWrite `GlyphAudit` 读取实际被绘制的 `IDWriteTextLayout`，记录 glyph run 的字体身份、glyph 数和 glyph index 0。控件字体、请求的 family 和最终 glyph 字体不能混为一谈。
- 不复制 WindTerm 字体、Nerd Font 或主题资源。新增可携带字体或 fallback 资产必须另行审核许可。

## 固定网格设计决定

- 当前终端在 96 DPI / 100% zoom 下使用 **12×26 物理像素**单元格，Consolas 字号为 18 DIP；这是 TermForge 的固定网格设计，不是从实际 glyph advance 推导的度量，也不宣称与 WindTerm 像素等价。
- 超过当前一格/两格物理宽度的 DWrite 文字布局按可用宽度缩小字号，不扩大网格；布局缓存区分单元格宽度，并在调整后重新检查实际 glyph 和最终宽度。布局宽度指标不等于逐像素过冲或上屏验收。该规则不决定复杂字素如何聚类或应占几格，完整 ZWJ/keycap 合同归 [#39](https://github.com/JavaZeroo/termforge/issues/39)。
- DPI 与 50%–200% zoom 通过同一整数缩放产生单元格尺寸；x/y 布局、脏区、裁剪、光标、IME caret 和鼠标命中共用这些整数，避免不同子系统各自舍入。
- 行列由终端 HWND 客户区扣除原生滚动条后除以单元格尺寸得到；DPI/zoom 改变后，窗口、核心与 ConPTY 尺寸必须同步。物理行裁剪/reflow 仍遵循 ADR-0005 和 #29，不在本合同暗中改变。
- `renderer_metrics` 必须继续覆盖 GDI/DirectWrite、96/120/144/192 DPI 和 50/100/125/200% zoom，检查实际 glyph、缺字、dirty/full 图像和网格边界；另用不含 `assets/` 的独立进程验证私有 UI 字体缺失时明确失败。固定网格通过不代表复杂字素或字体 fallback 完整。

## 证据层级

1. **受控 DPI 消息。** `ui-dpi` 验证 96→144→192→96 的布局、焦点、编辑框、命中、隐藏标签和子进程尺寸。它不修改系统显示设置，`real_monitor_transition=false`。
2. **真实窗口跨屏。** `ui-monitors` 使用实际 `EnumDisplayMonitors`、`MonitorFromWindow` 和 `SetWindowPos` 在至少两台显示器间往返。必须记录真实矩形、窗口 DPI、rename 焦点/内容、可见与隐藏会话、网格和 dirty/full 图像，并满足 `real_monitor_transition=true`。
3. **不同 DPI。** 只有两台目标显示器的实际 DPI 不同时，`distinct_dpi=true` 才能成立。相同 DPI 的真实跨屏是独立有限结果，不能覆盖 Windows 显示缩放变化。
4. **实际呈现和物理输入。** 非输入 desktop 的 HWND、DIB 和消息证据不支持 `presented_done`；该字段保持 null。物理键鼠、用户实际可见画面和真实 IME 跨屏需各自指定环境，不能由本合同的消息测试替代。

## 验收与未知保留

- 真实跨屏证据必须能拒绝伪造的阶段、窗口位置、DPI、可见性和 `distinct_dpi`；少于两台显示器是明确环境缺口，不是通过。
- 当前环境若所有目标显示器 DPI 相同，只验收真实跨屏路径；#31 不因该结果自动关闭，不同 DPI 与实际呈现继续保持未完成。
- 任何声明支持的 Windows 显示环境都记录系统版本、监视器矩形、实际 DPI、源码/二进制身份和重复结果。不得通过修改用户系统显示设置来制造无人值守验收。
