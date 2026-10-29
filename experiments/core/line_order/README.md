# #164：行身份与文档顺序的有界比较

状态：**精确header绑定源a84a287已独立编译；首租约真实caller RED为3 FAIL/exit1，两Adapter correctness通过；独立18case成本租约均exit0。无产品选型或性能验收。** 基线为 `35a323a3165dae6998237bfd768c7c54c9c8ae99`。这不是区域/origin协议实现，也不恢复任何实验产品后端。

本目录是独立CMake工程，目标 `r164_probe` 带 `EXCLUDE_FROM_ALL`；根CMake和生产Parser/Screen/TextDocument/TextSelection/TextSearch/HistoryViewport均不修改。没有新增依赖。本次运行源为 `a84a287b1cb0404ce6245ba42d6bcb55a0c96f64`，实际Release x64 EXE为144896B、SHA256 `187d349cd8356aef2abd8ced617223b42178034837fef80e6dec7ab4538a3c08`。新编译、实际CL.read两header依赖、首租约及成本租约分开保留。详细原始数值、环境/端点、推荐及回收缺口见[结果报告](../results/2026-10-29-line-order-r164/README.md)和[脱敏validation](../results/2026-10-29-line-order-r164/validation.json)；本文后续更新仅文档，不把新HEAD冒充已执行源。

旧源码 `28c98d58b05e5590a2ef483f16eda8807cc7cde5` 已由root做一次configure/build，两个root均exit0；实际 `r164_probe.exe` 为144896B、SHA256 `2cde284594264bc6bb348a00aec8d0e4edd24751620a0cfd45c010926adad4a2`。两次即时Job accounting仍有残留成员，各请求一次终止后最终Job0，不能称全树自然构建。原计划/命令/stdout/stderr/清理和EXE身份留在私有 `local/compile-r164-0/`，该EXE未执行。此编译不归属于当前header绑定修订，不提供原caller RED、两Adapter正确性或成本结果；preflight-v1/v2原件保持不变。

## Module、Interface与真实seam

Module集中做精确ID定位和文档顺序比较。Interface是：实际Screen的只读行值、帧epoch、单调order_revision、有序证明，以及`locate(id)`/Document位置比较的失效结果。Adapter不分配行ID、不重编号、不决定失效锚点的UI行为。存活位置随行移动保留ID/版本；order_revision仅使排名缓存失效，不自动使所有TextPosition失效。帧切换epoch仍使旧帧位置无效。

两个Adapter在同一seam变化：

| Adapter | 有序证明有效时 | 无序时 | 实际代价/未知 |
|---|---|---|---|
| ScanAdapter | 对实际retained行二分 | 精确扫描实际Screen | 无额外位置表；重复查询可线性放大成本 |
| LazyTableAdapter | 同一二分 | 按epoch/revision惰性构建ID→位置表 | 本原型用排序vector；建表/排序/存储成本存在，容器不是永久Interface或产品选择 |

Source的通知由隔离fixture负责。**生产公开行存储、导入、隐藏帧和全部变更点还未接入这个通知契约**；遗漏revision会使缓存错误，不能声称已有可直接发布的生产接口。有序证明不得由ID大小猜测；任意重排后撤销证明。无序行后来进入history仍必须走无序路径，不能永久认为history有序。

## 调用图与未迁移假设

| 现有调用者 | 固定源中的依赖 | 本准备覆盖/缺口 |
|---|---|---|
| Screen::find_line、TextDocument::index_of | lower_bound_line假定retained ID递增 | 原实现真实RED；实验Document改用Adapter |
| TextSelection::set/reconcile/copy | index_of、物理相邻行、版本及软链 | 从固定生产class生成模板副本，仅替换Document类型，准备文字复制/失效检查 |
| TextSearch::navigate/match | before的数值ID顺序、rows_的ID二分 | 从实际class生成实验副本，改为Document顺序/精确排名；仅全新静态快照 |
| TextSearch增量rewind/emit/prune | >=changed_id、>=emit_from_id、<=removed等 | **未迁移、未验证**；副本删除notify_change入口，结构版本变化只报告明确失效，不冒充增量合同通过 |
| HistoryViewport::reconcile/reveal | lower_bound_line及已淘汰锚点回退 | 仅准备原有sorted追加/淘汰真实caller检查；无序时只验证Module精确anchor定位/缺失，未替换UI回退 |
| candidate_terminal的导入/别名 | find_line、normalize_identities | 只记录依赖；未运行候选、未适配导入，不作为产品路线 |

生成副本保留生产Selection/Search的class机制及公共DTO，不再定义重复DTO。它们由 `prepare.py` 从固定git源生成到`local/`，不提交大份复制文件。Search的队列、扫描和结果机制仍来自生产class，但保留的增量代码不因此得到验收。快照内禁止变更内容；结构变化有显式拒绝结果，没有清空全部读者来掩盖定位错误。

## 独立正确性

`--existing-red`调用**原**Screen/TextDocument/TextSearch：Screen真实分配1..7，隔离fixture删除2并把7移到6前，得`[1,3,4,5,7,6]`。字面期望为ID7→4、ID6→5、ID2缺失，以及从显示中的7命中向下找到6命中。实际首租约分别在ID7定位、ID6定位和搜索导航得到3 FAIL、exit1；删除ID2检查通过。它是原生产caller的实测RED，不是仅模型自比。

`--correctness`在同一进程依次检查两Adapter，实际exit0，验证：

- 同一字面ID/位置oracle，`needle\r\nneedle`复制和静态搜索顺序。
- 行移动保持ID、内容版本、文字和存活位置；删除ID不复活；revision变化不沿用旧排名。
- 多行选择结构改变后明确失效；静态Search遇结构变化拒绝旧请求/结果。
- 原有真实autowrap的后继关系，仅实际相邻时构成软链。
- 实际Screen切屏后旧Source/位置不可跨帧；刷新Source后缓存不能泄露主帧ID。
- 原有sorted追加/淘汰定位及HistoryViewport回退；不改变既有排序路径政策。

这些局部检查已在首租约各一次完成；旧EXE没有执行，不能混入新源身份。无序视口UI、增量搜索持续进展、导入/resize事务与生产通知闭合仍不在已验证范围。

## 执行计划与停止条件

准备命令只生成文本和false计划，不调用CMake/编译器/目标EXE：

```powershell
<Python> -B -X utf8 experiments/core/line_order/prepare.py
<Python> -B -X utf8 experiments/core/line_order/prepare.py --materialize local/line-order-r164/preflight-v3
```

本地`false-plan.json`绑定source HEAD、8份实际产品依赖、实验源码、生成字节和Python/CMake/编译器文件SHA。工具身份仅是只读文件身份，没有运行`--version`。实际EXE的path/SHA/observed为null/null/false，只有拟构建路径；`execution_authorized=false`、authorized_cases空、out/marker/整批实际上限均null。不得覆盖旧false计划来授权。

下面保留命令形状供审查，实际root使用独立compile-r164-1目录与两份true租约，均已有原件；**这些不是新的运行授权**：

```powershell
cmake -S experiments/core/line_order -B build/line-order-r164-v3 -G 'Visual Studio 17 2022' -A x64 -DR164_GENERATED_DIR=<所审v3计划相邻generated绝对目录>
cmake --build build/line-order-r164-v3 --config Release --target r164_probe --parallel 2
<Python> -B -X utf8 experiments/core/line_order/run_plan.py --plan <独立已审true计划>
```

首租约只含一次原caller RED和一次两Adapter correctness，各进程10秒、runner批次25秒；后续18case由root另发成本true租约，各进程10秒、runner批次200秒，已各一次执行完成。旧false计划没有改为true，旧原件保留；本次报告不授权再执行。Runner在创建marker/out前拒绝false、缺实际EXE身份、源码/生成字节/工具身份漂移、改动过的case配方或预算；超时/异常退出保留raw并停止，不重试。它不构建、不运行原版、GUI或子基准套件。

构建必须显式选择所审计划绑定的generated绝对目录，且selection/search两份文件均存在。CMake将它们的规范绝对路径作为带引号的编译定义；probe仅通过这两个宏include，无 `generated/*.hpp` 搜索路径回退。两个实际header也直接列入target源，真实include使编译器追踪对应header依赖；普通`.`/`..`路径下同名generated旧副本不参与这两次include。本次已由真实CL.read依赖核对为计划v3的Selection/Search这两份header；此前shadow入口修正与新编译身份均保留，不以仅静态CMake文字代替实际依赖。

## 固定成本配方与实际结果

2 Adapter × H=100/1000/10000 × append/reorder/evict，共18进程；各进程10秒，runner批次监督上限200秒，由独立成本租约授权并完成。N=H+6。append为已有序满history追加并自然淘汰；reorder仅旋转内部四行；evict从无序活动行开始，随后追加使无序ID流入history并淘汰。它们不是新终端协议。

每进程1轮warmup sample＋3个保留sample，每sample固定32个round；每round一次结构变化、16次定位：14个命中rank为`0,N/4,N/2,3N/4,N-1,H,H+1,H+2,H+3,H+4,H+5,0,N/2,N-1`，另2个缺失ID为0和UINT64_MAX。因此每sample512次、每进程含warmup2048次定位；rank除法为整数除法。Scan最坏定位probe上界为2048×N，H=10000时20,492,288，**不是运行测量或时间保证**。

静态Search在每round、sample和成本进程中均为0次，search_cost=null。全历史Search含额外定位/校验，可能平方放大，必须另定工作量与预算，不能塞进本10秒配方或将小快照搜索当作该成本结果。

实际raw记录mutation、首次定位及其余15次定位的独立计时；字面oracle校验在定位计时外，计数器成本包含在内。raw逐round保留warmup/sample标签、定位probe数、建表次数与校验checksum。table_payload_bytes只计vector capacity×元素大小，**不是分配器总占用、RSS或全内存**；排序内部比较不算定位probe。没有纯净计量对照、共享宿主控制或产品性能门槛，因此不据此选择产品实现或宣布性能等价。

本次2304条round及36864次定位的配方/计数/checksum已离线重算，公开JSON可严格还原18份原stdout的CRLF字节。checksum只加后15次查询，首查另有独立oracle。建议优先接续无序LazyTable候选并保留Scan对照，但单次变更后查询数、生产revision通知、完整incremental Search及产品选型仍未知。两次外层监督在root结束后即时各有1个身份未知成员，各一次Terminate请求后Job0，terminated计数仍0；不声称整树自然或已识别/实际杀死该成员。
