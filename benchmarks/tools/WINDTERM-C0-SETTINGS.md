# #133：本地 cmd 实际网格与设置读取准备

状态：仅离线准备，**新原版运行数为 0**。[固定计划](results/2026-10-29-c0-settings-proposal/plan.json)为 `execution_authorized=false`，没有输出目录或 attempt marker。生产源固定为 `28f293a5d2c01954dc8b418a02231603128955a6`；等待 root 复核及独立执行授权，不改 false proposal 或旧 #119 原件。

[#133](https://github.com/JavaZeroo/termforge/issues/133) 的父项为 #3，真实前置 #119 已关闭。沿用其 14 行/536B ASCII 全文 oracle、原版本地 cmd→producer、OwnedJob 和非输入 desktop 边界；本项只调查请求配置是否有实际读取路径，不取得完整 C0、历史淘汰或性能验收。

## 静态线索、请求与实际值

下表的默认值来自固定官方资源，是静态线索，均不填入 actual：

| 配置 key | 静态值 | 本次请求 | 请求写入位置 |
|---|---|---|---|
| `window.autoAdjustRowsAndColumns` | true | false | 独立 synthetic `user.sessions` |
| `window.columns` / `window.rows` | 80 / 24 | 120 / 40 | 同上 |
| `window.scrollbackLimit` | 999999 | 10000 | 同上 |
| `text.fontFamily1` | Roboto Mono | Cascadia Mono | 自有副本 `app/global/wind.config` |
| `text.fontSize` | 10 | 12 | 同上 |
| `terminal.autoComplete.enabled` | true | false | 同上 |

官方 `terminal/configs/statusbar.config` 对当前 view 调用 `terminal.activeView()` / `terminal.view()`，并读取 `view.windowRows/windowColumns`。`terminal/configs/session.config` 和 `global/wind.config` 提供上述 key；这不能证明 profile 请求已消费或当前网格为 120×40。

实际取证分开保留：producer 的 console window/buffer 行列与模式；原版 view 的 windowRows/windowColumns；document 的 screenLineCapacity；QWidget 的 geometry、QFont family/pointSizeF/pixelSize。capacity 不替代 PTY rows，QWidget 字体不证明自绘 glyph、真实 fallback 或字体文件身份。Cascadia Mono 缺失时只记录可读返回或 unknown，不安装字体、不调整用户 DPI。

## 受控配置差异

[新 runner](windterm_c0_settings.py)先调用固定 #119 的 `prepare_run()`：完整核对原 4,047 文件清单后才修改自有副本。仅 `global/wind.config` 的三个声明 key 允许变化，其他原发行文件必须逐一保持 SHA 相同；synthetic session 只追加上述四个请求字段。

该 global 文件含 UTF-8 BOM、CRLF 和内联 `//` 注释，不能当严格 JSON 重写。离线准备发现并保留了这个格式事实。补丁仅替换三条唯一匹配行的标量值 token，保留 BOM、注释、空白及其他所有字节。计划固定修改前 SHA 与预计修改后 SHA；执行时再记录实际前后 SHA、逐 key 的带类型旧/请求值、原/配置后完整 manifest SHA 和唯一差异文件集合。离线摘要重新从原始字节计算补丁，核对输出精确相同。

因此身份是**固定二进制及原发行物基线 + 明确的自有配置差异**，不称整个包未改。对其他字段的改动、缺失/重复 key 或格式不符均在启动前拒绝。原始配置文件和副本只留 local，不提交资源内容。

## 独立源与公共 helper

旧 `windterm_cmd.py`、`qt_cmd_diagnostic.js` 保持逐字节不变，SHA 分别为 `e517f8cab36d55e5d4bdda3fc14d4641e5ce6256991af04762dbf43c25961297`、`9ed0d6ee6bd6e82bae6d9a6802690b3b0b83f437c7b20f0333008b5f6c214756`。新 runner 只调用旧工具的公开 workload/producer、全文 oracle、设置投影和清理 helper，不改 base 全局变量或伪装旧 run。

[新原生启动边界](../../research/ui/cmd_owned_launcher.py)仅负责独立 desktop、suspended→Job→resume、公共 REA CLI 等待及最终清理。新 probe 由固定旧 Qt helper 前缀（`function state(){` 之前）与[新 driver](../../research/ui/qt_cmd_settings.js)组成，不复制旧完整 runner。九个实际 source SHA、两个模板 SHA、旧前缀 SHA、组合模板 SHA 均在计划中；运行后另记替换 run_id/thread/目录参数后的完整注入脚本 SHA，离线精确重建比较。

Oracle 输入和 run_id 在计划中预先固定。另建授权计划时可用 `--run-id` 保留已 review 的 nonce 与全部 536B 输入；必须使用新计划文件和唯一输出，不能修改旧 false proposal。producer 仍先发送固定清屏序列后写完整 payload；producer 的计数器不作为终端消费或呈现端点。

## 有界只读发现与出口

新 probe 仅调用已有静态依据的无参 current-view 读取，优先 `activeView()`、再 `view()`；窗口指标只读属性，不调用未知 getter 方法。设置发现限定四个 owner × 七个固定 key，共 28 项属性读取；另对每个 owner 保留最多 128 项外显遍历范围内的 13 名白名单命中。发现函数名不会执行 `getSetting()`、`setting()` 或 `value()`。此限制不保证 QJSEngine 内部枚举耗时，因此没有观测开销结论。

每条实际读取保留 status/type/value。owner 缺失、undefined、非标量、读取异常、类型不符、非有限数值或不可公开字符串均为 null 并保留原因。未知值不替换成请求或静态默认。实际读到其他行列/字体，也按实际返回记录，不改窗口、ROI 或等待预算追求请求值。

最多一次原版执行，REA capture=30 秒、CLI wait=45 秒。原版更早启动并另等 2 秒，准备及回收另计，整程上限为 null。禁止用户输入桌面、剪贴板、个人会话或显示配置操作。执行者需等待 CLI 真正结束及 Job active=0 后再审计。结构/身份失败保留原件和停止，不重跑；配置只部分可读时，合法摘要明确为 `partial-settings-diagnostic`。

公开 CLI 在全部校验完成后才创建输出，只保留完整文本校验结果、合成输入、白名单标量与哈希；原日志、像素、PID、路径、profile 和原资源留 local。producer 按 run_id/负载关联，不声称已独立核验进程祖先。consumer_done/presented_done=null，C0 可见且无遮挡、完整 C0、10000 行淘汰、UTF-8/其他开关全量验证、字体文件/fallback、扰动和性能基线均未知，不关闭 #3/#4。

## 离线验证

[公共测试](test_windterm_c0_settings.py)覆盖严格 14 行 oracle、请求/实际/未知分离、全 manifest 先验、仅三 token 的 BOM/注释字节保留、配置和发现白名单、普通/`-O` CLI 拒绝 false plan 且不创建输出、cmd 元字符提前拒绝，以及合成 WinAPI 边界的 Job-before-resume 和 CLI 异常回收。[合成 QJSEngine 边界](../../research/ui/test_cmd_settings_probe.js)执行实际新读值逻辑，验证有限枚举、异常值抑制和不调用未知方法。

新工具普通及 `python -O` 各 14 项通过；旧工具 12 项保持通过，Debug 配置 CTest `windterm_cmd_contract` / `windterm_c0_settings_contract` 2/2 通过；组合注入脚本另经 Node 语法检查。所有验证均未启动原版。旧源码与旧证据保持独立；本方案提供可 review 的下一次调查工具及输入，不提供新的实际配置或 C0 通过事实。
