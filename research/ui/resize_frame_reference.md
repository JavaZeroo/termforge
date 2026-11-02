# 主/备用帧 resize 与末列状态：独立观察协议

本文件定义 [运行工具](resize_frame_reference.py)、[控制器](resize_frame_reference.js)和[严格导出器](export_resize_frame_reference.py)的固定协议，不是本组原版运行结果。目标限于 #27/#29 的帧与末列状态，不扩展到高度变化、单列、Unicode、样式、TUI 或本地 Shell。旧冻结探针与归档保持不变。

## 不重复的已有证据

- 主屏固定 78B 正文在 91→61→91 的文档 marker 保留、矩形重排/恢复与 DSR 不变已有三阶段原版证据；`next-char`、CR、LF、显式 CUP 已各有独立四阶段 strict 导出。它们不回答备用屏或 pending-wrap。EL1 不作为本提案的已通过合同。
- `mode_state_reference` / `mode_reentry_reference` 已有无 resize 的首次/重复/再次 1049、每屏 origin 与主屏 margin 控制；不重跑整套 14 阶段。新样本只加 resize 交叉。
- #123 / #151 已记录无 resize 的主屏 pending-wrap 跨 1049 及 DECAWM 开/关末列行为；新样本只观察尺寸变化前已到末列时，下一字符落点。

## 公共冻结条件

所有 case 各用一个 fresh 原版进程、一个合成 loopback Telnet 会话和独立一次性计划。固定官方 WindTerm 2.7.0、现有 protected launcher/peer、REA 6.7.0 及实际模块身份；源码归档继续使用真实 `reference-source.json`，不 mock Git，不改已执行工具/计划。

每个 case 的公开 QWidget 窗口都只走 **1360×749 → 1000×749 → 1360×749**，NAWS/capacity 必须为 **91×20 → 61×20 → 91×20**。表中重复相同宽度是动作阶段，不增加尺寸请求。每阶段末尾只追加一次 `D = ESC[6n`；记录原回复字节，不从 caret API 或可见矩形生成 DSR。

初始/resize NAWS 稳定 600 ms，marker/文档/DSR 快照稳定 400 ms，保留完整事件数和前后边界。前一阶段观察已保存并 ack，peer 才释放下一动作。尺寸变化由 UI 在 peer 已 armed 后调用公开 resize；静止动作阶段禁止未经安排的 NAWS 变化。

以下转义均表示实际字节，`+` 只表示串接，不发送加号。三段公共输入：

```text
R = "\x1b[?6l\x1b[?7h\x1b[r\x1b[0m\x1b[2J"
P = R + "\x1b[2;1H0123456789\x1b[3;1HANCHOR"
B = P + "\x1b[?7l\x1b[8;91Huv\x1b[?7h\x1b[10;91Hwx"
D = "\x1b[6n"
```

`B` 为既有 78B 正文，SHA-256 `c7dd4a7fa283dd0003ca8b2ae13dcb46f143a4717bf5a14eb5f5acb068a9fa1c`。下表“输入”均不含末尾统一追加的 D。所有空输入都是真的零额外字节，不补 CUP、重画正文或恢复模式。

## A：活动备用屏的正文保留与显示重排（3 阶段）

| 阶段 | 网格 | 输入 | 观察目的 |
|---|---|---|---|
| 0 | 91×20 | `"\x1b[?1049h" + B` | 在备用屏建立同一固定正文，先采 marker 与 DSR 基线 |
| 1 | 61×20 | 空 | 仍处于备用屏：文档正文是否保留、矩形是否重排；不推断跟主屏相同 |
| 2 | 91×20 | 空 | 仍处于备用屏：是否恢复正文和矩形，DSR 怎样变化 |

只关心活动备用屏；不在本 case 再切回主屏，不用已知主屏行为当通过条件。原版主/备用内部存储结构和完整 active grid 不在观测声明内。

## B：隐藏主屏缩窄后返回，区分正文、保存光标与 margin/origin（8 阶段）

主屏已知状态为 margin **2..5 / origin ON**，备用屏设置成 **3..7 / origin OFF**，以便区分恢复与状态串用。主屏长行在设置 margin 前写入；后续不再重设主屏 DECSTBM，避免把恢复错误修补掉。

| 阶段 | 网格 | 输入 | 观察目的 |
|---|---|---|---|
| 0 | 91×20 | `B + "\x1b[2;5r\x1b[?6h\x1b[2;10HMP"` | 主屏正文及进入备用屏前的精确 DSR 基线 |
| 1 | 91×20 | `"\x1b[?1049h\x1b[3;7r\x1b[?6l\x1b[2;10HAP"` | 明确相反备用屏状态；随后主屏保持隐藏 |
| 2 | 61×20 | 空 | 在备用屏完成尺寸变化，先采缩窄后活动帧基线 |
| 3 | 61×20 | `"\x1b[?1049l"` | **先采原样返回主屏**：正文 marker、矩形及 DSR，不添加 CUP 或打印标记 |
| 4 | 61×20 | `"\x1b[1;1HPS\x1b[99;1HBS"` | 在返回时原有 origin 下采上边界/饱和下界 marker 与 DSR |
| 5 | 61×20 | `"\x1b[?6h\x1b[1;1HPI\x1b[99;1HBI"` | 显式 ON 控制，保持当前 margin；上边界与饱和下界可观察 |
| 6 | 61×20 | `"\x1b[?6l\x1b[1;1HPO\x1b[99;1HBO"` | 显式 OFF 控制，仍不重设 margin，判别样本与控制是否可分 |
| 7 | 91×20 | 空 | 采恢复宽度后的剩余正文；此时 origin 已被控制动作设 OFF，不能冒称原样恢复状态 |

阶段 3 的 DSR 才是未经探针动作改动的保存光标观察；阶段 4–6 的光标均由各自明确 CUP 产生。通过 PS/BS 与 PI/BI、PO/BO 的位置和 DSR 共同判别：控制重合/不一致就保持 unknown。PI/BI 给出当前 margin 的可观察上/下界，不能只凭一条相对 DSR 宣称保存了 2..5，也不能据此证明所有滚动行为或私有模式位。阶段 7 仅复验仍存在的 `v/w/x` 与已记录的控制输出，不声称未扰动的 origin 再次恢复。

## C/D：尺寸改变前末列状态，开/关 DECAWM 分进程

只用 `P` 加两个独特 marker `W`、`X`。`W` 是尺寸变化前最后写入的可打印字符；随后只允许 DSR 和窗口动作，直到明确的 `X`。不用 CUP 清掉潜在 pending 状态后再称它仍存在。

### C：91 列末端 → 缩窄 → 下一字符（每个模式 4 阶段）

| 阶段 | 网格 | `pending-on-shrink` 输入 | `wrap-off-shrink` 输入 |
|---|---|---|---|
| 0 | 91×20 | `P + "\x1b[8;91HW"` | `P + "\x1b[?7l\x1b[8;91HW"` |
| 1 | 61×20 | 空 | 空 |
| 2 | 61×20 | `"X"` | `"X"` |
| 3 | 91×20 | 空 | 空 |

阶段 0 建立末列基线；阶段 1 为 resize 后、下一字符前的独立 DSR/正文基线；阶段 2 记录 W 保留/覆盖与 X 位置；阶段 3 观察增宽恢复。ON/OFF 都不预设“换行”“钳位”或“原地覆盖”的答案。

### D：61 列末端 → 增宽 → 下一字符（每个模式 5 阶段）

| 阶段 | 网格 | `pending-on-grow` 输入 | `wrap-off-grow` 输入 |
|---|---|---|---|
| 0 | 91×20 | `P` | `P + "\x1b[?7l"` |
| 1 | 61×20 | 空 | 空 |
| 2 | 61×20 | `"\x1b[8;61HW"` | `"\x1b[8;61HW"` |
| 3 | 91×20 | 空 | 空 |
| 4 | 91×20 | `"X"` | `"X"` |

阶段 2 是已缩窄尺寸中重新建立的末列基线；阶段 3 先观察增宽，阶段 4 才输出 X。本组独立 fresh 进程，不承接 C 的 X 或清理动作。这里调查旧 61 列末端在新 91 列下的行为，不把光标已经不在新末列视为测试夹具错误。

## 发布字段、停止与规模

- 每阶段分别保留：确切输入/DSR hex、source hash、NAWS/event 边界、实际窗口 inclusive QRect/size、完整协议回复、文档 marker 的存在数/行列/position、对应可见矩形及独立 caret API。marker 消失、重排或控制不可分首先是观察；不能为了满足预期重新填字或改窗口。无法唯一绑定的 marker 矩形记录 unknown，不选择一个近邻位置代替。
- DSR 与显示网格分层：尤其旧末列 91→新宽 61，**不得仅因 DSR 列大于 61 就拒绝记录**。安全数值上限可固定为行 1–200、列 1–500，不能把当前视口尺寸当成尚未证明的协议约束。逻辑行长度也不能用 61 截断，正文发布仍只允许本 case 输入导出的有限合成字母/数字/空白形式；不发布任意私有全文。
- 机械停止条件：身份/归档字节漂移、错误 case/phase、窗口或 NAWS 不符/行数不再为 20、未 armed 的尺寸变化、前阶段未 ack 就发送动作、缺少/重复/额外回复、快照不稳定、非预定终端输入、合成正文白名单失败、超时或 Job 无法回收。语义结果与假设不同不等于机械失败，保留结果另判读。白名单拒绝时保留 raw，离线修订需独立身份，禁止覆盖原失败或为导出重跑目标。
- 继续既有 41/42/43 秒内部期限、45 秒 REA 捕获、60 秒 CLI 等待，不延时取绿；这些不是复制/启动/清理整个生命周期的硬上限。主动终止后 Job 空不等于自然退出。每计划最多一次原版启动，不复用旧计划重试。
- 建议先执行 A、B、C-ON、C-OFF：**4 个进程、最多 19 个阶段**；D-ON/OFF 用独立新计划补增宽前的末列状态，再增加 **2 个进程、10 个阶段**。全部上限为 **6 个进程、29 个阶段**，不组合 DECOM×pending、主/备用×pending、颜色/宽字或更多边界。若前一组已出现改变设计的事实，先判读再准备后一组。

工具可复用 resize_address 的 public geometry/NAWS/phase-ack 框架和 mode_state 的可分辨控制，但以新文件/新源码包/新输出 schema 实现，不修改任何冻结执行源。本文件不生成计划或触发运行；工具实现后先做离线机械守卫，实际执行由执行方在已有授权范围内协调。

## 新工具的具体约束

CLI case名称依次为`alternate-active`、`hidden-primary`、`pending-on-shrink`、`wrap-off-shrink`、`pending-on-grow`、`wrap-off-grow`。前四项标记`initial-four`，后两项标记`deferred-grow`；后两项不是首批自动执行内容。每份计划单独绑定源归档与生成probe，最多一次attempt，失败不重放。

控制器由运行源中的不可变阶段表生成其case、label、window、grid与resize索引，作为模板替换内容纳入实际脚本hash。peer在每个阶段比较该阶段的网格，不能硬编码“动作一定在61列”或“最后一阶段一定resize”。每次观测保存后再ack；主屏原样返回与增宽后X之前的阶段都不可被跳过。

合成文本使用有限集合：固定原始词、MP/AP在零基列9的覆盖、PS/BS/PI/BI/PO/BO在列0的两字符覆盖，以及61/91边界的固定片段与尾空格。W/X只允许本case已发出阶段中的有限边界写入。没有任意字符正则或任意长度padding；marker全局数量不得超过源输入次数，尚未发送的控制词不允许提前出现。允许marker丢失、尾部裁掉、切换后看到相反帧的已知源词或控制不可分；不凭期望策略拒绝这些有界观察。

例如`PS23456789`、`PSCHOR   MP`是由已知源的前两字符覆盖生成的有限词；`X`加60个空格来自所声明61列写入范围。它们不是新增任意正文的发布许可。无法由冻结集合解释的结果保留raw并失败，离线后续修订需要独立来源身份，不能覆盖原失败或据此重跑目标。

## 离线验证入口

```powershell
python -B -X utf8 -m unittest research.ui.test_resize_frame_reference research.ui.test_export_resize_frame_reference -v
python -B -O -X utf8 -m unittest research.ui.test_resize_frame_reference research.ui.test_export_resize_frame_reference -v
node research/ui/test_resize_frame_stages.js research/ui/resize_frame_reference.js
```

Python用受控内存连接执行真实peer，Node执行实际控制器源码，均不启动原版、连接网络或操作原生UI。完整导出夹具使用5个无可执行内容的合成模块，只在测试命名空间替换身份和文件数量；生产导出仍固定4047文件。离线通过不代替原版行为或完整grid验收。

运行身份为本模块的10个`SOURCE_FILES`与严格`reference-source.json`归档；无manifest才使用真实Git身份，不mock Git。导出另记录3个`EXPORT_SOURCES`，并按case实际3/8/4/5阶段校验全部ack、两个resize握手、完整provider与逐阶段观察。协议DSR独立限制行1…200、列1…500，61列视图中的DSR91合法；不据此扩大输入或输出尺寸。
