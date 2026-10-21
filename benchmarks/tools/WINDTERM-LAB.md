# WindTerm 2.7.0 隔离诊断入口

## 应用身份

主要对照选择官方 Windows x64 2.7.0。2026-10-21 从[官方 release](https://github.com/kingToolbox/WindTerm/releases/tag/2.7.0) 下载，记录如下；这是本次取得的文件哈希，不是上游签名认证声明。

| 文件 | 字节 | SHA-256 |
|---|---:|---|
| Windows Portable x86_64.zip | 32678162 | `b05c3c7371340ff1302967247ffcd3a3ba27b237f72010e9157086143c453e50` |
| 包内 WindTerm.exe | 13534208 | `7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0` |
| 最初调查的本机 WindTerm.exe | 13534720 | `cd6019817313030de1849633de7fe58af3361a1ccd8045fb086570fb7f4ed833` |

差异原因未知，不能把两个 EXE 当作相同构建混用结果。没有复制本机用户会话、历史或凭据；安装包、解压资源与实际 profiles 都仅保留在 gitignore 的 `local/`。

## 准备

仓库根目录执行；目标目录必须尚不存在：

```powershell
gh release download 2.7.0 --repo kingToolbox/WindTerm --pattern WindTerm_2.7.0_Windows_Portable_x86_64.zip --dir local/windterm-lab-download
.venv\Scripts\python benchmarks/tools/prepare_windterm.py local/windterm-lab-download/WindTerm_2.7.0_Windows_Portable_x86_64.zip --out local/windterm-official
.venv\Scripts\python benchmarks/tools/generate.py --out benchmarks/generated/windterm-smoke --kind ascii --count 100 --variant pseudorandom
```

准备器核对 ZIP 和 EXE 哈希、拒绝覆盖目录，生成 `profiles.config` 指向新建 `profile-home`。路径定位机制依据[维护者说明](https://github.com/kingToolbox/WindTerm/discussions/606)；JSON `path` 键结构通过本地定位文件格式核实，没有读取其指向的 profiles。路径配置不等于运行时隔离已验证，`lab.json` 初始明确标记未验证。

## 首次人工烟测

1. 打开 `local/windterm-official/WindTerm_2.7.0/WindTerm.exe`。核对配置目录在新建的 `profile-home` 下；如出现目录选择框，选择该目录。
2. 新建本地 Cmd 或 PowerShell 会话。先只运行 100 行烟测，不将其计入 T01 基线。
3. 在该会话中进入仓库根目录，执行以下命令。`--app` 必须替换为本机准备器生成的 EXE 绝对路径。

```powershell
.venv\Scripts\python benchmarks/tools/terminal_run.py benchmarks/generated/windterm-smoke --app D:\Code\termforge\local\windterm-official\WindTerm_2.7.0\WindTerm.exe --out benchmarks/local-results/windterm-smoke-01 --sample
```

4. 核对可见结束标记 `TERMFORGE-END ascii 0000000100` 与 `oracle.txt` 末尾文本。诊断状态行出现在标记之后；不要把人工看到标记的时刻充当呈现时间。结果保存于指定目录。

运行器要求 Windows、终端 stdout、匹配 EXE 的唯一祖先进程；不满足时在播放前拒绝执行。应用路径匹配仅确认进程归属，不证明当前 UI 配置正确或该会话是全新会话。它不通过 UI 输入任何命令。

## T01 与开销校准

烟测成功后记录实际 C0 配置，再将输入目录替换为 `ascii-8m`（缺少时按工具 README 生成）。`--sample` 开启资源采样；省略则关闭；两种都记录生产端时间。每次选择新输出目录。按[协议](../PROTOCOL.md)完成新会话、预热、稳定等待和交错 on/off，至少各 5 次；正式基线另按 2 次预热、7 次重复、2 批次执行。此入口只支持单次运行，不自动保证上述条件。

`run.json` 包含应用/输入身份、状态及生产事件；开启采样时另有 `resources.jsonl` 和 `resource-summary.json`。失败 stderr、超时、被截断的 producer 记录均保留。运行器只终止自己启动的播放器，不关闭 WindTerm。

采样窗口覆盖播放器启动前至退出后约 500 ms，不等于输出计时窗口。controller/sampler 和 producer 都在 descendant 内，不能再把 sampler 加上去当总量。短负载 CPU 可能低于采样分辨率。实际配置、完整文本正确性、消费/呈现端点、捕获开销仍未自动验证。

准备器已在真实官方包上运行，启动的新实例在指定 `profile-home` 内创建了 `.wind`；这验证了定位配置生效，尚非全部文件访问的隔离审计。运行器通过受控文件接收端集成测试与超时保留测试。

2026-10-21 已核验首次真实 WindTerm 生产端烟测：100 行、7,732 字节全部写出，退出码 0，7 个资源样本有效。见[烟测记录](../results/2026-10-21-windterm-smoke/README.md)。**实际 C0 配置、完整画面正确性与正式性能基线仍待验证。**
