# #133：原版本地 cmd 网格已读取，C0 配置仍为 partial

固定 WindTerm 2.7.0 的一次有界本地 cmd 诊断中，producer 的实际 console window 为 **120×40**，原版 `terminal.activeView()` 返回对象的 `windowColumns/windowRows` 也为 **120/40**。14 行/536B oracle 全文及 1 条空尾行匹配。document 的 `screenLineCapacity=23` 单独记录，不能替代 PTY 或 view 的行数。

这只支持本次两个窗口读取点及全文正确性。QWidget 元数据为 Roboto Mono 9pt / pixelSize -1；它不证明终端实际 glyph 使用该字体，也不能据此断言 Cascadia Mono 12pt 请求失效。实际 glyph/fallback/字体文件身份、10000 行历史上限的有效性、自动补全关闭是否生效、完整 C0、消费/呈现和性能仍未知。

Refs [#133](https://github.com/JavaZeroo/termforge/issues/133)、[#3](https://github.com/JavaZeroo/termforge/issues/3)、[#4](https://github.com/JavaZeroo/termforge/issues/4)。本记录不自动关闭这些任务，不改变产品路线或验收容差。

## 固定执行与配置差异

- 生产源 `28f293a5d2c01954dc8b418a02231603128955a6`，执行准备 HEAD `b22293f7fbc78c282fa01b12d28e53e5e2ff00c9`；[工具及方案](../../WINDTERM-C0-SETTINGS.md)。九个实际 source 文件及两个 Qt 模板、旧 helper 前缀、组合模板和完整注入脚本分别绑定哈希。旧 #119 runner/probe 没有修改，没有 monkeypatch base 全局或伪装旧 run。
- [新授权计划](plan.json)的私有原件 SHA 为 `d1678750af02c27e60dec373b38ca49cca8cedd254a0c3708ea9e1b841494afc`，使用原 review nonce `e736ad1ca79c4cfc85d468fdd5e5899e` 和完整 536B 输入。[原 false proposal](../2026-10-29-c0-settings-proposal/plan.json)保持原样；实际只执行一次，没有补跑。
- 固定官方 EXE SHA 为 `7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0`。原 4047 文件清单 SHA 为 `e459adf2f12adb6a1b7f55e66a9da1c22794185e66e848057d21e0c60d669203`；五个实际加载模块的完整文件 SHA 和模块路径守卫已核对。
- 自有副本只修改 `global/wind.config` 三个唯一标量 token：`text.fontFamily1="Cascadia Mono"`、`text.fontSize=12`、`terminal.autoComplete.enabled=false`。BOM、CRLF、注释及所有其他字节保留；实际修改后 SHA 为 `01a5465e6b4dec6567ac4cf3d21df360f09b737f77c0861ec3f250400806081d`。其余 4046 个原发行文件逐一未变，受控差异的 key/前后 SHA、配置后完整清单 SHA 见[原样白名单出口](validation.json)。身份是固定二进制/原发行物基线加受控配置差异，不称整个包未改。
- synthetic `user.sessions` 请求 `window.autoAdjustRowsAndColumns=false`、columns=120、rows=40、scrollbackLimit=10000。该会话文件与请求身份一致，但写入字段不等于设置已被消费；静态默认与实际读数仍分开。

## 实际读值

| 读取点 | 结果 | 解释边界 |
|---|---|---|
| producer console window | columns=120，rows=40 | 实际 console API 返回 |
| producer console buffer | columns=120，rows=3000 | 不是 WindTerm 保留历史上限 |
| producer mode | 原 mode=3，VT 启用成功 | 不表示完整终端语义或性能通过 |
| `terminal.activeView()` | 实际返回 object；windowRows=40、windowColumns=120 | getter 属性读值，未用默认值补造 |
| document | screenLineCapacity=23 | 独立文档容量读点，不能当 PTY rows |
| QWidget | geometry=`[0,0,905,428]`；QFont family=Roboto Mono、pointSizeF=9、pixelSize=-1 | 控件元数据，不是终端绘制字体或 glyph/fallback 证据 |
| 28 条 dot-key 配置读取 | 21 条 unavailable、7 条 owner-unavailable；值均 null | 未读出有效配置，不填请求或静态默认 |
| 有限名字枚举 | 前 3 个 owner 各保留 128 项范围并截断；仅 active_text/active_view 命中 screenLineCapacity/windowColumns/windowRows，terminal 无命中；global_settings owner 不可用 | 不能据截断名单认定其他 API 不存在，也没有执行名字命中的未知方法 |

没有将 console buffer 3000 解释为 scrollbackLimit，也没有把 QWidget 的 9pt 与配置请求 12pt 当作终端 glyph 的直接比较。字体请求是否作用于终端绘制仍未知，不安装个人宿主字体或改变 DPI。

## 全文、CLI 与回收

producer 按固定 run_id 和 payload SHA 关联，独立重建 BEGIN、12 条编号 token、END 共 14 行和 CRLF 536B。原版一次 `settings_complete` 的 15 条 `lineText` 恰为完整 14 行及空尾行，没有 trim、跳过前缀或只检查 END marker。consumer_done/presented_done 均为 null。

REA Evidence ID 为 `ev_68b4214467f4886a31fd9f0a8292c26b83cbaa4801bc63a4109b4a0d84fce6ac`，无脚本错误、截断、清理错误或 stderr。root 监督实际 CLI session 至 exit=0 后才交离线收尾；没有活 CLI 交接。OwnedJob 为 active=0、total=14、terminated=0，run_id/原版 target PID 绑定一致，profile 仍只有一个合成会话。启动器主动结束原版，Job=0 不证明自然 GUI 退出，producer 祖先关系未独立观测。

capture=30 秒、CLI wait timeout=45 秒；目标更早启动并另等 2 秒，准备与回收另计，整程上限为 null。原版运行在非输入 desktop，不能满足 C0 可见且无遮挡条件；这是插桩正确性/配置读取诊断，没有观测开销或性能结论。

## 独立离线审计与发布身份

[独立重算摘要](audit.json)在不调用生产 summarizer 作为断言依据的条件下，分别核对了准备提交及当前九源、运行时五模块、组合及完整注入 JS、完整 oracle、4047 文件清单、三项 literal token byte patch、session 请求文件、原始实际读取、13 份 raw 哈希和清理等式。没有再次运行原版，也未修改计划、source 或任何原件。

原普通/`python -O` 出口逐字节相同，SHA 为 `2e1e2dbfffb3f2f3ed914c032b1eb0c0d859734f54f6f216215e6be29c22e3d4`；公开 [validation.json](validation.json)直接保留普通出口原字节，未添加或改写字段。计划与额外审计分别放在独立文件。源码随后合并 main `5f7373f`，九个生产源仍与准备 HEAD 相同；发布源码不反填为执行源码。

工具公共边界测试仍分列：新工具普通/优化解释器各 14 项、旧工具 12 项；Debug CTest 两项通过。它们使用合成 Qt/WinAPI、CLI 拒绝和文本 oracle，不计作额外原版样本。raw 日志、PID、路径、原资源、profile 与注入原件留 local；公开只有合成输入、白名单标量及 hash。

下一最小取证点是先静态确认当前 view/document 的有效配置读取接口，以及能绑定合成文本的绘制字体读取点，再做独立有界观测。未经核实的方法不能靠枚举名执行；历史 10000 淘汰另需完整编号负载与淘汰 oracle。当前不扩展此批，不以新增读取点消除完整 C0、glyph、autocomplete、历史或性能的未知。
