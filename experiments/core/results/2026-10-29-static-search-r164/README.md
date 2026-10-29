# 静态 Search 成本：固定快照诊断（#164）

独立四 case 批次完成，支持继续调查无序定位在生成 Search caller 中的成本放大，**不构成产品选型、性能验收或完整增量 Search 验收**。它与[此前18项定位成本](../2026-10-29-line-order-r164/README.md)分开，不回填旧结果；生产核心、根CMake和原probe不改。[协议](../../line_order/static-search-cost-plan.md)与[全部脱敏数值](validation.json)可独立核对。

## 身份与失败保留

基点为 `a8b338b4f5f96e72eba00a5e495fb8dcb85cd0d3`。C++/CMake编译源为 `cc07c1296cbaf2ebbc0e832df6af500af494f9f5`；实际Release x64 EXE为98304B，SHA256 `2ef1d42653e349b434fa1c42fe61a2dcd31d196caccfd2dacfd77b162654d675`。实际CL.read和编译宏均绑定所冻结的两份生成header：Selection SHA `88bf44a9497ac6afdf526589dfc5b77b9ed03cca760fe2aa09188e92e0d958df`、Search SHA `6db8ee628eec1eacabc54bfdc27eb35c27327e1fc1b8b25b44cad58a2001afde`，无同名旧副本回退。

首个独立租约actual-0只启动Scan100：target exit0，但旧runner误要求epoch>0，拒绝Screen真实初始epoch0，runner exit1，后三项未启动。原计划、失败、stdout和时间样本全部保留。修正 `a6157dea2d38bcbbeb29d48fc757d522374c3e2c`只改decoder、纯测试和协议说明，严格要求整数epoch0；实际保留stdout回归与同步改变全部range的负值/非零/类型反例，普通及`-O`各8/8。未改C++、CMake、生成header或EXE。

随后另授权actual-1：runner源a6157de复用上述cc07c129编译EXE，四case各一次，均exit0、无timeout、stderr为空，runner exit0、diagnostic_complete=true。本文是后续离线投影，发布提交不是运行源。JSON分列两租约、false/true计划、源/工具/EXE/构建与raw哈希；不把actual-0追认为成功，也不将两次Scan100拼成重复采样。

## 配方、端点与环境

固定顺序Scan100、Scan1000、LazyTable100、LazyTable1000；每进程1 warmup（-1）及3保留样本（0/1/2）。Screen为6×8，N=H+6；每行7个合成ASCII，16个命中rank为`floor(j*(N−1)/15)`，命中行`needle.`、其余`..x....`，除末行外hard break。交换两个非命中可见行产生真实ID无序；epoch0、revision1、内容及结构均不变。

每样本新Adapter，同Adapter依次执行cold `absent!`全扫（0命中）、warm `needle`全扫（16命中）、一次match(15)、末行col7锚点next/no-wrap（无结果）。期望由实际物理rank及Screen行ID/version/epoch和字面列区间[0,6)独立构造。每scan budget64：H100/1000分别为17/158步、1059/10059 work、847/8047字节、106/1006行；未触及result_limit64。所有阶段按有界进展、状态、范围及精确lookup/probe oracle核对。

时间为steady_clock纳秒单位，不代表纳秒精度：start调用、各step调用之和/最大单step、单次match及navigate分别计时。计数器开销包含；fixture、字面oracle、字符串构造、阶段间控制、JSON/flush及对象析构不在这些调用端点内。六端点之和不是整个样本或进程时间。两个Search对象同时存活；table payload只计位置vector容量，RSS/峰值均未知。

环境为共享Windows x64本机，MSVC19.42.34433.0、CMake3.31.0、SDK10.0.26100.0，Release `/O2 /Ob2 /MD /EHsc /W4 /utf-8 /std:c++17`及NDEBUG。编译身份来自实际元数据/依赖；本批未保存执行期间独立CPU/OS build清单或连续遥测，不挪用旧18项成本的宿主盘点补录。未隔离负载、锁频/亲和性；没有进程交错或独立进程重复。

## 数值与解释

下表为actual-1三个保留样本各端点的中位数，单位µs；cold/warm scan列为该次全扫的step时间之和，max-step列为三个样本各自最大step的中位数。各列独立统计，不能相加还原样本中位数。warmup及所有原始值均在JSON，无删样或追加取样。

| Adapter | H | cold scan | warm scan | match(15) | no-wrap | cold max-step |
|---|---:|---:|---:|---:|---:|---:|
| Scan | 100 | 244.2 | 198.2 | 18.6 | 349.6 | 35.9 |
| Scan | 1000 | 7548.5 | 6722.6 | 1450.9 | 30491.1 | 534.7 |
| LazyTable | 100 | 123.6 | 128.3 | 4.0 | 33.5 | 11.8 |
| LazyTable | 1000 | 1115.6 | 1155.7 | 60.2 | 749.7 | 41.9 |

每样本两次scan的lookup分别为346/3300；last-match为111/1011，no-wrap为1841/16241。Scan每scan probe为17713/1583289、last-match为6201/511551、no-wrap为100226/8193926；LazyTable相应为2360/32950、757/10094、12560/162154。各样本计数一致；LazyTable每样本仅cold scan重建一次，payload为1696/16096B，warm及后续调用不重建。

生成match逐行定位全部已扫row，Scan单次可为O(N²)、LazyTable为O(N log N)；导航逐项验证16个候选，固定计数与该路径的源码推导一致。这是**实验生成caller**的结果，不等同生产TextSearch已测得同样成本。probe只计定位比较/访问，不含建表遍历、排序和唯一性检查，不能当总工作或用其比值宣布加速。

LazyTable cold-step时间包含首次定位的分配/建表/排序，但没有独立建表计时、首step时间或max所在序号，不能把max-step称为建表耗时。H1000的warmup cold max为185.3µs；三个保留样本cold max为65.1/41.9/36.3µs，warm max为17.7/56.0/73.9µs，保留原波动，不以cold−warm差值归因全部建表成本。

## 监督、公开边界与未知

每target10s、runner批次50s、外层root55s及共享cleanup5s均为固定诊断预算，不是性能阈值或整生命周期硬上限。actual-1 runner记录3.079s、外层3.781s，仅表示各自监督端点。两租约各33冻结输入后验0变化；本次离线导出复核78份既有本地文件0变化。

actual-0/1的root均已回收且句柄关闭，但root结束即时Job分别仍为1/31/0、1/76/0；各一次Terminate请求后为0/31/0、0/76/0。剩余成员身份及是否实际杀死成员未知，terminated计数均0，**不称整树自然退出**。

JSON公开actual-1的100行协议（4 fixture＋96 stage）和actual-0另25行原协议的数值投影，五份stdout均可逐字节重建原CRLF及核SHA；不复制命令、私有路径/PID、随机permit或其他raw日志。validation为326455B，SHA256 `2ec4d9f6869643d0d5c5ff9ab6246049371eefa70dff1e5b04aec19e5246483d`。完整增量Search、结构变更/revision通知、UI/真实查询分布、RSS/峰值、Qt/ConPTY和WindTerm等价仍未验证；本报告不改变#164或后续实施任务状态。
