# #164：独立静态Search成本准备

状态：初版为源码/false计划准备；执行源`cc07c1296cbaf2ebbc0e832df6af500af494f9f5`的唯一实际批次仅启动Scan100，target exit0，但runner错误要求epoch>0而拒绝真实epoch0，CLI1、后三项未启动。该批次失败原件保留；本次只修decoder并离线读取原stdout回归，不重跑或追认四case完成。基点`a8b338b4f5f96e72eba00a5e495fb8dcb85cd0d3`。既有18项定位cost的Search次数仍为0；本协议不回填旧结果。根CMake、生产8头文件、原probe/prepare/run_plan和18项配方不变。

## 固定配方与独立oracle

独立EXCLUDE target `r164_static_search_cost`，复用原`prepare.py::generated_sources`，不修改已发表的Search转换。Selection/Search均通过所审generated目录的规范绝对header宏include，明确列入target依赖；编译后仍需核实际依赖，静态CMake不冒充CL.read证明。

4个目标进程：Scan/LazyTable × H=100/1000，顺序固定Scan100、Scan1000、Table100、Table1000。各10s、runner批次50s，最多各一次；不构建、不重试、不扩到H10000。期限是诊断预算，非性能容差。同步文件/Git检查及异常收尾没有整程强制中断保证；实际执行还需root另定外层精确Job监督，runner自身只监督目标root，不能由timeout声称树回收。

屏幕6×8，N=H+6。用实际Screen.put/newline建满history；每行7个ASCII，除最后一行外均hard break。命中rank `floor(j*(N−1)/15),j=0..15`写`needle.`，其余写`..x....`；交换可见行2/3（均非命中）造成真实ID非单调，`sorted_proven=false/revision=1`。fixture不reset或切换buffer，故epoch必须是Screen初始值的精确整数0；负值、非零及bool/float均拒绝。fixture检查全文/ASCII span/行界、真实非零唯一ID及版本；原始输出保存全部实际行ID/version、epoch和16个字面期望区间`[0,6)`，不使用Adapter/Search计算期望。

每进程1 warmup样本(-1)＋3保留样本(0/1/2)，每样本新Adapter、同一immutable Source：

1. 新Search `absent!`：cold start→完整step，0结果；LazyTable首个定位建表一次。
2. 同Adapter上新Search `needle`：warm start→完整step，16个结果，按物理rank逐项核ID/version/epoch/列；result_limit=64，不能capped。
3. 一次`match(15)`，必须为最后字面命中。
4. 一次next/no-wrap navigate，anchor最后一行col7，必须无结果；实际检查16候选，不另加导航矩阵。

两次scan固定budget64、同代generation、无notify_change/结构或内容变化；每次必须load N行、消费8N−1字节、使用U=10N−1 work（N load＋7N文字＋N−1 LF＋N finalize）、恰S=ceil(U/64)个step。任何零进展、代/帧失效、错结果、capped、超work/call界都留raw失败，不改断言取绿。

## 静态计数界限

本快照无audit/rewind/实际prune；step入口校验与front存在检查不计work但各最多一次locate。因此每完整scan locate≤3N+2S+1；last-match恰N+5，no-wrap恰16(N+9)+1，每样本总locate≤L=23N+4S+152。4样本Scan probe≤4LN：H100为1,126,992；H1000为96,262,128。**不是10s完成保证**。

decoder另按固定64步边界核精确scan计数：令E为U%64非零时1，否则0；C为所有k=0..S−1且64k%10非零时的`floor(64k/10)+1`集合（这些step从已加载行内部开始）。每scan locate恰3N−2＋E＋S−1＋|C|；Scan probe恰3N(N+1)/2−N−1＋EN＋S−1＋ΣC。这里front存在检查定位rank0，仅一次probe，不能笼统当成每次都扫全N。该推导仍是待实际C++反证的源码oracle，未伪装成实测计数。

生成match每次遍历全部rows并定位，即Scan单次O(N²)、Table O(N log N)；navigate可乘命中数16。这是实验生成caller路径，不称生产TextSearch已测得相同成本。独立decoder还核Scan last-match probe恰N(N+1)/2+5N、navigate probe恰16N(N+1)/2＋7Σ(rank+1)＋33N。Table lookup probe保守界为4L(ceil(log2 N)+1)，H1000为1,052,568；建表遍历/排序/唯一性检查未计probe，不能用probe比当总工作或加速比。

## 端点、计划与停止

每样本6条stage：cold start、cold step、warm start、warm step、last-match、navigate；每进程另1条fixture，共25条。分别记录steady_clock ns单位的start、各step调用时间之和/最大单step、match、navigate以及每stage lookup/probe/rebuild增量、当前表payload；时间允许0，不代表ns精度。字符串构造、fixture/oracle、阶段之间的计时器/控制、JSON/flush和对象析构在这些调用端点之外；counter执行开销包含。两个Search对象在样本中同时存活；payload不是峰值/完整分配器或RSS，RSS始终null。所有warmup和样本保留，无宿主隔离、无跨进程重复/交错，不能推出产品性能验收。

`prepare_search_cost.py --materialize <新local目录> --compiler <所选cl绝对路径> --cmake <所选cmake绝对路径>`只读工具文件identity，不执行version。生成false计划绑定HEAD、目录全部文件和8生产头文件、两生成header字节/路径、Python/CMake/编译器bytes/SHA；actualEXE/build-evidence/output/marker均null。后续true计划必须另建、绑定真实EXE/build证据并授权精确4case/50s；false和旧18cost许可不能触发新target。

`run_search_cost.py --plan <新独立true计划>`在marker前核类型/全部recipe、source/generated/tool/Python/EXE/build身份及新目录；marker独占新建，output与marker分离且不重叠generated/runtime。每case前后验和batch末再验输入；按原样保存command/stdout/stderr/exit/timeout及机械decoder结果，错误停止，无fallback。CLI0仅表示固定诊断完成，product_acceptance=null；未经审查的本地raw不直接公开。

离线回归可通过环境变量`TERMFORGE_R164_STATIC_SEARCH_OBSERVED_STDOUT`提供保留的Scan100 stdout；测试核固定SHA后按实际协议解码，并同步改变fixture和所有range的epoch验证负值/非零拒绝。不提供该私有fixture时只跳过这项实录回归，不把合成协议当实录。未知：其余三配方的真实计数与时钟样本、完整四case预算、完整增量Search、生产revision通知、UI回退、实际查询分布、RSS/峰值、Qt/ConPTY端点和WindTerm等价；构建依赖身份由独立构建证据核对。本修正不修改这些codepaths，也不关闭#27/#29/R2。
