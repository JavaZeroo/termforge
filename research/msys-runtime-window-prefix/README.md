# MSYS resize 输入前缀研究补丁

本目录保存 [#52](https://github.com/JavaZeroo/termforge/issues/52) 的外部运行库实验，结果与限制见[实验记录](../../docs/research/vim-msys-runtime-prototype.md)。这是公开 MSYS/Cygwin 源码的研究补丁，不是 WindTerm 源码；不进入 TermForge 的默认构建或发布包，也不表示选定新的产品依赖或兼容策略。

## 来源与许可

- 上游：[git-for-windows/msys2-runtime](https://github.com/git-for-windows/msys2-runtime/tree/2e2ef940151485490aff0c7b00c00965ef7a71a4)，固定提交 `2e2ef940151485490aff0c7b00c00965ef7a71a4`。
- 原文件：[winsup/cygwin/fhandler/console.cc](https://github.com/git-for-windows/msys2-runtime/blob/2e2ef940151485490aff0c7b00c00965ef7a71a4/winsup/cygwin/fhandler/console.cc)，属于 Cygwin，文件头要求遵循 `CYGWIN_LICENSE`。
- 本补丁的上下文和修改遵循该文件的上游许可，**不由 TermForge 根目录的 MIT 许可证覆盖**。固定版本的 [CYGWIN_LICENSE](CYGWIN_LICENSE) 将该子目录默认许可列为 LGPLv3 或更新版本，并保留其中的 Linking Exception；完整文本 [COPYING.LIB](COPYING.LIB) 和 [COPYING](COPYING) 随附，三份许可文件均原样来自同一上游提交的 `winsup/`。
- 修改方：TermForge contributors；日期：2026-10-27。仅调整 `WINDOW_BUFFER_SIZE_EVENT` 分支的事件消费位置；保留 `send_winch_maybe` 的解锁、SIGWINCH 与再加锁。鼠标分支、信号处理器 longjmp 的实际回归及正式兼容方案均未由本补丁验收。

## 核对补丁

在上述固定上游 checkout 中运行 `git apply --check /path/to/window-prefix.patch`，然后应用补丁。原源与补丁源分别构建，不能拿新的二进制直接覆盖原验收目标并宣称通过。补丁 SHA-256 为 `e942c2d08b7cc5a294203bf0a52f7414b9b19f3cd675624644feadf907418c77`。

本轮使用独立 SDK 和原 Vim 的隔离副本，TermForge 自身的产品代码、测试方法、输入、断言与期限未改；构建条件、实际样本及不能外推的边界见实验记录。DLL、原始终端输出、工作目录与工具日志仅保存在本地，没有随本目录发布。
