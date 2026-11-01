# WindTerm 2.7.0：六种属性的 1049 同帧输出比较

[#27](https://github.com/JavaZeroo/termforge/issues/27) 的六个独立属性 case 各执行一次新计划、五阶段、最多五次 QWidget grab。六份原始捕获均完整，本地结构核对和同帧签名分类支持相同模式：进入和重复进入备用屏后的首段输出匹配 SGR0 控制，返回主屏后的首段输出匹配各自属性控制。**这是固定样本的输出行为，不是内部属性位、存储对象或完整样式状态的证明。**

六份 JSON 均由 VM 严格导出，读回后与本地原件独立核对，按原字节保存：[underline](underline.json)、[reverse](reverse.json)、[strikethrough](strikethrough.json)、[hidden](hidden.json)、[overline](overline.json)、[dim](dim.json)。[独立核对记录](audit.json)保留公开文件、原件和计划 SHA256，以及每个 case 的重算结论。公开文件不包含原始像素。

## 条件与固定输入

使用固定 WindTerm 2.7.0 x64、隔离 Windows VM、自有非输入 station/desktop、单个合成 loopback Telnet 会话。直接 Telnet 输入不经过本地 Shell 的 ConPTY/WinPTY。六次实际 NAWS 均为单个 `91×20` 事件，第一事件后冻结，UI capacity 均为 20；每次五帧均为 888×377、DPR1。不从像素尺寸补推系统缩放、字体或 glyph 身份。

- 官方 EXE SHA256：`7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0`；4,047 文件清单 SHA256：`e459adf2f12adb6a1b7f55e66a9da1c22794185e66e848057d21e0c60d669203`。VM 导出逐文件验证原版源包与自有副本，并检查实际模块路径守卫。
- 14 文件执行源码归档清单 SHA256：`db9f8cd97c88b35084be8a6ef0ecd948d4fc61187b3585eafe80c2afa3819b93`；基点 `f72c5c4f68dd7eb579ad1e523762333b135aa074`。基点不是探针已提交的声明，逐文件字节哈希在各 JSON 中保留。
- 固定 REA `6.7.0` 的准备身份由原冻结启动器核对；本属性计划没有记录 OS/Python/Node 二进制身份，不借用其他调查计划补成已绑定的运行身份。
- 每次保留 5 份 capture 和 10 条 provider 消息，无消息截断或 cleanup error；最终自有 Job 均为 `active=0,total=3,terminated=0`，profile 仅一个合成会话、stderr 为空。六个独立计划各消费一次原版启动，导出操作不重启原版。

下表用 `n` 表示该 case 唯一请求的属性：4 underline、7 reverse、9 strikethrough、8 hidden、53 overline 或 2 dim。`\x1b` 为单字节 ESC，每段源后均独立追加 `\x1b[6n`，等待对应 DSR 和捕获成功确认后才释放下一段。

| 阶段 | 精确合成源（ESC 转义展示） | n 为一位数的源字节数 | 六次实际 DSR | 作用 |
|---|---|---:|---|---|
| 0 | `\x1b[0m\x1b[2J\x1b[H\x1b[2;3HN0 H H \x1b[nmS0 H H` | 34 | `(2,16)` | 主屏正常/属性控制，结束前态为属性 |
| 1 | `\x1b[?1049hE0 H H \x1b[0mEN H H \x1b[nmES H H` | 36 | `(1,21)` | 首次进入后先写未知样本，再写两组控制 |
| 2 | `\x1b[4;3H\x1b[0mA0 H H AN H H \x1b[nmAS H H` | 34 | `(4,23)` | 已知正常样本及控制，结束备用屏前态为属性 |
| 3 | `\x1b[?1049hR0 H H \x1b[0mRN H H \x1b[nmRS H H\x1b[0m` | 40 | `(1,21)` | 重复进入后先写样本；最后 SGR0 不写字 |
| 4 | `\x1b[?1049l M1 H H \x1b[0mMN H H \x1b[nmMS H H` | 37 | `(2,37)` | 返回后先写样本，再写两组控制 |

表中空格都是实际 ASCII 空格，阶段 4 返回后有一个前导空格。可执行精确字节由[固定探针的 `phases` / `phase_records`](../../attribute_state_reference.py)提供；overline 的 `53` 使每段各多一个字节，即 35/37/35/41/38。源总量分别为 181 字节、overline 186 字节，另各 20 字节 DSR 请求。

## 已捕获的限定事实

每帧的正常与属性控制都包含两次独立 H/前置空格样本，要求相同文本行、相同 ROI 形状/采样数、重复签名完全一致。比较使用**有序 QRgb 数组的 SHA256、形状和采样数**；只比较同帧样本与控制，不能用颜色直方图相同代替像素顺序相同。

| 请求属性 | SGR | 阶段 0 控制 | 阶段 1 首次进入 | 阶段 2 已知正常样本 | 阶段 3 重复进入 | 阶段 4 返回主屏 |
|---|---:|---|---|---|---|---|
| [underline](underline.json) | 4 | 可区分 | SGR0 | SGR0 | SGR0 | SGR4 |
| [reverse](reverse.json) | 7 | 可区分 | SGR0 | SGR0 | SGR0 | SGR7 |
| [strikethrough](strikethrough.json) | 9 | 可区分 | SGR0 | SGR0 | SGR0 | SGR9 |
| [hidden](hidden.json) | 8 | 可区分 | SGR0 | SGR0 | SGR0 | SGR8 |
| [overline](overline.json) | 53 | 可区分 | SGR0 | SGR0 | SGR0 | SGR53 |
| [dim](dim.json) | 2 | 可区分 | SGR0 | SGR0 | SGR0 | SGR2 |

表中的 SGRn 表示唯一匹配该帧显式 SGRn 控制的**输出签名**。阶段 0 没有未知样本；阶段 2 是已知 SGR0 的正控，不是额外未知状态分类。各帧的两组控制均可区分且组内一致，阶段 1/3/4 的样本均有唯一匹配。6 次原版运行合计 30 个独立阶段，不因多个离线分析或输入方式而增加原版样本数量。

每次 28 对 H/space ROI、56 个原始数组、8064 个 QRgb；单个 ROI 为 8×18。hidden 不要求存在可见墨迹：签名仍必须通过重复、形状和同帧控制区分条件。字形消失本身不会让采集失败，也不会直接成为内部 hidden 位的证明。

## 导出与未覆盖项

[导出器](../../export_attribute_state_reference.py)在捕获之后独立添加，单独记录自身和错误原因白名单源码身份，不回填进执行前冻结的 14 文件清单。它要求外部保留的计划 SHA、单次 attempt、完整 4,047 文件源包与副本、四模块、源码归档、实际注入脚本、provider 消息与捕获一致、合成会话及最终 Job 状态全部校验，才允许写入新的公开文件。像素、原版资源、路径、PID、profile 与诊断原件留在本地。公开内容只含固定合成输入/正文、DSR、有限标量、签名及来源哈希。

[离线测试](../../test_export_attribute_state_reference.py)的 12 项测试在普通与 `-O` 模式均通过；同一合成录制的导出字节相同。包含六种属性、控制不可分、hidden 无墨迹、源码/包/脚本/计划变化拒绝、ROI 与合成文本限制、失败前缀不推进及停止前不虚构帧。合成测试不是额外原版运行或性能结果。

读回后的独立审核仅使用 Python stdlib，没有导入探针、分类器或导出器作为 oracle：重新构造六组字面输入，核对 DSR、固定文本投影及公开与原始 ROI 的一一对应，重算共 **48,384 个 QRgb** 的有序签名与同帧分类，核对外部保留的六个计划 SHA、14 个执行源哈希、导出器源码和 guest 成功输出的文件 SHA。[audit.json](audit.json)在普通/`-O` 下逐字节一致。主机没有重新访问原版全部包文件；完整 4,047 文件检查来自 VM 严格导出，读回审核明确只复核该来源记录。

首次串行导出批次原计划包含 reentry 与六属性共 7 件，在外层 **150 秒**期限触发 timeout、exit 124；此前已完成 reentry 和五属性共 6 个文件并逐项输出成功哈希。自有 Job 回收验证通过，不能称整个批次 passed。dim 随后单独执行离线导出，exit 0、状态 passed、回收验证通过，其文件另与成功哈希核对。两份外层记录的哈希与分离状态保留在 audit.json；这次补导没有新增原版运行或再次抓图。

SGR1/bold 保持[此前五阶段控制不可分](../2026-10-29-sgr1-reference/README.md)的 unknown；本次六种属性不能代替粗体调查，也不证明粗体未实现。未覆盖属性组合、字体/fallback 的有效状态、完整活动网格、颜色与属性组合、resize/reflow、RIS、真实本地 Shell/ConPTY、自然退出、物理呈现或性能。主屏内容可能保留在公开文本 API 的混合文档中，不能将它视为备用屏活动网格。

原版由自有启动器主动结束；Job 清空不证明自然 GUI 退出。45 秒 REA capture 与 60 秒 CLI wait 不含前置启动和后续清理，没有整程硬上限或性能容差声明。本目录不独立关闭 #27 或 R2。
