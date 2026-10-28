# 正式 C++ / Qt / 固定 ConPTY 的独立 hosted 构建

[#117](https://github.com/JavaZeroo/termforge/issues/117)在固定源码 `ecb559800e8146126117360ca24c519e08698ae6` 上完成专用 Windows CI；[验证身份](validation.json)分列输入、运行、原失败及限制。完整 R2 尚未完成。

## 实际结果

- [PR run 40700440118](https://github.com/JavaZeroo/termforge/actions/runs/40700440118)和[push run 40700431496](https://github.com/JavaZeroo/termforge/actions/runs/40700431496)均为 attempt 1、success。PR 实际检出 `5c3bea1ae05e5515d4cf7d6c36b1f098776be233`，与上述来源提交的 Git tree 完全一致。
- `windows-2025` 实际为 `windows-2025-vs2026/20261021.278.1`，编译器自报 MSVC 19.51.36260.0；固定 Qt SDK 仍是 Qt 6.10.3 的 MSVC 2022 x64 包。两个 Qt target 构建成功，候选核心关闭，后端固定为微软 ConPTY 1.25.260930003。
- 独立下载并审查 PR 的准备摘要：4358 项 SDK 文件的树哈希 `a35ab6fe76b62e4d9bd6d3c6b0d476c63583070cea5c9118ef5bb02042ffe266` 与本地两次不同目录的干净提取一致；归档、清单、SBOM 和 ConPTY 双文件另有固定哈希。仅新增相对 `qt.conf`，不修补 Qt 二进制或 `.prl`。
- PR 的 `qt_cpp_shell` 为 1/1，通过；真实 cmd 经程序化按键得到 750，子进程自然退出37，启动错误可见、窗口关闭回收均通过。外层 Job active=0、total=7、terminated=0。CTest 单项0.62秒、总0.68秒仅作诊断记录。

## 保留的失败与修复

首次 PR/push 的依赖准备失败于许可文件身份。真实 `git -c core.autocrlf=true checkout-index` 将该 LICENSE 转为 CRLF，复现了错误哈希；`.gitattributes` 明确该文件为 LF 后恢复清单哈希，许可内容和清单哈希未改。下一源码依赖准备通过，在 configure 处因 hosted 没有 VS 2022 实例失败；按实际映像改用 VS 2026 generator 后通过。两对失败 run 与各源码均保留，不重跑覆盖。

独立审查发现的摘要写入失败会泄露路径问题，已用公共 CLI 反例复现并修正：无法写摘要时仍输出固定错误码 JSON、退出1，保留准备阶段状态。最终准备测试6/6；Standards 与 Spec 复核均无剩余发现。

## 证据边界

详细回读的是 PR 的准备产物和 smoke 输出；push 的 success 只引用提供者元数据。现有默认/系统后端 CI、Alacritty 参考、原本地 VS 2022 验证与这次 hosted VS 2026 验证分别保留，不能合并为完整功能验收。

本工作流不导出运行二进制或完整部署目录哈希，不生成可分发包。offscreen 及程序化按键不证明原生上屏、物理输入、IME、真实不同 DPI、完整 TUI 或性能等价；`presented_done=null`，性能容差未评估。原始提供者日志、SDK 和部署目录不提交。
