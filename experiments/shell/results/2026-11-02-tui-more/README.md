# 2026-11-02-tui-more · More 分页器产品配置冒烟

冻结 Windows 11 x64 下系统 More.com 的第一批应用身份与操作轨迹，并在当前产品配置（自主 C++ 核心 strict/recovery + 固定 ConPTY 1.25）下逐阶段回放核对预期字节。轨迹与独立协议预期见 docs/research/tui-more-trajectory-2026-11-02.md。

## 结论摘要

- 固定 ConPTY 1.25：进入（411 字节）与退出（810 字节）经 strict 与 recovery 两路回放均通过（exit 0，屏幕正确），且 4 次捕获确定一致。
- 系统 ConPTY：进入与退出 recovery 通过、strict 以 unsupported escape 失败于 OSC 标题；进入字节非确定（360 变体 B / 375 变体 A 两种，各含 ECH 70 或 CUF 79）。
- 逐项协议核对：DA1（ESC[c）出现在固定 ConPTY 启动包（strict 回复 ESC[?6c、recovery 静默）；DSR 6n、1049、滚动区在 More 轨迹中均未出现；ED2 只在系统 ConPTY 出现，固定 ConPTY 用空格加 CR 清屏。

## 逐条结果

| 检查 | 后端 | 模式 | 结果 | 屏幕/错误 |
|---|---|---|---|---|
| fixed-enter | 固定 1.25 | recovery | 通过 | TF_PAGE_23 + -- More (56%) -- |
| fixed-enter | 固定 1.25 | strict | 通过 | 同上 |
| fixed-quit | 固定 1.25 | recovery | 通过 | TF_PAGE_40 + 空行，正文进历史 |
| fixed-quit | 固定 1.25 | strict | 通过 | 同上 |
| system-enter 变体 A/B | 系统 | recovery | 通过 | TF_PAGE_23 + 提示 |
| system-enter 变体 A/B | 系统 | strict | 失败 | unsupported escape（OSC 标题） |
| system-quit | 系统 | recovery | 通过 | TF_PAGE_40 + 空行 |
| system-quit | 系统 | strict | 失败 | unsupported escape（OSC 标题） |

机器可读记录见 validation.json。

## 承载缺口

本切片用现有 session.hpp 探针驱动 More（未提交的 local/more_traj_probe.cpp），核心回放用已构建的 recovery_probe 与 replay。未搭新骨架；固定 ConPTY 捕获复用 third_party/conpty/manifest.json 的固定文件对。系统 conhost 双变体根因、交互键（= 、F、P<n>/S<n>）、非文本输入与其他语言提示未覆盖。
