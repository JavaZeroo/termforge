# 2026-11-02-tui-vim · Vim for Windows 产品配置冒烟

冻结 Windows 11 x64 下固定 Vim for Windows（Git for Windows 自带 9.1 patches 1–532）的应用身份与操作轨迹，并在当前产品配置（自主 C++ 核心 strict/recovery + 固定 ConPTY 1.25）下逐阶段回放核对预期字节。轨迹与独立协议预期见 docs/research/tui-vim-trajectory-2026-11-02.md。

## 结论摘要

- 固定 ConPTY 1.25：Vim 80×24 全轨迹（进入 1049h → 插入 → 保存退出 1049l → 主屏恢复）2424 字节，3 次捕获逐字节一致；60×12 变体 1180 字节同构。
- recovery 回放：进入/插入/保存/退出/主屏恢复全程通过（exit 0，进入后备用屏显示编辑器、退出后主屏恢复空屏、历史 0）。忽略 19 条序列。
- strict 回放：在偏移 31 的 ESC[22;0;0t（XTWINOPS 保存标题，3 参数）失败，错误 too many CSI parameters。根因在 terminal.hpp:81-82 的参数门限（非 H/f/r 终字节的 CSI 限 1 参数）早于 terminal.hpp:100-108 的 t 分支。
- 逐项协议核对：1049 备用屏进出、DA1、DSR 6n、DECSTBM、ED2 均在轨迹出现且核心已处理；DA2、括号粘贴、DCS 同步、多参数 XTWINOPS 标题未实现（recovery 忽略、strict 失败）。

## 逐条结果

| 检查 | 尺寸 | 模式 | 结果 | 屏幕/错误 |
|---|---|---|---|---|
| vim-enter | 80×24 | recovery | 通过 | 备用屏 22 行波浪号 + [New] |
| vim-exit | 80×24 | recovery | 通过 | 主屏 24 行空，历史 0 |
| vim-full | 80×24 | recovery | 通过 | 主屏恢复空 |
| vim-full | 80×24 | strict | 失败 | too many CSI parameters（偏移 31） |
| vim-enter | 60×12 | recovery | 通过 | 备用屏 10 行波浪号 + [New] |
| vim-exit | 60×12 | recovery | 通过 | 主屏 12 行空 |
| vim-full | 60×12 | strict | 失败 | 同上 |

机器可读记录见 validation.json；冻结样本见 experiments/core/fixtures/vim-fixed-80x24-trajectory.bin 与 vim-fixed-60x12-trajectory.bin。

## 与 More 切片（PR #306）的差别

More 固定 ConPTY 进入只含 ConPTY 启动包加正文，strict 双通；Vim 是全屏 TUI，额外携带 1049、DA1/DA2、DSR 6n、DECSTBM、括号粘贴、DCS 同步与多参数 XTWINOPS 标题，strict 尚不能承载。这标注了固定路线对全屏 TUI 的支持边界，不构成 #28 整体验收。

## 承载缺口

本切片用未提交探针 local/vim_traj_probe.cpp 驱动 Vim 记录 stage-enter/insert/text/exit，核心回放用已构建 recovery_probe 与 replay。未搭新骨架；固定 ConPTY 捕获复用 third_party/conpty/manifest.json 的固定文件对。原版 WindTerm 的 60×3000 缓冲区/60×12 窗口与 ConPTY 60×12 的映射、#52/#56 的 resize 中途丢输入、strict 应否实现多参数 XTWINOPS t 与 DA2/括号粘贴/DCS 回复均未决定。
