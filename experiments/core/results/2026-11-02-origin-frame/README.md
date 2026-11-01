# #27：依据两个原版新会话保存每屏 origin

自主 C++ 核心将 DECOM origin 从共享位改为随 `FrameState` 保存：首次创建备用屏时为关闭，返回主屏时恢复主屏原值。依据是固定 WindTerm 2.7.0、91×20、两次独立新会话的相反前态观察；此前共享 origin 的工程选择不符合这两组结果。

本次基点仍为 `f72c5c4f68dd7eb579ad1e523762333b135aa074`，叠加前序 #27 修复与本项修改。前序 [mode-cross](../2026-11-02-r27-mode-cross/README.md) 的 5 项、2 项和首次 15 项结果保留各自身份，不把它们改写为已经验证本次 origin 实现。

## 原版事实与限定回放

原版两 case 已有[公开归档](../../../../research/ui/results/2026-11-02-mode-state-reference/README.md)：[main ON / alt OFF](../../../../research/ui/results/2026-11-02-mode-state-reference/main-on-alt-off.json) 与 [main OFF / alt ON](../../../../research/ui/results/2026-11-02-mode-state-reference/main-off-alt-on.json)。此次实现直接核对的本地原件为 `local/r27-vm/mode-on-ui.json` 与 `mode-off-ui.json`，原件及公开导出的 SHA-256 记入 [validation.json](validation.json)。原件含直接 DSR 与公开文本/marker 位置；不读取或推断内部字段偏移。补充公开链接没有改变下面 31 组测试和 15 项核心结果的旧源码身份；后续重复进入调查及修复独立记录。

| 新会话条件 | 首次备用屏未改 origin 的样本 | 返回主屏未改 origin 的样本 |
|---|---|---|
| main ON → alternate OFF | `3;7r` 后 CUP(99,1) 回复 `(20,1)`；AS 位于物理行 0。显式 ON 对照为 `(5,1)`、AI 行 2；OFF 对照为 `(20,1)`、AO 行 0 | 回复 `(4,1)`，PS 位于行 1；与 ON 对照一致 |
| main OFF → alternate ON | 同样为 `(20,1)`、AS 行 0，并有相同 ON/OFF 对照 | 回复 `(20,1)`，PS 位于行 0；与 OFF 对照一致 |

两组返回主屏后，开启 origin 并 CUP(99,1) 均得到 `(4,1)`；下一次 CRLF + `NW` 得到 `(4,3)`、NW 物理行 4，R01、R06–R08、SB 哨兵保持。它们支持本次 `2;5r` 主屏 margin 下界恢复。物理行使用零基坐标，DSR 使用一基坐标；原版文本 API 的混合文档行号没有直接搬入自研帧模型。

[decom_origin_test.cpp](../../decom_origin_test.cpp) 固定两组各 14 段合成输入、14 条字面 DSR，以及 MP/AS/AI/AO/AP/PS/PI/PO/NW 的限定位置。strict/recovery × 整段/逐字节共 8 次累计回放、112 个阶段检查。这里是 28 个独立原版阶段的四种核心输入方式，不能称为 112 个新增原版场景。

## 最小实现与验证

- `FrameState` 新增默认关闭的 origin；`swap` 与 `active_state_copy` 同步保存，使创建、切换和既有 resize 副本路径使用同一状态归属。
- 本记录对应的实现当时没有为重复 `1049h` 追加 origin 重置，保持现有当前帧路径；当时的原版重复进入及退出后再次进入语义仍未知，不能从首次进入推导。随后[独立 reentry 原版样本](../../../../research/ui/results/2026-11-02-mode-state-reference/alternate-origin-reentry.json)已取得结果，后续修复与 35 组测试另记，不改写这里 31 组测试的旧身份。
- RIS 保持先前“关闭 origin”的工程效果：当前帧及保存帧都关闭，避免切回主屏时意外恢复旧 ON。没有改写 RIS 清屏、margin 或 buffer 策略；备用屏 RIS 的完整原版语义仍未知，其回归不计原版匹配。

真实 RED 先于生产修改：编译成功，`decom_origin_core` 的既有 23 组通过，新增 8 组全失败，即 23/31、CTest exit 8。A 的首次备用屏及两组返回主屏的 DSR/marker 均与新字面 oracle 不符。

最小修复后的首个单项 GREEN 为 31/31、CTest 1/1、exit 0、0.20 秒。随后补上 RIS 跨帧关闭 origin 的既有政策防回归，并重新构建 15 个核心目标；最终 15/15、exit 0、0.69 秒。构建保留 `edit_csi_test.cpp` 原有 C4456 同名局部变量警告，未修改该文件。日志在 `local/r27-history-wrap/origin-*.log`；最终源码/二进制及日志身份独立记录。运行耗时不构成性能对比。

这只完成 origin 的限定原版行为修复和核心回归。完整属性、重复进入、resize/reflow、Qt/固定 ConPTY、真实 TUI、原生动作、系统剪贴板与性能仍各自验收；本记录不关闭整个 #27 或 R2。
