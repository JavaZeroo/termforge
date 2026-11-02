# 预置宽屏 L 的独立 EL0 / EL2 观察协议

本协议由 [运行工具](resize_erase_reference.py)、[控制器](resize_erase_reference.js)和[严格导出器](export_resize_erase_reference.py)实现，限于 #27/#29 的缩宽后擦除范围。它与 [原 edit 协议](resize_edit_reference.md)使用不同源码、schema、初始输入、计划和输出目录；不是对旧失败计划的重放，也不是原版运行结果或产品验收。

原 edit 协议在缩至61列后写入左侧 L，准备动作本身可能先改变旧尾部，无法继续把后续差异归因于EL。本协议把L放到初始91列正文中，缩宽后的准备只发CUP，不再打印任何字符。旧工具、失败原件和冻结身份均保持不变。

## 新初始输入：85 B，不是原样的78 B

在既有78B正文的唯一 `CSI ?7 l + CUP(8,91)` 前插入 `CUP(8,1) + L`。插入段 hex 为 `1b5b383b31484c`，长7B；随后旧 `uv` 和 `wx` 顺序不变，最后可打印输入仍是旧 `wx`，不补光标修正。初始DSR如实采集，不从文字位置生成。

| 身份 | 字节数 | SHA256 |
|---|---:|---|
| 旧 B 正文 | 78 | `c7dd4a7fa283dd0003ca8b2ae13dcb46f143a4717bf5a14eb5f5acb068a9fa1c` |
| 插入段 `CUP(8,1) + L` | 7 | `a83c0170f70a7531847866708b8eaaf4adbb0e0303cd6607d7e13e302ee458db` |
| 新初始正文 | 85 | `a301c656926ae72c07d2c91b67e1734286ce50b7cb38c5b4f6b64f06db0563b3` |

新正文的完整 hex：

```text
1b5b3f366c1b5b3f37681b5b721b5b306d1b5b324a1b5b323b3148303132333435363738391b5b333b3148414e43484f521b5b383b31484c1b5b3f376c1b5b383b39314875761b5b3f37681b5b31303b3931487778
```

计划与公开导出均记录 `initial_body_derivation`：原源字节数/hash、插入位置hex、插入段字节数/hex/hash，以及新源字节数/hex/hash。阶段0也独立携带新85B的身份，禁止把它写成“同一个78B工作负载”。

## 两个 fresh case，各五阶段

`erase-right` 使用 EL0；`erase-whole-line` 使用 EL2。每项使用独立新进程、新计划、新输出目录，每计划最多一次attempt，共最多2进程、10阶段。每阶段统一追加一次DSR `1b5b366e`，下表输入不包含这4B。

| 阶段 | 窗口 / 必须观察到的网格 | 输入 | 目的 |
|---|---|---|---|
| 0 | 1360×749 / 91×20 | 新85B正文 | 在宽屏建立L与旧尾部的初始观察 |
| 1 | 1000×749 / 61×20 | 空 | 独立采缩宽后、CUP前的正文与DSR |
| 2 | 1000×749 / 61×20 | `1b5b383b363148`，仅CUP(8,61) | 独立采擦除前的定位控制 |
| 3 | 1000×749 / 61×20 | EL0 `1b5b304b` 或 EL2 `1b5b324b` | 采L、v及其他固定marker的留删、位置与矩形 |
| 4 | 1360×749 / 91×20 | 空 | 恢复宽度，只观察，不补画或打印marker |

阶段2先保存观察，再检查DSR为 `(8,61)`，且正文逐行等于阶段1。只有通过才写ack、放行EL。**该检查不要求L或v仍然存在**；如果缩宽本身已改变尾部，阶段1和2仍可一致并继续。若CUP本身改变正文或定位控制不成立，保留包含第三观察的失败原件，不发送EL，不重放计划。

阶段3不预设EL0/EL2应保留或删除L/v。分别记录两者皆在、只剩L、只剩v、两者皆无，以及源位置、矩形、DSR和增宽后的变化；不同结果不能自动升级成完整原版匹配或内部策略证明。CUP使用一基坐标，新右界为零基列60；旧保留尾部零基列61…90与其区分。

## 有界导出与机械守卫

原文候选仅由新85B中的固定合成行产生。初始阶段允许明确的91列尾空格；缩宽阶段再允许61/91边界片段及相应尾空格。EL后只允许对这些有限源字符串的一个有限连续区间填空格，保留显式尾空格与截尾空格形式，再作固定边界投影。该集合包括61个空格、`L`加60/90个空格、保留或清除v，以及软后继x被局部清除得到的90空格加`w `。它不是任意字符正则、任意正文或无限padding。

L/v/w/x、ANCHOR与各原数字的数量不能超过固定源次数；私有文本、新marker、重复marker、超界padding以及非有限源派生字符串均拒绝。未预期结果保留raw，另行调查和冻结修订，不覆盖原失败或为导出重跑目标。公开输出只保留固定输入、DSR、marker/矩形/网格标量、正文hash、来源及原件hash，不发布原始全文、profile、路径、PID或资源。

执行前后绑定10个运行源、真实source archive、固定WindTerm/Qt/REA/Node/Python身份；严格导出另记录3个导出源，校验4047文件分发与执行副本、合成profile、实际probe字节、完整provider、5个ack和两个resize握手。旧计划不可复用，输出必须为新的工作树`local/`目录；成功或失败均保留`.attempted`。

只有阶段1/4允许resize；初始和resize NAWS连续稳定600ms，观察快照稳定400ms。错误case/阶段/来源、错窗口/网格、行数变化、未armed的NAWS变化、过期握手、缺ack、额外/重复回复和限界失败均停止。DSR的安全上限独立为行1…200、列1…500，不用61列视口作为其数值上限。沿用41/42/43秒内部期限、45秒REA及60秒CLI；这些不是整个复制/启动/清理生命周期的硬上限。自有Job主动结束不证明自然GUI退出。

## 离线验证入口

```powershell
python -B -X utf8 -m unittest research.ui.test_resize_erase_reference research.ui.test_export_resize_erase_reference -v
python -B -O -X utf8 -m unittest research.ui.test_resize_erase_reference research.ui.test_export_resize_erase_reference -v
node research/ui/test_resize_erase_stages.js research/ui/resize_erase_reference.js
```

Python用受控内存连接执行真实peer，Node执行实际控制器源码；不启动原版、连接网络或操作原生UI。完整导出夹具用5个无可执行内容的合成模块，只在测试命名空间替换身份/数量；生产仍要求4047文件。测试通过仅验证协议与守卫，不是本组原版成功次数或R2验收。
