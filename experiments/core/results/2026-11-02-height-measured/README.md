# 固定 91 列下的纯高度字面回放

[原版三阶段观察](../../../../research/ui/results/2026-11-02-resize-height-measured-reference/README.md)中，75 字节合成正文在 `91×20 → 91×14 → 91×20` 保持相同；DSR 为 `19;7 → 14;7 → 14;7`，缩高后七个 marker 上移五行，恢复高度后保持该位置。原公开结果 SHA256 为 `32d3f6cf2d1c6746ff517b684ecd8c631c86ecc87874209ba5f831ea244730e0`。

现有 `Screen::resize` 在宽度固定 91 时通过该字面回放：strict/recovery × whole/bytewise，共 **12/12 阶段、596 断言零失败**。其中 176 个独立工程断言检查自有 history 与 ID/version；原版 `top=null`、`scroll=5` 并不证明其内部正好保存五个物理历史行。显示 marker 对比不使用原 API 文档 offset 作为 Cell 列，也不是完整 raster 或 active-grid 验收。仅忽略末端空字符串容量，内部空行和空格精确保留。

首轮测试夹具误把 `source_end=200` 传给只有 20 行的文档，四种 feed 都在第一阶段被有界接口拒绝：实际 0/0 完成阶段、4/88 断言失败。原源、EXE、构建及运行日志已保存。只改为实际 `retained_size()`，原版预期和生产代码都未修改；这不是产品 RED→GREEN 修复。修后注册 CTest 单项 **1/1**，0.04 s。

测试提交 `d4c79efb5a0ed1f0968682b4271d207923476417`；实际构建执行时 HEAD 为 `ef1da6e`，测试/CMake 为未提交改动，之后的源字节与提交逐一核对。成功产物及源身份在运行后固定，无运行前 manifest；详见 [validation.json](validation.json)。该单项在之前的 31/31 核心、4/4 Qt 整合之后执行，不回填成同一次 32 项整合。

这里只验证纯高度已有行为。先缩宽再改高度时，物理 resize 仍可能裁掉显式保留的右侧正文；联合宽高、非默认 margin/origin、备用屏、后续写入与完整 Qt 路径继续开放，#27/#29 未关闭。
