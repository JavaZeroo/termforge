# 限定窗口呈现探针（V1 受限验收）

当前核心生成 3×32 的固定场景，复制成带版本号的只读快照，再通过 GDI 或 DirectWrite/Direct2D 软件 DC target 绘制。程序创建自己的非输入 desktop 和真实 `WS_VISIBLE` HWND；从不切换用户的 desktop，不接收用户点击，不捕获用户桌面。`InvalidateRect → UpdateWindow → WM_PAINT → BeginPaint` 以及消息泵驱动绘制，结束后销毁窗口、注销窗口类，GUI 线程退出后由父线程关闭 desktop。这个探针仍是独立实验；共享的 `render.hpp` 另用于 [A1 shell](../shell/README.md)。

```powershell
# Windows；先按 CONTRIBUTING.md 从根目录构建。
python experiments/presentation/run.py --bin-dir build/bin/Release --out local/experiments/presentation-new
python -m unittest experiments.presentation.test_presentation -v
```

测试通过 `TERMFORGE_BIN_DIR` 选择构建；显式路径不存在会失败，不回退旧程序。输出目录必须不存在，默认两轮固定种子随机顺序，共 8 次窗口运行；每次限时 30 秒。最后另跑真实窗口绘制后故意挂起的控制组，3 秒后终止这个不创建子进程的探针，核验进程与唯一命名 desktop 都已回收。正常运行还明确检查 `DestroyWindow` / `CloseDesktop` 成功。

保留初始/最终 BMP、独立 client DC BMP（可捕获时）、哈希、JSONL、计时原始数组与失败。CI 的 `TERMFORGE_TEST_ARTIFACTS` 指定合成产物目录；未设置时成功测试删除临时目录，失败证据保存于 `local/experiments/presentation-tests/failure-*`。

## 正确性与捕获边界

场景是 ASCII、`e` 加组合重音、中文宽字符、粗体与前景/背景色，以及按快照光标坐标绘制的黄色下划线光标。自动检查包括：

- 修改生产端状态后旧快照仍保持原文与旧光标；消费端只读整个快照版本。
- 修改中间行并移动最后一行的光标，dirty 行为 `[1,2]`。局部重绘与独立全量重绘逐像素 RGB 一致，第一行不变；旧光标消失、新光标与无光标图的差异仅落在指定单元格。
- 19 个非空文本格各自有墨迹；重音版本与不带重音的同一字母图像不同；前景/背景色独立取样。DWrite 对**实际用于绘制的同一个 layout**检查 glyph index、字体名称与布局宽度，中文和组合字符各有记录。U+10FFFF 负例必须被缺字检测器识别。
- HWND 扩大再还原，`WM_SIZE` 和最终 client 尺寸得到检查，还原图与全量图一致。这不实现终端状态 resize 或 reflow。

主要截图由 `WM_PRINTCLIENT` 复制 `WM_PAINT` 已完成的自有 retained DIB。它能证明窗口绘制路径及该快照图像，不能证明合成器最终呈现。另用 `GetDC(本任务 HWND)` 捕获 client DC，单独记录是否成功、是否与 PRINTCLIENT 一致。即使二者一致，`presented_done` 仍为 `null`；程序未调用 `SwitchDesktop`，用户没有看到这些窗口。

96 / 144 是显式逻辑缩放（1× / 1.5×），`GetDpiForWindow` 单独记录真实窗口 DPI。本机实测真实 DPI 均为 96；没有冒领跨物理显示器的 DPI 迁移。GDI 未观测实际字体链接/fallback，其 `dwrite_*` 字段是另外一个参考布局的结果，不能当作 GDI 字体正确性证明。V1 的字形覆盖受限验收以 DWrite 路径为准，GDI 保留作对照。

## 已核验结果与计时

2026-10-23（北京时间），Windows 11 build 26200 / MSVC 19.42.34433 Release：8 次运行全部通过，上述 client DC 对照均一致，4 项 unittest 通过，超时清理通过。请求字体为 Consolas，实测中文 fallback 为 Microsoft YaHei UI；`é` 使用 Consolas。19 个 glyph 均非缺字、宽度未超出对应格，组合重音与中文预览已查看。

[运行身份](results/2026-10-23/manifest.json)、[完整原始样本](results/2026-10-23/samples.jsonl)、[计时分布](results/2026-10-23/summary.json)、[超时清理](results/2026-10-23/timeout.json)、[初始图](results/2026-10-23/dwrite-96-initial.png)、[最终 1.5× 图](results/2026-10-23/dwrite-144-final.png)。数据和图像只有本程序的合成内容；未分发系统字体文件。历史 [2026-10-22 离屏实验](results/2026-10-22/manifest.json) 保持原样，其 `window_verified=false` 只描述当时的实现。

每种配置 2 次进程运行、各 24 次暖资源绘制，共 48 个 draw / submission 样本。字体/笔刷、DWrite factory/target/format 与固定场景的 text layout 复用；缓存仅在这个有界固定场景中使用，尚无通用淘汰策略。

| 字段 | 端点与限制 |
|---|---|
| `draw_samples_ms` | 在 WM_PAINT 内从更新 retained DIB 前到绘制完成（含 EndDraw/GdiFlush）；不含 BeginPaint、完整消息调度或外部呈现 |
| `frame_submit_samples_ms` | 从 BitBlt 前到 GdiFlush 返回；即 client DC 提交 API 时长，不是帧上屏时间 |
| `capture_ms` | 一次 WM_PRINTCLIENT 与 GdiFlush；捕获/文件写入不在 24 次绘制样本内 |
| `timer_pair_samples_ms` | 128 对相邻时钟读数，观察计时粒度与读数成本；不是整个观察器的校准 |

本批 draw 中位数约 1.27–3.30 ms；同一配置 min/max 跨度明显，原始分布保留。相邻计时器读数大量为零，p95 约 0.0001 ms；零不表示时钟调用没有成本。未完成无观察器对照、独立机器/批次或合成器端点校准，因而不根据这些短样本发布后端性能排名或 WindTerm 性能结论。

按 [ADR-0004](../../docs/adr/0004-presentation-backend.md) 的 DWrite 路线已接入固定网格 A1。共享缓存新增 1,024 项上限；本探针的历史结果仍保留原始身份，不冒充当前构建。物理 DPI 切换、完整字体/字素/整形、长时资源与真实呈现测量继续保留为缺口。A1 集成时还复现了资源析构后切出桌面的 ERROR_BUSY；改为独立 GUI 线程完整退出再关闭 desktop，验证见[第三批记录](../../docs/research/third-batch-a1-2026-10-23.md)。

接口参考：[WM_PAINT](https://learn.microsoft.com/en-us/windows/win32/gdi/wm-paint)、[WM_PRINTCLIENT](https://learn.microsoft.com/en-us/windows/win32/gdi/wm-printclient)、[CreateDesktop](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-createdesktopw)、[Direct2D DC render target](https://learn.microsoft.com/en-us/windows/win32/api/d2d1/nn-d2d1-id2d1dcrendertarget)、[DrawGlyphRun 回调](https://learn.microsoft.com/en-us/windows/win32/api/dwrite/nf-dwrite-idwritetextrenderer-drawglyphrun)。源码为按公开接口独立实现。
