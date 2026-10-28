# 原版本地 cmd 自动正确性诊断

本工具对应 #119，是 #3 的一个有界切片。它启动固定官方 2.7.0 的独立副本、全新合成 profile 和本地 cmd；cmd 运行一个 14 行 ASCII producer。它不操作用户输入桌面、剪贴板或显示设置，不连接远程服务。

```powershell
# 使用已有 REA 6.7.0 公共 CLI，不安装/更新 provider。
D:/Code/termforge/.venv/Scripts/python.exe -B -X utf8 benchmarks/tools/windterm_cmd.py run `
  --source D:/Code/termforge/local/windterm-official/WindTerm_2.7.0 `
  --rea D:/Code/termforge/research/ui/r1-tools/node_modules/.bin/rea.cmd `
  --out local/windterm-cmd/new-run --seconds 30
```

输出目录必须不存在且位于当前仓库 `local/` 下。复用 `research/ui/launch.py` 的发行物内容清单、禁首启导入的合成配置、home 环境隔离和 Owned Job；挂起创建后先加入 Job，再执行。独立非输入桌面不会切换到前台。所有外部 CLI 及目标子进程同样有界回收。隔离配置与桌面不是文件系统或网络沙箱。

producer 通过 `cmd /c` 启动，C argv 的 `list2cmdline` 不能视为 cmd 引用器。因此在读取/复制发行物或创建输出前，显式拒绝 out、Python 解释器和 runner 路径（传入及解析后的拼写）中的 `& | < > ^ ( ) % ! "` 与控制字符。具有这些字符的正常文件路径也不支持。`--seconds 30` 是 Frida 观察时长，公共 CLI 另有45秒等待上限；准备、两秒启动等待及有界清理另外计入整个生命周期。

producer 在计时事件外清屏，从本地 cmd 控制台写入合成数据，记录 run_id、payload SHA、原始生产事件及控制台窗口/缓冲尺寸，然后保持存活供文本读取。结束/退出不代表消费或呈现完成。

producer 记录通过 run_id 和负载身份关联；probe 与 cleanup 通过原版 target PID 关联。不据此声称已经核验 producer 的父子进程链。

独立探针通过已调查的固定 Qt5 API 打开唯一合成会话，并在其 GUI 线程读取 `activeText.visibleLineCount` 和每一条 `lineText(i)`；最多 128 行。仅末尾 marker 用于决定何时获取完整文本，Python 校验器对照独立生成的所有 14 条逻辑行逐行比较，剩余行只能为空。不会 trim 空格、忽略前缀、只比 marker 或从配置文件值推定当前实际设置。未知 API、缺失行、超时、错误、截断或身份不符均不能通过。

`diagnostic.json` 是供审查的白名单摘要，原始 `run.json`、producer、REA evidence、复制发行物和 profile 全部留在忽略目录，不提交。`actual_settings` 只记录实际可读的控制台尺寸、文档容量、控件几何和 QWidget 字体；该字体不代表全部 QPainter/glyph fallback，其他 C0 配置仍未知。用同一执行源码可离线重算：

```powershell
python benchmarks/tools/windterm_cmd.py summarize --out local/windterm-cmd/new-run
python -m unittest benchmarks.tools.test_windterm_cmd -v
```

**这是 instrumented correctness diagnostic。** 它没有原生输入或画面捕获，`consumer_done`、`presented_done` 固定为 null，`performance_gate=not_evaluated`。生产计数器仅原始本地事件，不发布吞吐/时延。它没有执行正式 T01、观测开销校准、历史/Unicode或完整 WindTerm 基线，也不续跑旧手工 5/12 批次。
