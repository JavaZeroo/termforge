# WindTerm 2.7.0 origin/wrap/margin：一次有限观察

[完整报告](../../../../docs/research/windterm-origin-wrap-reference-2026-10-29.md)解释确定输入、DSR、文档投影及未知边界；[执行前计划](../../../../docs/research/windterm-origin-wrap-plan.md)保留25阶段和原期限。

- 原版执行源 `72e661d964b771eae226e3bc301ed213c5e55ce8`，唯一正式计划SHA `0bb60918f4236c36349d9b87f2cc4c0c71c5147a89a12ddeff1db7b8d6959dc6`；原版运行一次。
- 离线审计/出口源 `fb1a40a2b6efea4ae5cf528607cd1d853e6ecd4e`，整合main7e896cb后8执行源字节不变。没有重新运行原版或Node。
- [validation.json](validation.json)：25阶段/25个bare DSR/25份文档快照；334B输入＋100B请求，157B回复；完整NAWS91×24→91×25，第二事件冻结91×25。普通/`-O`出口逐字节相同，41,269B，SHA256 `9774e25bab9f5f751368e19f915e5d1ec8f4332be606070f15b5ffb33ce5b675`。
- [publication-audit.json](publication-audit.json)：4,047分发文件、8源码、43原件和25投影独立复核；相关纯测试普通/`-O`各21项通过。旧false三份均无自身attempt；初版out已由正式trueplan创建，别名明确保留。

Job0/3/0、stderr0及合成单会话复核通过；启动器主动终止原版，不能称自然GUI退出。文档不是active grid，caretColumn不是DSR列。原版完整grid/样式、每屏origin位、返回后margin下界、其他尺寸/分块、固定ConPTY、实际呈现及性能均未验收。

原43/41/42/45/60秒局部期限不构成整程硬上限，`whole_lifecycle_bound_seconds=null`。本记录不实现产品模式，也不关闭#27/#41/#56/R2；实际专有副本、原始Evidence/profile和所有私有标识只留local。

```powershell
python -B -X utf8 -m unittest research.ui.test_origin_wrap_reference research.ui.test_export_origin_wrap_reference -v
python -B -O -X utf8 -m unittest research.ui.test_origin_wrap_reference research.ui.test_export_origin_wrap_reference -v
# 对已有私有原件的只读审计导出；不启动原版，输出必须是新路径。
python -B -X utf8 -m research.ui.export_origin_wrap_reference --directory local/origin-wrap-run-0 --plan local/origin-wrap-preflight/authorized-20261029.json --destination local/new-origin-wrap-export.json
```
