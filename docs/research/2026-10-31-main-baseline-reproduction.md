# 在干净本机复现 main 的 CTest 基线

固定一套从 `main` 出发的最小步骤与实测结果，用于判定"某个可靠性失败是否可复现"，避免把环境缺失误判为产品缺陷。

## 目的

`#38`、`#144`、`#40` 等可靠性项均要求"原失败保留、不重跑取绿"。判断一次失败是不是产品缺陷之前，需要先确认本机能跑出与 CI 同形的基线；否则缺依赖、缺构建产物、缺 `.git` 都会被当成失败。

## 前置

- Windows 11 x64、Visual Studio 2022、CMake（能自行定位 VS 实例）
- Python 3.12
- 仓库声明的两份依赖，缺一即产生非产品失败：

```sh
python -m pip install -r benchmarks/requirements.txt              # psutil==7.2.2
python -m pip install -r experiments/differential/requirements.txt # pyte==0.8.2、wcwidth==0.2.13（带 hash）
```

## 步骤

```sh
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel 8
ctest --test-dir build -C Release --timeout 300 -j 4
```

CTest 通过 `TERMFORGE_BIN_DIR` 或 `build/bin/Release` 定位测试二进制；本机实测分别在 9 秒、80 秒、175 秒量级完成。

## 实测结果（2026-10-31，main `e9d8e6a`）

| 条件 | 结果 |
|---|---|
| 依赖齐全、完整构建、`-j 4` | **65/65 通过** |
| 缺 `pyte`（未装 `experiments/differential/requirements.txt`） | `differential`、`workflow_sessions` 以 `ModuleNotFoundError: No module named 'pyte'` 失败 |
| 无构建产物（仅有源码） | 17 项以 `Missing <binary>; Build with CMake and set TERMFORGE_BIN_DIR` 失败 |
| 无 `.git`（例如 `git archive` 导出后） | `windterm_alternate_export`、`node_contract_plan` 等以 `fatal: not a git repository` 失败 |

后三类都能在装齐依赖或补上构建产物后转为通过，**不是产品缺陷**。

## 并行下的偶发失败

全量 `-j 4` 时，`conpty`（CTest #57）的 `test_large_ordered_payload` 偶发 `returncode=1`：

```
FAIL: test_large_ordered_payload
AssertionError: {'mode': 'large', 'status': 'failed', 'timed_out': False, 'returncode': 1, ...}
```

复现矩阵：串行 `--repeat until-fail:3` 3/3 通过；三个 conpty 测试 `-j 3` 两轮通过；全量 `-j 4` 两轮中失败一次。

`experiments/conpty/probe.cpp` 在会话关闭后用固定 1 秒窗口等待句柄/线程数回落，非 startup 轮次还要求 `steady_resources`；重负载下该窗口可能不足。这是**候选原因**，不是已证根因，观测细节与建议见 `#38` 的对应评论。

## 判读口径

1. 先确认两份 requirements 已安装，再看失败
2. 单个测试失败时先串行复跑，区分"并行争用"与"确定性失败"
3. 报失败必须给出：源码/二进制身份、构建配置、依赖状态、串行与并行两种结果
4. 一次未复现不构成修复；重跑取绿不能回填旧失败原因

## 边界

本页只固定本机基线；hosted CI 的宿主、并发与权限差异以各 workflow 为准，不能用本机结果替代 hosted 证据。
