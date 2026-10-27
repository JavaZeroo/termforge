# #29：resize 等待快照与显式清空

本切片修复候选两个公开接口反例，不关闭 #29。候选为 `0.26.1-dev+termforge.5`，系统 ConPTY；默认核心和 PR #50 实验官方后端未修改。基点为 PR #108 的 `678a7f41df8323e550b2804f664f2a3d7510d0a4`。

## 原因与行为

- resize 已更新尺寸后，pending replay 曾把旧宽度的行和旧光标放回公开 Screen；新回归实际报 `pending replay row width differs`。现在冻结最近一次完整的 resize 后状态，旧快照仅用于完成回放时的身份匹配。四个目标尺寸 2×16、2×4、1×1、3×5 按每个拆包检查尺寸、光标和宽字配对，完成时发布真实写入。
- `?25l → CSI 03J → ?25h` 曾将已清空历史从旧缓存补回，实际报 `clear during replay resurrected old history`。现在由 Rust 解析器报告主屏 ED3/RIS 清空代次，经 C ABI 传给适配器，在 pending 判断前释放旧回放和历史缓存。原生历史为空也会通知；ED2 不清历史，显式清空不计为淘汰。
- 清空回归包含 ED3/03J/RIS × 回放开始前/等待中/完成后/同一 feed 完整事务 × 整段/逐字节，共 24 组合；另有两种分块下的“原生历史空而缓存非空”和 ED2 对照。检查选择/Find 失效及后续输出不复活；旧 861B whole/byte、列位置和超限释放回归保留。

## 复验与来源

Windows 候选/system 完整 CTest **49/49**；Rust 工作区 **157 上游单元 + 6 适配/程序单元 + 1 doc test** 全通过；重建 trace 后，候选报告测试 **6/6** 通过。核心与 CTest 对应本地 `14db868`，报告版本修复对应 `eedb577`，两者 C++/Rust 源码完全相同；公开代码提交为 `3a7510a8f6df95309b62d0531d150cd592aea825`，与 `eedb577` 的 tree 一致。

实测源码、二进制、原始日志哈希、各次结果及限制见 [validation.json](validation.json)。Rust workspace、公开候选检查和完整 Windows 候选 CTest 分列；构建配置保持 `TERMFORGE_BUILD_CANDIDATE_ADAPTER=ON`、Release、系统 ConPTY。TUI 测试进程 PATH 包含已有 Git for Windows Vim，不更改系统 PATH。首次配置未完成导致的 cache 错误，以及首次补丁编译错误均与语义反例分开留存，不用它们代替 red。

新增核心逻辑是按既有一致快照/显式清空合同独立编写的 C++ 适配与 Alacritty 补丁。固定上游 commit、Apache-2.0 和注册表依赖不变；来源台账及当前 runner 版本同步到 `.5`。没有引入 WindTerm 源码、反编译输出或资源。

[原版两次 resize 观察](../../../../docs/research/windterm-resize-text.md)另列：42 条合成逻辑文本在五阶段保留，恢复尺寸后文本投影回到基线。它支持继续保留文本，不证明本修复与原版清空/冻结行为一致。

## 未完成

完整历史 reflow、跨宽度缓存行的显示、备用屏缓存、全部滚动区域交叉、连续 resize 与真实 child ack、最终官方后端、真实 DPI、实际呈现和性能仍需独立验收。现有零原生历史的缓存启发式仍有未闭合边界；本补丁不宣称解决全部历史保留。不能将局部测试或原版文本接口观察拼成 R2 完成或性能等价。
