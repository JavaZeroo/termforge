# 自主 C++ 的 1049 文字、光标与颜色子集（#135）

本项把 `CSI ?1049h/l` 接入自主 C++ Parser，复用 [#130 的双帧存储](../2026-10-29-buffer-state-r2/README.md)。首次及重复进入备用屏归首格、清除旧备用文本并复位写入前景/背景颜色；退出恢复主屏文字、光标、颜色与 pending-wrap，重复退出不回退后续主屏写入。未知 private 模式及多模式列表不会部分执行。

依据是原版 [#118 文字/光标](../../../../docs/research/windterm-alternate-reference-2026-10-28.md)、[#123 pending-wrap](../../../../docs/research/windterm-alternate-style-reference-2026-10-28.md)及 [raw-capture-v1 的局部 H/space 输出](../../../../docs/research/windterm-raster-raw-reference-2026-10-29.md)。不把颜色正控的输出匹配写成读取了内部 pen。完整物理备用帧的文字擦除是本实现方法；原版全部网格/背景、bold 等非颜色属性、47/1047/1048、margin/origin/wrap 模式族及其组合仍未验收。其他属性保留每帧拥有状态，不能据工程默认值推断原版。

## 固定源码与公开接口

运行实现为 `40bee8e504410ed167bee600a1a5597417204809`；`c1be53ebdbc8f4094749924d821699eb5368eba0`只更新旧负例并增加正向 CLI 测试；将 main `5f7373fc79eb152d8e4574b1d2b0dfbdd7068f77` 合入后，最终完整验证源码为 **`575bd282b7f61fc4c47da0cad7739b0c46d8c1f2`**。本报告为后续文档整理，不改变运行身份。[validation.json](validation.json)保存实际源、EXE、日志和输入清单 SHA。

Parser 负责模式协议，Screen 负责帧状态和读者身份；`Parser::screen` 地址、JSON 和候选 C ABI 保持原边界。直接存储激活同帧仍幂等；协议重复进入另更新逻辑 epoch，防止清屏后重写相同文字时旧选区复活。激活交换容器，不复制整份历史；备用输出不污染主屏历史，物理 resize 沿原策略同时维护隐藏帧。

[公开边界测试](../../alternate_mode_test.cpp)覆盖 strict/recovery、首次/重复进入及退出、原版同一 `91×25` 网格的 208B 五阶段源（不含 DSR 查询）的全部分割点和逐字节输入、无中间 DSR 的末列 W→切屏→返回 X、历史/resize 与旧/新文档读者。预期为独立字面文本、位置、颜色和状态，未以同实现快照互比代替 oracle。strict 未知序列继续 fail-fast；recovery 的取消、截断、ESC 重同步和参数/长度上限仍回归。

## 真实红绿与旧判据更新

三个实际运行反例依次为：原 Parser 拒绝 private CSI；重复进入未归首格/清旧文本；重复进入后写回相同 `ALT` 使原选区被文本协调逻辑接受。前两者由协议接线/复位解决，第三者由逻辑 epoch 失效解决。固定边界、原失败日志与当时精选源码/EXE 身份保留；第一次 green 的 EXE 未在下一次构建前单独取哈希，JSON 如实为 null，不补造历史身份。

首次完整源码 `40bee8e` 为 **52/54，exit8，298.93s**：`core` 与 `recovery` 的旧负例仍把新支持的 `?1049h` 当未知。随后将未知模式样本改为仍不支持的 `?9999h`，保留原拒绝/吞序列检查并增加两条独立正向 CLI 往返测试；相关 2/2 通过。没有为这些负例失败修改生产 C++，也没有增大期限或跳过检查。两次完整运行之间 **101 个 runtime 文件逐字节相同**；新增 raw 工具回归使最终注册数为55，不把两次运行拼接为一次。

## 最终完整验证

Windows 11 x64 build26200，VS2022 / MSVC19.42.34433.0，Release，Python3.12.10，Qt6.10.3；Qt 开启、candidate adapter 关闭。Qt 使用随包固定微软 ConPTY1.25.260930003，其余既有 Windows 诊断各自使用 system backend。固定 Qt SDK 的 4,358 文件树 SHA 为 `a35ab6fe76b62e4d9bd6d3c6b0d476c63583070cea5c9118ef5bb02042ffe266`。

```powershell
cmake -S . -B build/qt-1049 -G 'Visual Studio 17 2022' -A x64 -DTERMFORGE_BUILD_QT_SHELL=ON -DTERMFORGE_BUILD_CANDIDATE_ADAPTER=OFF -DCMAKE_PREFIX_PATH=<固定QtSDK> -DTERMFORGE_CONPTY_BACKEND_DIR=<固定ConPTY> -DPython3_EXECUTABLE=<Python3.12.10>
cmake --build build/qt-1049 --config Release --parallel 2
ctest --test-dir build/qt-1049 -C Release --verbose --output-junit <工作树>/local/cpp-1049-r2/14-reviewed-ctest.xml
```

仅测试进程 PATH 前置 Git `usr/bin`，`PYTHONUTF8=1`、`TERMFORGE_REQUIRE_SYSTEM_CLIPBOARD=0`；Qt 对应 CTest 单独使用 offscreen。configure/build 均 exit0，最终 **55/55、exit0、289.74s**。时间仅为测试耗时，不是性能结果。Qt 为1项，其余54项为自研核心、既有或独立诊断，不能写成55项 Qt 产品能力。

运行前后 **7,196 文件**一致：源码2,737、Qt产品runtime25、smoke runtime27、既有诊断runtime49、SDK4,358。完整 manifest/raw 日志本地保留，公开只列精选身份及清单哈希。Qt smoke 1.47s，原有限 cmd 标签、错误隔离和真实事件循环退出前回收通过，outer Job `active0/total26/terminated0`；它没有新增 TUI/备用屏 GUI 验收，`presented_done=null`。

CTest 注册级无 skip，框架内部有 **13 条 skip**，逐条列在 JSON：4项可选参考整类、7项需要 disposable hosted VM 的真实剪贴板 UI、1项 opt-in 原版失败注入，以及本次 `paste_system` 私有 station 的系统 API 以 **OpenClipboard error5** 跳过。该 API 未通过，不能用完整55项计数或旧独立成功覆盖；没有为取绿重跑。完整 LastTest 原字节 SHA 保留；只从其中 ASCII skip 行生成脱敏字段，私有证据路径不公开。

Spec 0 项发现，Standards 0 项硬违规/0 项需单列 smell；完整测试及文档随后补齐交付证据。原版数据与本项目单元/CLI/Qt测试各自保留身份。本项不验收完整模式、真实 TUI/IME/不同 DPI、实际 presented、干净机器运行库分发、性能预算或 R2。
