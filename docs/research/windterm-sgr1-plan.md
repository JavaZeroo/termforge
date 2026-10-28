# 1049 的 SGR1 输出签名：离线方案

对应 [#146](https://github.com/JavaZeroo/termforge/issues/146)，接续 #123/#126 的未知非颜色属性。当前只交付工具、合成验证和 `execution_authorized=false` 的独立计划；**没有运行 WindTerm、没有原版结果**。旧 raw-capture-v1 的七个生产源、计划与证据保持字节不变。

目标是比较固定 WindTerm 2.7.0 在 1049 进入、重复进入与返回后，新写的 H/space 是否匹配同一张图中的 SGR0 或 SGR1 控制输出。匹配表示该固定样本的完整像素输出签名相同，不能直接解释为内部 `Style.bold`、QPainter pen 或实际字体字重相同。原版优先；重要独立差异仍需用户确认。

## 五阶段与可区分前态

| 阶段 | 精确输入的作用 | 采集后的源末态 |
|---|---|---|
| 0 | 主屏清空，SGR0/SGR1 各写两个 H 控制 | SGR1 |
| 1 | `1049h` 后先写无 SGR 的 `E0 H H`，再写两组控制 | SGR1 |
| 2 | 备用屏明确写 SGR0 样本和两组控制 | SGR1，为重复进入提供不同于 SGR0 的前态 |
| 3 | 重复 `1049h` 后先写 `R0 H H`，再写两组控制；最后追加不写字的 SGR0 | SGR0，与主屏原有源末态 SGR1 区分 |
| 4 | `1049l` 后先写 `M1 H H`，再写两组控制 | 不用于下一转换 |

这里的末态来自精确发送源的声明，并非已读出的原版内部属性。返回标记前保留一个显式空格，避免与主屏末尾文字拼接而失去完整 token 绑定。每阶段源后追加一次 `CSI 6n`，只在独立 UI 确认后前进。固定输入、长度与 SHA 见[不可执行提案](../../research/ui/results/2026-10-29-sgr1-proposal/plan.json)。

## 采集与分类

[工具](../../research/ui/sgr1_reference.py)复用已审 raw Qt 模板，只以受保护锚点替换五阶段 marker 表、case/status 名称和输出文件名。原生 sret、QPixmap/QImage 生命周期、选择端点、marker/ROI、DPR、caret 和每阶段单次 grab 的实现不变；生成模板 SHA 与实际注入文件 SHA 分别记录。REA 固定 6.7.0，准备计划记录 shim、package.json 与入口脚本身份；这不代表完整工具依赖树已重现。

每个未知样本有两个 H，SGR0 与 SGR1 控制也各有两个 H。采集保留完整有序 H/space QRgb 数组于私有目录，分类完全离线，不在采集中按颜色选择是否继续。结构、身份、空图、ROI/DPR、选择端点或 caret 失败停止；不移动 ROI、不补抓、不重试。

[离线审计](../../research/ui/sgr1_analysis.py)要求同帧同一文本行、唯一位置与严格递增的 glyph 矩形、相同 ROI 形状/样本数、各组内部的完整有序签名一致。SGR0/1 控制不可分、控制重复不一致或目标未唯一匹配时保持 unknown。颜色差异、主墨色并列不阻断已经成立的完整输出签名匹配，但只记录颜色关系；`font_weight_mechanism=null` 和 `internal_bold_verified=false` 保持不变。

[导出器](../../research/ui/export_sgr1_reference.py)核对执行计划、源/模块/注入脚本、全部 NAWS 事件、实际 DSR、原始消息与文件一致性、逐阶段确认和 Job/profile 回收。只输出合成源、白名单标量、派生签名和原件哈希；原始像素、资源、路径、PID、profile 留私有。早期停止不补造未冻结的网格或后续画面。

## 执行与停止边界

- 执行前固定生产提交、唯一新目录与单次 marker，最多一次原版启动、每阶段一次 grab；当前 false 提案不能创建 attempt 或启动目标。
- 仅官方固定 4047 文件清单、EXE/Qt 完整 SHA、自有副本、合成 loopback Telnet、非输入 station/desktop 和 OwnedJob；不修改个人桌面、剪贴板、DPI 或会话。
- 沿既有 REA capture 45 秒、CLI wait 60 秒与清理边界；启动在前、清理在后，没有整程 45/60 秒保证，不构成绩效测量。
- 原版运行需在独立预审后另建 true 计划；本提案的任何离线成功都不批准或模拟执行。

完整 style、内部笔刷、全网格、47/1047/1048、margin/origin/wrap、本地 Shell、物理呈现、性能及 R2 均未由本项验收。
