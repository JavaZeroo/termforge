# 候选 C++ 终端适配器

`CandidateTerminal` 是 #27 的独立拥有值适配切片，不是默认产品核心。它把固定 `0.26.1-dev+termforge.2` 的 `Term/Processor` 接到现有 `Screen`/`Line`，保留自研核心用于回退和差分。

## 构建与验证

默认 CMake 不需要 Rust。显式启用适配器：

```powershell
cargo build --manifest-path experiments/candidates/alacritty/Cargo.toml --release --locked --lib
cmake -S . -B build/candidate-adapter -A x64 -DTERMFORGE_BUILD_CANDIDATE_ADAPTER=ON
cmake --build build/candidate-adapter --config Release --target candidate_core_test
ctest --test-dir build/candidate-adapter -C Release -R candidate_core --output-on-failure
```

Linux 使用相应 CMake 配置。候选 workflow 在 Windows/Ubuntu 都执行该目标。

## 边界

- Rust `staticlib` 只暴露 opaque handle、C 结构体和调用方缓冲区；`CandidateTerminal` 负责析构、错误转换和值拷贝，不暴露 Rust 引用、异常或并发可变网格。
- `feed` 以 1 MiB 为单次上限；`resize` 有独立尺寸检查；`finish` 保留当前候选 trace 的 no-op 语义，不宣称刷新不完整序列。
- 每次 feed 同步网格并排干事件。`PtyWrite` 等事件返回拥有字节，后续产品必须接入有界 `InputQueue`，不能直接写 ConPTY。
- C++ 同步历史/可见行、稳定 ID、逐行版本、样式、光标、软硬换行、next_id/wrap_columns 和实际淘汰数。普通 reset 保留 ID，滚动复用行更新 ID；初始 Storage 方向也显式检查。
- 不走 JSON。Rust/C++ 文本由调用方拥有，未做增量 FFI 或性能验收；候选 damage 的产品化仍属后续。
- 空格/空单元格仍受上游 `is_empty` 限制，显式尾空格不能由该投影裁决；真彩色等非 16 色样式仍为 `-2`，需要后续产品样式扩展。
- `screen.resize` 与候选 `Term::resize` 同步尺寸，但 reflow 合同归 #29；独立测试不替代默认物理行策略裁决。

## 当前验证

`candidate_core_test` 覆盖 whole/逐字节、Unicode/组合标记、备用屏恢复、真实光标回复、事件排干、稳定 ID/版本/淘汰、选择/搜索/HistoryViewport、软硬换行、滚动区、ED1/ED2、resize 和错误参数。证据见 [validation](results/2026-10-25-candidate-adapter/validation.json)。

未完成：实际 InputQueue、默认 ShellWindow 整合、真实 ConPTY 备用屏工作流、完整 R1 回归、产品性能与呈现验收。#27 不因此提前关闭。
