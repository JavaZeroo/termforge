# 自主 C++ / Qt 最小本地会话入口

状态：**最小迁移入口限定检查通过；完整 R2 仍未验收**。本记录仅覆盖首个 C++/Qt 本地会话迁移入口，不宣布 R2、完整终端兼容、WindTerm 等价或可分发开发包通过。

入口按 [ADR-0008](../../../../docs/adr/0008-cpp-core-qt-widgets.md)使用自主 C++ 核心与 Qt Widgets，进程由[随包固定微软 ConPTY](../../../../third_party/conpty/README.md)承载。当前固定 80×24、单个本地 cmd、基本 ASCII 输入和 16 色绘制；原 Win32 工作区、历史浏览、选择/复制、Find、完整键盘/IME、动态 resize 与会话恢复尚未迁移。Alacritty 的历史、备用屏、字素及 TUI 结果不转记为本入口能力。

## 固定身份与当前终态

环境为 Windows 11 build 26200、x64 Release、Qt **6.10.3 / MSVC 2022 x64 qtbase**，动态链接 Core/Gui/Widgets，测试另用 Test；固定微软 ConPTY 为 **1.25.260930003**。候选适配器关闭。Qt 使用 **offscreen** 平台和本项目独立取得的 OFL Roboto Mono，`presented_done=null`。

| 检查点 | 结果 | 边界 |
|---|---|---|
| 初次整合提交 `9d9b506c0c268a304046a20d096018a821fd3d78` 的完整构建 | **失败** | 旧 margin 探针宏替换与显式 Backend 类型冲突，保留 `build-final.log`；不能写成初次整合全绿 |
| 后续提交 `0fd35e23095c611865c6970ba9a3b8b910ef1efa` 的完整构建 | **退出 0** | 复用已有显式 Backend 注入的探针实现，未修改原 175B 输入；保留 `build-final-fixed.log` |
| 最终源码完整 CTest | **47/47，退出 0，320.79 秒** | 新 Qt 检查 1 项与旧 system 诊断 46 项分列；内部 12 条跳过记录不能当通过 |

[validation.json](validation.json)冻结所引用日志的哈希，并绑定最终构建本地 manifest。该 manifest 的 **340 项文件已重新核对全部匹配**，其中运行时目录文件 **91 项**，不把 `CMakeCache.txt` 算作运行时文件。公开 JSON 保留 34 项精选源码/依赖/运行时身份及完整 manifest 哈希，未冒充完整清单副本。

## 实际反例与分次验证

| 片段 | 观察与处理 | 结果的含义 |
|---|---|---|
| `interface-red` | 缺少 `terminal_widget.hpp`，构建退出 1 | **编译红**，不是已复现的运行故障 |
| `runtime-first` | 测试 35.03 秒超时，留存 CTest 输出没有子进程诊断文字 | 失败保留，单凭该记录不裁决根因 |
| `runtime-diagnostic` | 加日志后仍超时，报告缺少 offscreen 平台插件 | 暴露部署缺口，没有改变为通过 |
| `runtime-platform` | 补插件后 0.21 秒到达实际 cmd 路径，但正文墨迹断言失败，并报告缺字体 | 平台插件问题与字体/墨迹问题分别留证 |
| `runtime-font` | 部署独立 OFL 字体后局部 CTest **1/1**，测试 0.36 秒；外层 Job active=0、total=4 | 该早期版本只验证较小场景，不包含后来增加的启动错误和人工关闭检查 |
| `runtime-close-red` | 新增关闭窗口场景后，5.52 秒报告未回收正在运行的 Shell | 真正的运行红；外层后来 Job=0 不能把窗口关闭动作改判成功 |
| `runtime-close-green` | 增加 closeEvent 回收后，局部 CTest **1/1**，测试 0.47 秒；外层 Job active=0、total=7 | 实际覆盖退出、错误图像和关闭，但它早于最新监督器清理 P2 修正，不能替代最终源码完整矩阵 |

局部 Qt 检查通过公开按键事件向真实 cmd 输入 `set /a 731+19`，独立期望为 **750**，并检查结果三格有新增墨迹；还检查正常退出 **37**、启动失败的错误文字墨迹，以及人工关闭后的回收。外层 Job 总数只是聚合计数，不据此推测每个进程的身份。这里列出的秒数是测试耗时，不是终端性能指标。

中间红记录的 HEAD 为 `4441248`，同时存在工作树实现变更；保留当时源码/二进制的文件哈希，但不把每个局部 green 都标为最终提交的结果。Standards/Spec 报告的 P2 修正目前来自根代理汇总，没有伪造独立审查日志；最终复核范围为 `4441248..0fd35e2`：Standards 无未解决硬问题/实质异味，Spec 无未解决新增问题；清理 P2 的最终源码已随完整 CTest 通过。

## 后端适配与部署负例

独立 Backend/Session 检查白名单摘要记录：缺失/篡改运行时明确拒绝、正常构建修复文件且不重新链接 EXE、篡改构建输入被拒绝。system 与 fixed Session 各自验证 ready/echo、resize、正常退出 37、EOF 109 与回收；它们是独立配置，不能合成 Qt 产品组合验收。原始 Session JSON 含进程标识，只保留本地索引、哈希和脱敏汇总。

最终源码还分别移除 offscreen 插件和 OFL 字体，均观察到明确拒绝及 Job active=0；测试二进制身份保持，文件已恢复。负例只证明这两个部署缺口可见，不证明完整运行库部署或干净机器使用已经通过。

[Qt 依赖 manifest](../../../../third_party/qt/manifest.json)区分原始官方 7z、aqtinstall 修补的 SDK 与实际部署文件身份。对应源码、第三方许可清单、替换条件及干净机器验证仍有待完成，当前不是可发布包。

## 复现与公开边界

配置与构建入口见 [src/qt/README.md](../../../../src/qt/README.md) 和 [Qt 构建依赖](../../../../third_party/qt/README.md)。局部命令为：

```powershell
ctest --test-dir build/qt-cpp -C Release -R '^qt_cpp_shell$' --output-on-failure
```

完整 CTest 由根代理保留同一源码/配置的终态，本草稿不估计通过数。没有真实键盘、原生窗口上屏、不同 DPI、完整 IME/TUI、长期稳定或性能等价验收；受控图像结果不能替代用户实际看到画面的时间。

公开内容仅含独立代码对应的合成断言、汇总和哈希。本地 proof 路径是取证索引，不是公共附件；原始日志、截图、布局、PID、desktop 名、私人绝对路径与 WindTerm 私有材料均不随本记录发布。

最终完整运行另有 **12 条内部跳过记录**：4 个参考/可选依赖整类跳过、7 个实际系统剪贴板场景、1 个未启用的原版失败注入。新 Qt 入口的三类行为检查全部执行，外层 Job active=0、total=7、terminated=0；这些聚合计数不单独证明每个进程的身份。完整 47 项含独立旧配置，不能表述成新 Qt 产品的所有既有能力通过。
