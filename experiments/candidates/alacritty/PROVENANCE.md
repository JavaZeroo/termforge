# 固定候选来源与本地补丁

当前适配版本为 `alacritty_terminal 0.26.1-dev+termforge.4`，不是 crates.io 原始 `0.26.0`。

## 上游身份

- 上游仓库：https://github.com/alacritty/alacritty
- 固定 commit：`542147067dd016fc5f49120ef0ba605a98a4e3a7`
- 固定目录：`alacritty_terminal/`
- 许可证：Apache-2.0；`vendor/alacritty_terminal/LICENSE-APACHE` 为上游根许可证的实体副本，不保留指向仓库外的符号链接。
- 固定内容：`Cargo.toml`、`CHANGELOG.md`、`LICENSE-APACHE`、`src/`。未复制约 46 MiB 的上游 `tests/` 行为录制；内联单元测试和 doc test 保留并执行。
- 选择此 commit 是因为其 0.26.1-dev changelog 已包含 PTY non-blocking、ViMotion、逐格零宽字符内存上限和零列 Row 修复。它不代表正式 0.26.1 已发布。

`termforge.3` 的 `Cargo.lock` 固定 58 个注册表依赖包，连同根适配器与固定 vendor 两个 path crate 共 60 个包。当前最高声明 Rust 版本为 1.85，实际验证工具链为 1.92.0；升级时须重新审查整个依赖图，不能沿用旧锁定图的 1.88 结论。

## termforge.2 补丁

1. `src/term/mod.rs`
   - ED1：`ClearMode::Above` 在光标位于第 1 行时也清理页首前序行。
   - ED2：`ClearMode::All` 只清活动页，不把可见行迁移到历史，保留完整 damage。
   - 在 LF/wrap 路径维护 hard-break 与 wrap-columns。
   - 添加 ED1/ED2、软硬换行、初始/滚动/备用屏身份及淘汰回归。
2. `src/grid/row.rs`
   - 增加全局单调 `u64` 行 ID，clone/移动保留，新行/serde 反序列化分配新 ID，耗尽时失败；滚动复用时显式更新 ID。
   - 增加 hard-break/wrap-columns，普通内容 reset 保留 ID 并清除换行元数据；serde 线格式不增加字段。
3. `src/grid/mod.rs`
   - 滚动插入的新逻辑行更新 ID，保留被移动行的 ID；记录有界历史实际淘汰数，显式清历史不冒充淘汰。
4. `src/grid/storage.rs`
   - 删除只交换 4 个机器字的 unsafe Row 布局优化，改为安全 `slice::swap`，保证 cells、ID 和元数据一起交换。
   - 按 Storage 的真实 top/bottom 索引方向分配初始 ID，不能只依赖 `resize_with` 或物理 Vec 顺序。
5. `Cargo.toml`
   - 固定独立构建所需的 edition/MSRV，并以 `0.26.1-dev+termforge.2` 明确标识补丁版本。

## termforge.3 补丁

1. `src/term/mod.rs`
   - 为非活动屏幕保存独立 `inactive_scroll_region`，`swap_alt` 与网格一起交换两份滚动区域。
   - resize 与 RIS 同时重置两份滚动区域；公开 C++ feed 覆盖整段/逐字节、主屏 `2;3r`、备用屏 `4;5r`、恢复后局部滚动及 resize/RIS 边界。
2. `Cargo.toml`
   - 以 `0.26.1-dev+termforge.3` 标识补丁，未增加第三方依赖。

真实 ConPTY 的宿主 margin 差异由 [#41](https://github.com/JavaZeroo/termforge/issues/41) 保持开放；差异观察固定为 `margins_per_buffer_verified=false`，不作为产品能力通过。WindTerm 定向静态证据只用于 screen 状态归属分析，不等于运行时恢复或等价证明。

原始上游与本目录的差异可用固定 commit 对比复核；差分/真实应用/计量 manifest 另记录每个 vendor 文件的 SHA-256。后续补丁必须更新版本、测试和本文件，不得覆盖来源或继续称为未修改上游版本。

## termforge.4 补丁

1. `src/term/grapheme.rs` 与 Handler 边界：立即可见的流式扩展字素；首样式、透明查询、mutation seal、宽度差插入/剪裁、64 标量/256 字节无损分片与有界诊断；不复制整屏或整行。
2. `src/term/cell.rs`：用有界后缀保存完整聚类，去除旧 9 个零宽字符静默丢弃；保留 clipped / fragment 身份。原始第三方版权和 Apache-2.0 不变。
3. `src/grid/resize.rs`：一列目标不再无穷重排 wide 格，保留完整文本为明确 clipped footprint。普通多列 reflow 策略不变。
4. 精确新增 `unicode-segmentation = 1.13.3`（Unicode 17，MIT OR Apache-2.0，MSRV 1.85.0，无运行时依赖），精确固定既有 `unicode-width = 0.2.2`（Unicode 17）。当前 59 注册表包、两个 path crate；锁定图最高声明 MSRV 1.85。
5. 本地规范测试资料 `tests/data/GraphemeBreakTest-17.0.0.txt` 来自 Unicode 17 官方数据，SHA-256 `e2d134d2c52919bace503ebb6a551c1855fe1a1faec18478c78fff254a1793ec`，766 个用例；附独立 `UNICODE-LICENSE.txt`。这不是声称复制了被排除的 Alacritty 全部录制。

完整控制/宽度/资源和字体策略见[流式字素合同](../../../docs/research/streaming-graphemes.md)。固定 WindTerm 的运行时字素/宽度仍未知。

## 历史基线

#24–#26 的原始候选为 crates.io `0.26.0`，crate SHA-256 `bda177466b9524d59f1b12f0dd30b68696788e9992a7e959021c4a0ed96fcf59`，对应上游 commit `94e7c8874e526b1e67b349d9ba30ddf81669119e`。当时的 90 match + 5 reference_difference、project/interactive 计量和平台/维护审查保持原批次身份，不改写为 termforge.1/2 结果。

termforge.1 是首个 ED/资源/身份 headless 切片；termforge.2 增加滚动复用、初始顺序、历史淘汰和实际 C++/C ABI 适配，旧批次不覆盖新补丁。
