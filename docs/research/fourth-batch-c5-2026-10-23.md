# 第四批：C5 resize 与 G1 基础闭环

日期：2026-10-23。实施基点 `8e77b0d`。规格来源为 STATUS-AND-BACKLOG 的 C5、G1；[ADR-0005](../adr/0005-physical-row-resize.md) 固定具体内容、光标、历史与同步规则。

## 交付

`Screen::resize` 支持物理行裁剪和补空白、宽字符边界修复、光标保持、有界历史；不做逻辑行重排。窗口支持普通缩放和最大化，将新尺寸提交给核心、快照和独立 ConPTY resize worker。worker 只保留一个在途请求和一个最新请求，UI 持续排空字节。关闭先 join worker，再回收伪控制台；不与 `ClosePseudoConsole` 并发使用 HPCON。

自动测试通过真实 HWND 的 SetWindowPos 操作尺寸，使用独立 console helper 查询实际缓冲区维度。成功 API 返回本身不被当作子进程尺寸证据。尺寸变化触发完整视口失效，每次 resize 场景的局部绘制均与全量绘制进行 RGB 比较。普通用户运行不执行该额外比较，也不记录屏幕。

## 本机证据

Windows 11 build 26200、MSVC Release、Python 3.12.10。根构建与完整 CTest **12/12 套件通过**：133 项 Python 测试，另有跨平台原生 `resize_test`。后者包含内容期望、历史/样式/组合标记、1 列宽字符恢复、非法尺寸不变性、部分 UTF-8/CSI 与 300 次尺寸往返不变量。完整 ASCII/Unicode 回放各 17 次通过。

独立 runner 再执行三轮 normal、close、resize、resize-close，外加一次 watchdog，共 13 个场景，均通过。resize 场景核对 60×12、快速请求后 100×28、洪流中缩放后 72×18；关闭场景核对前两次，随后在洪流 resize 请求后立即关闭，不声称观察到了最后一次子进程尺寸。两种场景均检查进程树归零、自有 desktop 消失；正常退出保留 37，监督器验证存活 shell 树超时回收。

已检查自有最终 DIB：72×18、洪流之后的 `TF_AFTER`、`TF_SIZE_72x18`、退出命令与光标可见。该图不是用户桌面截屏或实际合成器呈现证据。

公开[运行身份](../../experiments/shell/results/2026-10-23-resize/manifest.json)及 [13 条合成验证元数据](../../experiments/shell/results/2026-10-23-resize/samples.jsonl)。helper 的命令回显含本机绝对构建路径，因此 stdout/stderr、完整屏幕与 BMP **只保留本地** `local/experiments/c5-final`；公开记录排除这些字段，并保存本地完整记录的 SHA-256。未改写前批原始样本。公开元数据不是独立完整的屏幕重放证据，源码脚本可重新生成原始记录。

## 结论与限制

A1/C5 具备 G1 的基础闭环：从源码构建、打开本地 shell、输入/输出、缩放窗口与退出。核心尺寸变化不 flush 解析器；输出流没有 resize 世代边界，不能声称每个中间帧或历史都与 ConPTY 内部布局相同。物理行裁剪会丢弃缩窄后的右侧内容；没有 reflow、IME、选择复制/粘贴、备用屏/滚动区、完整 TUI、SSH 或产品安装包。

resize 场景的额外全量绘制比较会增加开销；脚本输入到状态/提交仍只是诊断端点，`presented_done=null`。本轮不发布延迟排名、吞吐提升或 WindTerm 差距。Windows Server 2022 既有资源门槛问题仍独立保留。

下一批从 C3/C4 状态兼容性和 U1 输入能力继续走向 G2；协议来源/库调查可独立开展。后续加入 reflow 或备用屏时必须重新验证 resize 交叉行为。
