# 固定高度请求的独立几何观察

本工具调查旧 [height-only 失败](results/2026-11-02-resize-height-reference/README.md) 缺失的实际窗口几何。旧 749→569 请求后 NAWS 为 91×14，失败时没有记录实际窗口，不能确定最小窗口、终端区域或 splitter 的作用。本工具使用新 namespace、源码归档、计划和进程；不修改旧探针或将其预期 10 行改成 14 行。

## 一次固定请求变化

唯一 case 为 `height-geometry`，只请求 `1360×749 → 1360×569` 两个窗口尺寸，不恢复、不尝试其他高度、不改 splitter。初始实际窗口必须为 1360×749、NAWS 为 91×20，且 terminal capacity 为 20；初始不符时先记录实际值再停止。

第二个状态**不预设实际窗口或目标行数**。请求返回、每次 NAWS 事件数变化、最终稳定时分别记录实际窗口、NAWS、terminal capacity；窗口可能受约束而与请求不同，NAWS 也可能不变。这些都只是观察，不能由采样完成推出原语义样本通过。全程没有终端正文、DSR、CUP、CR/LF、清屏或补写；只有 loopback Telnet 协商和文件 ack。

新 peer 在独立 namespace 复用冻结 `r1_vt_fixture.py` 的 Telnet 状态机、loopback 绑定与清理，仅替换 payload 发送部分为几何握手，并拒绝任何应用层输入字节。初始采样 ack 后才 arm 唯一一次高度请求。变化只能发生在该阶段，列数固定为 91，实际行数安全界限 1..200；事件至多 64。不使用最终观测更新计划预期。

## 测量先保存，附加 widget 独立失败

窗口 `QWidget::geometry()` 的 inclusive LTRB 与计算尺寸、NAWS 值及事件数、终端公开 `screenLineCapacity/visibleLineCount` 分列。每个 snapshot 有相对时间、阶段、采样原因和原始请求尺寸。窗口/NAWS/capacity 联合稳定满 600 ms 才形成阶段观察；窗口、容量或事件数变化都会重新开始计时。错误轴、初始尺寸不符、读取失败和 deadline 分支先保留可安全读取的数据，再输出固定错误码。

UI 轮询间可能收到多个 NAWS：peer 保留逐个事件，而该次 snapshot 只能配对当时最新 NAWS 与窗口几何。不会给已经过去的中间事件补造几何；事件数量增加也会重置稳定时间。

终端区域仅使用已有 `qt_r1_text.js` 中验证过的 Qt 绑定：`allWidgets`、`parentWidget`、`inherits`、`geometry`、`mapToGlobal`。从唯一 TerminalView 绑定其 ScintillaEditBase 和 `qt_scrollarea_viewport`，只记录这三者、有界父链及父链上 splitter 的直接可见子区域。parent-local QRect 与 global origin 分列，不直接混算。没有新增或猜测 minimumSize、QSplitter::sizes 等 ABI。

widget 枚举沿用 2,500 个上限，父链最多 32 层，最终记录至多 64 个节点。公开节点仅有临时序号、父序号、固定角色、矩形、global origin 和 splitter/handle/visible 布尔值。进程内指针只用于关联，不输出任意 objectName、className、窗口标题或完整 widget dump。

无法唯一绑定、父链循环、节点过多或附加几何接口失败时，`widgets.status=unavailable`、`nodes=[]`、角色值全部为 null，并保留固定错误码。独立窗口/NAWS/capacity 仍可形成观察。整个终端会话尚未建立的启动失败，与会话已建立后的可选 widget 测量失败分开。

## 身份、时限与公开边界

- 复用固定 WindTerm 2.7.0 exe/完整包 manifest、Qt 模块路径、合成 profile、独占 station/Job 和单次 `.attempted` 守卫；计划失败后不得重放。
- JS 主循环 deadline 41 s、timer 42 s、启动器参数 43 s、REA 捕获 45 s、CLI 等待 60 s。新几何 peer 本身上限 43 s。它们不构成全生命周期硬上限，`whole_lifecycle_bound_seconds=null`；不因失败延长。
- 快照最多 96 个；时刻、NAWS、窗口、节点与父图都检查类型、界限和关联，重复/过期 ack、应用输入或未 arm 的变化拒绝。
- `SOURCE_FILES` 固定为 9 个执行源，根 `reference-source.json` 使用严格 `termforge-mode-reference-source-v1` 归档；有 manifest 时不调用 Git，无 manifest 时读取真实 Git。prepare/run 前后保持源码与清单字节校验。
- 严格出口仅白名单化完整观察中的标量、临时 widget 关系与源码/运行身份，不转发任意原件字段。可选 widget 缺失会保留，不写成完整区域几何验证。
- 成功采样的公开结果固定 `status=observation`、`semantic_acceptance=false`。实际窗口/区域/NAWS 的两次配对不能证明内部布局因果，也不验证正文、高度历史、DSR、备用帧、ConPTY、性能或 R2 完成。

## 离线验证与执行分工

```powershell
python -B -X utf8 -m unittest research.ui.test_resize_geometry_reference research.ui.test_export_resize_geometry_reference -v
python -B -O -X utf8 -m unittest research.ui.test_resize_geometry_reference research.ui.test_export_resize_geometry_reference -v
node research/ui/test_resize_geometry_stages.js research/ui/resize_geometry_reference.js
```

Python peer 测试通过内存连接执行实际受保护的 Telnet parser；Node 在纯 V8 中执行实际 controller、终端 getter 和已有 Qt 绑定的调用路径。测试包括请求与实际高度不同、未知行数、NAWS 不变、600 ms 整体重置、判错前保存、widget 缺失/重复/循环/接口失败、固定 deadline 及输出脱敏。离线验证不启动原版、GUI、socket 或 native 构建。

原版执行者在源码提交冻结后统一打包并建立一次性计划。工具准备和离线成功均计为原版运行 0 次；实际结果另行保留，不能替换旧 height-only 失败记录。
