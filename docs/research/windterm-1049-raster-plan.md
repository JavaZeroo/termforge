# 1049 当前样式：一次受控 Qt 栅格调查计划

状态：计划已准备，尚未执行。基点为 `72f40e10a8db0ef7585f8bec82f2be72f78c3d33`；等待 [#123](https://github.com/JavaZeroo/termforge/issues/123) 修订复核后再启动新原版批次。只调查进入、重复进入及退出后的当前前景/背景色，不重复完整模式矩阵，不改核心。

## 固定目标与边界

- 官方 WindTerm 2.7.0 Windows x64 EXE SHA：`7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0`；4,047 文件清单 SHA：`e459adf2f12adb6a1b7f55e66a9da1c22794185e66e848057d21e0c60d669203`。
- Qt5Widgets SHA：`3788c669d4b645e5a576de9fc77fca776bf516d43c89143dc2ca28291ba14358`；Qt5Gui SHA：`5e7d2d41b8b92a880e83b8cc0ca173f5da61218604186196787ee1600956be1e`。导出、局部指令和 Qt 5.15.2 官方头文件已只读核对。
- 继续使用独立 WindowStation/Desktop、OwnedJob、独立 HOME/profile 和一个 `127.0.0.1` 合成 Telnet 会话。用户桌面、剪贴板、DPI/显示设置均不参与。
- **最多新启动原版一次**。新生产脚本、新执行身份和新唯一输出目录，不复用旧批结果。REA capture 45 秒、CLI wait timeout 60 秒；原版启动在 CLI 之前，整程时限另列为未知，不能称 45 秒整程保证。
- Qt 的 QWidget grab 是受控栅格，不是物理显示/真实 presented。动态注入仅用于正确性；不计时、不声明性能或完整终端样式等价。

## 五个最小阶段

`ESC` 为 `0x1b`。每阶段末尾追加 `ESC[6n`，收到回复、模式值和栅格记录后才确认下一阶段。标签只用于从独立原版文档 API 定位，比较的字形始终为 `H`。每阶段只抓一次 viewport；第一阶段两个 H 属于同一张校准图。

| 阶段 | 精确合成源（不含末尾 DSR） | 目的 |
|---|---|---|
| 0 主屏校准 | `ESC[0m ESC[2J ESC[H ESC[2;3H ESC[31;44m M0 H H`，表格中空格仅按下一段规则编码 | 在固定 SGR31/44 下取得两个同字形 H 与各自前面的显式空格，校准位置、背景和墨迹 |
| 1 首次进入 | `ESC[?1049hA0 H` | 不插入 SGR 或 CUP，观察进入后的当前样式 |
| 2 改变备用样式 | `ESC[4;5H ESC[32;45m A1 H`，间隔规则同下 | 取得另一组 H 的前景/背景颜色参照 |
| 3 重复进入 | `ESC[?1049hA2 H` | 不补 SGR，观察重复进入后的当前样式 |
| 4 退出后写入 | `ESC[?1049lM1 H` | 不补 SGR，观察主屏恢复后新写 H 的当前样式 |

为消除表格排版歧义，阶段 0 的文字载荷严格是 `M0 H H`，阶段 2 严格是 `A1 H`；控制序列之间、控制序列与载荷之间没有额外空格。实际来源十六进制及 SHA 必须在执行前由新脚本导出并固定。进入、重复进入、退出后均不补发清屏/样式恢复命令。

此次使用修订后的 peer，在每条完整 NAWS 解析时立即记录与检查，不能只看 `recv` 块末态。等 UI capacity 与 NAWS 稳定后才冻结源与坐标；冻结后尺寸改变、模式 getter 失败或阶段确认缺失均停止。新批事件级守卫结果只属于新执行，不回填旧 #123。

## Qt 调用合同

全部在 GUI 线程、原版阶段完成后调用。借用的自有 viewport 必须由父链、名称及模块路径守卫绑定。QRect 输入为 **16B 的 inclusive x1/y1/x2/y2**，全 viewport 为 `[0,0,width-1,height-1]`。

| 固定导出 | RVA | 已核对的调用 |
|---|---:|---|
| `?grab@QWidget@@QEAA?AVQPixmap@@AEBVQRect@@@Z`（Widgets） | `0x39520` | `grab(widget, result, rect)`；返回 result |
| `?toImage@QPixmap@@QEBA?AVQImage@@XZ`（Gui） | `0xad6f0` | `toImage(pixmap, result)`；返回 result |
| `?pixel@QImage@@QEBAIHH@Z` | `0x8e560` | `uint32 pixel(image, int x, int y)`，x/y 为 EDX/R8D |
| `?width@QImage@@QEBAHXZ` / `?height@QImage@@QEBAHXZ` | `0x94770` / `0x8d1a0` | `int(pointer)` |
| `?isNull@QImage@@QEBA_NXZ` | `0x8da30` | `bool(pointer)` |
| `?devicePixelRatio@QPixmap@@QEBANXZ` / `?devicePixelRatio@QImage@@QEBANXZ` | `0xabe80` / `0x8c2c0` | `double(pointer)` |
| `??1QImage@@UEAA@XZ` / `??1QPixmap@@UEAA@XZ` | `0x86350` / `0xab020` | 普通析构 `void(pointer)`，不是删除析构 |

QPixmap 与 QImage 各分配 32B，**不预先构造**，由正常返回的 sret 调用构造。只在相应调用正常返回后标记对象已构造；`finally` 按 image→pixmap 顺序调用普通析构。用 `QImage::pixel` 读取 QRgb，不解释内部 format、stride 或像素内存布局。官方依据：[QWidget](https://raw.githubusercontent.com/qt/qtbase/v5.15.2/src/widgets/kernel/qwidget.h)、[QPixmap](https://raw.githubusercontent.com/qt/qtbase/v5.15.2/src/gui/image/qpixmap.h)、[QImage](https://raw.githubusercontent.com/qt/qtbase/v5.15.2/src/gui/image/qimage.h)、[QPaintDevice](https://raw.githubusercontent.com/qt/qtbase/v5.15.2/src/gui/painting/qpaintdevice.h)。

## 坐标、DPR 与墨迹校准

1. 独立读取原版 `lineText`/`lineStart`，按唯一标签定位 H 与其前面的显式空格，调用 `visibleRectFromPosition` 获取真实矩形；同时主动调用 `isAltScreen/isMainScreen`。不沿用旧批的 x=147、8×18 或具体屏幕大小作为假定。
2. 记录实际 viewport 几何、pixmap/image DPR、非空状态、image width/height 和标记矩形。必须能用这组实测量建立逻辑坐标到图像坐标的一致对应，ROI 完全在图像内；非整数边界或尺寸关系不明就保持未知，不平移 ROI 搜索。
3. 只对 H 和显式空格的小 ROI 用 `QImage::pixel` 采样。校准空间格需给出一致背景；两个校准 H 需有非背景墨迹，且其背景及主要墨迹颜色一致。背景不明确、无墨迹或颜色分布不能稳定区分时停止样式判断。
4. 记录 `background_rgba`、`dominant_ink_rgba`、有限颜色直方图、ROI 尺寸及私有 ROI 数据哈希。抗锯齿产生的墨迹色仅按栅格统计描述，不冒称 QPainter pen 的原始 RGB。
5. 阶段 1/3/4 与阶段 0、阶段 2 的同字形 H 比较前景/背景标量。仅在标记绑定、DPR/坐标和墨迹校准均成立时报告具体一致/差异；歧义保持 `null`。这不是容差、性能预算或全颜色空间的验收。

空图、空 ROI、无墨迹、坐标/DPR 校准失败、原版异常或失去阶段握手，均保存失败/未知后结束这一次运行。不能改变假设、重新抓取试到绿或额外启动原版。完整栅格/ROI 原始数据、插桩、路径、PID 和原版资源留在 `local/`；公开仅发布可审查合成源、身份与脱敏颜色/尺寸标量，生产校验用显式 `require/ValueError`。

## 执行前出口

父任务复核 #123 修订后，才为本计划固定新执行 issue、完整新脚本、每阶段字节/SHA、脚本哈希及唯一输出目录，并正式启动这一次。当前没有新原版运行、栅格或颜色结果；计划不解除现有样式未知项。
