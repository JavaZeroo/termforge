# 视觉端点与输入诊断

此工具只建立可观测的测量链路。没有测量 WindTerm，也没有证明画面捕获开销符合正式基准要求。

## 自建窗口校准

```powershell
.venv\Scripts\python benchmarks/tools/calibrate_visual.py --out benchmarks/local-results/visual-new --events 10
```

会短暂打开一个独立 Tk 窗口，发送 F13 测试按键并读取其 128×64 像素标记区域。开始后不要输入或切换窗口；丢失焦点、目标退出、身份变化、按住修饰键或注入失败都会中断。仅捕获指定 ROI，不保存桌面图片。使用全新结果目录。

注入 CLI 只对自己启动并核验归属的测试进程开放。Windows 虚拟环境解释器可能通过启动器创建子进程，因此核验实际窗口进程必须属于已启动的进程树。SendInput 是全局输入，焦点检查和注入不能原子绑定；这一限制使当前注入工具只适合受控 fixture，不作为任意日常应用的自动输入器。

每批运行两个条件：不主动延迟，以及接收按键后等待 80 ms 再提交像素更新。每个条件默认 10 次。标记编码 nonce XOR sequence，全部 32 个单元格中心 3×3 像素必须匹配；陈旧标记、部分单元格更新不应通过。它验证的是标记区域，不验证完整界面。

结果文件：

- `summary.json`：每次输入提交/返回、捕获观测、目标接收和绘制提交时间；时钟往返样本、方法成本、版本与源码哈希。
- `frames.jsonl`：所有捕获调用前后时刻、匹配结果、实际间隔、匹配帧哈希。没有像素内容。
- `fixture-stderr.txt`：仅本地诊断。失败的尝试保留 `aborted`/timeout，不伪装成完成。

可用 `python benchmarks/tools/validate_visual.py <结果目录>` 校验事件、捕获时序、汇总与时钟边界的一致性。校验通过不代表性能门槛通过；完整 JSON Schema 仍待补齐。

窗口在正常/失败路径会被关闭；额外 watchdog 防止孤立 fixture 长期停留。测试不启动 WindTerm 或连接服务器。

## 时间含义

`submit_ns` 是调用 SendInput 前的 QPC/perf_counter_ns，`return_ns` 是 API 返回；API 接收事件不保证目标已处理。

采集使用可见桌面 `BitBlt` → top-down 32-bit DIB → `GdiFlush` → 内存读取，记录调用前后时刻。`capture_observed_latency_ns` = 首个匹配捕获结束 − submit。它包含采样、复制和调度延迟，**不是 compositor frame timestamp，也不是物理按键到光子的时间**。

`observation_bracket_ns` 保留上一非匹配捕获开始到首次匹配捕获结束的区间（相对 submit）。这是捕获层观测区间，不是严格物理显示时间上下界；不把多毫秒采样结果报告成微秒级显示精度。`actual_frame_gap_ns` 才是实际间隔，不能把 sleep 8 ms 当作实际 125 fps。

目标回调产生 `input_received_ns` 和 `paint_submitted_ns`，后者仅表示调用绘制更新，不证明画面已呈现。先做 20 次时钟往返，使用最小 RTT 的 `[remote−after, remote−before]` 作为偏移区间，不假定往返路径对称。端到端观测使用同一观测进程时钟，不依赖远端中点估计。

默认每组只有 10 次，报告中位数与原始样本；P95/P99 为 null。达到各自样本门槛后才计算；失败样本仍需要单独报告。100 次 capture-only 调用只是方法耗时诊断，不是“启用捕获对目标的扰动”校准。

## 固定像素模板工具

`watch_template.py` 为之后的终端结束标记适配提供录制/等待接口，不做 OCR，也不注入按键。此接口已经有像素匹配单元测试，尚未在 WindTerm 中验证。模板需要来自**已经人工确认正确的最终状态 ROI**，应同时包含结束 marker 和足够的最终状态上下文，避免只匹配空白背景。

```powershell
# 12345 / 0x123456 是占位参数，必须用隔离测试窗口的实际 PID/HWND。
.venv\Scripts\python benchmarks/tools/watch_template.py record --pid 12345 --hwnd 0x123456 --rect 0 0 128 64 --template benchmarks/local-results/my-template
.venv\Scripts\python benchmarks/tools/watch_template.py watch --pid 12345 --hwnd 0x123456 --rect 0 0 128 64 --template benchmarks/local-results/my-template --out benchmarks/local-results/watch.jsonl
```

窗口必须始终可见且位于前台；应由外部驱动启动观察器以保持目标焦点。先重置测试状态，再启动 watch，最后由独立驱动触发输出。首次画面就匹配会返回 `stale_template`，不能当作新完成；匹配前至少要观测到一次不匹配。

模板录制保存 ROI 原始 BGRA，可能包含文字；只保存在忽略的本地目录。watch 会校验模板哈希/尺寸，比较全部 RGB 通道（容差 4，忽略 GDI alpha），记录匹配时刻和哈希。字体、DPI、窗口尺寸、颜色、光标闪烁及覆盖窗口都可能影响匹配，模板必须按实际固定配置建立。

此工具尚不构成完整 T01 驱动：没有启动目标会话、自动设置字体或建立 producer 与画面同一运行 ID 的关联。模板正确性和终端历史完整性也需要另行验证。

## 待完成

- 在隔离 WindTerm 会话中验证结束文本模板和实际 T01 路径。
- 捕获/采样开启与关闭的正式扰动对照；目前旧资源校准门槛仍未通过。
- 受控终端回显区的输入驱动，不能直接把 fixture 数字当作终端输入延迟。
- 更低扰动捕获后端、完整记录 schema 和汇总校验。

依据：[Microsoft SendInput](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput)、[BitBlt](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-bitblt)、[CreateDIBSection](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createdibsection)、[Python perf_counter](https://docs.python.org/3/library/time.html#time.perf_counter)。
