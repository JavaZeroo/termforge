# 保留原生与末端 margin 合同的实现路径

> 后续（2026-10-29）：本页的“候选仅可应用”状态已被真实构建与 20 次同源运行取代，路径结论与维护方案见 [conpty-margin-path-decision.md](conpty-margin-path-decision.md)。

[#56](https://github.com/JavaZeroo/termforge/issues/56) 的当前交付是可执行的逐字段原生 oracle、固定上游的补丁候选及下一次构建入口。
**原生缺口仍存在，尚无经过真实运行证明的完整修复路径。** 补丁仅检查可应用，没有编译或替换任何后端。
这保留 [#41](https://github.com/JavaZeroo/termforge/issues/41) 的原合同与开放状态，不改变 #34 产品决定。

## 逐字段合同与测量边界

Microsoft [每缓冲区 scrolling margins 声明](https://learn.microsoft.com/en-us/windows/console/console-virtual-terminal-sequences#scrolling-margins)
要求主/备用屏分别持有区域。`CHAR_INFO` 同时包含字符和属性；
[ReadConsoleOutput](https://learn.microsoft.com/en-us/windows/console/readconsoleoutput) 读的是调用应用所在控制台的矩形，
[GetConsoleScreenBufferInfo](https://learn.microsoft.com/en-us/windows/console/getconsolescreenbufferinfo) 给出该控制台的网格、窗口和光标。
这些 API 没有直接返回 DECSTBM 状态的字段，必须以区域内实际滚动和区域外保持来验证；末端正确无法代替调用应用的原生状态。

| 必须保留的判据 | 原 175B 能证明什么 | 必须另做的 R2 交叉 |
|---|---|---|
| 主屏 `2;3r`，备用屏 `4;5r` 分属各屏 | 四阶段精确文字与逐格 `CHAR_INFO`；返回主屏继续滚动，区域外 `TOP_FIXED` / `BOTTOM_FIXED` 保持 | 重复切换、默认区域、origin/wrap/pending-wrap 与保存恢复 |
| 主屏返回与光标 | `restored` 文字/属性与 `before` 一致；下表固定光标逐项成立 | 非零起点、光标可见性/形状、应用查询与 API 写入交叉 |
| 属性保留 | 原 source 不设置 SGR；所有格保持初始捕获属性，包括滚动空白格。当前实际初始值为 7 | 明确 SGR/色彩、erase 属性、主/备用屏样式恢复；本夹具不能证明这些变体 |
| 宽度与边界 | `80×24`、`12×7`；窄网格结束标记分成两行，不裁剪尾部取得通过 | 宽字符、极窄/极短、不断 resize、区域边界随尺寸变化 |
| 历史与末端 | 原运输完整保留供独立候选回放；源 API 与末端分别判据 | 主/备用屏历史隔离与淘汰、resize/reflow/选择/Find；原 175B 无足够历史压力 |
| 生命周期 | 原 5 秒 child wait、15 秒外部期限、共享 5 秒 cleanup；自然退出 37、EOF 109、自有内外 Job0、无 force | 最终 Session/GUI/应用矩阵及长期资源 |

原 source SHA-256 为 `01878a5d54c343007309abb89faeb90c89262a8b55e420c5f267342ed9b27e59`，175 字节不变。
新增 [独立 oracle](../../experiments/conpty/margin_contract.py) 直接使用以下固定结果，不调用 TermForge parser 推导预期。
行/列均从零计；未列行为空，`12×7` 的最后标记分为 `TF_MARGIN_DO` / `NE` 两行：

| 阶段 | 非空行与位置 | 光标 `(row,column)` |
|---|---|---|
| before | 0 `TOP_FIXED`；1 `REGION_A`；2 `REGION_B`；3 `BOTTOM_FIXED` | `(0,0)` |
| alt | 0 `ALT_OUTSIDE`；3 `ALT_C`；4 `ALT_D` | `(4,5)` |
| restored | 与 before 相同 | `(0,0)` |
| final | 0 `TOP_FIXED`；1 `REGION_C`；2 `REGION_D`；3 `BOTTOM_FIXED`；5 起结束标记 | `(6,0)` |

oracle 同时比较两个原生读入口的文字、完整格/属性和光标。初始颜色由原夹具外部默认决定，因此只验证其跨阶段保持，
不把某次默认颜色推为所有宿主的硬合同。历史、resize、改变样式和末端各阶段光标仍明确列为未测。

## 本批固定运行与失败保留

[白名单证据](../../experiments/conpty/results/2026-10-26-native-margin-path/validation.json) 固定 main `3d6d44d` 的未修改原生探针、
单独冻结的候选 `.4` trace、官方同包 DLL/EXE 双 SHA、源码、flags、期限与生命周期；原件只留 `local/`。
这是源头诊断配置，**不是 #55 的联合产品验收**。本批官方探针 flags 为 0，不能替代 Draft 产品的 Console-width flags 24。

| 样本 | 原生与末端结果 | 生命周期 |
|---|---|---|
| 系统 `80×24` / `12×7` VT-on | 原生 before/alt/restored 的全部检查正确，final 文字/格错误而光标正确；末端同样错误 | 均自然 37、EOF109、Job0，无 force |
| 官方 1.25 `12×7` VT-on 补充批 | 原生 final 仍为 `REGION_CIXED` 等错误；末端正确 | 自然 37、EOF109、Job0，无 force |
| 官方 `80×24` VT-on 初始及有界补充各一次 | 两次均未产生四阶段状态，语义结果 unknown | 原 5 秒 wait=false；close 后观察到 `0xC000013A`，不能冒充自然退出；EOF109、外 Job0，无 force |

系统的 VT-off 两个原反例也随原运行入口保留，不计 VT-on 合同成功。
首次误选自研 C++ trace 导致 replay 拒绝的配置错误批原样私存并排除；后续补充与原失败分别列出，没有删除失败或延长预算取绿。
官方 80 列生命周期失败的原因尚未定位，不能据此推出每屏 margin 的原因。

## 固定上游的可证伪原因

核对对应官方包的提交 `dc8ae365847d41f62d1ebca93815bb00959bc12f`：

- [`SCREEN_INFORMATION::_CreateAltBuffer`](https://github.com/microsoft/terminal/blob/dc8ae365847d41f62d1ebca93815bb00959bc12f/src/host/screenInfo.cpp#L1787)
  删除新备用屏自己的 state machine，再共享主屏 `_stateMachine`。
- 同一 [`AdaptDispatch`](https://github.com/microsoft/terminal/blob/dc8ae365847d41f62d1ebca93815bb00959bc12f/src/terminal/adapter/adaptDispatch.hpp#L321)
  有两个保存光标状态，但只有一个 `_scrollMargins`；
  [`_SetAlternateScreenBufferMode`](https://github.com/microsoft/terminal/blob/dc8ae365847d41f62d1ebca93815bb00959bc12f/src/terminal/adapter/adaptDispatch.cpp#L1741)
  未保存、重置、恢复该字段，DECSTBM 则写这个字段。
- [原生 GetSet](https://github.com/microsoft/terminal/blob/dc8ae365847d41f62d1ebca93815bb00959bc12f/src/host/outputStream.cpp#L198)
  负责实际 buffer 切换；原生四阶段与末端可处在不同的状态实现中。

**推断：** 备用屏写入 `4;5r` 后，共享字段继续影响恢复的主屏，能解释已见 final 损坏。
这是固定源码和运行现象共同支持的待证伪原因，尚缺仅改变该状态归属的真实 red→green。
Microsoft 文档仍声明 per-buffer；目前没有上游确认允许把这种差异当成设计豁免，也没有证据证明其他上游版本已修复。

## 路径比较与具体下一步

| 路径 | 当前证据与边界 | 维护和采用条件 |
|---|---|---|
| 上游修复后固定正式包 | 最优先的长期分发方式；当前官方包 native 仍红，尚无通过的替代包 | 将独立原 fixture/oracle 和最小修复送审；上游接纳与发布仍未知，采用前按完整合同复验 |
| 小型 MIT fork | [候选 patch 与上游测试](../../experiments/conpty/upstream/README.md) 保存隐藏主屏 margin，新备用屏默认区域，返回恢复；只验证可以应用到完整固定文件 | 未编译/未运行，不得结项。隔离构建 adapter tests、OpenConsole 与配套 DLL；新来源清单/双文件 SHA、显式配置、同 API 族/模块生命期、原矩阵及负例通过后才讨论整合 |
| 直接修改 TermForge 末端或 VT-off | 末端不能修改子进程已见的 `CHAR_INFO`；VT-off 的原生文字和窄屏反例已否定其作为一般 raw 后端 | 无原生合同修复证据，不作为通过路径；不猜补原运输没有的控制序列 |
| 替换为另一 console server/桥接层 | 尚无同输入的原生与末端合格证据；重新实现 Console API/ConDrv/VT 同步的维护面远大于局部上游状态修正 | 必须先证明支持 API/Windows 边界、许可、部署与回收，不能只根据库存在选择 |

构建入口、原源 SHA、许可、API 生命期、负例和升级维护步骤在 [patch README](../../experiments/conpty/upstream/README.md) 固定。
下次先完成上游 adapter 测试和独立同源 DLL/EXE 构建，再为新 fork 身份增加审查入口；当前官方身份校验保持拒绝未知文件。
运行时先保持原175B、四阶段和期限，检查原生、运输与候选各层 red→green，再执行样式、历史与 resize 交叉。

公开 API 可用与产品支持范围分开：系统 CreatePseudoConsole 文档的 Windows 10 1809 下限，
不能证明新 DLL/EXE 或其 fork 在全部该范围可运行；固定上游 README 的 Windows Terminal 19041 要求也不能直接作为 ConPTY 包下限。
本次只有记录的 Windows 11 本机结果；完整最低系统、服务器和更新策略仍需 #34 定案及实际验证。

因此 #56、#41、#27、#29、#34 继续开放；#47/#55 的产品整合必须使用最终联合源码和实际后端重验，
本批不声明真实显示、长期资源、WindTerm 运行等价或性能结论。

## 复验入口

先沿 [原监督器](conpty-margin-backends.md#复现与剩余工作) 产生全新私有目录，再离线执行原生 oracle：

```powershell
python -m experiments.conpty.margin_contract <new-run>/_private/80x24-vt-on --columns 80 --rows 24
python -m unittest experiments.conpty.test_margin_contract -v
```

当前 native final 错误时第一条返回 1，这是保留的反例。纯测试包含旧 `REGION_CIXED`、逐阶段属性、光标、
第二读入口和缺失状态损坏的拒绝检查；它们不调用真实控制台，也不能替代 fork 运行验证。
