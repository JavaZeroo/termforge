# 真实 resize 后寻址：原版字面诊断

[四个独立原版样本](../../../../research/ui/results/2026-11-02-resize-address-reference/README.md)分别固定自然追加 `Q`、`CR Q`、`LF Q` 和 `CUP 11;2 H Q`。本诊断使用相同 78 B 初始正文与 `91×20 → 61×20 → 91×20`，实际调用 `Screen::resize`，分别核对协议 DSR、文档 marker 和显示投影 marker；不把投影宽度变化当作真实 resize。

**本报告固定的默认裁剪运行结果为失败：16/64 阶段通过，896 条断言中 308 条失败，退出码 1。** 四个 case × strict/recovery × 整块/逐字节组成 16 次核心回放，每次保留四个阶段。16 个初始阶段通过；48 个缩窄、动作后、恢复阶段均失败。原版只运行过四份相应严格样本，核心 feed 数不能算作新增原版样本。第五个 EL1 原版记录在本轮归档时尚在单独补充审计，未纳入此诊断。

缩窄丢失 `v/w`，裁剪后软续接的文档坐标也随之改变。恢复宽度不能找回正文；动作后的 marker 与显示位置继续失配。各阶段发生断言失败后仍继续本次回放，以保留后续行为；测试不会改写输入、延长期限或将失败当预期通过。该结果与 [只读显示投影切片](../2026-11-02-display-projection/README.md)不矛盾：后者只投影尚未被裁剪的内容。

本报告归档到 `3c04716` 时，`resize_address_test` 是 `EXCLUDE_FROM_ALL` 的显式诊断目标，未注册 CTest；运行基点及逐文件身份仍以 [validation.json](validation.json) 为准。后续[保留列宽事务切片](../2026-11-02-retained-columns/README.md)使该目标参与默认构建，CTest 只注册显式 `--retained-columns` 入口。无参数仍走原裁剪逻辑；本报告的默认路径失败没有改成预期通过，也不因另一入口或常规 CI 通过而获豁免，#27/#29 保持开放。

```powershell
cmake --build <core-build> --config Release --target resize_address_test --parallel 1
& <core-build>/bin/Release/resize_address_test.exe
```

本轮为 Windows x64、Visual Studio 2022、MSVC 19.42.34433.0、SDK 10.0.26100.0、Release、Qt OFF、候选适配器 OFF；编译成功后运行得到上述行为失败。精确源码、二进制和本地日志哈希见 [validation.json](validation.json)。哈希为运行后采集；日志含本地路径，只公开摘要与哈希。

本诊断不核对完整 grid、空白样式、字体像素、正式 Qt/固定 ConPTY、备用屏、历史淘汰、宽字或性能。DSR 和正文/显示断言分别评价；通过某一类坐标不能推断其余行为通过。独立原版样本的输入、版本、运行来源和失败边界以原版报告为准。
