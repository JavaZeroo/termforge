# 行身份与文档顺序的有界比较（#164）

建议保留统一的精确定位/文档顺序 seam 和可证明有序时的快路径；无序路径优先接续 LazyTable 候选，Scan 保留为正确性对照。**这不是产品选型或性能验收**：结构变更通知尚未接入生产所有入口，完整增量 Search、无序视口 UI 和真实调用频率仍未知。本次没有修改生产核心、实现区域/origin协议或运行 Qt、ConPTY、WindTerm、CTest/full。

## 来源与实际执行

合同见 [#164](https://github.com/JavaZeroo/termforge/issues/164)，实现与调用图见 [实验说明](../../line_order/README.md)。基线为 `35a323a3165dae6998237bfd768c7c54c9c8ae99`；本次编译和两份独立执行计划均绑定源 `a84a287b1cb0404ce6245ba42d6bcb55a0c96f64`。Release x64 EXE 为144896B，SHA256 `187d349cd8356aef2abd8ced617223b42178034837fef80e6dec7ab4538a3c08`。实际 CL.read 依赖恰为计划 v3 的两份生成 Selection/Search header，未沿旧 `generated/*` 搜索路径回退。旧源28c98d5/旧EXE `2cde2845…` 仅编译、没有执行；旧v1/v2/v3 false计划及两次编译、实际执行原件均保留，各自身份不合并。

当前主机的事后只读元数据为 Windows 11 Pro x64 10.0.26200、Intel Core i7-9700（8核/8逻辑处理器）；MSVC19.42.34433.0、CMake3.31.0、SDK10.0.26100.0，Release `/O2 /Ob2 /DNDEBUG /MD`。CPU/OS元数据不是执行期间连续遥测。共享宿主未隔离负载、未锁亲和性/频率，进程顺序固定为先Scan后LazyTable，没有交错或独立进程重复来排除漂移。

首次租约的两个 C++ 进程各执行一次：原 Screen/TextDocument/TextSearch 的真实 caller 检查 **exit1、3 FAIL**（ID7→4、ID6→5、从显示ID7向下找到ID6），删除ID2检查通过；两 Adapter 的 correctness 进程 **exit0**。它覆盖小固定夹具的定位、选择复制、静态 Search 顺序、存活ID/内容版本、删除不复活、revision/epoch失效、实际软链相邻性及原sorted历史淘汰回退。generated Search 只支持全新静态快照，结构变化明确拒绝旧结果，不能据此接受完整增量搜索或无序视口UI。

随后独立成本租约18个 C++ 进程各一次，全部exit0、无timeout、stderr为空。每进程10秒、runner批次200秒为冻结诊断上限；首租约为10秒/25秒，不是性能容差。本文仅离线分析这些原件，没有重跑目标。

## 配方、oracle 与端点

2 Adapter × H=100/1000/10000 × append/reorder/evict，保留行N=H+6。append在已满且有序的history追加/淘汰；reorder反复旋转活动区内部四行；evict先交换两个活动行，再追加使无序ID进入history并淘汰。evict全程没有重新证明有序，即使造成无序的ID已被淘汰，也保守保持无序路径。每进程1个warmup样本（sample=-1）和3个保留样本（0/1/2），每样本32轮，每轮1次结构变更及16次定位。14个命中rank为 `0,N/4,N/2,3N/4,N-1,H,H+1,H+2,H+3,H+4,H+5,0,N/2,N-1`（整数除法），另2个查询为缺失ID0/UINT64_MAX。总计2304轮、36864次定位，其中1728轮/27648次属于保留样本。

每次命中/缺失在定位计时之外检查实际返回位置；checksum只累计后15次查询中的13个命中rank+1，两个miss加0，首查另有独立assert、没有计入checksum。每轮累计lookup/probe/rebuild/checksum已离线按行ID变化和二分/扫描规则独立重算；18份原stdout的全部CRLF字节亦可由公开数值数组严格还原。完整2304条数值及54个保留/18个warmup样本汇总在 [validation.json](validation.json)，附原件、计划、工具、源和EXE哈希。

mutation只计结构操作和revision递增；first lookup单列；other15是其余15次定位的整体计时，不能当作单次延迟。query构造、oracle与JSON输出不计入定位端点，计数器开销包含在内。LazyTable无序首查含分配、遍历、排序、唯一性检查、旧vector释放和本次定位。计时为steady_clock纳秒单位，不代表纳秒精度；原始样本存在0值及量化，保留全部样本、未去异常点或追加取样。

## 实测摘要

时间单位为µs/round：先将每个保留样本32轮分别求平均，再取三个样本均值的中位数；min/max及原数值见JSON。各时间列分别取中位数，不能把列中位数相加还原全16定位列。probe/rebuild列含warmup的128轮累计；payload为该case最后一轮的表容量字节。

| Adapter | H | workflow | mutation | first lookup | other15组 | 全16定位 | lookup probes | rebuilds | table payload B |
|---|---:|---|---:|---:|---:|---:|---:|---:|---:|
| scan | 100 | append | 0.172 | 0.044 | 0.356 | 0.400 | 15,488 | 0 | 0 |
| scan | 100 | reorder | 0.053 | 0.016 | 3.000 | 3.016 | 161,536 | 0 | 0 |
| scan | 100 | evict | 0.219 | 0.019 | 3.003 | 3.022 | 161,536 | 0 | 0 |
| scan | 1000 | append | 0.241 | 0.066 | 0.478 | 0.547 | 21,888 | 0 | 0 |
| scan | 1000 | reorder | 0.056 | 0.019 | 29.497 | 29.528 | 1,543,936 | 0 | 0 |
| scan | 1000 | evict | 0.328 | 0.019 | 27.509 | 27.528 | 1,543,936 | 0 | 0 |
| scan | 10000 | append | 0.319 | 0.169 | 0.728 | 0.875 | 28,928 | 0 | 0 |
| scan | 10000 | reorder | 0.075 | 0.013 | 329.547 | 329.569 | 15,367,936 | 0 | 0 |
| scan | 10000 | evict | 0.641 | 0.022 | 341.300 | 341.319 | 15,367,936 | 0 | 0 |
| lazy_table | 100 | append | 0.247 | 0.050 | 0.350 | 0.397 | 15,488 | 0 | 0 |
| lazy_table | 100 | reorder | 0.056 | 0.919 | 0.178 | 1.097 | 13,568 | 128 | 1,696 |
| lazy_table | 100 | evict | 0.188 | 0.959 | 0.178 | 1.134 | 13,574 | 128 | 1,696 |
| lazy_table | 1000 | append | 0.263 | 0.072 | 0.516 | 0.588 | 21,888 | 0 | 0 |
| lazy_table | 1000 | reorder | 0.050 | 8.838 | 0.228 | 9.066 | 19,968 | 128 | 16,096 |
| lazy_table | 1000 | evict | 0.309 | 9.047 | 0.225 | 9.259 | 19,968 | 128 | 16,096 |
| lazy_table | 10000 | append | 0.428 | 0.263 | 1.775 | 1.988 | 28,928 | 0 | 0 |
| lazy_table | 10000 | reorder | 0.087 | 143.631 | 0.306 | 143.938 | 27,008 | 128 | 160,096 |
| lazy_table | 10000 | evict | 0.822 | 135.347 | 0.325 | 135.672 | 27,009 | 128 | 160,096 |

在这组无序、每变更后16次定位的固定配方中，LazyTable的重建成本主要落在首查，后15次查询较少遍历；三种H的reorder/evict保留样本均值中位数均小于Scan。H=10000时，reorder全16次定位为Scan329.569µs、Table143.938µs，evict为341.319µs、135.672µs；Table首查分别为143.631µs、135.347µs。**不能推广到一次变更仅查询一次或其他命中分布**：此处Scan首查总是rank0，重建是否摊薄取决于真实查询频率。

有序append两者实际lookup probe数相同、rebuild=0、payload=0，走相同快路径；H=10000的时间仍明显不同，说明本次顺序/宿主/微计时不足以据微小样本选择产品。lookup probe只是各实现规定的访问/比较计数：Table建表遍历、排序和唯一性检查不计入，ordered路径另计最终相等访问，因此不能把probe比直接当总工作或加速比。

Table无序payload为1696/16096/160096B，即本构建的当前vector capacity×元素大小；不含重建时旧/新表并存、分配器、排序临时、其他对象或RSS。所有成本round中静态Search次数为0、search_cost=null；首租约的小快照搜索正确性不提供全history搜索成本或完整incremental合同。

## 回收与保留的未知

两次runner根均exit0且根句柄已回收。外层监督器在根结束后即时accounting仍各有1个身份未知成员：首租约 `1/14/0`、成本租约 `1/30/0`（active/total/terminated），各请求一次TerminateJob后分别为 `0/14/0`、`0/30/0`，关闭前仍为0；terminated计数未增加。**这不证明该未知成员被实际杀死，也不证明整树自然退出**。新的configure/build同样保留各自即时残留及一次终止请求，不能以root exit0代替构建整树自然回收。

报告整理前冻结94份私有输入（旧false计划/compile0、新compile1、两lease及supervisor、实际EXE/tlog），并核对15份源和2份生成文件；报告完成后的不变检查单独保留。公开内容只含固定合成数值、可审查源码标识、哈希及脱敏环境；原始计划、命令、主机/账户、路径/PID和监督器全文保持私有。

下一片应先闭合生产变更通知/失效职责，再用实际caller的查询与修改频率核对候选。容器、完整增量Search、无序视口fallback、导入/resize事务、峰值/RSS、Qt/ConPTY端到端、WindTerm性能等价及数值验收门槛均未确定；不据本次结果关闭#27/#29/R2或声称已实现区域协议。
