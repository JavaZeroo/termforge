# 保留列宽与帧修正：分配置整合回归

固定源码 `d765c2b305983815c9ba7a35d2dd4f49d30fb99c` 的选定核心回归 **24/24，通过，1.27 秒**；Qt / 随包固定微软 ConPTY 的默认路径回归另为 **4/4，通过，12.66 秒**。生产核心对应 `afdcf59f942f2b7cd3607ad7ab9bd8be9dc907dd`；后续 CMake 增加擦除诊断与离线审计注册，没有在本头新增产品行为。

**显式保留正文/投影仍未接入默认 Qt。** 独立擦除诊断仍保留 24/40 阶段、96/600 断言失败、退出 1；默认物理裁剪与 Qt B/CD 缩放差异也没有因本轮通过而获豁免。本报告不是完整 #27/#29 或 R2 验收。

## 固定身份与配置

[validation.json](validation.json) 保存 57 份选定源码、28 个实际回归 EXE、2 个仅构建 EXE、44 个选定已部署运行时文件及 7 份日志的 SHA-256。选定核心 header/测试、Qt 源与仓库 include、根 CMake 和固定依赖辅助文件均逐字节匹配 `d765c2b`；两份仅构建 EXE 为擦除诊断与 Qt 产品入口，不将构建成功记为执行验收。

哈希在运行后采集，完整运行前输入 manifest 未留存，明确为 `null`。选定文件清单不冒充完整构建输入清单；后续修改或重建不回填本轮身份。公开结果不包含原始日志中的本地路径。

共同配置为 Windows x64、Visual Studio 17 2022、MSVC 19.42.34433.0、Windows SDK 10.0.26100.0、Release、并行度 1，候选适配器关闭。核心构建关闭 Qt；Qt 构建使用 Qt **6.10.3**、`offscreen`、固定微软 ConPTY **1.25.260930003** 与固定 CRT **14.51.36247.0**。四个 Qt 测试目录的 `conpty.dll` / `OpenConsole.exe` 均核对到固定 manifest，不以系统 ConPTY 代替。

## 原生回归分别记录

| 配置 | 实际结果 | 覆盖与日志 |
|---|---|---|
| 选定已注册核心测试 | **24/24；1.27 秒** | `frame-final-core-build.log`、`frame-final-core-ctest.log`；尺寸/历史/选择/Find、owner/映射/投影、寻址/编辑/末列/隐藏帧、模式/回复/行序等，精确 24 项名称见 JSON |
| Qt / 固定 ConPTY | **4/4；12.66 秒** | `frame-final-qt-configure.log`、`frame-final-qt-build.log`、`frame-final-qt-ctest.log`；`qt_cpp_shell`、`qt_unicode_input`、`qt_cpp_resize`、`qt_terminal_modes` |

核心结果包含[保留列宽与编辑](../2026-11-02-retained-columns/README.md)、[末列 resize](../2026-11-02-resize-pending/README.md)及[隐藏帧切换](../2026-11-02-resize-hidden/README.md)的注册回归。它证明这些固定合同在本头继续通过，不把各自未观察的行为扩展成完整帧或 Unicode 一致性。

Qt 四项仍使用原默认 `Screen::resize` / `snapshot()` 路径。`qt_terminal_modes` 只包含已验证的五个模式阶段，未执行独立 `--resize` 正文诊断；基础 `qt_cpp_resize` 通过也不证明正文重排接线完成。`offscreen` 回归不能替代真实 DPI、物理呈现或完整 GUI 工作流验收。

## 离线工具验证按原轮次保留

下列记录验证探针、导出/补充审计和控制器，均不运行新的 WindTerm 原版，也不计为产品测试。

| 原轮次 | 注册 CTest 结果 | 内部组与记录 |
|---|---|---|
| frame + 早期 erase 注册组 | **6/6；90.16 秒** | `frame-erase-registered-ctest.log`：frame 普通/`-O` 各 31 项及 Node 8；早期 erase 普通/`-O` 各 23 项，另含其 Node 控制器注册项 |
| erase 补充审计注册组 | **3/3；71.35 秒** | `erase-supplement-registered-ctest.log`：更新后的 erase 普通/`-O` 各 35 项及 Node 9 |

frame 的 31 项组成为基础 23 + 隐藏帧补充 8；更新后 erase 的 35 项为基础 23 + 补充审计 12。JSON 另保留冻结源码中对应方法数量及摘要作为组构成核对；它不将较早的 23 项运行追认为后来 35 项，也不把各轮整树身份追认为最终原生构建。两轮完整运行前输入清单仍未知，旧 erase Node 项未单列精确内部计数，不倒填后轮的 9。

上述时间只是各次测试执行时间；不同配置、不同轮次不相加成总通过率，也不作为性能预算或可靠性重复验收。

## 独立失败继续开放

- [擦除 RED](../2026-11-02-resize-erase/README.md)：固定两例仍是 **24/40 阶段、96/600 断言失败，退出 1**。诊断目标参与构建，但未注册为验收 CTest、未设 `WILL_FAIL`，不在核心 24 项中；本轮没有把旧失败二进制结果冒称为新一轮执行。
- [默认裁剪诊断](../2026-11-02-resize-address/README.md)：无参数调用原 `Screen::resize` 的历史 16/64 阶段失败保持；显式保留入口的成功不替换这个失败身份。
- [Qt B/CD 正文缩放诊断](../../../shell/results/2026-11-02-qt-terminal-modes/README.md)：固定 ConPTY 原生重排与核心裁剪的旧红仍未修复，不在 Qt 四项中。

后续擦除新位置、默认路径接线、运输交错、高度变化、完整 Unicode、历史/选择/Find 产品交互与性能比较仍须各自验证。本报告不合并这些未决项，也不增加新的原版运行计数。
