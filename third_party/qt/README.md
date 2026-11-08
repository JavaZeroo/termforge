# 本地 Qt 构建依赖

当前 C++/Qt 迁移入口固定使用 Qt **6.10.3**、Windows desktop **MSVC 2022 x64** 的 `qtbase` SDK，应用动态链接 Core、Gui、Widgets；测试入口另使用 Test。Qt Widgets 的当前路线见 [ADR-0008](../../docs/adr/0008-cpp-core-qt-widgets.md)。本目录只保存来源说明和 [manifest.json](manifest.json)，不提交 Qt 归档、DLL、插件或本地 SDK。

## 来源与身份

manifest 固定了 Qt 官方 Updates.xml 和原始 qtbase 7z 的 URL、大小与哈希。官方包名为 `qt.qt6.6103.win64_msvc2022_64`，归档大小 **42,748,757 B**，SHA-256 为 `4db84dee7fe3c558f242bef0a88852613af76580dc6d2b24596479f47004dad7`；本次整理已重新核对本地原始归档及 Updates.xml。

对应源码为 [qtbase 6.10.3 源码归档](https://download.qt.io/archive/qt/6.10/6.10.3/submodules/qtbase-everywhere-src-6.10.3.tar.xz)及 Qt 官方仓库的 `v6.10.3`。本次为提取随包许可文本下载并测量了该源码归档：**50,509,856 B**，SHA-256 `383dc907816338f0cba72088a524c07458dfc69ce684ca9132fcc4fe91c24b0b`；归档本身不提交、不随产品分发。产品的完整对应源码交付条件仍属于分发审计，不因本记录完成。

最初本地准备使用 aqtinstall 3.3.0，会修补 qmake 和 `.prl` 的安装前缀。下面的自动复建直接解压同一官方归档，仅写相对的 `bin/qt.conf`（`Prefix=..`），保留原始 qmake 与 `.prl` 字节。**原始归档、两种准备方式得到的 SDK、最终部署目录是不同身份**，不能用归档哈希冒充 SDK 或产品目录的完整哈希；具体构建结果按各次证据绑定实际输入和输出。

## 本地构建

在 Windows x64、Python 3.12、CMake 3.31 和 Visual Studio 2022 C++ 工具环境中，无须全局安装 Qt 或修改 PATH：

```powershell
python -m tools.prepare_qt_build
```

命令读取本目录、ConPTY 和 [Microsoft CRT](../msvc_runtime/README.md) 的 manifest，下载固定官方输入至忽略的 `local/qt-downloads/`，核对每项大小及 SHA-256，再用 CMake 的归档读取器生成全新的 `local/qt-build-deps/qt/` SDK、`local/qt-build-deps/conpty/` 文件对和 `local/qt-build-deps/crt/`。它核对 Qt 构建／部署工具、四个模块、两个平台插件、SDK SBOM 和实际 qmake 版本；ConPTY 只提取命名成员并再次核对双文件；CRT 安装包仅作归档读取，固定十个 AMD64 DLL，并保留原许可和 NOTICE。没有全局注册、运行安装程序或环境修改。

`--cache <directory>` 可复用已取得的原始输入；`--offline` 要求 Qt、ConPTY、CRT 固定归档及 CRT 原许可文档四项均已缓存，不允许下载。缓存身份不符直接失败，不覆盖坏文件；`--out <directory>` 必须尚不存在，避免覆盖或混用已有 SDK。准备失败不发布半成依赖；原始下载和临时提取目录不会进入源码。命令输出及 `local/qt-build-preparation.json` 仅包含公开版本、内容哈希、文件数和固定错误码，不含本地路径或原始日志。SDK 的树哈希绑定写入相对配置后的所有文件，不等于归档哈希。

使用已核对的 Qt SDK 和[固定微软 ConPTY 文件对](../conpty/README.md)，只设置本次构建参数，无须修改全局 PATH：

```powershell
cmake -S . -B build/qt-cpp -G "Visual Studio 17 2022" -A x64 `
  -DTERMFORGE_BUILD_QT_SHELL=ON `
  -DTERMFORGE_BUILD_CANDIDATE_ADAPTER=OFF `
  -DBUILD_TESTING=ON `
  -DQt6_DIR="$PWD/local/qt-build-deps/qt/lib/cmake/Qt6" `
  -DTERMFORGE_CONPTY_BACKEND_DIR="$PWD/local/qt-build-deps/conpty" `
  -DTERMFORGE_MSVC_RUNTIME_DIR="$PWD/local/qt-build-deps/crt"
cmake --build build/qt-cpp --config Release --target termforge_qt_shell qt_cpp_shell_smoke qt_cpp_resize_smoke
python tools/msvc_runtime.py manifest --directory build/qt-cpp/qt-bin/Release --executable termforge_qt_shell.exe
python tools/msvc_runtime.py manifest --directory build/qt-cpp/qt-smoke-bin/Release --executable qt_cpp_shell_smoke.exe
python tools/msvc_runtime.py manifest --directory build/qt-cpp/qt-resize-bin/Release --executable qt_cpp_resize_smoke.exe
ctest --test-dir build/qt-cpp -C Release -R '^qt_cpp_(shell|resize)$' --no-tests=error --verbose
```

`Qt6_DIR` 应包含该版本的 `Qt6Config.cmake`；正式入口缺少或不匹配固定 ConPTY/CRT 时明确失败，不回退系统版本。CRT 按实际 VC Tools 目录版本检查兼容上限。最小 SDK 未安装翻译，当前构建使用 `windeployqt --no-translations --no-compiler-runtime` 部署 Qt，另显式复制固定 app-local CRT；Qt 默认 compiler-runtime 部署会携带待执行的安装包，不能代替这批便携目录的 DLL。完整运行时清单绑定最终目录，不代表所有许可和产品打包已验收。

专用 [Qt C++ CI](../../.github/workflows/qt-cpp.yml) 在 `windows-2025` 中执行同一准备、产品及两个显式 Qt smoke target 构建，以及 `qt_cpp_shell` / `qt_cpp_resize` 的 offscreen / OwnedJob 检查。它不继承默认／系统后端或 Alacritty 作业的通过结果；runner 验证不扩大 Windows 11 x64 或其他平台的产品支持声明。自动产物仅保留路径无关的准备摘要和三份完整运行时清单，不上传 SDK、部署目录或原始日志；runner 的 offscreen 成功不替代隔离 Windows 的原生加载与交互验证。

CI 的 `windows-2025` 已实际解析到 `windows-2025-vs2026`，该映像没有 VS 2022 实例；因此工作流显式使用 [CMake 的 VS 2026 generator](https://cmake.org/cmake/help/latest/generator/Visual%20Studio%2018%202026.html)，要求 CMake 4.2 或更新。Qt SDK 仍是固定的 MSVC 2022 x64 包，不把 SDK 名称改称 VS 2026。[微软的二进制兼容说明](https://learn.microsoft.com/en-us/cpp/porting/binary-compat-2015-2017?view=msvc-170)允许较新链接器消费较早工具链构建的动态库，但实际兼容性仍由这次独立构建和会话检查证明；本地 VS 2022 验证与 hosted VS 2026 验证分别记录。

## 产品 staging 与部署边界

`windeployqt` 调用显式带 `--no-system-d3d-compiler --no-system-dxc-compiler`：系统 D3D 编译器与微软 DXC OOB 重分发文件既不在本仓库固定依赖中，也不是当前 Widgets 光栅入口静态链接的依赖，因此不从开发机 Windows SDK/System32 借用。`Qt6Core.dll` 静态导入的 `icuuc.dll` 由 Windows 11 System32 提供；构建目录中由 windeployqt 复制的副本只作为来源证据核对（必须与 System32 字节一致），staging 排除它而不随包分发。

[`packaging/qt-product/manifest.json`](../../packaging/qt-product/manifest.json) 冻结正式入口实际部署的 Qt 库/插件集合、OS 提供项、被排除的系统重分发文件与许可证据。`python -m tools.stage_qt_product stage` 只接受该清单、当前固定依赖准备摘要、固定 SDK 树哈希与逐文件字节都匹配的构建目录，输出路径无关的 `product-manifest.json`（含 source commit/tree、构建参数、入口、逐文件 SHA-256 和许可证据）；它会带入 [components.json](components.json) 冻结的部署组件清单、22 份规范许可文本、机器可读清单副本与 `QT-THIRD-PARTY-NOTICES.txt`。`verify` 子命令可重新核对已生成 staging 的文件集合与哈希。该 staging 是工程候选，不是 R2 包：干净 Windows 运行、单屏 96/144 DPI、对应源码/替换条件审查及最终包验收均未完成。

## 部署组件与许可清单

[components.json](components.json) 由 `python -m tools.build_qt_component_inventory build` 从**同一固定 qtbase 6.10.3 源码归档**与 SDK 自带 SPDX SBOM 生成；`check` 子命令会重新解压固定归档、重算并逐字节比较已提交清单与许可文本，CI 每次运行该检查。证据分层如下：

- **已部署组件（42 个）**：仅当 SBOM 包关系把第三方包连到实际部署的 Qt 模块/插件包时才列入；每个组件还须与源码归档中的 `qt_attribution.json` 条目（Id、版本、许可 id、许可文件）交叉核对。清单记录部署文件、上游来源、归属文件成员与 SHA-256、上游许可文件与版权声明。
- **未部署声明**：源码树中存在但 SBOM 无对应包、归属模块未部署的声明分别列入 `declared_not_deployed`；仅通过内部构建/平台包可达的构建工具声明（如 extra-cmake-modules、KWin CMake 模块）列入 `excluded_attributions`，并标注理由。源码树中存在不等于部署二进制包含。
- **字节身份限制（实测）**：固定归档内 SBOM 的 packaged-file 校验只含 SHA-1 且**与归档内 SDK 二进制不匹配**；因此 SBOM 仅作为模块/组件关系证据，部署文件字节身份由固定归档 SHA-256、`prepare_qt_build` SDK 树哈希和 staging 逐字节核对承担。该限制记录在清单 `sbom.binary_file_checksums` 中。
- **许可文本**：22 个被引用许可 id 的规范文本从同一源码归档 `LICENSES/<id>.txt` 提取并随仓库提交（[LICENSES/](LICENSES/)）；组件原始的许可/版权文件身份记录在清单中而不重复提交。Qt 模块采用动态链接 LGPL-3.0-only 选项，其余备选许可文本不随包分发。
- **状态**：`materials_prepared_user_review_pending`。对应源码交付、重链接/替换条件、法律与干净机器验证仍未完成；清单与 notice 是事实索引，不是再分发结论。

## 许可与分发边界

Qt 模块保留自己的许可，不由项目 MIT 许可覆盖。[Qt 6.10 许可说明](https://doc.qt.io/qt-6.10/licensing.html)与 SDK SBOM／头文件显示，当前 Core、Gui、Widgets、Test 的模块许可选项包含 LGPL-3.0-only，以及相应 GPL／商业许可选项；manifest 保留精确 SPDX 表达式及实际 SBOM 身份。当前应用计划采用**动态链接的 LGPLv3 路径**，这不是对完整产品分发条件已经满足的声明。仓库随附从固定 qtbase 6.10.3 源码归档 `LICENSES/LGPL-3.0-only.txt` 提取的许可原文（[LICENSES/LGPL-3.0-only.txt](LICENSES/LGPL-3.0-only.txt)），staging 会把该文本连同其归档成员身份与 SHA-256 一起带入产品目录。

Qt 内还包含使用其他许可的第三方代码；不能仅凭模块级许可表达式代替实际部署文件清单。部署组件清单与规范许可文本已按固定 SBOM/源码归档整理，但产品分发前仍须完成对应源码与替换条件的核对，以及干净机器运行验证；当前记录只支持本地构建依赖、部署文件与许可材料的来源和身份。staging 的许可索引将该状态标为 `component_inventory_prepared_user_review_pending`，不将其写成已完成的再分发结论。
