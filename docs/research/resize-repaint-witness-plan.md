# #156 缩小与重绘交错的固定机制诊断

这是独立 C++ 诊断的执行前合同，不是产品修复、性能测试或 CI 根因裁决。实现以实际失败的 `58fee2805cde34c67683a79473ca56f9df3cbf69` 为基点，不修改 Win32、Qt、Parser、Screen、TextDocument 或 TextSelection。原始失败 artifact 保持原件；本诊断不上传图片、私有路径、进程或个人会话。

## 已知事实与缺口

[#156](https://github.com/JavaZeroo/termforge/issues/156) 的旧默认 Win32/system-ConPTY 运行中，阶段 82 为 111×7/document 82，阶段 83 为 149×10/document 85，两者选区复制均为 2499B。阶段 84 恢复 111×7 后，document 仍为 85、top 从 75 变 78，选区失效；原选中行的部分身份对应的文字确实改变。TextSelection 已允许文字、span、边界不变的纯 version 更新，不能仅以 version 变更解释失效。

`ResizeWorker::settled()` 只说明原生 resize API 返回，不证明 Parser 已消费前一重绘、child 已确认尺寸或画面已呈现。原记录缺少每阶段已消费 transport offset 和 parser cursor。本诊断只验证给定交错能否形成相关坏状态，无法证明 CI 使用了这个切点、真实调度或唯一根因。

## 输入与初始状态

使用实际 `Parser(ParseMode::recovery)`、`TextDocument` 与 `TextSelection` 的公开边界。初始化 111 columns × 7 rows、history capacity 100；由失败源 `text_client.cpp` 的原 fixture 生成函数构造 `TF_FIND_000` 至 `TF_FIND_079`、`TF_FIND_READY 080`，再按 `ShellWindow::select_all_text` 的实际范围规则 Select All。禁止直接写入 Screen 行、补造行身份或更改选择内容以匹配目标。

初始硬门槛是 history 75、document 82、cursor row 6/column 0、实际复制字节与完整原 fixture literal 相等且恰为 2499B。任一门槛失败就保留部分记录并以 exit 2 停止；先调查缺失的源复现条件，不调整输入、ID 或断言来取绿。新 Parser 使用自身自然分配的相对 ID，不声称复制了 CI 启动前的绝对 ID 偏移。

transport 只由原创合成标记与 VT 构造。准备脚本在内存中与固定原 archive 的相关 member 逐字节比较，不提取、覆盖或直接 dump archive 文件：

- 原 failure ZIP SHA-256：`ea38ca1099f6f52420d77bee16796d04e986eab02c8912a692e013bf4d12282a`。
- 完整 transport：1071B，SHA-256 `c1d7cb18d5da399a838c997e0abe9375fd4db037e9f8f609ac02e1a58878cb84`。
- 五段长度：210 / 210 / 210 / 231 / 210；全局边界 0 / 210 / 420 / 630 / 861 / 1071。
- 普通段 SHA-256：`2f9e22d1b637e223fee1aa4fa0f9bcf3dab37dbccc29702cbfa8c3b591cd8de6`；第四段：`0ace2466754cef766944ccf2b80dce8bf85947eb7debaa057a8f7d61044d1610`。

每段包含 hide cursor、Home、`TF_FIND_075..079` 与 READY 行的原序重绘。第四段有四空行并以 CUP 7;1 收束，其余段只有一空行。所有字节和顺序都保留，不根据重复文字去重；切分只决定 resize 插入点。

## 四个 case，各一次

每个 case 使用全新 Parser/selection 和相同初始门槛。前三次依次 resize 至 112×7、112×7、111×7，各喂入对应完整 210B 段；第四次先到 149×10，再按下表喂第四段前缀、缩小到 111×7、喂原尾部（如有），最后喂完整第五段。尺寸统一写作 columns×rows。

| Case | 第四段已消费 | 全局 offset | 缩小边界 | 静态预测，非验收断言 |
| --- | ---: | ---: | --- | --- |
| A | 231B | 861 | 完整第四段后 | 选区保留 |
| B | 204B | 834 | 第七物理行的 EL 后，原尾部 27B 待消费 | 可能出现与 CI 相近的相对行身份/文字覆盖和选区失效 |
| C | 225B | 855 | CUP 7;1 完成后，仅 show cursor 待消费 | 选区保留 |
| D | 219B | 849 | CUP 7;1 之前 | 可能因缩小时 cursor 位于更低行而出现另一种移动/覆盖路径 |

只执行这四个 case，各一次，不扫描额外切点，不重试、筛选或丢弃不支持假设的结果。A/B/C/D 均完整消费 1071B；诊断不得修改产品 policy。

## 观测、判定与边界

每次 seed/feed/resize 后按照当前 policy reconcile，再通过公开接口复制。Select All 建立后也记录一个 checkpoint。输出 JSONL 包含固定 case/sequence、累计 transport offset、cursor/wrap、grid/history/document、epoch/discarded、选区 reconcile 前后范围、复制状态/长度/摘要、首个失效 sequence，以及全部保留行的 index/ID/version、文字与 span 摘要、ending/joins-next。行摘要使用十进制 FNV-1a64，供比较变化；它不替代输入/源/原件的 SHA-256 身份，也不推断 glyph 或呈现。

固定 51 个 checkpoint（A 12、B/C/D 各 13），每个最多 40000B、document 最多 110 行、copy 最多 8192B；协议总 stdout 上限 2MiB、stderr 64KiB。无时钟、sleep、网络、窗口、ConPTY、worker 或外部进程进入 C++ witness。未来进程外执行上限为 10s；这只是诊断资源守卫，不是性能要求。准备阶段不启动任何核心、编译器或诊断程序。

单 case 最终复制不是原 2499B 就报告 `observed_bad_state`，始终 `semantic_acceptance=false`、`ci_cause_established=false`。预测只记录 `prediction_matches`，不控制退出码或筛选结果。CLI 0 只表示四个预定诊断齐全且记录成功，允许包含已观察坏状态；不是产品正确性通过。协议、初始门槛或写出错误返回 2，保留 stdout/stderr/exit 和所有已完成 case，不悄悄重跑。不向 CTest 注册这个诊断，避免把已知坏状态当成自动产品通过。

## 源、构建与执行授权

`resize_repaint_witness` 是 CMake 的 `EXCLUDE_FROM_ALL` 显式 target，只链接现有 header-only `termforge_core`，无新依赖/后端。准备脚本不编译、不运行 witness，只读 Git 和固定 ZIP、生成独立合成 transport 与本地冻结 plan。脚本要求源码已提交且工作树干净、输出目录不存在、固定原件与 transport hash 正确，逐项确认原八个相关文件在失败源、本次源及显式比较源 `6a10e5322a1bddeb038670e50f32ff6deac145cf` 的 Git blob 一致。

本地 plan 记录准备提交、15 个实际源/参考文件的 SHA-256 与 Git blob、原 ZIP 身份、输入字节/段身份、四个 case 与预算。计划的构建配置是 Visual Studio 17 2022 x64 Release、C++17、`/W4 /utf-8`、parallel 1，关闭 BUILD_TESTING、Qt shell、candidate adapter；只构建显式 witness target，不运行 CTest。CMake 默认生成的优化、运行库和其他实际 flags 不能仅凭该意图视为已核。

**当前仅允许准备；编译与执行授权为 false。** 实际 compiler 路径/hash/version、展开后的 flags、EXE 大小/hash 和构建日志均为 null，不能借用旧 EXE 身份。等 root 发放短诊断租约后，先核所有冻结输入与原 archive，再独立配置/构建并记录真实 compiler/flags/EXE；若源码或协议需要修订，先重新冻结新身份、保留旧 plan，不用旧执行预算暗中重试。

未来只能使用记录的绝对 EXE 和输入路径启动一次、保留完整 stdout/stderr/exit；执行后核输入、15 源和原 archive 均未改。原 CI artifact、其他旧证据和生产源码保持原样。实际结果、机制支持程度、未知与后续是否需要产品修复归 #156，不把这份执行前计划写成验收或批次日志。
