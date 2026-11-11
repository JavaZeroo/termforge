# B/CD 校准重试：预写动作预算与停止条件（2026-11-12）

## 授权与基线

- 授权依据：issue #29 comment 6741368867 的修订离线计划 + 用户最新指示：允许**一次**新增校准执行（新增额度 1，首失败停止）；校准成功后才执行原尚未使用的唯一 v2 正式取证授权（仍有效，不需重新请求）。
- 基线：main `abc71fe5a3577c5cfb54fe4869506c24fb7288e0`（#332 合并）；工作分支 `agent/bcd-k3/record-hidden-retention-2e7373117ea0`。
- 修复提交：`8272d21`（校准 peer 双轴合同修复 + 离线测试）、`3a3009c`（v2 夹具同一守卫修复）。
- 新源哈希：`resize_bcd_calibration.py` = `e3e2d261ca9f08237852a928acc6418a54665f1991de873916149288f7a0735f`；`resize_bcd_calibration.js` = `296f02e4f384f8a722e62b7f8b39f3f70918d5bbc4ba95d35ff8b1c82f946efe`（未变）；v2 夹具 `resize_bcd_address_reference.py` = `71a5c17378e55e54954ae5632f1684ed0bdfb0656cbb3a1c2d755d10b4699e92`。
- 旧失败/plan/attempt/raw 全部保留：v1 plan `7718a4b8…`、marker `20fd56d7…`、校准 plan `954636ee…`（2026-11-09）不重放、不改写。
- 严格 SSH 主机身份校验；不绕过 known_hosts，不重启主机/服务，不改电源，不触碰个人桌面/剪贴板，不改设置。

## 阶段与动作预算

1. **身份/可达（只读，最多 8 条远程命令）**：SSH 登录、hostname、VM 状态、guest 会话标识。任一不符即停止。
2. **环境身份（只读，最多 10 条）**：固定 WindTerm 2.7.0 EXE SHA-256（`7b10531d…`）、旧 plan/marker 哈希未变、staging 源副本 `r29-bcd-calibration-src-0` 存在且哈希未变、显示当前 1024×768。不得修改或重录旧文件。
3. **部署修复源**：guest 内新建 staging 副本 `r29-bcd-calibration-src-1`（复制 src-0），仅覆盖 `research/ui/resize_bcd_calibration.py` 为修复版，并更新 `reference-source.json` 清单中该文件哈希（其余文件哈希不变）；上传后逐文件复核哈希。src-0 不动。
4. **显示调整（一次，需记录并恢复）**：1024×768 → 1600×1200（同 2026-11-09 路径）；记录 change_result 与 bounds。
5. **冻结新计划**：`python -m research.ui.resize_bcd_calibration --prepare --authorize-execution --case calibrate` 生成新 plan（新源哈希、`execution_authorized=true`），记录 plan SHA-256；`run_plan` 以 `.attempted` 守卫只允许一次。
6. **一次校准执行（≤300s）**：4-phase 固定窗口/网格；phase1/3 必须有 armed 后新目标 NAWS；phase2 同尺寸 checkpoint；任何窗口/DSR/phase/frozen 不符即停；不因失败重跑/换窗/改预期。
7. **恢复显示**：1600×1200 → 1024×768，记录 restore_result 与 bounds。
8. **仅在校准 4/4 成功后**：冻结 v2 新源/计划/身份/attempt（窗口取自已校准几何），执行唯一 v2 正式取证；失败即停。

## 停止条件

- host key 不匹配、SSH 登录失败、非专用取证 VM/guest、WindTerm 2.7.0/执行源不符。
- 旧 plan/attempt/raw 已被改写（v2 尚未执行时只记录，不重复）。
- 环境不可用（主机不可达/VM 不在运行/guest 不响应）：报告具体阻塞，不重启主机/服务或改设置。
- 校准任何 phase/字段不符、peer 未 clean、`.attempted` 已存在。
- 同一故障额度用尽；不再重试/换名/滚动增加预算。

## 记录

每次动作记录时间、命令、只读结果/哈希、是否改变环境；私有 raw 留本地（专用 VM），公开材料仅身份与脱敏结果；显示临时调整必须记录并恢复。
