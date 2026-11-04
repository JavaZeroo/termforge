# 正式 Qt / 固定 ConPTY：隐藏帧七阶段完整正文保留

本切片把 2026-11-04 用户确认的“隐藏帧返回保留完整正文、接受 WindTerm 返回裁剪差异”合同贯通到**正式 Qt / 固定 ConPTY 路径**：真实 child 写入固定 91×20 主屏正文，公开 Qt 控件缩窄到 61 列，child 进入备用屏，仅改变高度（20→14→20），`1049l` 返回主屏，再恢复宽度到 91。逐阶段分别记录真实 child 尺寸/输出、固定 ConPTY transport、核心 owner/行身份/正文以及 Qt 显示页/光标/命中。

这是固定场景的**正式路径离屏证据**，不是原版 oracle，也不是物理呈现、DPI、IME、性能或完整 reflow 验收；#312 原严格失败、离线补充审计与 `semantic_acceptance=false` 均不改写。

## 场景与固定源

固定 75 B ASCII 源（与 #311 核心工程合同同一字面样本）：

```text
\x1b[?6l\x1b[?7h\x1b[r\x1b[0m\x1b[2J
\x1b[2;1HHTOP  \x1b[6;1HHARD_A CRLF HARD_B  \x1b[12;90Huvw  \x1b[19;1HLOWEND
```

主屏文档第 11 行（零基）为 `89 空格 + uvw`（92 字符，按 91 列物理行 + 1 格软续接保存）；第 18 行为 `LOWEND`。七阶段依次为：主屏正文 91×20 → 缩窄 61×20 → `1049h` 进入备用屏 → 仅缩高 61×14 → 恢复高度 61×20 → `1049l` 返回主屏 61×20 → 恢复宽度 91×20。

## 验证入口

新增 `qt_hidden_retention`（`src/qt/hidden_retention_test.cpp`、`src/qt/hidden_retention_fixture.hpp`）：真实 child 通过独立命名管道逐阶段确认自己的控制台几何与选中原生行；测试只在 child ACK 后核对核心/显示，更不复用 resize 请求本身作为 child 确认。门禁已注册到 CTest 与 Qt workflow。

## 实测结果

本地 Windows 11 x64 定向构建、offscreen；正式 Qt 路径 CTest **7/7 通过（8.99 s，含新测试）**；新测试 **58 项检查、0 失败**，连续两次运行除 `child_pid` 外输出一致；核心回归 **41/41 通过（23.05 s）**。

| 阶段 | child CSBI | 核心 | 光标 | 正文/显示核对 |
|---|---|---|---|---|
| 1 主屏正文 | 91×20 | 91×20 primary | `[18,6]` | 保留行 id `12`；显示行 11=`89 空格+uv`、12=`w`、18=`LOWEND`；显示光标 `[18,6]`；命中 (11,89)→行 11 列 89 |
| 2 缩窄 | 61×20 | 61×20 primary | `[18,6]` | 源行仍为 91 Cell；显示行 11 空、12=`28 空格+uvw`、18=`LOWEND`；命中 (12,28)→行 11 列 89 |
| 3 进入备用屏 | 61×20 | 61×20 alternate | `[1,8]` | 备用屏显示 `ALT_MARK`；隐藏主屏正文=固定 92 字符行，行 id/光标不变；正常追加历史为空 |
| 4 仅缩高 | 61×14 | 61×14 alternate | `[2,10]` | 备用屏 `ALT_SHRUNK`；隐藏主屏前缀 5 行进入历史、保留行仍为 91 Cell、正文完整、行 id 不变、光标行随前缀移动 |
| 5 恢复高度 | 61×20 | 61×20 alternate | `[2,12]` | 备用屏 `ALT_RESTORED`；隐藏主屏正文/历史/行 id 不变，未被回拉或重写 |
| 6 1049l 返回 | 61×20 | 61×20 primary | `[13,6]` | 历史 5 行 + live 20 行；正文 SHA 与阶段 1 相同（`0e683ff0…`）；显示行 7=`28 空格+uvw`、13=`LOWEND`；显示光标 `[13,6]`；命中 (7,28)→保留行 11 列 89 |
| 7 恢复宽度 | 91×20 | 91×20 primary | `[13,6]` | 正文 SHA 仍相同；显示行 6=`89 空格+uv`、7=`w`、13=`LOWEND`；命中 (6,89)→保留行 11 列 89 |

关键事实：

- 阶段 1/2/6/7 的逻辑正文 SHA-256 均为 `0e683ff0a35c4b0a07acd6c5ca015280f99e3586911bc1d3da7f40252b3ff50e`，即隐藏高度变化与 1049 返回未丢失用户已确认的完整正文。
- 隐藏主屏只在宽度实际变化时适配足迹；仅高度变化保留 91-Cell 源行。返回主屏后源行仍可按当前宽度投影为完整的 92 字符逻辑行。
- 显示页、显示光标与命中均消费同一保留源身份；核心 owner/行 id `12` 与历史结构在七阶段中保持。
- child 原生行单独记录：阶段 6/7 原生第 11 行为空（ConPTY 原生重排网格），而产品保留源行完整；**原生网格不是已确认保留文本的 oracle**，本记录不把两者混为同一对照。

## 复现

```powershell
cmake -S . -B build/qt-cpp -G "Visual Studio 17 2022" -A x64 `
  -DTERMFORGE_BUILD_QT_SHELL=ON -DBUILD_TESTING=ON -DTERMFORGE_BUILD_CANDIDATE_ADAPTER=OFF `
  -DQt6_DIR="<prepared>/qt/lib/cmake/Qt6" `
  -DTERMFORGE_CONPTY_BACKEND_DIR="<prepared>/conpty" `
  -DTERMFORGE_MSVC_RUNTIME_DIR="<prepared>/crt"
cmake --build build/qt-cpp --config Release --target qt_hidden_retention_test --parallel 2
ctest --test-dir build/qt-cpp -C Release -R '^qt_hidden_retention$' --output-on-failure
ctest --test-dir build/qt-cpp -C Release -R '^(qt_cpp_(shell|resize)|qt_unicode_input|qt_unicode_render|qt_terminal_modes(_retained)?|qt_hidden_retention)$' --output-on-failure
```

环境、依赖身份、逐阶段记录、源码/产物哈希见 [validation.json](validation.json)；公开样例见 [hidden-retention.json](hidden-retention.json)（仅含固定源、阶段标量与 `child_pid`，无私有路径）。

## 边界

- 仅离屏证据：非物理呈现（`presented_done=null`）、非真实 DPI/焦点、非原生 IME、非性能验收。
- 仅一个固定 75 B ASCII 样本与上述七阶段尺寸；不覆盖其他尺寸、连续输出、宽字/Unicode/结合字、保存光标交叉、历史淘汰上限、选择/Find/历史浏览用户流程、完整 TUI 或同时缩宽+缩高的原版视口/滚动/CUP。
- 保留正文是用户 2026-11-04 已确认的**有意差异**；原版观察、严格审计失败、补充审计与 `semantic_acceptance=false` 保持原身份，不为匹配原版丢失结果而裁剪正文。
- #27/#29/#30/#7 仍 OPEN；不声明完整 resize/reflow、WindTerm 等价或 R2 完成。默认 `Screen::resize` 的物理裁剪行为未改变。
