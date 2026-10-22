# 离屏呈现探针（V1 未完成）

用当前核心构造同一场景，分别通过 GDI 和 DirectWrite/Direct2D 软件 DC target 画入本程序创建的 DIB。只读取自己的位图，不捕获用户桌面。这里没有实际窗口、事件循环或可交互终端，尚不选择产品后端。

```powershell
# Windows；先按 CONTRIBUTING.md 从根目录构建。
python experiments/presentation/run.py --bin-dir build/bin/Release --out local/experiments/presentation-new
```

输出目录必须不存在，每个子进程限时 30 秒。保留初始/最终 BMP、哈希、JSONL 和失败。字体请求为 Consolas，实际 fallback 字体身份尚未知。

场景包含 ASCII、`e` 加组合重音、中文宽字符、粗体与前景/背景颜色，分别使用 96/144 DPI。先画完整场景，再修改一行并局部重绘；与另一次完整绘制逐像素比较 RGB。GDI 的 alpha 未定义，不参与比较。`Cursor_` 是文字中的下划线，**尚未实现真实光标绘制**。

2026-10-22 Windows 11 build 26200 / MSVC 19.42.34433 Release：4 种配置的局部更新均与全量绘制一致，输出非空。DirectWrite 96 DPI 的自有初始位图还经过人工图像检查，中文、重音与颜色可见。[身份](results/2026-10-22/manifest.json)、[原始记录](results/2026-10-22/samples.jsonl)、[预览](results/2026-10-22/dwrite-96-initial.png)。非空与增量一致性不能独立证明所有字形正确，也不是跨后端像素一致性要求。

`draw_probe_ms` 包含创建字体/绘制资源、几次绘制、状态操作、检查及写文件；不是稳态绘制性能，不能据此比较 GDI/DirectWrite 快慢。`window_verified=false`、`presented_done=null` 保留真实边界。

V1 剩余：真实窗口和可控退出、只读快照/更新边界、光标、窗口 DPI/resize、字体 fallback 与布局验证、绘制/帧提交/可观测呈现端点及扰动误差，最后再形成后端 ADR。本探针的逐格字体创建仅为受限正确性检查，不作为产品实现。

接口参考：[DirectWrite 入门](https://learn.microsoft.com/en-us/windows/win32/directwrite/getting-started-with-directwrite)、[Direct2D DC render target](https://learn.microsoft.com/en-us/windows/win32/api/d2d1/nn-d2d1-id2d1dcrendertarget)。源码为独立实现。
