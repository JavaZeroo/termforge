# 1049：按六种属性的原版输出对照修正写入状态

自主 C++ 核心在 `CSI ?1049h` 首次或重复进入备用屏时，除原有颜色复位外，清除暗色、下划线、反显、删除线、隐藏和上划线。退出仍恢复主屏原有写入状态。**粗体的原版控制尚不可区分，保留现有策略，没有把全部 `Style` 统一清零。**

依据是[六个原版独立属性 case](../../../../research/ui/results/2026-11-02-attribute-state-reference/README.md)的固定 H/前置空格同帧输出比较。每个 case 为五个阶段，共 30 个原版阶段；阶段 0 仅建立可区分的正常/属性控制，阶段 1 首次进入、阶段 2 显式 SGR0、阶段 3 重复进入的样本均唯一匹配 SGR0 控制，阶段 4 返回主屏的样本匹配对应属性控制。

| 属性 | SGR | 首次/重复进入后的样本 | 返回主屏后的样本 |
|---|---:|---|---|
| 暗色 | 2 | SGR0 输出 | SGR2 输出 |
| 下划线 | 4 | SGR0 输出 | SGR4 输出 |
| 反显 | 7 | SGR0 输出 | SGR7 输出 |
| 隐藏 | 8 | SGR0 输出 | SGR8 输出 |
| 删除线 | 9 | SGR0 输出 | SGR9 输出 |
| 上划线 | 53 | SGR0 输出 | SGR53 输出 |

这些是固定输出样本的事实，不是直接读取原版内部属性位。完整 styled grid、擦除后的全部空白背景、粗体、任意属性组合、真实桌面呈现和其他渲染入口均未因此验收。

## 字面回放与红绿结果

[alternate_mode_test.cpp](../../alternate_mode_test.cpp)逐阶段使用与原探针 `phase_records` 完全相同的五段源，检查字面正文、光标、主/备用屏和指定 H/空格的属性。原始 marker 列与核心字面位置独立核对；核心不重放原探针附加的 DSR 请求，也不冒称重新观察了原版像素。

strict/recovery 各覆盖整段、每阶段全部两段分割位置和逐字节输入：共 **506 个五阶段轨迹、2530 次阶段检查**。这些是 30 个原版阶段的核心回放，不是新增原版样本。已有完整空白/样式网格测试继续单列为工程策略，修正其错误的“重复进入保留删除线”预期，同时保留未分类的粗体策略和主屏隔离检查。

| 验证 | 结果 |
|---|---|
| RED | 单目标构建成功，`alternate_mode_core` 失败；strict/recovery × 六属性共 12 个反例，均在阶段 3、零基列 3 的 H/前置空格保留了旧属性。目标 0.02 秒，CTest 0.04 秒 |
| GREEN | 最小属性修改后，`alternate_mode_core` 1/1 通过；目标 0.24 秒，CTest 0.26 秒 |

命令保持单目标，`--parallel 1`：

```powershell
cmake --build local/r2-core-build --config Release --target alternate_mode_test --parallel 1
ctest --test-dir local/r2-core-build -C Release -R '^alternate_mode_core$' --output-on-failure
```

基点为 `f72c5c4f68dd7eb579ad1e523762333b135aa074` 加工作树修改。RED 精确源码未单独快照，保持 null；GREEN 的三份选定源码与 EXE 哈希在运行后采集，不冒充完整构建前清单。文档整理时的当前源码哈希另列；后续联合构建不倒填这两个运行的身份。日志只公开 SHA256 与脱敏结果，原始像素和私有路径不发布，详见 [validation.json](validation.json)。

这是 C++ 核心的限定修复，没有重新运行 Qt、ConPTY 或原版。它不关闭整个 #27、#29 或 R2，不构成性能结论。每屏 origin 的首次进入见[原记录](../2026-11-02-origin-frame/README.md)，重复进入的独立修复见[1049 reentry](../2026-11-02-1049-reentry/README.md)。
