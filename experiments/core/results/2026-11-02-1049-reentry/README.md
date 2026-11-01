# 1049：重复进入与再次进入时关闭备用屏 origin

自主 C++ 核心现在在每次 `CSI ?1049h` 时关闭备用屏 origin，再归首格。主屏 origin 仍随独立帧保存，`1049l` 返回时恢复；不会让备用屏的显式 ON 改写主屏状态。

依据为[原版模式目录](../../../../research/ui/results/2026-11-02-mode-state-reference/README.md)的第三个独立新会话 [alternate-origin-reentry](../../../../research/ui/results/2026-11-02-mode-state-reference/alternate-origin-reentry.json)。此前 [main ON / alt OFF](../../../../research/ui/results/2026-11-02-mode-state-reference/main-on-alt-off.json)与 [main OFF / alt ON](../../../../research/ui/results/2026-11-02-mode-state-reference/main-off-alt-on.json)只覆盖首次进入和主屏恢复；其 [origin-frame](../2026-11-02-origin-frame/README.md) 源码、31 组和 15 项回归身份原样保留，不改写为已经包含本次修复。

## 新观察及核心合同

固定原版 WindTerm 2.7.0 x64、91×20 的第三组共 14 个阶段：先将主屏和备用屏 origin 明确设为 ON，检查重复 `1049h`；返回主屏后再进入，检查同样的未改动样本，并各设 ON/OFF 对照。直接 Telnet 输入不经过 ConPTY，使用直接 DSR 与公开 marker 位置，不读取内部模式字段。

| 样本/对照 | CUP(99,1) 后的 DSR | marker 的核心字面物理行（零基） |
|---|---|---:|
| 重复进入后未显式改 origin：DS | `(20,1)` | 0 |
| 显式 ON：DI | `(5,1)` | 2 |
| 显式 OFF：DO | `(20,1)` | 0 |
| 返回后再次进入、未显式改 origin：ES | `(20,1)` | 0 |
| 显式 ON：EI | `(5,1)` | 2 |
| 显式 OFF：EO | `(20,1)` | 0 |

两个未改动样本均与 OFF 对照一致；两次返回主屏的 DSR 均为 `(2,12)`，保留主屏写入位置。表中核心物理行是按协议和原版选定 marker 观察固定的字面预期，不直接复制原版合并文档的行号，也不是完整网格对照。

[decom_origin_test.cpp](../../decom_origin_test.cpp)新增这一组 14 段精确源、14 条 DSR 和六个 marker 检查，strict/recovery × 整段/逐字节共四次回放、56 个阶段。三组原版总计 42 个独立阶段；回放次数不增加原版样本数。文档整理另核对了新 C++ 源与探针源逐字一致、DSR 与本地原件逐条一致。

## 红绿与身份

| 验证 | 结果 |
|---|---|
| RED | 构建成功，`decom_origin_core` 31/35 组通过；新增四种回放均在 duplicate/later 样本的 DSR 和 marker 失败。目标 0.21 秒，CTest 0.23 秒 |
| GREEN | 增加备用屏 origin 关闭后，35/35 组、CTest 1/1 通过。目标 0.27 秒，CTest 0.29 秒 |

```powershell
cmake --build local/r2-core-build --config Release --target decom_origin_test --parallel 1
ctest --test-dir local/r2-core-build -C Release -R '^decom_origin_core$' --output-on-failure
```

基点仍为 `f72c5c4f68dd7eb579ad1e523762333b135aa074` 加工作树修改。这两次运行未单独保存完整源码/EXE 快照，相应字段保持 null。当前选定源码哈希独立记录，其中已包括后来加入的[六属性修复](../2026-11-02-1049-attributes/README.md)，不能将它当作较早 GREEN 的精确源码。日志只公布哈希和脱敏结果，详见 [validation.json](validation.json)。

本记录没有重跑测试，没有覆盖 resize、完整样式/网格、Qt/固定 ConPTY、TUI、实际呈现或性能；不关闭 #27 或 R2。后续统一回归保持独立运行身份。
