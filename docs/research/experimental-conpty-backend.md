# 显式 side-by-side ConPTY 实验整合（#47）

[每屏 margin 调查](conpty-margin-backends.md)确认固定官方新后端可以保留原 DECSTBM；系统后端已丢失的信息不能由末端猜测补回。本切片将这个可行路径接到实际 `Session` 和 GUI，**保留默认系统后端，未完成 R2 后端决定或完整回归**。

## 接口、身份和生命期

`Backend::system()`、`reviewed_side_by_side(absolute_directory, GlyphWidth)` 和显式构建的 `configured()` 使用同一个对象边界。`Session` 创建、resize 和关闭均调用该对象的同一 API 族；模块由共享所有权保持，Backend 成员在 Session 中最先声明、最后析构，使输出读线程、HPCON 和清理过程结束前 DLL 始终有效。

来源/部署见[固定包入口](../../third_party/conpty/README.md)。CMake 不下载；运行时也不探测已安装的 Terminal。不混用 DLL 与系统 create/resize/close，不在缺失或错误文件时静默回退。独立 native 检查实际创建 80×24、由 child API 观察 120×40、正常退出37、EOF109、自有 Job 归零，并确认临时 Backend 销毁后 DLL 仍在、Session 析构后才卸载；两个文件各自篡改都拒绝。

## 宽度策略的 A/B

固定1.25包默认 flags 0 时，真实 `ReadConsoleOutputCharacterW`/`CHAR_INFO` 对本项目四个复杂文字样本产生新的 readback 投影，原严格 Unicode 合同失败，末端仍保留原文。只改官方 `GLYPH_WIDTH_CONSOLE=0x18` 后，四样本回到既有合同承认的原文/非BMP替代投影，原完整 Unicode/glyph 检查两轮通过；没有修改 `unicode_checks.py`。全部原 flags0失败和 flags24结果仍保留。

因此显式实验 `configured()` 固定选择 Console width；系统和通用诊断保持默认0。实际 GUI margin 记录还保存 Session 的 backend 和 creation flags，测试端必须显式选择对应合同；不会根据末屏自行挑选较宽松的解释。该固定包头文件及源码来源已经绑定到 manifest；这个策略与旧 `0x08` passthrough 无关，也不构成完整字素/宽度或 native API 等价验收。

## 实际窗口和已有回归

原175字节 margin source、四阶段 native capture、区域外文字、主历史、光标、style 和 retained/full frame 均保留。新实验合同独立要求末端恢复 `REGION_C`、`REGION_D`、`BOTTOM_FIXED`，同时要求 native 继续显示已复现的 `REGION_CIXED`。源控制台与末端分列，#41 不关闭。默认 system 合同继续检查原始差异，没有删除旧失败或补发 VT。

原 ESC resize 场景曾以宿主的 `CSI ?25h` 当 resize 完成信号。新宿主没有这个重绘副作用，原夹具超时。现在由真实 peer `GetConsoleScreenBufferInfo` 确认60×12并发出独立 ack，随后发送原单个 ESC；resize与输入共用原五秒预算，外层期限不变。真实 ESC 两轮通过；Vim/DSR/resize写盘曾在早期flags0阶段通过两轮，不能将它用于证明最终Console-width策略通过。

最终C++源码固定在 `5030058`，随后 `0954fea` 只整合 margin Python 诊断的异常/清理修复。[公开验证记录](../../experiments/conpty/results/2026-10-26-reviewed-backend/validation.json)包含15个源码文件的git blob哈希、四项二进制哈希、固定包/完整MIT身份、逐后端margin结果和白名单化的正反结果；未提交原始屏幕、transport或路径。

最终Console-width构建的后端生命期/拒绝检查6/6、margin诊断清理检查9/9、既有TUI检查5/6；TUI中ESC、主/备用屏、局部滚动、每屏margin和完整Unicode各两轮通过。基础cmd Unicode/编辑/Ctrl-C/正常退出三轮、四种本地shell/CWD/退出两轮、持续输出关闭三轮通过。More子阶段完整提示→单q→EOF通过，但包含Vim的整套native capture失败，不能将More成功表述成整套通过。

Find、选择/zoom、split后台会话以及flags0的Vim/完整native capture是较早源码阶段的限定结果。不同阶段分别保存，不把早期结果回写为最终同源码完整验收；`runtime_observation_test`此次仅构建，未执行。

## #50 评审后的测试监督器修订

独立 Standards/Spec 评审确认 `test_backend.py` 的异常清理可能覆盖原始超时，并跳过 Job 关闭和记录；目录准备失败也会泄漏提前创建的 Job。修订先准备目录，再取得 Job；assignment 失败只杀仍等待 GO 的自有根进程，不发送 GO。终止、排空、Job 观察、关闭和写盘分别保障，所有清理等待共用原五秒预算；未知观察保留 `null`，首次非零 Job 与强制清理后的观察分列，不能归零后转为通过。

原实现的六组纯异常负例先复现失败，修复后再加入证据/summary 写盘失败边界，`BackendCleanupFailureTests` 共8/8通过，且不受 Windows skip 限制。现有真实 `BackendTests` 6/6再跑通过（7次调用，含同包两文件各自篡改拒绝）；只复用已构建二进制和固定包。此次只修改测试监督器，不改变产品后端和下列已知红项，也不重写前述固定源码的历史验证记录。

## 仍未闭环的真实差异

- **连续 GUI resize。** 原 `resize` 场景在stage43等待精确 `TF_SIZE_100x28`，末端实际出现 `TF_SIZE_100x28OW`，并有旧文字/位置及重放后的 Unicode 差异。Console width并未修复。没有删除尾部、替换source或放宽行匹配；需要裁决候选 resize/reflow 与新宿主同步VT/原生API状态的交叉。
- **Vim兼容。** 最终Console-width构建在原stage59没有得到resize事件文件，命令出现在插入文字中；同批6项TUI检查为5通过、1失败，失败项第二轮未运行。独立native real-app capture也在 `new-buffer` 阶段失败，返回1，editor字节数1999；外层Job仍归零。没有改成睡眠、跳过Vim或降低既有事件文件断言，早期flags0的成功与本次失败分列。
- **粘贴并发时序。** 原 3MB 请求、768字节 receiver checkpoint和持续输出均保留，但新宿主在GUI看到checkpoint前已经接收全部输入，transfer为completed。这个运行不能证明未完成期的Cancel/关闭行为。不能把completed当cancelled，也不能仅降低原16KiB阈值；需要保持原负载与独立回收断言的确定性sender进度栅栏，再验证实际产品取消。
- **源 native margin。** 固定新包仍未修复 native readback 的每屏 margin；末端正确不替代原生API或依赖它的应用验证。
- **出口。** 尚无该实验后端的完整R1同次工作流、实际IME/跨屏、长期资源、性能或WinTerm对照结论；默认Alpha、#34产品决定、#35和R2范围均未改变。

这些缺口继续由 #47 / #41 及相应父项保留，实验可选择不等于可靠日常版本。验证仅在自有Job/非输入桌面进行，原始transport、错误原文和图像继续留私有目录；`presented_done=null`。
