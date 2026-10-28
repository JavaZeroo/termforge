# 完整历史显示与源坐标检查（#29）

混宽缓存历史曾把不同长度的行交给固定宽度呈现器；仅把页面裁成矩形会丢失源文字，并使选择和 Find 使用错误坐标。现在保留完整源文档，对混宽历史软链建立临时显示行和双向来源映射，鼠标、选区/查找高亮及阅读位置通过源坐标连接。硬边界、空行、宽字和活动网格分别遵守[候选合同](../../CANDIDATE.md#历史显示与源坐标)。

这是候选实验的局部修复，**#29 与 R2 仍未验收**。原版未知行为、重要差异、性能容差和最终选型仍按 ADR-0006 处理。

## 固定源码与结果

环境为 Windows 11 x64（10.0.26200）、Release、显式 Alacritty `termforge.10`、系统 ConPTY。不是默认核心或实验官方 ConPTY 后端的联合结果。

| 源码 | 检查与结果 | 证据边界 |
|---|---|---|
| `1fafb999d6e031ac7206e499062f7e1b2f1a53ff` | 完整历史投影、源映射与 Find；旧 history 和原 681 高度 selection 场景各运行一次，均退出 0、无超时，Job active=0、desktop 已回收 | 原场景/期限保持，selection 的 system_clipboard=false；两次单次成功不反填旧失败根因 |
| `f41f747be27ad7b1de965b2279948eca7473cc0e` | 补空历史行阅读锚点与 Snapshot 形状门禁；最终完整 CTest **57/57**，退出 0，308.33 秒 | 内部 **9 项跳过**：8 项实际系统剪贴板、1 项未启用的原版失败注入；套件耗时不是终端性能测量 |

[validation.json](validation.json)记录源码/运行时哈希、分次断言、跳过项和本地日志索引。最终构建冻结的 208 项文件在终态后全部重哈希匹配。此前 Rust 205 项与 runner 6/6 属于[来源接入检查点](../2026-10-28-source-ownership/README.md)，本轮没有重跑或修改 Rust，不拼成另一配置的整体验收。

## 反例与验证范围

| 问题 | 实际失败依据 | 修复后的检查 |
|---|---|---|
| 非矩形历史页面 | 公开快照行宽与屏幕列数不一致，运行退出 1 | 保留完整源行，输出固定宽度页；初次相关检查 6/6 |
| 选择来源映射 | 新接口不存在导致编译红；不是既有运行错误的反例 | 显示 EFGH 对应源列 4–8；跨片段正反向选择 CDEF 无额外换行，硬边界保留 GH/CRLF/B |
| 宽字旧自动折行空隙 | 修正判据后公开 `project_line` 运行红 | 跳过旧 leading spacer，保留宽字原子性；单列显示仍映回两格源范围 |
| Find 丢失源列 | 旧 line-ID reveal 显示长源行开头，运行红 | 按源行与列定位；实际核心 resize/repaint 4→8→4 保持阅读位置和选择 |
| 空历史行锚点 | 空硬行滚动时被跳到活动区，运行红 | 区分无候选与有效末行，空行页首及源行末端 reveal 保持 |
| 非矩形页进入呈现器 | 公共 Snapshot 构造接受畸形页，运行红 | 两个入口统一拒绝；受控 renderer 32 个 DPI/zoom/backend 场景通过 |

旧宽字检查使用 ASCII helper，误把 wide trailer 当空格，失败受判据干扰；保留原日志但不作为因果依据。真正依据是 `wide-corrected-red` 与随后通过。部分中间 green 只有固定日志，没有逐次独立构建 manifest，不冒充最终 Git blob 的验证。

Standards：本轮 `8521cd7..f41f747` 最终新增硬问题 0、实质代码异味 0，保留布局重建与高命中量 Find 的未测成本。Spec：空锚点 P2 和形状缺口经实际红→绿及独立复核关闭，新增未解决问题 0。审查结果来自独立代理消息的根汇总；更早来源组合的维护建议及各次审查保留在原检查点。

## 仍未完成

- 最终源码的 `--source-partial-visible` 仍得到 `B\r\nABCDEFGH\r\n\r\nC\r\nD`，EFGH 只有一个命中，但 B 仍排在 A 前；该观察模式返回 0 不表示比较通过。显示投影没有修复源顺序。
- 混合原生行的部分来源、多旧行合并及跨历史/活动区软链尚未完整覆盖；不得按文字去重或永久裁剪缓存掩盖。
- 没有执行独立的“只改显示宽度、不调用核心 resize”实验；实际 resize 往返不能代替它。原版 Unicode、交互与普遍覆盖政策仍需取证，本轮没有新增原版运行。
- 源映射、布局重建、逐字素页映射及高命中量 Find 的成本未测；没有原版性能等价、数值容差或最终核心选型结论。
- 实际系统剪贴板、不同 DPI、完整 child/模式交叉和实验官方后端分别验收。受控 DIB/renderer 结果不等于实际呈现，`presented_done=null`。

旧 `eea3a2c` / `95cd2c3` 的 52/53、history retained/full 差异及 selection 的不同退出路径继续保留在[原证据](../2026-10-28-source-ownership/README.md)。新运行通过不证明这些历史事件只有同一个原因。

## 复现与公开边界

按[候选构建入口](../../CANDIDATE.md#构建与验证)生成 Windows Release candidate/system 配置，在测试进程 PATH 提供既有 Vim，设置 `PYTHONUTF8=1` 后运行：

```powershell
ctest --test-dir build/candidate-adapter -C Release --output-on-failure
```

局部入口为 `candidate_history_viewport_projection`、`candidate_history_viewport_text`、`candidate_history_viewport_wide`、`candidate_history_viewport_find`；命令只说明如何复验，不表示在其他宿主已通过。未启用要求隔离 hosted VM 的实际系统剪贴板门槛。

公开内容只含独立代码、合成断言、汇总和哈希。本地 proof 路径是取证索引，不是公共附件；原始日志、GUI/frame/layout、进程/desktop 标识、私有路径及 WindTerm 发行物不随本记录发布。
