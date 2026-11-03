# 固定 ConPTY 的 resize 与输出机制

2026-11-02 只读核查，服务于 #27/#29 的正式 Qt 接线。**固定版本没有可用的 `PSEUDOCONSOLE_RESIZE_QUIRK` 开关；内部 reflow 不意味着向宿主重发重排后的整屏。** 本文没有新增运行、修改 flags、依赖或产品路线，也不将源码解释记为动态验收。

## 固定身份

本次重新核对[包清单](../../third_party/conpty/manifest.json)、本地包原字节及 zip 成员。微软 [v1.25.2733.0 release](https://github.com/microsoft/terminal/releases/tag/v1.25.2733.0) 的同名 asset 大小及 digest 与本地一致；[annotated tag](https://api.github.com/repos/microsoft/terminal/git/tags/643609c3f9e022c9fb44366a43f5ebb8567f9722) 指向 **`dc8ae365847d41f62d1ebca93815bb00959bc12f`**。以下源码链接均固定在该 commit，历史链接另行标明。

| 对象 | SHA-256 |
|---|---|
| `Microsoft.Windows.Console.ConPTY.1.25.260930003.nupkg`，1,750,533 B | `6d6f8b008c655d814c498d8205d4012dcd4d0b6dfc1096461d0a185d1e3caf03` |
| win-x64 `conpty.dll` | `feeef341d891643c62d30b6b07800bc70f0bc148f44cb8c3bee84aa557ae805a` |
| 同包 `OpenConsole.exe` | `3d66b23d0a71bb8eed2b77edc8b9df9bf54ce6c8fb74c863a30e760997f80586` |
| 包内 `inc/conpty.h` | `976ccae27ccd70a4710275d05bbedbc7139a8e04b2537bfca0a827f70414f53d` |

包内 header 与固定源码 [`src/inc/conpty-static.h`](https://github.com/microsoft/terminal/blob/dc8ae365847d41f62d1ebca93815bb00959bc12f/src/inc/conpty-static.h) 逐字节相同。以上建立发布包、pair、tag 与公开接口的来源关联，**不等于可重现构建或二进制控制流的动态证明**；不公开原始包或私有运行材料。

## flags 与实际机制

当前 [Session](../../experiments/conpty/session.hpp) 创建实参为 0，[正式后端配置](../../experiments/conpty/backend.hpp) 叠加 `0x18`，含义是 `PSEUDOCONSOLE_GLYPH_WIDTH_CONSOLE`。固定 header 的其他公开位是 inherit-cursor `0x1`、字宽选择 `0x08/0x10/0x18`、ambiguous-wide `0x20`。固定 [Create 路径](https://github.com/microsoft/terminal/blob/dc8ae365847d41f62d1ebca93815bb00959bc12f/src/winconpty/winconpty.cpp#L117-L200) 没有消费旧 `0x2` 的路径；按源码增加该位不会改变启动参数，本次未运行 A/B 验证。

历史 `PSEUDOCONSOLE_RESIZE_QUIRK=0x2` 控制的是 resize 后是否额外整屏 invalidation，**不是关闭内部 reflow**。删除前默认 false；开启后由终端负责自身布局，避免额外整屏重绘。[旧实现](https://github.com/microsoft/terminal/blob/a8582978afa50ece88edc7eda9e182ced64876e2/src/renderer/vt/state.cpp#L291-L342) 与[旧默认值](https://github.com/microsoft/terminal/blob/a8582978afa50ece88edc7eda9e182ced64876e2/src/host/ConsoleArguments.hpp#L130-L138)仅说明历史行为。微软在 2024-08-01 的 [`450eec48…` / #17510](https://github.com/microsoft/terminal/commit/450eec48de252a3a8d270bade847755ecbeb5a75) 删除该 flag 和 VtEngine，改为 API 调用期间生成 VT；不能把旧开关套到当前包。

固定源码支持以下区分：

- **resize 请求与完成不同。** [`ConptyResizePseudoConsole`](https://github.com/microsoft/terminal/blob/dc8ae365847d41f62d1ebca93815bb00959bc12f/src/winconpty/winconpty.cpp#L280-L302) 写入独立 signal pipe；返回成功表示消息写入成功，不是布局完成或重绘屏障。[接收线程](https://github.com/microsoft/terminal/blob/dc8ae365847d41f62d1ebca93815bb00959bc12f/src/host/PtySignalInputThread.cpp#L165-L187)随后执行内部 resize。
- **原生布局与输出流不同。** wrap-text [默认 true](https://github.com/microsoft/terminal/blob/dc8ae365847d41f62d1ebca93815bb00959bc12f/src/host/settings.cpp#L17-L45)，[主屏走 reflow、备用屏走 traditional resize](https://github.com/microsoft/terminal/blob/dc8ae365847d41f62d1ebca93815bb00959bc12f/src/host/screenInfo.cpp#L1352-L1407)；ConPTY [不创建旧 Renderer](https://github.com/microsoft/terminal/blob/dc8ae365847d41f62d1ebca93815bb00959bc12f/src/host/srvinit.cpp#L826-L865)。[`WriteCharsVT`](https://github.com/microsoft/terminal/blob/dc8ae365847d41f62d1ebca93815bb00959bc12f/src/host/_stream.cpp#L378-L435)先更新内部 state machine，再输出输入文本，保留 LF 转换及少量模式注入；它不是原生网格的持续镜像流。
- **查询光标可能产生协议交互。** resize 会标记 cursor 可能失配；外部 GetConsoleScreenBufferInfo(Ex) [必要时等待 DSR 同步](https://github.com/microsoft/terminal/blob/dc8ae365847d41f62d1ebca93815bb00959bc12f/src/server/ApiDispatchers.cpp#L594-L606)。源码明确允许内部 buffer 因 VT/reflow 与宿主不同，[同步只处理光标](https://github.com/microsoft/terminal/blob/dc8ae365847d41f62d1ebca93815bb00959bc12f/src/host/screenInfo.cpp#L1412-L1515)。查询及回复可能干预 cursor，位置一致不能证明正文一致。

## 对已有 B/CD 红例的解释与限制

[既有 Qt / 固定 ConPTY 记录](../../experiments/shell/results/2026-11-02-qt-terminal-modes/README.md)中，80×24→60×12 后原生网格重排 B/CD，物理裁剪核心丢失右端正文并留下旧 D。另一条同源 transport 会话捕获 265 B，resize 边界后没有搬移 B/CD、清旧 D 或整屏重绘字节。这与固定源码的“内部 reflow + 按写入生成 VT”一致，不能据此归因于 parser 忽略已有重绘，也不能通过添加旧 flag 修复。

该运输捕获**不是同一次 Qt 会话**，源码不能替代其动态顺序证据。原生 grid 也不能替代 [WindTerm 原版分层观察](../../research/ui/results/2026-11-02-resize-state-reference/README.md)的显示 oracle；同时，原生缓冲会影响 legacy Console API 读写，不能整体忽略。保留旧 B/CD 失败，不把独立模块、纯 VT 或尺寸通过扩写为完整本地 shell 验收。

## 最小下一实验（尚未执行）

复用原固定 pair、`0x18`、B/CD 合成输入、阶段门和回收预算，在**同一个正式 Qt SessionModel 会话**中：

1. 于实际运输消费边界记录最多 64 KiB 字节、offset、resize 请求/worker 调用、native 尺寸 ACK 和 DSR 往返；API 返回仅记信号成功，以阶段和尺寸 ACK 确认落地。
2. 分别固定 native grid/cursor、核心源正文及软硬边界、协议 cursor、ready 投影与源映射、Qt 实际消费的 page/layout revision；不以 native 行号替代显示行号。
3. 检查是否真的出现重排/擦除字节，以及源内容和显示 page 是否符合原版合同。若有重绘则转查 parser/调度；若无重绘但源或 page 丢字，则仍是未完成的保留/投影闭环。记录查询光标的副作用，不能用光标一致掩盖正文差异。
