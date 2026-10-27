# #29：主屏历史缓存归属与容量淘汰

本切片修复候选的透明输入丢历史、主/备用屏缓存归属和重复淘汰问题。基点为 PR #109 的 `6a108e1d15f00dc9497dac79a8bf75a3559469f8`，候选升至 `0.26.1-dev+termforge.6`；固定上游、59 个注册表依赖及许可证不变。它不关闭 #29，不代表正式采用 Alacritty，也不改变默认核心或 PR #50 实验官方后端。

## 原因与行为

- 原生历史为零曾被推断为显式清空，导致 resize 重绘后只输入空串就丢失缓存 A/B。现在由解析器清空代次裁决 ED3/RIS，空串、NUL、DSR 保持历史及已有选择/Find；ED3 仍使历史与文字动作失效。
- 主屏缓存与回放来源现在共同保存拥有值 Screen、原生行别名及淘汰端点，进入备用屏时挂起，退出时恢复。备用屏不显示主屏历史；备用屏 ED3 保留主屏缓存，RIS 使两屏旧缓存失效。覆盖整段/逐字节、合并切换、pending 回放中断与备用屏 resize。
- 原生淘汰按实际消失的原生身份去除旧行；缓存独有行的淘汰另行累计。容量裁掉仍驻留 Rust 的行时，C ABI 先核对整个最旧前缀的 ID，再删除实际历史，保持配置容量。已淘汰的缓存行还须从旧回放快照退休；删除发生在行版本比较之后，避免使旧行指针失效。

两项独立审查反例均保留原始 red 与修复后 green：

| 反例 | 修复前 | 修复后 |
|---|---|---|
| 旧原生 A 与较新缓存 D 交错；容量留下 B/C/D/E | 在 `b966adc` 上，一次空 feed 使淘汰数 1→2 | 内容保持，计数保持 1；整段/逐字节及增高不复活 A 的回归通过 |
| 缓存 A/B 后缩高至 1 行，旧回放仍含已淘汰 A | 在 `93b18db` 上，连续三次空 feed 使计数 1→2→3→4 | 历史 B/C/D/空行与计数 1 保持；后续 pending/备用屏切换不恢复 A |

公开 `CandidateTerminal::feed/resize` 回归保留原 861B 重放、列位置和显式清空检查。另一个 Rust FFI 测试核对错误第二行 ID 时完全不修改、备用屏拒绝、正确前缀移除、零数量 no-op、容量保持和增高不复活；它只验证接口合同。

## 验证与复现

Windows 11 x64 候选/system 完整 CTest **49/49**；其中 `paste_system` 内部有 **1 项跳过**：私有 window station 的 `OpenClipboard` 返回 5，真实系统剪贴板门槛仍未验证。Rust 工作区 **157 上游单元 + 7 适配/程序单元 + 1 doc test** 通过，重建 trace 后候选报告测试 **6/6** 通过。Standards 与 Spec 最终代码审查均为 0 项。

源码提交、tree、每个源码 blob/工作文件 SHA-256、二进制及本地原始日志哈希见 [validation.json](validation.json)。本地审查代码为 `4940050`，公开代码 `1c1a64e59803e2cd0be6e07a4cd4a052705f5a7e` 与它使用相同 tree；最终结果只对应候选/system/Release，不能转用于默认配置或实验官方后端。早期两轮完整套件虽通过，随后仍发现覆盖外反例，已分列保存。

```powershell
$env:RUSTUP_TOOLCHAIN = '1.92.0'
# 本次使用已有 Git for Windows Vim；仅修改测试进程环境。
$env:PATH = 'C:\Program Files\Git\usr\bin;' + $env:PATH
cmake --build build/candidate-adapter --config Release
cargo test --manifest-path experiments/candidates/alacritty/Cargo.toml --locked --workspace
cargo build --manifest-path experiments/candidates/alacritty/Cargo.toml --locked --release
python -m unittest experiments.candidates.alacritty.test_candidate -v
ctest --test-dir build/candidate-adapter -C Release --output-on-failure
```

构建配置与前置见[候选说明](../../CANDIDATE.md)。过程日志留在本地；公开内容只有合成场景、脱敏观察与哈希。新增逻辑是按已有历史/清空合同独立编写的 TermForge 适配及固定 Apache-2.0 Alacritty 补丁，没有引入 WindTerm 私有源码、反编译输出或资源。本轮未运行原版，也未做性能测量。

## 仍失败及未完成

在同一最终代码上，2×8、history=4 输入 A/B/C/D，resize(4,4) 后重绘 C/D，滚至历史首行的快照仍为 **8、8、4、4** 列，当前屏幕宽度为 **4**；独立形状检查退出 1。随后空 feed 的历史数保持 **2**，明确区分已修复的丢历史与未修复的混宽视口。未执行该反例的实际渲染，不宣称已观察到崩溃，也不通过永久裁剪文本掩盖它。

完整逻辑/物理行映射、增长后的全部顺序与 reflow、连续 resize、备用屏/滚动区组合、原版选择/Find 保留行为及真实 child ack 继续归 #29。真实不同 DPI、完整系统输入组合、实验官方后端、实际呈现、性能及历史可靠性缺口各自验收；局部通过不构成 R2 出口或原版等价。
