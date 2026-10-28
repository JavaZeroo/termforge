# Qt / 固定 CRT 的隔离 Windows 运行验证

本结果只覆盖 [#138](https://github.com/JavaZeroo/termforge/issues/138) 的 Windows x64 app-local 运行库依赖，父项为 [#35](https://github.com/JavaZeroo/termforge/issues/35)。微软 CRT 固定为 **14.51.36247.0**，来源、逐层提取身份、十个 DLL 和原许可见 [依赖说明](../../../../third_party/msvc_runtime/README.md)。完整记录为 [validation.json](validation.json)，不包含远程地址、VM UUID、账户、XML、口令、截图、DLL 或原始日志。

## 原包失败与纯 CRT 修订

第一次实际 Qt 加载前，隔离 Windows 11 Enterprise Evaluation 26H2 x64（26300.9457，2 vCPU / 6 GiB）的五个 System32 `vcruntime/msvcp140` 文件不存在，VC++ runtime uninstall 产品列表为空。guest 已安装 QGA/QXL/SPICE 测试控制工具，不能称为未经工具安装的裸系统。

原始输入固定在 `cf8b40b2e44f9e475cfc42b5008d46c670ff1756`：产品 25 个运行时文件、smoke 27 个；原 ZIP SHA-256 为 `967a98e24b1ff56165d46064096ad90804660b8eb99dfac59bfd3dc6b1012a37`。首次在真实交互 Session 2 创建产品进程，resume 前确认继承自有请求 Job，产品立即以 **`0xC0000135`** 退出，没有可见窗口。观测脚本 exit 0 只表示成功取得结果；不表示产品通过，也未确定 loader 最先缺少的单个 DLL 名称。

修订目录保持原 52 个文件逐一哈希不变，只在每个运行时目录添加十个固定 CRT DLL、微软原许可及 NOTICE。修订产品在 `windows` 平台下 5 秒后仍运行，WinAPI 枚举到一个可见 Qt 窗口，向自有窗口发送 WM_CLOSE 后产品 exit 0，无强制终止。System32 五个文件前后仍不存在，VC++ 安装记录前后仍为空。[完整文件映射](causal-runtime-files.json)同时列出原 52 个文件和新增 24 个文件，避免用重编译后的成功解释这组纯 CRT 因果比较。

同一 `cf8` 修订 smoke 使用 `QT_QPA_PLATFORM=windows`，测试进程通过有界 GO gate 后调用实际 cmd：独立预期 `750`、双标签 `750/427`、关闭后 `778`、失败页后的 `147`、退出 37/41、错误文字、正文墨迹、输入隔离和真实 `app.exec()` 的关闭前回收均通过，smoke exit 0。它是进程内 Qt 测试输入与图像检查，没有物理键盘或用户呈现端点验收。

三个请求的 Job 分别记录 active/total/after-active 为 **0/5/0、0/7/0、0/28/0**，根句柄已回收、清理验证通过，没有超时或请求强制终止。观测脚本、产品原生退出、限定 smoke、Job 清理和 `presented_done=null` 在 JSON 中分列。

## 新源码的独立集成

随后从干净提交 **`c5a47adc42db592c4c1966062cbd06b12f0f88ed`** 使用全新离线准备目录和全新 CMake 构建目录验证接入，与上面的 `cf8` 因果对照分开。Qt 6.10.3 官方 MSVC 2022 x64 qtbase 的 4,358 文件树哈希仍为 `a35ab6fe76b62e4d9bd6d3c6b0d476c63583070cea5c9118ef5bb02042ffe266`；固定 ConPTY 为 1.25.260930003。实际构建工具目录为 VC Tools **14.42.34433**，编译器自报 19.42.34433.0，CMake 3.31.0 / Python 3.12.10 / Release x64，candidate adapter 关闭。

自动准备、配置、两个 Qt 显式 target 构建及 offscreen CTest **1/1** 通过，外层 Job active 0 / total 26 / terminated 0。配置缺少显式 CRT 目录时 exit 1；每次构建重新校验并部署 CRT，单独删除自有输出中的一个已核对 DLL 后，无须 EXE 重链接即可恢复相同哈希。实际工具集无法识别或新于固定 CRT 时拒绝构建，不从编译器 `19.x` 推测 `14.x`，不下载或回退系统 CRT。

[产品清单](product-runtime.json)包含 37 个文件，[smoke 清单](smoke-runtime.json)包含 39 个文件；清单排除自身。独立传入 guest 的包另外保留两份清单，因此冻结核对 78 个文件。新源码包也独立完成 Session 2 的原生窗口加载/WM_CLOSE 以及 `windows` 插件下的有限 cmd smoke，退出和清理检查同样分列；最终对原包、纯 CRT 修订包和新源码包全部重新核对哈希。

新增 CRT 的 21 项公共 CLI 子进程测试覆盖离线准备、CAB 切片/内部成员、DLL/许可/NOTICE、工具集上限、部署失败前不改变目标、完整清单和重复生成稳定性，另保留 6 项既有 main CLI 边界测试。普通与 `-O` 均 **27/27** 通过，其中五项在真实 NTFS junction 上检查包外文件、输入/输出根及祖先路径均被拒绝。内部审查的两组红例保留了旧实现错误接受 junction、甚至枚举到包外合成文件的事实；修复后在解引用或下降目录前拒绝 reparse point。

修订包准备脚本另有一次独立失败：PowerShell 5.1 的 `PSObject.Properties.Count` 不产生所需数组计数，在复制和启动前退出；改用 `@(...).Count` 后才生成修订包。最早新增 CLI 测试也曾因工具文件尚不存在而出现 setup error，该记录不冒充行为红例；之后的显式 `unimplemented` 行为红例与通过记录独立保存。私有原始记录的大小、哈希和请求脚本原文/实际 staged 字节身份均在 validation 中保留。

## 来源与限制

[原包 PE imports](original-pe-imports.json)核对 42 个 PE 文件；新源码的 [产品 imports](integrated-product-pe-imports.json)和 [smoke imports](integrated-smoke-pe-imports.json)分别核对 30/32 个 PE 文件。EXE、Qt Core/Gui/Widgets 的 MSVCP/VCRUNTIME 依赖与固定 CRT 文件集对应；固定 ConPTY 文件对的直接依赖另列。这里只记录静态导入，不把它当作动态行为或全部许可的证明。

此项不交付完整 R2 发行版、Qt/第三方完整分发审计、实际跨 DPI 产品工作流、IME/Find/rename、全应用矩阵或性能等价。没有数值性能验收，测试耗时不作性能指标；`presented_done` 保持 null。CI 与双轴审查的实时结果由 PR 维护，不回填为本地已验证事实。
