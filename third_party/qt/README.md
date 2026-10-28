# 本地 Qt 构建依赖

当前 C++/Qt 迁移入口固定使用 Qt **6.10.3**、Windows desktop **MSVC 2022 x64** 的 `qtbase` SDK，应用动态链接 Core、Gui、Widgets；测试入口另使用 Test。Qt Widgets 的当前路线见 [ADR-0008](../../docs/adr/0008-cpp-core-qt-widgets.md)。本目录只保存来源说明和 [manifest.json](manifest.json)，不提交 Qt 归档、DLL、插件或本地 SDK。

## 来源与身份

manifest 固定了 Qt 官方 Updates.xml 和原始 qtbase 7z 的 URL、大小与哈希。官方包名为 `qt.qt6.6103.win64_msvc2022_64`，归档大小 **42,748,757 B**，SHA-256 为 `4db84dee7fe3c558f242bef0a88852613af76580dc6d2b24596479f47004dad7`；本次整理已重新核对本地原始归档及 Updates.xml。

对应源码为 [qtbase 6.10.3 源码归档](https://download.qt.io/archive/qt/6.10/6.10.3/submodules/qtbase-everywhere-src-6.10.3.tar.xz)及 Qt 官方仓库的 `v6.10.3`。这里只标识源码获取位置，没有把源码归档下载、哈希复核或完整对应源码交付算作已完成。

最初本地准备使用 aqtinstall 3.3.0，会修补 qmake 和 `.prl` 的安装前缀。下面的自动复建直接解压同一官方归档，仅写相对的 `bin/qt.conf`（`Prefix=..`），保留原始 qmake 与 `.prl` 字节。**原始归档、两种准备方式得到的 SDK、最终部署目录是不同身份**，不能用归档哈希冒充 SDK 或产品目录的完整哈希；具体构建结果按各次证据绑定实际输入和输出。

## 本地构建

在 Windows x64、Python 3.12、CMake 3.31 和 Visual Studio 2022 C++ 工具环境中，无须全局安装 Qt 或修改 PATH：

```powershell
python -m tools.prepare_qt_build
```

命令读取本目录和 ConPTY 的 manifest，下载固定官方归档至忽略的 `local/qt-downloads/`，核对每个归档的大小及 SHA-256，再用 CMake 的归档读取器生成全新的 `local/qt-build-deps/qt/` SDK 与 `local/qt-build-deps/conpty/` 文件对。它核对 Qt 构建／部署工具、四个模块、两个平台插件、SDK SBOM 和实际 qmake 版本；ConPTY 只提取 manifest 命名成员并再次核对双文件。SDK 只用于构建，没有全局注册或环境修改。

`--cache <directory>` 可复用已经取得的原始归档；`--offline` 要求两个归档均已缓存，不允许下载。缓存身份不符直接失败，不覆盖坏文件；`--out <directory>` 必须尚不存在，避免覆盖或混用已有 SDK。准备失败不发布半成 SDK；原始下载和临时提取目录不会进入源码。命令输出及 `local/qt-build-preparation.json` 仅包含公开版本、内容哈希、文件数和固定错误码，不含本地路径或原始日志。SDK 的树哈希绑定写入相对配置后的所有文件，不等于归档哈希。

使用已核对的 Qt SDK 和[固定微软 ConPTY 文件对](../conpty/README.md)，只设置本次构建参数，无须修改全局 PATH：

```powershell
cmake -S . -B build/qt-cpp -G "Visual Studio 17 2022" -A x64 `
  -DTERMFORGE_BUILD_QT_SHELL=ON `
  -DTERMFORGE_BUILD_CANDIDATE_ADAPTER=OFF `
  -DBUILD_TESTING=ON `
  -DQt6_DIR="$PWD/local/qt-build-deps/qt/lib/cmake/Qt6" `
  -DTERMFORGE_CONPTY_BACKEND_DIR="$PWD/local/qt-build-deps/conpty"
cmake --build build/qt-cpp --config Release --target termforge_qt_shell qt_cpp_shell_smoke
ctest --test-dir build/qt-cpp -C Release -R '^qt_cpp_shell$' --no-tests=error --verbose
```

`Qt6_DIR` 应包含该版本的 `Qt6Config.cmake`；正式入口缺少或不匹配固定 ConPTY 时明确失败，不回退系统版本。最小 SDK 未安装翻译，当前构建使用 `windeployqt --no-translations --no-compiler-runtime`，只形成本地构建目录，尚不是可发布的独立产品包。

专用 [Qt C++ CI](../../.github/workflows/qt-cpp.yml) 在 `windows-2025` 中执行同一准备、两个显式 Qt target 构建，以及 `qt_cpp_shell` 的 offscreen / OwnedJob smoke。它不继承默认／系统后端或 Alacritty 作业的通过结果；runner 验证不扩大 Windows 11 x64 或其他平台的产品支持声明。自动产物仅保留路径无关的准备摘要，不上传 SDK、部署目录或原始日志；Qt 原生窗口、真实输入／DPI 和干净机器运行仍须另验。

CI 的 `windows-2025` 已实际解析到 `windows-2025-vs2026`，该映像没有 VS 2022 实例；因此工作流显式使用 [CMake 的 VS 2026 generator](https://cmake.org/cmake/help/latest/generator/Visual%20Studio%2018%202026.html)，要求 CMake 4.2 或更新。Qt SDK 仍是固定的 MSVC 2022 x64 包，不把 SDK 名称改称 VS 2026。[微软的二进制兼容说明](https://learn.microsoft.com/en-us/cpp/porting/binary-compat-2015-2017?view=msvc-170)允许较新链接器消费较早工具链构建的动态库，但实际兼容性仍由这次独立构建和会话检查证明；本地 VS 2022 验证与 hosted VS 2026 验证分别记录。

## 许可与分发边界

Qt 模块保留自己的许可，不由项目 MIT 许可覆盖。[Qt 6.10 许可说明](https://doc.qt.io/qt-6.10/licensing.html)与 SDK SBOM／头文件显示，当前 Core、Gui、Widgets、Test 的模块许可选项包含 LGPL-3.0-only，以及相应 GPL／商业许可选项；manifest 保留精确 SPDX 表达式及实际 SBOM 身份。当前应用计划采用**动态链接的 LGPLv3 路径**，这不是对完整产品分发条件已经满足的声明。

Qt 内还包含使用其他许可的第三方代码；不能仅凭模块级许可表达式代替实际部署文件清单。产品分发前仍须完成运行库／插件及第三方许可声明、对应源码与替换条件的核对，以及干净机器运行验证；当前记录只支持本地构建依赖的来源与身份。
