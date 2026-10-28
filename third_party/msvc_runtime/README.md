# 固定的 Microsoft Visual C++ Runtime

Qt/C++ Windows x64 入口随应用目录部署固定的 **14.51.36247.0** 桌面 CRT。它不安装到 System32，不修改宿主的运行库，也不在用户启动时下载或回退系统 CRT。本目录仅提交来源、原始内容哈希、提取协议和自有 NOTICE；不提交微软安装包、DLL 或许可原件。

## 官方来源与许可

[manifest.json](manifest.json) 固定微软官方 `VC_redist.x64.exe` 的具体 download.visualstudio.microsoft.com 地址、大小和 SHA-256，不把会变化的 aka.ms 地址当作版本身份。取得的安装包 Authenticode 状态为 Valid，签名者 Microsoft Corporation；所选 DLL 为 AMD64、文件版本 14.51.36247.0，微软签名有效。准备工具通过固定包的完整哈希绑定这批已核对字节，不把本机动态证书验证当作离线准备的额外网络依赖。

原始许可从 [Microsoft Visual C++ v14 Runtime 许可页面](https://aka.ms/VCRedistLicense)所链接的官方 DOCX 获取，按大小和 SHA-256 核对后原样复制为 `LICENSE-MICROSOFT-VC-REDIST.docx`，同时附带 [NOTICE.txt](NOTICE.txt)。微软文件保留其原许可，不归 TermForge 的 MIT 许可所有。[微软分发说明](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files?view=msvc-170)及 [Visual Studio 2022 可分发代码清单](https://learn.microsoft.com/en-us/visualstudio/releases/2022/redistribution)规定了许可用户及可分发文件范围；最终分发者仍须符合自己的 Visual Studio 许可条件。此项依赖准备不是完整产品分发条件的验收。

## 无安装提取与固定文件集

`python -m tools.prepare_qt_build` 在准备 Qt 和 ConPTY 的同时准备 `local/qt-build-deps/crt/`。已有 SDK 时可使用：

```powershell
python -m tools.prepare_qt_build --crt-only --out local/crt-dependencies
```

`--offline` 仅接受缓存中身份正确的固定安装包及许可文档；缺失或篡改明确失败，不覆盖坏缓存。输出目录必须不存在。工具按 manifest 验证安装包、附加 CAB 的固定偏移/长度/哈希、内部 `a4` CAB、每个指定成员的大小/哈希及 AMD64 PE 标识，然后原样提取。使用已有 CMake/libarchive 的 CAB 读取器，不执行 EXE/MSI，也不要求新增 7-Zip 或安装工具。

固定文件集为 `concrt140.dll`、`msvcp140.dll`、`msvcp140_1.dll`、`msvcp140_2.dll`、`msvcp140_atomic_wait.dll`、`msvcp140_codecvt_ids.dll`、`vccorlib140.dll`、`vcruntime140.dll`、`vcruntime140_1.dll`、`vcruntime140_threads.dll`。同一 CAB 中的 OpenMP `vcamp140.dll` 和 `vcomp140.dll` 不部署；不携带 MFC、debug、ARM64 或 Windows 自带 UCRT。

## 构建与完整运行时身份

Qt 入口要求绝对路径 `-DTERMFORGE_MSVC_RUNTIME_DIR=<prepared>/crt`。配置及每次目标构建均重新检查固定的十个 DLL、原许可和 NOTICE，任何缺失/篡改均失败；目标无须重新链接也会修复输出目录中的 CRT。原 Qt 和应用文件不被替换，仍使用 `windeployqt --no-compiler-runtime` 部署 Qt。

[微软的兼容合同](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170)要求 runtime 至少与构建工具同新。CMake 使用实际选中的 VC Tools 目录版本（例如本机 `14.42.34433`、已观察的 hosted `14.51.36231`）与固定上限 `14.51.36247` 比较；无法识别或工具集更新时失败。编译器自己的 `19.x` 版本不能转换推测成 `14.x`。Qt 另保持已固定的官方 MSVC 2022 x64 二进制来源，其兼容性由独立构建和运行证据验证。

构建成功后，为两个入口各生成完整运行时清单：

```powershell
python tools/msvc_runtime.py manifest --directory build/qt-cpp/qt-bin/Release --executable termforge_qt_shell.exe
python tools/msvc_runtime.py manifest --directory build/qt-cpp/qt-smoke-bin/Release --executable qt_cpp_shell_smoke.exe
```

`runtime-manifest.json` 记录目录内全部文件的相对路径、大小和 SHA-256，以及固定 CRT manifest 身份；不包含本地绝对路径或凭据。清单排除自身，必须在最终构建/部署完成后生成，后续改动须重新生成；它不替代对 EXE 源码、构建配置或其他第三方许可的记录。单文件部署使用原子替换，整目录不承诺事务性；未成功完成部署的目录不得作为包使用。

app-local 方式不会通过系统的运行库安装自动获得更新，固定版本的升级、重新验证与重新发包由项目维护。当前只闭合这批固定 Qt/C++ 入口的运行库依赖，不表示正式发布包、完整 R2、跨 DPI 或物理呈现验收。
