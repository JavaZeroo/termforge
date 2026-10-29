# 自有 desktop 生命周期的有界诊断（#160）

两组固定观察已完成；本地原watchdog首次probe为已消失，**未复现CI首次可重新打开的现象，原CI原因仍未知**。正对照验证显式第二个handle存活时可重新打开，释放后四次独立probe均不存在。本次只修补充观察器，未修改生产监督器或首次判据，不宣布产品修复或关闭#160/#38/R2。

## 实际身份与执行

collector源为 `e8455b82d1cdd682a7dfde9f2303c7f1b43f63be`；复用源 `6e67be541b134084ca8b0a64a94308e8e3d23242` 已编译的Release x64默认C++/system ConPTY/Win32。shell为1236480B/SHA256 `85094afb626c162140608f8f5303cd30558c82cd98f534725b1dbeaf072aa2ff`，launcher为47616B/SHA256 `5165f20be105f529ae2c14b885aab7b537735852091e319ce0fb7dc261273cf4`，完整runtime12文件；此次未重编译。五份原监督/launcher/shell源与原CI `01a979d` 相同，不合并Qt/固定微软后端或其他配置的成功。

环境为Windows x64 10.0.26200、Python3.12.10；原构建为MSVC19.42.34433.0/CMake3.31.0。configure/build根均返回0，但即时Job尚有成员，各请求一次终止后归0，只证明编译成功，不证明构建整树自然回收。

原负例为 [PR157/default job123121553707](https://github.com/JavaZeroo/termforge/actions/runs/40774909044/job/123121553707)：原3秒watchdog、timeout/99/readiness和Job0满足，creator关闭后首次Open成功。原ZIP SHA256 `05392812a79a0b38d48384b47905c85c9f46285cbedcf0b4afa2275550c9efbb`。本地观察不覆盖该负例。

本批两个case各一次：

| case | 原首次probe | 后续四次独立probe | 原verdict |
|---|---|---|---|
| retained-second-handle | 第二handle仍持有，可打开 | 释放后均Open error2、已消失 | 不适用 |
| original-watchdog | creator关闭后error2、已消失 | 均error2、已消失 | passed |

原watchdog仍为3秒：期限时Job5/6/0，原终止后root99、唯一readiness、stderr空、Job0/6/0、creator CloseDesktop成功。两次GO前严格核对retained root活性、PID/creation/image及精确Job；退出后同handle/PID/creation不变，但两次补充image query返回31，当前image明确unknown，引用既有预GO证明，不伪称查询成功。其余补充observer错误为0。

目标offset为0/50/250/1000ms；两个case的实际值均为0/47/250/1000ms。事后只读clock metadata为GetTickCount64、分辨率0.015625s；**目标不保证精确调度或最小等待**。原实际值全部保留，不补采、不覆盖首次判据，不作释放延迟的性能验收。

所有owned creator/control/probe/Job/duplicate-process handle均一次成功Close；borrowed thread desktop不关闭。外层执行30秒＋共享清理5秒：root自然exit0、无timeout/外层Terminate、Job0/18/0，根句柄关闭。原watchdog由其原协议主动终止，不能称它自然退出。白名单投影见 [validation.json](validation.json)。

## 保留的失败与未知

旧collector `6e67be5` 已执行过一批：正对照完成；watchdog退出后补充image-query31打断原record.update求值，旧sample缺exit/stdout/job_after。旧原件保持，不补造字段、不拿它解释CI。修订只允许同retained证明下退出后image31为gap；其他observer错误延迟到原sample落盘和复制后再使诊断失败。真实AST反例为旧7项中2项RED，新普通与`-O`各19/19，纯mock和本次原生观察分列。

发布只离线投影，47个已有文件后验零变化，未重跑。公开JSON只含合成协议、布尔、计数、相对offset和哈希；desktop/window/thread/handle/PID、绝对路径、原始计划和完整日志私有保存。同步Win32/文件调用没有内部强制中断，硬回收来自外层自有Job；CLI0/diagnostic_complete仅表示固定观察完成。

仍未知：CI首次Open成功的持有者、释放时序及未观察的外部/无窗口引用。本地first absent不证明CI修复；若继续在CI调查，应另行冻结一次计划，保留原期限和原首次判据。
