# 固定候选来源与本地补丁

当前适配版本为 `alacritty_terminal 0.26.1-dev+termforge.1`，不是 crates.io 原始 `0.26.0`。

## 上游身份

- 上游仓库：https://github.com/alacritty/alacritty
- 固定 commit：`542147067dd016fc5f49120ef0ba605a98a4e3a7`
- 固定目录：`alacritty_terminal/`
- 许可证：Apache-2.0；`vendor/alacritty_terminal/LICENSE-APACHE` 为上游根许可证的实体副本，不保留指向仓库外的符号链接。
- 固定内容：`Cargo.toml`、`CHANGELOG.md`、`LICENSE-APACHE`、`src/`。未复制约 46 MiB 的上游 `tests/` 行为录制；内联单元测试和 doc test 保留并执行。
- 选择此 commit 是因为其 0.26.1-dev changelog 已包含 PTY non-blocking、ViMotion、逐格零宽字符内存上限和零列 Row 修复。它不代表正式 0.26.1 已发布。

`termforge.1` 的 `Cargo.lock` 固定 59 个注册表依赖包，连同固定 path crate 共 60 个包。当前最高声明 Rust 版本为 1.85，实际验证工具链为 1.92.0；升级时须重新审查整个依赖图，不能沿用旧 0.26.0 锁定图的 1.88 结论。

## termforge.1 补丁

1. `src/term/mod.rs`
   - ED1：`ClearMode::Above` 在光标位于第 1 行时也清理页首前序行。
   - ED2：`ClearMode::All` 只清活动页，不把可见行迁移到历史，保留完整 damage。
   - 在 LF/wrap 路径维护 hard-break 与 wrap-columns。
   - 添加 ED1/ED2、软硬换行和备用屏身份回归。
2. `src/grid/row.rs`
   - 增加全局单调 `u64` 行 ID，clone/移动保留，新行/serde 反序列化分配新 ID，耗尽时失败。
   - 增加 hard-break/wrap-columns，reset 保留 ID 并清除换行元数据；serde 线格式不增加字段。
3. `src/grid/storage.rs`
   - 删除只交换 4 个机器字的 unsafe Row 布局优化，改为安全 `slice::swap`，保证 cells、ID 和元数据一起交换。
4. `Cargo.toml`
   - 固定独立构建所需的 edition/MSRV，并以 `0.26.1-dev+termforge.1` 明确标识补丁版本。

原始上游与本目录的差异可用固定 commit 对比复核；差分/真实应用/计量 manifest 另记录每个 vendor 文件的 SHA-256。后续补丁必须更新版本、测试和本文件，不得覆盖来源或继续称为未修改上游版本。

## 历史基线

#24–#26 的原始候选为 crates.io `0.26.0`，crate SHA-256 `bda177466b9524d59f1b12f0dd30b68696788e9992a7e959021c4a0ed96fcf59`，对应上游 commit `94e7c8874e526b1e67b349d9ba30ddf81669119e`。当时的 90 match + 5 reference_difference、project/interactive 计量和平台/维护审查保持原批次身份，不改写为 termforge.1 结果。
