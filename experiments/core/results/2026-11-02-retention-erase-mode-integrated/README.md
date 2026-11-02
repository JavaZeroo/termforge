# 保留正文、擦除与活动区域的核心 / Qt 整合回归

产品源码及根 CMake 固定为 `3c3d3f33409df23d163044a5f8202b21c7536090`。本轮重新构建后，**26 项已注册 native core 测试全部通过，0.99 秒；Qt 默认路径 4 项全部通过，8.55 秒**。30 个实际测试 EXE 的身份按本轮产物重新采集，不沿用之前 `d765c2b` 的 24 项核心或 Qt 二进制哈希。

这确认已闭环的有限核心切片能共同通过现有回归；**保留正文 / 显示投影尚未接入 Qt 默认 resize 路径**。默认裁剪与单独 B/CD resize 的历史失败仍分别保留，不被这里的通过结果覆盖。0.99 / 8.55 秒只是测试运行时间，不是性能验收数据。

## 实际执行范围

| 配置 / 分组 | 本轮实际结果 | 范围 |
|---|---|---|
| core，Qt OFF / candidate OFF，Release | 26/26，0.99 秒 | parser/screen、history、选择、Find、mapping、显示投影、保留列宽及各项 VT / 帧回归 |
| Qt，Qt ON / candidate OFF，Release | 4/4，8.55 秒 | `qt_cpp_shell`、`qt_unicode_input`、`qt_cpp_resize`、`qt_terminal_modes`；全部 `offscreen` |
| Qt 产品入口 | 构建成功，未在本轮执行 | `termforge_qt_shell.exe` 不计入 30 个测试 EXE 或 Qt 4 项 |
| Unicode 输入支持子程序 | 与 Qt 测试一起构建并部署 | `shell_input_client.exe` 单独记录构建产物与实际 app-local 副本，不新增 CTest 数量 |

26 项核心名单和实际 Release 命令由本轮 `core-test-targets.json` 与 `closed-slices-core-ctest.log` 逐项核对，包括新增注册的 `retained_resize_erase_core` 和 `retained_resize_mode_core`。这是明确列出的 native core 集合，不是仓库内全部 Python、Node、GUI 或远端测试总数。

Qt 四项通过 `src/qt/run_smoke.py` 调用各自 EXE，使用 `QT_QPA_PLATFORM=offscreen`、35 秒 CTest 超时。`qt_terminal_modes` 使用默认五阶段模式协议，没有 `--resize` 参数；不能把它当作 B/CD resize 诊断通过。Unicode 输入测试从自身目录调用支持子程序，已确认该部署副本与同轮构建副本字节完全一致；支持子程序并非额外独立测试用例。

当前已闭合的相关字面证据分开保留：

- [五例 EL0 / EL1 / EL2](../2026-11-02-resize-erase/README.md)：完整字面测试从 56/96 阶段通过、200/1440 条断言失败变为 96/96、零失败；原版来源仍是 2 个原 strict 加 3 个原失败后独立补充通过案例。此项已注册，并在本头重建后通过。
- [三例活动区域 / 保存末列](../2026-11-02-resize-mode/README.md)：3 个原 strict、共 16 原阶段，核心 16×4=64 阶段；32/64、124/2192 条失败变为 64/64、零失败。保存末列案例此前已绿，新增修复集中于活动区域两例。

这些字面断言计数来自各自独立红绿记录；本轮 CTest 再确认其测试程序通过，不新增原版运行，也不把原失败补充审计改称原 strict。

## 配置与部署身份

两套构建均为 Windows x64、Visual Studio 17 2022、MSVC 19.42.34433.0、SDK 10.0.26100.0、Release，构建使用单 worker；`TERMFORGE_ENABLE_RESIZE_REPRO=OFF`。core 关闭 Qt，Qt 配置单独开启 Qt Widgets，两套都关闭候选适配器。

Qt 部署的 `Qt6Core.dll` 文件版本为 **6.10.3.0**。选定依赖包括 Qt Core/Gui/Widgets/Test/Network、运行测试所需 `qoffscreen.dll`、产品目录的 `qwindows.dll`、选定 CRT 及固定 ConPTY；共 **54 个部署文件**按本轮副本重新采集 SHA。四个测试目录和产品目录的 ConPTY 两文件均与固定 manifest 的 **1.25.260930003** 字节一致；五目录选定的三个 CRT 文件均与 **14.51.36247.0** manifest 一致。产品目录的依赖匹配只证明部署身份，不证明产品入口本轮实际启动。

| 本轮产物 | SHA-256 |
|---|---|
| `resize_erase_test.exe` | `60db11d43124e47b36205e9d25d645e7014253ad539c391672ebcf90cdf68420` |
| `resize_mode_test.exe` | `b91de04f55bcc96dcd646ee621ec0d3af5913833a3ac7c726fbcba8f52310fa1` |
| `qt_cpp_shell_smoke.exe` | `16d85ec9be3b7b1fb60e63451796cd8eb8905d5d5f4dbbd4fbab87b9b8729409` |
| `qt_unicode_input_test.exe` | `27af77f395c27466e141e15cd94ee5dcf491b7b6f94b7df9d8fe8203aef3e2da` |
| `qt_cpp_resize_smoke.exe` | `a8324f7c3a71b683bf2edbb12e67cc36d8f93d3b90c5a2d05d5ddfb3270bd4c2` |
| `qt_terminal_modes_test.exe` | `84c360911b7083509dd398dd3a969f856199af92f0e13c0bb5abccf9e3c076e5` |
| 仅构建的 `termforge_qt_shell.exe` | `0681aba127f2957a0c0b739be46fb6862f87aa18e1967a8e7f21be00cd2f964c` |
| 输入支持子程序，构建 / 部署副本相同 | `33f955a0d28249370e67c99579da4cc722e06cd34d8bbf623fb845cb9d47ffee` |

## 执行后的冻结记录

执行完成后，于 `2026-11-02T03:50:25.281676+00:00` 将 60 个选定源码文件、30 个测试 EXE、1 个仅构建产品 EXE、输入支持子程序的构建副本、54 个选定部署依赖、5 份日志、6 份构建配置及核心测试注册列表复制到本地独立目录；复制结束再次核对原文件未变化。选定源码含 core 头文件和测试、Qt 源码及递归仓库 include、CMake 与固定依赖 manifest，全部逐字节等于 `3c3d3f3`。

随后补充固定输入支持子程序的源码及实际 app-local 副本：源码同样等于 `3c3d3f3`，EXE 与最初冻结的构建副本一致。共 61 个选定源码文件的身份写入 [validation.json](validation.json)。最初 manifest 不改写，两次采集时间分别保留。

本地归档为 `local/r29-retention/closed-slices-identity-capture/`。其中 `identity.json` SHA 为 `e5422d6576c53a767ec04e4ce3a70b775a68f12c84f302efb4fded8cff0a18ef`，补充 `helper-identity.json` SHA 为 `5888e5e454994457e9876ac4acbdff5dd08cab7a51200271979f513baa28eeac`。采集时仓库 HEAD 是 `cd946bf798be4ee725f7ff518052e5a5a09c1ceb`，选定产品/CMake 字节仍匹配上述 `3c3d3f3`。后续 Unicode 改头或产物不回填此记录。

日志均在 `local/r29-retention/`，文件为 `closed-slices-core-build.log`、`closed-slices-core-ctest.log`、`closed-slices-qt-configure.log`、`closed-slices-qt-build.log`、`closed-slices-qt-ctest.log`。保存的是**执行后选定输入与产物身份**；完整运行前输入 manifest 没有保存，保持 `null`。本轮没有新增原版运行、离线工具测试或为采集身份再次执行 native 测试。

## 未关闭的边界

[默认 `Screen::resize` 裁剪对照](../2026-11-02-resize-address/README.md)和 [Qt 单独 `--resize` 的 B/CD 诊断](../../../shell/results/2026-11-02-qt-terminal-modes/README.md)仍以各自历史失败身份保留，本轮未重跑这两个入口。它们不是本次 26+4 绿色集合的一部分；旧擦除失败则已有上面的独立红绿修复，不再列为未解决项。

此处不证明 Qt 正式保留正文路径、完整 Windows 日常可用版本、全字符/样式/高度 resize、全部原版内部状态、真实不同 DPI、完整固定 ConPTY 生命周期、性能等价或 R2 完成。后续要接入 Qt 默认行为，仍需独立完成相应用户可见验证。
