# 固定微软拼音下的 Qt 终端 IME 组合/提交/取消（#155）

本片记录我们自己的 Qt 终端在**隔离 VM + 固定微软拼音**下收到的事件序列与提交字节。

## 环境与身份

- 宿主：隔离 Windows 11 VM（QEMU/自有 Job 监督），非用户桌面；
- 应用：`termforge_qt_shell.exe`（本地构建，37 文件部署包，windeployqt 部署 Qt 6.10.3 + 固定 ConPTY + 固定 CRT），非官方 WindTerm；
- 输入方式：`keybd_event` 发送 `n i h a o` 与空格选择候选；
- 观测：`TERMFORGE_QT_IME_LOG`（默认关闭的调试开关）逐事件记录 kind、UTF-8 十六进制与光标几何。

## 事件序列

```jsonl
{"kind":"preedit","bytes":1,"utf8":"6e","row":3,"column":16,"caret_x":152,"caret_y":74,"caret_w":9,"caret_h":22,"origin_x":143,"origin_y":83}
{"kind":"preedit","bytes":2,"utf8":"6e69","row":3,"column":16,"caret_x":152,"caret_y":74,"caret_w":9,"caret_h":22,"origin_x":143,"origin_y":83}
{"kind":"preedit","bytes":4,"utf8":"6e692768","row":3,"column":16,"caret_x":152,"caret_y":74,"caret_w":9,"caret_h":22,"origin_x":143,"origin_y":83}
{"kind":"preedit","bytes":5,"utf8":"6e69276861","row":3,"column":16,"caret_x":152,"caret_y":74,"caret_w":9,"caret_h":22,"origin_x":143,"origin_y":83}
{"kind":"preedit","bytes":6,"utf8":"6e692768616f","row":3,"column":16,"caret_x":152,"caret_y":74,"caret_w":9,"caret_h":22,"origin_x":143,"origin_y":83}
{"kind":"cancel","bytes":0,"utf8":"","row":3,"column":16,"caret_x":152,"caret_y":74,"caret_w":9,"caret_h":22,"origin_x":143,"origin_y":83}
{"kind":"commit","bytes":6,"utf8":"e4bda0e5a5bd","row":3,"column":16,"caret_x":152,"caret_y":74,"caret_w":9,"caret_h":22,"origin_x":143,"origin_y":83}
```

## 判读

- 组合串从 `n` 增长到 `ni'hao`（`6e692768616f`），说明系统输入法在我们的窗口内处于组合态；
- 候选选择后组合被清空（`cancel`，0 字节），随后提交 `e4bda0e5a5bd`，即 UTF-8 的「你好」；
- 提交路径为 `model_.send(commit.toUtf8())`，与 CI 中 `qt_cpp_shell_smoke` 的合成事件断言一致。

## 锚点

全局光标矩形 = 窗口原点 (143,83) + 光标格 (152,74) = **(295,157)**，单元格 9×22。

同一轮全屏像素对照中，组合期相对基线的变化框起点为 **(295,163)**（同列，纵向差 6 px，落在单元格内），两条独立路径指向同一锚点。

## 未覆盖与边界

- 候选窗身份/窗口矩形：UIA 顶层枚举在组合态只看到任务栏/桌面/应用窗口，未取到候选窗；
- 不同 DPI、完整字形、实际呈现与性能均不在本片；
- 本片不构成 #155 整体验收，也不代表 WindTerm 2.7.0 行为。
