# WindTerm 2.7.0：请求窗口高度与终端区域的两状态几何

一次独立新进程请求 `1360×749 → 1360×569`，两次实际窗口都达到请求尺寸；NAWS 与终端容量从 **91×20 → 91×14**。终端区域没有得到全部 180 px 的窗口高度变化。此次没有发送终端正文或 DSR，只记录窗口、NAWS、容量与有限 widget 几何，因此正文保留、光标及恢复高度策略仍未知。

[strict-export.json](strict-export.json) 是 VM 严格导出的原字节，SHA256 `f18a2320c073ed477f773b42076c9968c0c8af8530af19548d5695e66e8cf9b5`；本地核对范围见 [audit.json](audit.json)。执行源码基点 `3d4c8c2d6222caf0fc024729d9e0b34c020a69cf`，源 ZIP SHA256 `bee6194a440e730ca18f72059c607b3d16882f4397025e3e92460efdf2c10d7f`，单次计划 SHA256 `0b3f6723183bbd82f1ae3342f6980c1c6ddbba09ccd9173f3b19443e55d9ad67`。本地复核 9 个执行源、2 个导出源、归档清单以及全部 5 个公开快照的标量与父图；完整原包与私有 raw/provider 核验由此次 VM 严格出口完成，本地没有重开那些原件。

## 直接观察

| 测量 | 初始稳定 | 缩高后稳定 | 高度变化 |
| --- | --- | --- | ---: |
| 请求/实际窗口，px | 1360×749 | 1360×569 | −180 |
| NAWS，列×行 | 91×20 | 91×14 | −6 行 |
| terminal capacity | 20 | 14 | −6 行 |
| TerminalView，px | 906×379 | 906×259 | −120 |
| Scintilla editor，px | 906×379 | 906×259 | −120 |
| terminal viewport，px | 888×377 | 888×257 | −120 |
| 终端所在 splitter 上部子区域，px | 1360×439 | 1360×319 | −120 |
| 同一父 splitter 的下部子区域，px | 1360×219 | 1360×159 | −60 |
| 两子区域的父 splitter，px | 1360×659 | 1360×479 | −180 |

上部、下部和父节点分别对应两份稳定快照中的局部节点 5、18、6；这些序号仅在各记录内定位，不是可跨运行复用的对象地址。两个快照都唯一绑定 terminal/editor/viewport，节点数各 21。窗口 QRect 是 inclusive LTRB，初始 `[102,102,1461,850]`，结束 `[102,102,1461,670]`。viewport global origin 两次均为 `(330,196)`；parent-local 矩形与 global origin 分列，不混算。

此处可直接确认：窗口实际少了 180 px，终端 viewport 只少了 120 px，另一子区域少了 60 px。这个配对解释了先前把窗口高度变化全部算作终端行高度的预测为何不成立；它**不证明一般布局比例、splitter 内部算法、固定行距或其他窗口尺寸的行为**。

## 同步过程与局限

共 5 个快照：初始 NAWS、初始稳定、请求返回、后续 NAWS、结束稳定。请求返回时实际窗口已经是 569 px，terminal capacity 已为 14，peer 仍记录 20 行；157 ms 后读到 NAWS 14 行。结束稳定快照与最近 NAWS 快照相隔 618 ms，符合固定的 600 ms 联合稳定守卫。这些时间只说明本次采样顺序，不能作延迟或性能结论。

严格结果保留 `status=observation`、`semantic_acceptance=false`。终端应用输入、DSR 与应用回复均为 0，文档行数两次为 1；不是正文 resize 验收。拥有进程的请求 exit 0，RootReaped/CleanupVerified 为 true、JobAfterActive 为 0；仍不证明自然 GUI 退出。

本次结果不能回填[旧 height-only 失败](../2026-11-02-resize-height-reference/README.md)中未采集的实际窗口，也不把旧计划要求的 10 行改成 14 行。旧 `width-then-height` 仍未运行。下一步使用独立命名和源码身份的纯高度样本，固定 `91×20 → 91×14 → 91×20`：恢复端由新运行实际检验，正文、DSR、历史与恢复行为按未知目标继续观察。
