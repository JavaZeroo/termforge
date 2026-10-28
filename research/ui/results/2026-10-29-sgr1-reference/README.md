# 原版 1049 / SGR1 固定样本：采集完整，控制不可分

[#146](https://github.com/JavaZeroo/termforge/issues/146) 的唯一实际运行取得五阶段、五次独立 QWidget grab。**每帧的 SGR0 与 SGR1 双 H/space 控制完整像素签名均不可分，进入、重复进入和返回的比较结果都保持 unknown。** 这不能证明原版忽略 SGR1、不支持粗体，或 1049 保留/重置/恢复了内部字重。

## 身份与实际执行

- 实际执行 HEAD：`a115bc82119dcf1456e8bf9a7c0e15bee3d77174`；工具实现：`8d44041cefbf566b5fb1eb7433bdfa0231235ef1`。8 个生产源、5 个离线审计源与两提交逐一字节相同。[执行前材料](../2026-10-29-sgr1-proposal/README.md)保留其当时的零次运行状态和测试失败/修正身份。
- 固定官方 WindTerm 2.7.0 x64 EXE SHA256：`7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0`；4047 文件 canonical manifest：`e459adf2f12adb6a1b7f55e66a9da1c22794185e66e848057d21e0c60d669203`。离线重新枚举并核对源包和自有副本的全部 4047 文件、四模块完整 SHA 与实际模块路径守卫。
- 已消费的私有 true 计划 SHA：`d0286fade7bf18f3dbea5e44ed91b8c5e1a5f595b0900978abcc758e9b06af45`；[公开计划](plan.json)删除私有路径，分别记录准备时零次与完成后一次。原 false、true、attempt、源和原件均保留，不再执行。
- REA Evidence：`ev_c6e7637ba0e47033ec012529a35c909bb21a805f98b6743e809cdb28958721d4`。root 监督 CLI 退出 0；provider 无脚本错误、截断或 cleanup error。原始 evidence 216159B，路径、PID、profile、资源及像素数组不公开。
- 自有非输入 station/desktop、合成 loopback Telnet 和 OwnedJob。记录的最终 Job 为 `active=0,total=3,terminated=0`，profile 检查仅一个合成会话，stderr 为空。启动器主动结束原版，不把 Job0 或 terminated0 当作自然 GUI 退出。

## 五阶段观察

NAWS 完整事件为 `91×24 → 91×25`，第二事件后冻结，之后无新尺寸事件。五帧均为 888×451、DPR1；28 对 H/space ROI 共 56 数组、8064 个 QRgb，单个 ROI 为 8×18。完整有序 H/space 签名在本次 28 对中只有一个值；不是仅比较直方图或主墨色。

| 阶段 | 精确源条件 | 实际 DSR | 实际缓冲区 | 同帧控制 / 未知样本结论 |
|---|---|---|---|---|
| 0 | 主屏 SGR0/1 控制，结束为 SGR1 | (2,16) | 主屏 | 0/1 控制不可分 |
| 1 | `1049h` 后先写 E0，再写控制 | (1,21) | 备用屏 | 控制不可分，E0 无唯一分类 |
| 2 | 备用屏样本与控制，结束为 SGR1 | (4,23) | 备用屏 | 控制不可分 |
| 3 | 重复 `1049h` 后先写 R0，再写控制并追加不写字的 SGR0 | (1,21) | 备用屏 | 控制不可分，R0 无唯一分类 |
| 4 | `1049l` 后先写 M1，再写控制 | (2,37) | 主屏 | 控制不可分，M1 无唯一分类 |

SGR1/SGR0 的转换前态来自已发送源声明，并非读取了内部 style。所有 marker 与实际文本、位置、矩形、DSR 和活动缓冲区分别绑定；同帧两组控制各自重复一致。正常与 SGR1 控制本身相同，所以不能利用目标匹配推导 reset/inheritance/restore。

本批只核对固定发行物、合成会话、精确请求和栅格输出；没有读取实际终端 glyph 字体、fallback、字重或相关 profile 偏好的有效值。不能把同像素解释成某字体请求无效、粗体偏好关闭或原版属性未实现。没有换字符、颜色、字体或 ROI，也没有补抓或重跑。

## 离线审核与发布

[validation.json](validation.json)原样复制普通导出，与 `python -O` 导出逐字节相同，SHA256 为 `da5c2b2615339d057a71a94b3bd50580477f9d5e2e70a670ee1a8dde177b42d3`。[独立 stdlib 审核](audit.json)未导入探针、分析器或导出器作为 oracle；另外重算精确输入、DSR/NAWS、完整模板与实际 CRLF 注入文件、8+5 源、4047 文件、模块、五次 grab、28 对位置/ROI、完整像素签名、消息/原件一致性及回收记录。31 份私有记录保留大小和 SHA，源文件另归档于 local。

结果整理前合并了 main `4d7bd8a7c822135b1fdee5935262af3205ee565f`，合并提交 `848d479af194ad8cf1f6238dc4e33774ade7da43`；这属于发布上下文，不是再次执行的源码身份。合并后没有重跑原版、REA、Node、peer 或 guest，也没有重跑 15 项或完整产品矩阵。

本项只完成这次有界调查与不可判定结果的可重算发布。完整非颜色属性、内部 pen、全网格、其他模式、本地 Shell 运输、实际呈现和性能继续未知；#123/#126/#27 与 R2 不因此通过。REA capture45 秒、CLI wait60 秒不含前置启动和后续清理，不构成整程期限或性能结果。
