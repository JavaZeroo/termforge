# 固定候选来源与本地补丁

当前适配版本为 `alacritty_terminal 0.26.1-dev+termforge.10`，不是 crates.io 原始 `0.26.0`。

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

## termforge.5 补丁

1. `src/term/mod.rs`：记录解析器实际执行的主屏 `ED3` 和 `RIS` 清空代次，区分显式清空与历史淘汰；原生历史为空时仍通知外部缓存。备用屏 `ED3` 和 `ED2` 不递增代次，`RIS` 清空两屏后继续保留单调代次。
2. C ABI 与 C++ 适配器同步读取代次，在 resize 回放期间释放旧历史和身份缓存，避免显式清空后复活。公开 `feed/resize/Screen/TextSelection/TextSearch` 接口回归覆盖该行为，不依靠对原始字节搜索 `CSI 3J`。
3. 版本升为 `0.26.1-dev+termforge.5`，未改变固定上游 commit、第三方依赖版本或许可证。此补丁仍是候选实验，不代表正式产品选型或 WindTerm 清空行为已对照。

## termforge.6 补丁

1. `src/grid/mod.rs`：增加最旧历史前缀的容量淘汰操作，保持配置容量，裁掉实际存储中的旧行并累计原生淘汰数；与不计淘汰的显式清空分开。
2. C ABI 在修改前核对待淘汰主屏原生前缀的全部稳定 ID。C++ 合并缓存达到容量时同步移除仍驻留的原生旧行，避免空同步重复导入/淘汰或增高时复活。缓存独有行的淘汰单独累计；公开接口覆盖交错历史、重复空同步及增高不复活。
3. 候选 C++ 快照保留原生身份映射及淘汰端点，主屏缓存跨备用屏切换挂起/恢复，空原生历史不再推断为清空。容量淘汰也使旧回放快照中的缓存行失效，在行版本比较结束后才修改来源快照。相关实现不是 WindTerm 私有代码；原版完整 resize/显示语义仍需对照。
4. 升为 `0.26.1-dev+termforge.6`，同步当前报告身份；固定上游、注册表依赖与许可证未变。

## termforge.7 补丁

1. `src/grid/resize.rs`：原硬断行跟随拆分后的最终片段；内部软折行使用当前宽度。拓宽吞入空 continuation 时，将其硬边界转给接收行，保留后续独立空硬行；不改变已有单元格文字或光标重排算法。
2. 缩宽使物理行数量超过历史容量时，实际删除的最旧行计入原生淘汰数。相同尺寸、空同步和显式清空不重复计数；物理行合并不视作容量淘汰。
3. Grid 回归覆盖硬行/软链拆分、buffered continuation、完整/部分合并和空硬边界；公开 C ABI 的真实 feed/resize 校验导出边界及容量淘汰。C++ 长行复制回归仍失败，旧整行缓存与原生重排片段的来源归属尚未修复；本版本不宣称混宽历史、复制/Find、完整 reflow 或 #29 验收通过。
4. 升为 `0.26.1-dev+termforge.7`，固定上游、注册表依赖和许可证未变。新增补丁为独立实现，没有引入 WindTerm 私有源码或资源。

## termforge.8 补丁

1. `src/grid/origins.rs` 与 Row 的可选临时表记录一次 resize 的物理来源区间及独立硬边界；拆分、合并、宽字 spacer、容量删除、高度裁剪和光标重定位同步转移来源，不按文本去重。每行只新增可选表指针，未给每个 Cell 永久增加来源字段。
2. `Term::resize_with_map` 捕获主/备用屏，返回后清理全部 storage 槽的追踪状态；普通 `Term::resize` 不捕获。C ABI 暴露拥有值、epoch 和明确的失效规则，不将后续 feed 误认为仍保留旧来源。
3. 修复高度缩小一次滚动超过旧历史数量时的来源漏记：把同步移出容量的原可见行也记录为移除，保留其硬边界。普通顶部滚动按实际超容量行数计账，修复多行滚动少计；扩高为搬移合成空行而执行的滚动不计淘汰。公开 FFI 反例和 C++ 静态库调用覆盖来源、计数、生命周期及双屏命名。
4. 升为 `0.26.1-dev+termforge.8`；固定上游、注册表依赖和许可证未变。映射补丁独立实现，没有引入 WindTerm 私有代码或资源。C++ 历史缓存的源文本重复与空硬行丢失仍未修复，性能成本未测，完整 #29/R2 保持未完成。

## termforge.9 补丁

1. `Term::feed_with_origin_map` 复用临时稀疏来源表，追踪一次真实 parser feed 后仍存活的旧物理格和硬边界。实际覆盖、擦除及字素续接会使相应旧来源失效，即使新旧文字相同；插入、删除和滚动按实际移动传递来源。普通 feed 不分配来源表，不将可变借用误当写入。
2. 不封闭跨 feed 的待续字素；主/备用屏按事务前后真实切换正确命名。公开 Rust 集成测试核对同字重写、宽格关联清除、字素增长/缩减、插删擦除、硬边界、ALT/RIS 及后续 resize，不依赖内部 helper。
3. 修正非底行自动软换行后残留的硬边界元数据；文字、格内容、光标和原有 WRAPLINE 行为不变。C ABI 新增显式 `tf_terminal_feed_mapped` 和 feed 查询，复用物理来源记录布局，与 resize 共用单调 epoch 并拒绝错误操作类型或过期查询。
4. 移除记录是事务中的事件，不是完整擦除/先后顺序日志，也不授予外部历史保留许可。同一 feed 先淘汰、后 ED3/RIS、再输出时，最终清空代次不能区分清空前后的移除事件；调用方不能先清空缓存再无条件补回全部移除片段。
5. 升为 `0.26.1-dev+termforge.9`；固定上游、依赖和许可证不变。新增分支及映射分配成本未测；该检查点的 C++ 历史合并尚未消费来源映射，长历史复制仍失败。后续适配状态见 [CANDIDATE](../../core/CANDIDATE.md)，完整 #29/R2 验收继续开放。

## termforge.10 补丁

1. `Term::discard_oldest_history` 只淘汰历史前缀，不经过会封闭待续字素的通用 `grid_mut()`；可见格与光标不因历史维护改变。FFI 保持完整前缀校验、计数及成功调用使临时来源映射失效的合同。
2. 公开 C ABI 反例确认合法空/非空淘汰曾将跨 feed 的 `👩‍💻` 拆成两格字素加另一字素，光标列从预期 2 变为 4。修复后完整文本、宽度/spacer、光标、映射有效期通过；错误前缀控制不改状态。
3. 升为 `0.26.1-dev+termforge.10`，上游 commit、注册表依赖及许可证不变。本补丁为独立维护修复，不含 WindTerm 私有源码；不裁决原版覆盖政策或证明性能等价。

## 历史基线

#24–#26 的原始候选为 crates.io `0.26.0`，crate SHA-256 `bda177466b9524d59f1b12f0dd30b68696788e9992a7e959021c4a0ed96fcf59`，对应上游 commit `94e7c8874e526b1e67b349d9ba30ddf81669119e`。当时的 90 match + 5 reference_difference、project/interactive 计量和平台/维护审查保持原批次身份，不改写为 termforge.1/2 结果。

termforge.1 是首个 ED/资源/身份 headless 切片；termforge.2 增加滚动复用、初始顺序、历史淘汰和实际 C++/C ABI 适配，旧批次不覆盖新补丁。
