# Vim 捕获的运行目录隔离

[#52](https://github.com/JavaZeroo/termforge/issues/52) 的 native `new-buffer` 和 GUI stage59 是两条独立入口。本页只解释 native 捕获复用旧文件的原因；GUI 仍有真实失败，不能由本修正关闭 #52。实现通过 [PR #70](https://github.com/JavaZeroo/termforge/pull/70) 合入 `fix/conpty-backend-r47` 实验分支，尚未进入 main。

## native 反例与修正

原 `real_app_capture` 在新建输出目录的父目录运行应用。连续两个兄弟 capture 因而共享 `editor.txt`、`editor-startup.txt` 和 `editor-insert.txt`。第一轮 Vim 写盘后，第二轮打开已有的 12 字节文件，实际输出为 `"editor.txt" [unix] 1L, 12B`，原 `[New]` 断言必然失败。旧联合基线失败原件也具有这段输出。失败诊断中的 `editor_exit=91` 来自随后清理，不是 Vim 自然退出故障的证明。

同一旧 EXE、官方 `1.25.260930003`、Console width flags24、同一父目录的两次完整 cmd/More/Vim 捕获已复现第一绿、第二红 `new-buffer`；每次独立父目录的对照均绿。因此，这条失败来自夹具路径复用，不能认定为 Console width 兼容缺陷。

修正将应用工作目录设为本轮刚新建的 capture 目录。相对文件名、事件文件握手、输入字节、`[New]`、写盘内容、退出、真实 reader EOF 与外层 Job 检查均保留。兄弟运行和父目录已有文件不再影响本轮，也不会删除或覆盖父目录的文件。

新增真实回归在父目录预置旧编辑文件和 READY/INSERT，然后执行两个兄弟 capture，检查各自新文件、事件及父目录原件未变。该回归对旧 EXE 真实失败，对本树重建的修正 EXE 通过；完整真实应用模块 9 项通过。详细身份、原期限、原件哈希和回收结果见[脱敏证据](../../experiments/conpty/results/2026-10-26-vim-fixture-isolation/validation.json)。

## 复验

先在上述实验分支构建 `real_app_capture`，在当前进程 PATH 提供固定 Vim；设置 `TERMFORGE_REAL_APP_CAPTURE` 指向所验配置的实际 EXE。

```powershell
python -m unittest discover -s experiments/differential -p test_real_app_matrix.py -v
```

完整模块另需按 differential README 构建两方核心 trace。不要用系统 ConPTY 的成功代替显式官方配置。

## 尚未闭合

原 GUI `candidate-tui` 两轮本批仍为一绿一红；红例在 resize 后没有产生 InsertLeave，`:call writefile(...)` 成为插入内容，stage59 到原期限失败。两轮都回收为 Job0、私有 desktop 消失。目录隔离不影响此入口，尚无根因或修复结论。历史冻结基线、完整 TUI、呈现端点和后端整体回归仍分别保留。

有停止条件的 GUI 探针也未形成修复：strace 包装的两次运行在 Vim 启动时触发另一种 `c0000005`，不计原症状；临时观察真实 WriteFile 成功字节的三轮均绿，其中两轮 Ctrl-O 命令先于 CPR，否定了“这个先后关系必然导致红例”的假设。观察代码已移除，重新构建的原入口两轮均绿，事件/写盘/回复原检查保持；这仍不能解释已保存的真实 stage59 红例。探针二进制未在还原前记录哈希，证据明确保留该身份缺口并排除为验收。下一次需定位真实控制台输入到 Vim resize/按键处理的边界，不能用这批绿例关闭 GUI 问题。
