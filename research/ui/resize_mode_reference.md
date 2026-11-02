# WindTerm 2.7.0：活动区域缩宽与隐藏主屏末列返回协议

这是 #27/#29 的新独立 namespace `resize_mode_reference`。它承接 [ADR-0010](../../docs/adr/0010-bounded-resize-retention.md)的调查权限，只准备下述 **3 个 case、共 16 阶段**；每个 case 使用独立进程、全新源码归档、计划与输出目录，最多运行一次。原版尚未执行，本文没有观察结果或语义验收结论。旧 `resize_frame`、`resize_edit`、`resize_erase` 工具及其失败计划保持不变。

复用既有独立 WindowStation/Desktop、OwnedJob、固定 4047 文件原包、合成单会话与 loopback Telnet 架构。准备命令默认不授权执行，不启动原版、Node、REA 或 socket；由根代理冻结身份后串行执行原版。本工具没有添加新的私有结构读取，继续使用已有模板的准备守卫和公开 Qt 文档/几何接口。

## 固定输入与阶段

所有阶段末尾只追加一次 `ESC[6n`，等待恰好一条完整回复，再取得稳定文档及窗口观察、写入该阶段 ack，才允许下一动作或尺寸请求。DSR 行列一基，文档索引与 marker 矩形分开保存；DSR 独立限制为行 1–200、列 1–500，**不因当前网格只有 61 列而拒绝回复列 91**。不用 CUP 改写动作前光标。

窗口固定 `1360×749 → 1000×749 → 1360×749`，实际 NAWS 必须是 `91×20 → 61×20 → 91×20`。只有声明的两处 resize 可改变网格，高度一直为 20；不进行尺寸试探、校准或重试。所有输入均为下表可核对的 ASCII 控制与合成标记，不包含 CR/LF。

公共 reset：`ESC[?6l ESC[?7h ESC[r ESC[0m ESC[2J`，hex `1b5b3f366c1b5b3f37681b5b721b5b306d1b5b324a`。

### A：`active-primary`，5 阶段

| 阶段 | 网格 | 输入本体 |
|---|---|---|
| 0 `primary-region-origin-on` | 91×20 | reset；在第 1/2/3/4/5/6/20 行第 6 列分别写 `T0/R2/R3/R4/R5/B6/B0`；`ESC[2;5r ESC[?6h ESC[2;10H MP` |
| 1 `primary-shrunk-unmodified` | 61×20 | 空；仅缩宽后观察 |
| 2 `primary-upper-CUP` | 61×20 | `ESC[1;1H UP`，hex `1b5b313b31485550` |
| 3 `primary-lower-CUP` | 61×20 | `ESC[99;1H DN`，hex `1b5b39393b3148444e` |
| 4 `primary-restored-unmodified` | 91×20 | 空；仅恢复宽度后观察 |

第 20 行 `B0` 只在第 6 列写两个字符，不触发额外 LF 或末列换行。区域内外哨兵都位于第 6 列；UP/DN 从第 1 列写入，避免它们仅因候选寻址行不同就覆盖哨兵。这里检查 CUP 可达位置，不发送区域 LF，因此不能声称已经验证滚动行为。

### B：`active-alternate`，5 阶段

先发送 `ESC[?1049h`，然后按 A 相同 reset、哨兵及 `2..5r + origin ON` 初始化，末尾标记换成 `AP`。五个阶段标签依次为 `alternate-region-origin-on`、`alternate-shrunk-unmodified`、`alternate-upper-CUP`、`alternate-lower-CUP`、`alternate-restored-unmodified`。其余各阶段字节与几何同 A；全程备用屏保持活动，不把返回主屏的既有结果套用到此 case。

### C：`saved-primary-edge`，6 阶段

| 阶段 | 网格 | 输入本体 |
|---|---|---|
| 0 `primary-old-edge-W` | 91×20 | reset；`ESC[2;1H 0123456789 ESC[3;1H ANCHOR ESC[8;91H W` |
| 1 `alternate-before-shrink` | 91×20 | `ESC[?1049h ESC[2;10H AP`，hex `1b5b3f31303439681b5b323b3130484150` |
| 2 `alternate-shrunk-primary-hidden` | 61×20 | 空；先单独观察缩宽后的备用屏 |
| 3 `primary-return-unmodified` | 61×20 | 仅 `ESC[?1049l`，hex `1b5b3f313034396c`；独立保存返回 DSR/正文/矩形 |
| 4 `primary-next-X` | 61×20 | 仅 `X`，hex `58` |
| 5 `primary-restored-after-X` | 91×20 | 空；仅恢复宽度后观察 |

初始 origin OFF、wrap ON 明确设置；保存点在旧第 91 列，超出后续 61 列宽度。返回前后均独立采样，阶段 3 与 X 分离；不插入 CUP，也不直接读取内部保存列或 pending-wrap 位。阶段 5 已受 X 影响，不是未经修改的保存位置复验。

## 有限正文与严格出口

[运行工具](resize_mode_reference.py)枚举源派生的完整候选行；不是用任意 ASCII 正则放行正文。两种活动区域 case 只允许已出现的第 6 列哨兵、第 10 列 MP/AP 以及逐阶段开放的第 1 列 UP/DN，和固定 61/91 列分段、空白填充及尾空白裁减。保存末列 case 只允许数字锚点、ANCHOR、第 91 列 W、第 10 列 AP，及阶段 4 后出现的 X；X 候选列限定为旧/新边缘和 `91−61` 后缀所派生的零基 `0/29/30/60/61/90/91`，另包含已有 W 的同列/后一列。最长候选 92 字符，最多 200 条文档行。

每种已发送 marker 最多出现一次；未来标记、重复标记、超长空白或其他文本拒绝。缺少 marker、正文消失、W 覆盖/保留、frame 错位以及无法区分的控制均作为有界观察保留，不预设“应该正确恢复”的语义门槛。所有仍不在有限集合中的结果停止并保留原件；不能为了导出通过而自动扩成任意行或重新运行旧计划。

[离线出口](export_resize_mode_reference.py)重新核对计划外部 SHA、精确阶段/源字节、每阶段 ack、两次 NAWS 请求及 ready 边界、窗口矩形、逐条 DSR、provider/evidence、生成脚本、完整原包与模块、10 份运行源码及源码归档、合成 profile、一次 attempt 与 Job 回收。重复 JSON 字段、非有限数字及不完整录制拒绝。新的 3 份 exporter 身份另列；旧源码命名空间不修改。

公开结果只包含白名单标量、合成输入、marker、各阶段完整合成正文数组的摘要及来源哈希；正文全文、资源、截图、绝对路径、PID/profile 与 provider 原件留在私有输出。`mode_policy_classification=null`、`semantic_acceptance=false`、`presented_done=null`、`r2_complete=false`。主动结束原版后 Job 空不等于自然 GUI 退出。

## 身份、停止条件与离线验证

运行源清单固定为新 `resize_mode_reference.py/js` 与既有 `resize_state_reference.py`、`mode_state_reference.py`、`alternate_reference.py`、`r1_launch.py`、`launch.py`、`qt_r1_text.js`、`r1_vt_fixture.py`、`experiments/conpty/run.py`，共 10 份。源码归档采用既有严格 `reference-source.json` helper；有 manifest 时不伪造或调用 Git。出口源为新 exporter 加既有 `export_resize_state_reference.py`、`export_mode_state_reference.py`，共 3 份。

每计划 `.attempted` 以排他创建；失败 attempt 永久保留，该计划不能重放。未授权、source/host/REA 变更、错几何、额外/过期 NAWS、源/回复/ack/阶段不一致或界限/期限失败均停止。JS 稳定窗口分别为网格 600 ms、观察 400 ms；这是取样方法，不是产品性能门槛。REA 捕获 45 秒、CLI 等待 60 秒、JS 场景/定时器 41/42 秒仍分别记录，`whole_lifecycle_bound_seconds=null`，不拼成总生命周期保证。

```powershell
python -B -X utf8 -m unittest research.ui.test_resize_mode_reference research.ui.test_export_resize_mode_reference -v
python -B -O -X utf8 -m unittest research.ui.test_resize_mode_reference research.ui.test_export_resize_mode_reference -v
node --check research/ui/resize_mode_reference.js
node research/ui/test_resize_mode_stages.js research/ui/resize_mode_reference.js

# 只准备一个 case；不启动目标。每 case 使用全新 plan/out。
python -B -X utf8 -m research.ui.resize_mode_reference --prepare --case active-primary --source <pinned-original> --out <source-root>/local/active-primary --rea <rea.cmd> --plan <source-root>/local/active-primary-plan.json
# 根代理核对新冻结源及执行授权后准备独立的授权计划，再执行：
python -B -X utf8 -m research.ui.resize_mode_reference --plan <authorized-plan.json>
python -B -X utf8 -m research.ui.export_resize_mode_reference --directory <capture> --plan <authorized-plan.json> --expected-plan-sha256 <external-plan-sha256> --source-root <source-archive> --reference-source <pinned-original> --destination <new-public.json>
```

`--prepare --authorize-execution` 只在根代理已有执行授权时创建授权计划；执行阶段只接受计划，不能另传 case/source/out 改写输入。离线测试包含模拟 peer 和纯 V8 控制器、计划及完整合成录制的公开出口拒绝路径，不启动目标/VM/native。它们不能产生原版行为结论。
