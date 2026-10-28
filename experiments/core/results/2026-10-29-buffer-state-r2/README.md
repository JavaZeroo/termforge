# 自主 C++ 双缓冲存储与文档世代基础（#130）

本记录只验证 [#130](https://github.com/JavaZeroo/termforge/issues/130) 的独立工程基础：两个缓冲拥有各自状态，交换激活并隔离旧读者。**未接入 private CSI，也不验收 1049、完整 #27 或 R2。**

运行源码固定为 `63589fa21b68c3029501772b7bb415e66aa99e97`，已合入 main 基点 `337812bc27b10a8f4dee5e4e35ded21c90f4a151`。后续能力矩阵和本证据整理是文档提交，不改变该运行身份。字段、命令、源码/EXE 及原始本地记录哈希见 [validation.json](validation.json)。

## 实现与断言边界

- `Parser::screen` 保留原对象和主屏接口；另一帧私有拥有文本、光标、样式、pending-wrap、combining attachment、历史与丢弃计数。激活只交换状态，不复制保留历史；共享行 ID 分配器不随帧恢复。
- 主屏保持原历史容量，备用存储历史上限为零；两帧共享尺寸。物理行 resize 先准备两帧，再提交；隐藏帧分配 ID 耗尽时，两帧内容、几何和分配器均不半更新。零历史是工程界限，不代表原版文档浏览合同。
- `TextPosition` 带单调切换 epoch；selection、search 和 history viewport 检查世代。旧句柄不会因主屏返回且行 ID/version 保留而复活；非零 epoch 下新建的 alt/main 选择、查询及 viewport 仍可使用。未切换的默认 epoch 为零，现有三字段位置初始化及 JSON schema 保持兼容。
- [独立公开边界测试](../../buffer_state_test.cpp)使用自写字面内容验证两帧往返、光标/样式/wrap/附件、隔离历史、宽字裁剪、软硬行元数据、增长 ID、resize 回滚与旧句柄失效；没有通过测试内部调用次数代替可见内容断言。

原版样式继承、重复进入清空、保存/恢复协议策略及历史文档呈现均不在本项选择。存储激活的幂等性不能用来宣称原版 1049 重复进入也幂等。

## 固定配置与完整回归

Windows 11 `10.0.26200`、x64 Release，Visual Studio 2022 / MSVC `19.42.34433.0`，Python `3.12.10`。`TERMFORGE_BUILD_QT_SHELL=OFF`，`TERMFORGE_BUILD_CANDIDATE_ADAPTER=OFF`；既有 Windows 诊断使用系统 ConPTY。**本轮不构建 Qt/固定 ConPTY 产品组合，也不构建 Alacritty 参考。**

完整 configure 和 build 均退出 **0**；完整 CTest **50/50、退出 0**，耗时 **355.09 秒**。这是测试耗时，不是终端性能指标。运行前已观察固定提交及干净源码，完成后采集精选源码、45 个 EXE 和完整日志身份；没有单独的运行前冻结 manifest，也不声称可复现构建证明。

主体命令为：

```powershell
cmake -S . -B build/core-buffer-state -G 'Visual Studio 17 2022' -A x64 -DTERMFORGE_BUILD_QT_SHELL=OFF -DTERMFORGE_BUILD_CANDIDATE_ADAPTER=OFF -DPython3_EXECUTABLE=<Python-3.12.10路径>
cmake --build build/core-buffer-state --config Release
ctest --test-dir build/core-buffer-state -C Release --output-on-failure --output-junit <工作树>/local/buffer-state-r2/13-main-all-ctest.xml
```

仅测试进程的 PATH 前置 Git `usr/bin`，并设置 `PYTHONUTF8=1`、`TERMFORGE_REQUIRE_SYSTEM_CLIPBOARD=0`。路径占位符替代私人绝对路径。原始 configure/build/CTest/JUnit、退出码文件及完整 `LastTest.log` 保存在本地；JSON 公开其实际字节数与哈希，不发布原件。

CTest 注册级没有 skip，但 Python 框架内部有 **12 条 skip**，逐条名称及原因在 JSON：4 个可选参考整类、7 个需要 disposable hosted VM 的实际系统剪贴板 UI 场景，以及1个 opt-in 原版失败注入。成功 JUnit 输出被 CTest 截短，内部 skip 以完整 `LastTest.log` 为准。`paste_system` 注册及其中的系统 API 检查实际通过，不能替代上述跳过的 UI 场景或完整剪贴板验收，也不转为 Qt 能力。

## 分次失败与审查

7 条实际运行红分别覆盖 writing context、备用历史界限、隐藏 resize、旧 position、selection、search 和 viewport；每条的失败日志及当时源码/测试/EXE 身份记录均保留，脱敏哈希见 JSON。接口编译失败、选择测试夹具参数错误、早期只构建部分 target 导致缺 `receive.exe` 的回归准备失败另列，不计为算法运行红；最终整轮已完整构建该 target。

双轴审查无未解决阻断；原字段交换清单的重复已用私有同一 helper 消除，复审确认没有引入 copy 或改变 `noexcept` 边界。没有独立审查报告附件，因此不补造报告哈希。远端 hosted 检查属于 PR 的独立运行，不并入本地 50 项计数。

本公开记录仅含合成断言、脱敏配置、计数及身份哈希；不包含 WindTerm 私有材料、资源、会话、原始抓取或私人路径。它不证明原版一致性、完整备用屏、GUI/TUI 组合、IME/真实 DPI、全平台完整产品、可分发包或性能达标。
