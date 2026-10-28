# 本地 Qt 构建依赖

当前 C++/Qt 迁移入口固定使用 Qt **6.10.3**、Windows desktop **MSVC 2022 x64** 的 `qtbase` SDK，应用动态链接 Core、Gui、Widgets；测试入口另使用 Test。Qt Widgets 的当前路线见 [ADR-0008](../../docs/adr/0008-cpp-core-qt-widgets.md)。本目录只保存来源说明和 [manifest.json](manifest.json)，不提交 Qt 归档、DLL、插件或本地 SDK。

## 来源与身份

manifest 固定了 Qt 官方 Updates.xml 和原始 qtbase 7z 的 URL、大小与哈希。官方包名为 `qt.qt6.6103.win64_msvc2022_64`，归档大小 **42,748,757 B**，SHA-256 为 `4db84dee7fe3c558f242bef0a88852613af76580dc6d2b24596479f47004dad7`；本次整理已重新核对本地原始归档及 Updates.xml。

对应源码为 [qtbase 6.10.3 源码归档](https://download.qt.io/archive/qt/6.10/6.10.3/submodules/qtbase-everywhere-src-6.10.3.tar.xz)及 Qt 官方仓库的 `v6.10.3`。这里只标识源码获取位置，没有把源码归档下载、哈希复核或完整对应源码交付算作已完成。

本地 aqtinstall 3.3.0 会修补 qmake 和 `.prl` 的安装前缀。**原始归档、修补后的 SDK 文件、最终部署目录是不同身份**，不能用归档哈希冒充 SDK 或产品目录的完整哈希；具体构建结果按各次证据绑定实际输入和输出。

## 本地构建

使用已核对的 Qt SDK 和[固定微软 ConPTY 文件对](../conpty/README.md)，只设置本次构建参数，无须修改全局 PATH：

```powershell
cmake -S . -B build/qt-cpp -G "Visual Studio 17 2022" -A x64 `
  -DTERMFORGE_BUILD_QT_SHELL=ON `
  -DTERMFORGE_BUILD_CANDIDATE_ADAPTER=OFF `
  -DQt6_DIR="<Qt SDK prefix>/lib/cmake/Qt6" `
  -DTERMFORGE_CONPTY_BACKEND_DIR="<absolute fixed ConPTY pair directory>"
cmake --build build/qt-cpp --config Release --target termforge_qt_shell
```

`Qt6_DIR` 应包含该版本的 `Qt6Config.cmake`；正式入口缺少或不匹配固定 ConPTY 时明确失败，不回退系统版本。最小 SDK 未安装翻译，当前构建使用 `windeployqt --no-translations --no-compiler-runtime`，只形成本地构建目录，尚不是可发布的独立产品包。

## 许可与分发边界

Qt 模块保留自己的许可，不由项目 MIT 许可覆盖。[Qt 6.10 许可说明](https://doc.qt.io/qt-6.10/licensing.html)与 SDK SBOM／头文件显示，当前 Core、Gui、Widgets、Test 的模块许可选项包含 LGPL-3.0-only，以及相应 GPL／商业许可选项；manifest 保留精确 SPDX 表达式及实际 SBOM 身份。当前应用计划采用**动态链接的 LGPLv3 路径**，这不是对完整产品分发条件已经满足的声明。

Qt 内还包含使用其他许可的第三方代码；不能仅凭模块级许可表达式代替实际部署文件清单。产品分发前仍须完成运行库／插件及第三方许可声明、对应源码与替换条件的核对，以及干净机器运行验证；当前记录只支持本地构建依赖的来源与身份。
