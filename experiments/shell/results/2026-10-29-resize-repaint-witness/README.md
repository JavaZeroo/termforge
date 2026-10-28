# #156 缩小与重绘交错：限定 C++ 机制诊断

固定执行源 `178faba6c245298a1410d6954b6a7e1df1e9bbd7` 的一次四-case 诊断中，A/C 保留原2499B选区，B/D在第五段重绘时首次失效。它证明所列交错能通过真实 Parser/TextDocument/TextSelection 形成坏状态，**没有确定原 CI 的实际切点、调度或唯一根因，也没有修复生产逻辑**。`semantic_acceptance=false`、`ci_cause_established=false`、`ci_actual_cut_known=false`。

[执行前合同](../../../../docs/research/resize-repaint-witness-plan.md)、[诊断源码](../../../core/resize_repaint_witness.cpp)与[脱敏验证](validation.json)分别保留计划、实现和实际结果。准备提交与执行提交同为 `178faba`；本报告是后续文档发布，不充当运行源。原 false plan、授权执行 plan、原 CI artifact、全部编译/运行/分析 raw 均保留本地，不随报告上传。

## 固定输入与一次结果

诊断基于实际失败源 `58fee2805cde34c67683a79473ca56f9df3cbf69`，原八个相关文件与比较源 `6a10e5322a1bddeb038670e50f32ff6deac145cf` 的 blob 相同。没有运行 Win32/Qt UI、ConPTY 或原版。使用现有 recovery Parser、TextDocument、TextSelection 的公开边界，以原合成 fixture 函数生成 `TF_FIND_000..079` 和 READY 行；111×7、history capacity100时实际得到 history75/document82，并按真实 Select All 范围规则复制2499B。行 ID 由新 Parser 自然分配，不补造 CI 启动前的绝对偏移。

1071B transport 由原创标记和 VT 构造，与固定 CI member 在内存中逐字节相等，SHA-256为 `c1d7cb18d5da399a838c997e0abe9375fd4db037e9f8f609ac02e1a58878cb84`；五段为210/210/210/231/210B。四个 case 各一次，完整保留原字节与次序，不去重、不扫描切点、不重试。前三段各在112×7、112×7、111×7喂入，第四段先扩为149×10，再在固定前缀处缩回111×7，喂原尾及完整第五段。尺寸统一为列×行，offset为累计 transport 字节数。

| Case | 第四段缩小切点 | 缩小与原尾的实际观察 | 第五段后的结果 |
| --- | --- | --- | --- |
| A | 完整231B，offset861 | cursor row6；缩小后history75/document82 | 2499B保留 |
| B | 204B，offset834 | cursor row6；缩小后75/82。原27B尾在7行网格继续LF，使history78/document85，选区仍为2499B | 首次invalidated，sequence12/offset1071 |
| C | CUP 7;1已完成225B，offset855 | cursor row6；缩小后75/82；剩余6B show cursor无此变化 | 2499B保留 |
| D | CUP之前219B，offset849 | cursor row9；缩小本身使history78/document85；原12B尾后选区仍为2499B | 首次invalidated，sequence12/offset1071 |

B/D在第五段Home重绘后，原相对ID79..82对应的文字摘要改变，选区按既有 policy 失效；这不是纯version变化导致的失效。B缩小时丢弃扩展空行ID83..85，再由原尾滚动分配ID86..88；D缩小时移动前三行入history并保留ID83..85。两条分配路径和尾部version不同，不能把B/D当作同一条时序。B的部分尾version与旧CI相似，只能作为机制比较，不能据此认定CI在offset834缩小；CI每阶段的真实消费offset/parser cursor仍缺失。

## 构建、记录与监督边界

一次独立configure/build成功，0 warning/error；Visual Studio 2022 x64 Release，MSVC `19.42.34433.0`，实际编译参数含 `/O2 /MD /std:c++17 /W4 /utf-8`。只构建显式 `EXCLUDE_FROM_ALL` witness，关闭Qt/candidate/BUILD_TESTING，不注册CTest。EXE为112128B，SHA-256 `3fdd9172e955a468fc9db7a540dcc5042156cde2aafa8e0bee1786cb6f0189c4`；15个冻结源、输入、原archive、compiler/flags/EXE后验0变。

实际输出61条JSONL、51个checkpoint、817861B stdout，stderr为空，无超时或输出溢出。每次feed/resize均reconcile并记录cursor/history、行ID/version、文字/span摘要、选区与首失效点；FNV摘要用于比较变化，输入/源/EXE/raw身份另以SHA-256绑定。CLI0仅表示四个预定诊断记录齐全，包含两个`observed_bad_state`，不表示产品正确性通过。10s进程守卫和输出上限是诊断资源边界，不是性能结论。

每个阶段的GO host先进入独占Job后才启动子命令。configure/build结束时分别为Job0/30/0、0/20/0，无终止请求。witness root已回收并关闭handle，但其结束后仍观察到Job active1/total5/terminated0；监督器发起一次TerminateJobObject，再观察到0/5/0。没有为该剩余成员保留逐进程handle/身份，无法判断它是谁或是否在请求前后自然退出。terminated计数0不能证明没有影响；**不宣称全树自然退出**。

此诊断没有观测真实CI调度、child尺寸确认、Qt产品路线、画面呈现、WindTerm等价或性能，也未更改旧严格判据。后续定位与生产修复归[#156](https://github.com/JavaZeroo/termforge/issues/156)；原失败与未知不因机制见证而转为通过。
