# Vim/MSYS resize 输入前缀修补实验

固定公开运行库源码的最小补丁，使隔离环境中的原 GUI Vim 方法连续两批、每批两轮通过；同条件未修补的重建 DLL 首轮复现 stage59。这支持此前捕获的“SIGWINCH 解锁期间旧 Peek 前缀失效”解释。**[#52](https://github.com/JavaZeroo/termforge/issues/52) 的原固定运行库验收仍未完成**：本次没有修改 TermForge 产品、测试或正式兼容策略，也没有把实验 DLL 放入产品包。

## 固定身份与实验边界

TermForge 基点 `ed55a8758423b3273fbd98f07e0bfd77d88e46ac`，产品代码与 `c662011` 相同；候选 `0.26.1-dev+termforge.5`、官方 ConPTY `1.25.260930003`、flags24。所有 GUI 样本使用同一个无插桩 EXE，SHA-256 `7f26395942f107297565f50402568650e1e17c5eb23924bee1bfb02c50c60d16`。本机 Windows 11 x64，批次完成后查询系统版本为 `10.0.26200`；没有发布主机名。

固定 Vim 9.1 patches 1–532，EXE SHA-256 `2e0fda63a89171adfc46c9417d5105cc39ed778cc69a890b83a4eabc34e7646a`。Vim 与四个相邻 DLL 从既有安装按哈希复制到隔离目录；各变体使用同一个活动路径，只有 `msys-2.0.dll` 切换，其他四个文件保持原字节。没有复制 Vim runtime 配置目录。每次切换前检查上一方法的自有 Job 已清空、私有桌面已释放；批次后复核既有安装的五个文件未变。

| 运行库变体 | SHA-256 | 实际结果 |
|---|---|---|
| 隔离原 DLL | `3096d2ba9be20e90be1399edd391bac65d84bc387f5a419b502c9b326081930f` | 一次原两轮方法：先通过、后 stage59 |
| 固定源码未修补重建 | `d9e490fdbe31c233eab6578168fb4f2bda84f53252be3898d0cdd0e8610db721` | 首轮 stage59，立即停止 |
| 同条件修补重建 | `dd792766fa976b031cbff0540744b6f3d2b3fc0d695ed764a01457b65eab6258` | 两次原两轮方法：四轮通过，达到预定上限后停止 |

本批未附加 Frida，也未采集实际加载模块路径或新的输入 API 轨迹。文件身份和隔离路径是本批证据；此前的调用边界证据仍属于独立的[输入边界诊断](vim-msys-input-boundary.md)，不能混写成这批运行的调用记录。

## 原方法、停止条件与结果

直接调用现有 `CandidateTuiTests.test_real_fullscreen_editor_and_cursor_reply`，每次方法原有两轮，保留原输入、Vim 脚本、事件文件、精确断言及 25 秒内层 / 40 秒每轮外层期限。先限定原 DLL 一次方法；未修重建 DLL 最多两次方法，首次 stage59 或其他失败立即停止；只有重建基点出现原 stage59 才执行修补变体，最多两次方法，任何失败即停。实际共四次方法、七个 GUI 样本，没有额外 GUI 重试。

两个失败均为原 stage59，外层未超时，命令进入插入内容；`candidate-insert-log.txt` 均为相同的 221 B，resize 文件为空。两个失败的 `candidate-input.bin` 也为空、未完成刷新，**不能将其视为完整输入轨迹或“未收到输入”的证据**。原测试输入未改由源码身份保证，实际 API 字节链路只由前一诊断批次证明。

四轮修补结果通过原就绪、进入/离开插入模式、编辑、60×12 resize 文件、写盘、退出、主屏恢复、独立 child 尺寸及 CPR 回复检查；`presented_done` 仍为 `null`。原 DLL 的首轮也通过，但不覆盖其后失败。七轮均无外层超时，Job active/terminated 为 0，desktop gone。导出时仅离线重放原断言及验证器，复核保存的结果和文件哈希，没有重跑 GUI。

## 修改机制与尚未覆盖的路径

此前记录显示：`process_input_message` Peek 到 resize 后调用 `send_winch_maybe`，后者为信号递送临时释放 `input_mutex`；另一线程可以消耗 resize。旧调用恢复后按旧数量清理队列，取走新到的 Ctrl-O。这是已捕获失败的调用顺序，不能追溯填补没有原生输入记录的历史失败。

[研究补丁](../../research/msys-runtime-window-prefix/README.md)仅修改 `winsup/cygwin/fhandler/console.cc` 的 `WINDOW_BUFFER_SIZE_EVENT` 分支，12 行新增、6 行删除：在输入锁仍保护旧 Peek 前缀时，先 `ReadConsoleInputW` 消费已处理前缀并调整 `num_processed`；再保留原 `send_winch_maybe` 解锁、SIGWINCH 与再加锁，返回现有 read 循环，下一次重新 Peek。即使尺寸未实际变化也结束当前快照处理，后续输入进入新一轮。

没有删除为 less 信号处理器 longjmp 保留的解锁。结构保留不等于已做实际 longjmp 回归；`MOUSE_EVENT` 中的同类信号路径、读取失败注入、其他应用和正式兼容/分发策略尚未验证。补丁沿用既有 `ReadConsoleInputW` 错误处理，未声称修复这些边界。

## 构建条件与来源

- 运行库：[固定 MSYS 源码](https://github.com/git-for-windows/msys2-runtime/tree/2e2ef940151485490aff0c7b00c00965ef7a71a4)，提交 `2e2ef940151485490aff0c7b00c00965ef7a71a4`。未修补基点 tracked 源码干净；修补变体仅上述文件有 diff，公开补丁已做 `git apply --check` 并比对应用后的源码哈希。
- 独立 Git SDK：提交 `36b102843d99c4993084b29d839dc6f5ef4db2f9`，构建依赖只装入私有 SDK。完整[包版本清单](../../research/msys-runtime-window-prefix/build-packages.txt)随研究记录保存；其中 GCC/G++ 16.2.0，GNU C/C++17，binutils 2.47、make 4.4.1。
- Win32 头文件：SDK 提交 `eb069c08fe8ef4d2f19dc6b99c4816a4c51fd2ae`（2024-07-10）的 `usr/include/w32api`，包版本 `12.0.0.r0.g819a6ec2e-1`；同时固定裸包含与 `w32api/` 前缀包含路径。不能仅设置顶层 `CPPFLAGS` 后假定子目录采用了旧头文件。
- 配置保留 `--disable-dependency-tracking --disable-doc --disable-dumper --with-msys2-runtime-commit=2e2ef940151485490aff0c7b00c00965ef7a71a4`，生成构建文件后在目标 `winsup/cygwin` 目录构建 `all`。C 使用 `gcc -std=gnu17 -Wno-error=old-style-definition`；C++ 使用 `g++ -std=gnu++17` 和 `-O2 -g -Wno-error -Wno-narrowing -Wno-use-after-free`。最终 make 显式传入两个旧头文件的 `-isystem` 路径，`-j2`，每次构建上限 900 秒。构建准备失败和编译警告原件保留在本地，不计 GUI 结果。

这是新工具链重建实验，不是官方 2024 DLL 的逐字节复建。两个重建变体的 310 项构建对象清单具有相同键集合，六项哈希不同：`fhandler/console.o`、`libdll.a`、`msysdll.a`、`new-msys-2.0.dll`、`version.o`、`winver.o`，包含再生成的版本对象。只能声称一处主动源码修改，不能声称二进制只有一个函数或一个文件不同。

公开源码补丁遵循固定上游的 LGPLv3-or-later 与 `CYGWIN_LICENSE` 中的 Linking Exception；完整许可及修改归属见[来源说明](../../research/msys-runtime-window-prefix/README.md)，不套用项目 MIT。它是 MSYS/Cygwin 研究材料，不属于 WindTerm 公开或逆向源码。

## 证据与结论限制

[白名单验证记录](../../experiments/shell/results/2026-10-27-vim-msys-runtime-prototype/validation.json)包含源码、EXE/DLL、补丁/许可、对象差异、全部七轮结果、原文件哈希与回收状态；DLL、原始终端输出、个人路径及完整工具日志仅本地保存。

本实验为原诊断补充了重建基点 red 与修补变体四轮 green，但没有重新执行完整 57 项 CTest，没有获得 WindTerm 同应用对照，也没有形成性能或普遍可靠性结论。原固定运行库下的完整官方 56/57 结果保持不变；#52、#47 和 Draft PR #50 的正式出口继续开放，不能用替换外部应用 DLL 自动关闭原合同。
