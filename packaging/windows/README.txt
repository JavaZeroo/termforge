TermForge @VERSION@ — Windows x64 Alpha
源码提交：@COMMIT@
项目：https://github.com/JavaZeroo/termforge

这是首个供体验的预发布包，提供已按限定合同验收的 R1 基础文字交互。
尚未达到可日常替代 WindTerm 的开发版，也没有性能等价结论。

运行
1. 将整个 ZIP 解压到一个目录。
2. 双击 termforge_shell.exe。
3. 保留同目录的 shell_clipboard_reader.exe 和 assets/fonts/ 全部文件。

运行不需要安装 Python、CMake 或 Visual C++ Redistributable。程序使用静态
MSVC 运行库（/MT）。此版本未进行代码签名，Windows 可能显示未知发布者。
包面向 Windows x64；已验证环境限于 Windows 11 本机和 Windows Server 2025 CI。
Windows 10、Windows Server 2022 及其他系统不在本版本已验范围内。

基本操作
- Ctrl+Shift+T：新建主区 cmd 会话；Ctrl+T：新建底部会话，合计最多 8 个。
- Ctrl+Tab：切换当前区域标签；Ctrl+W：关闭当前会话；F2：重命名标签。
- 拖选文字后 Ctrl+Shift+C 复制；Ctrl+Shift+V 或 Shift+Insert 粘贴。
  多行粘贴先确认；Enter 确认发送，Escape 取消。
- Ctrl+Shift+F：查找当前会话文字；Escape 关闭查找。
- 滚轮/滚动条：浏览最多 100 行历史；Ctrl+Shift+Home/End：历史首/尾。
- Ctrl++ / Ctrl+-：调整文字大小；Ctrl+0：恢复 100%；F11：扩大底部 Shell。

配置与退出
布局比例、侧栏显隐和焦点模式保存到：
%LOCALAPPDATA%\TermForge\workspace-v1.layout
ZIP 免安装，但配置不全部保存在解压目录，因此不是全数据便携模式。
目前不会保存会话、命令、标签标题、窗口尺寸或文字缩放比例。
关闭标签或主窗口会结束对应的本地 shell 及其自有后代进程。

当前边界
- 仅本地系统 cmd；有界历史、复制/粘贴、字面量查找及限定 Microsoft 拼音场景。
- 无 SSH/SFTP，常用 TUI、备用屏、逻辑行 reflow、完整持久化尚未完成。
- 缩窄窗口按物理行裁剪文字，增宽不会恢复被截去的内容。
- Explorer/Filer/Outline 是空面板；UI 尚未完整对齐 WindTerm。
- 真实跨显示器 DPI、其他输入法、长期稳定性及性能对照仍待验证。
这是真实本地 shell，可访问当前用户的文件，不是隔离沙箱。

校验与反馈
发布页提供 SHA256SUMS.txt，可用 PowerShell 的 Get-FileHash 校验 ZIP。
包内 build-info.json 记录版本、源码身份与文件哈希。
问题反馈：https://github.com/JavaZeroo/termforge/issues
请提供版本、Windows 版本、复现步骤及期望/实际结果；不要提交私密终端内容。

许可
TermForge 原创代码采用 MIT（见 LICENSE）。随附 Roboto Mono 字体采用
SIL Open Font License 1.1（见 assets/fonts/OFL.txt 和 README.md）。
本项目独立实现，与 WindTerm 或 KingToolbox 无关联。
