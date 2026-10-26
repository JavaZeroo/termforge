# WindTerm 2.7.0 平台、架构与发行边界

> 当前产品优先级由 [ADR-0006](../docs/adr/0006-confirmed-project-direction.md)确认：首个完整版本验收 Windows 11 x64，最终覆盖 Windows/Linux/macOS；其他系统版本、最低版本和架构后续单独明确。下表的官方声明及历史调查不改写，也不自动成为 TermForge 首个版本的支持承诺。平台可行性可提前调查，Linux/macOS 完整客户端验收在 R5 的 Windows 出口之后完成。

调查日期：2026-10-24。本文是 [#23](https://github.com/JavaZeroo/termforge/issues/23) 的单一范围台账，供 R2 核心路线（#26）和产品架构（#34）判断候选支持缺口；不进行核心构建、功能实现或平台验收。全量目标仍归 [#19](https://github.com/JavaZeroo/termforge/issues/19)，TermForge 当前能力与限制以 [FEATURES](../docs/FEATURES.md) 为准。

## 固定对象与证据口径

- 官方 `2.7.0` tag 经 [tag ref API](https://api.github.com/repos/kingToolbox/WindTerm/git/ref/tags/2.7.0) 核对，指向 commit [`a8336c1d1e3dd1a981500dae8ef9eb42066de95f`](https://github.com/kingToolbox/WindTerm/commit/a8336c1d1e3dd1a981500dae8ef9eb42066de95f)。源码链接均固定到该 commit，不使用会漂移的 `master`/`main`。
- 目标事实只采用[固定 commit README](https://github.com/kingToolbox/WindTerm/blob/a8336c1d1e3dd1a981500dae8ef9eb42066de95f/README.md)、同一 commit 的公开源码/配置、[2.7.0 release](https://github.com/kingToolbox/WindTerm/releases/tag/2.7.0) 和 [release API / 资产元数据](https://api.github.com/repos/kingToolbox/WindTerm/releases/tags/2.7.0)。未用二手文章补全范围。
- “官方声明”是作者列出的支持范围；“发行物事实”是 release 实际资产名称、格式和元数据；“源码静态事实”只描述固定公开代码。三者都不是实际运行通过。
- 本次没有启动任何 WindTerm 2.7.0 发行物，因此没有新增运行观察。既有 Windows x64 运行基线继续使用 `PLAN.md` 固定的 EXE SHA-256 `7b10531dccc7cd614ae559f36f1ef518e0c96335637431b4646354823b2804d0`；它是包内 EXE 身份，不是 ZIP 的 SHA-256，也不能证明其他平台或架构。
- “未列出/未知”不等于已证明不支持；反过来，公开源码存在某平台条件编译也不等于官方客户端在该平台通过验收。范围若需改变，必须新增带日期的一手证据和明确裁决，不能静默缩窄。

## 冻结结论

1. 官方声明和实际二进制发行覆盖 **Windows、macOS、Linux**；2.7.0 当前资产只覆盖 **Windows x86_32、Windows x86_64、macOS x86_64、Linux x86_64**。没有官方 ARM 资产，ARM 及其他未列出架构保持未知。
2. 官方列出的最低边界是：两个 Windows 架构均从 **Windows 7** 起；macOS x86_64 从 **macOS 10.13** 起；Linux x86_64 为 **Generic Linux，glibc 2.17 及以后**。这些是 release 声明，不是本次运行验证，也不是完整的发行版、内核、Service Pack 或桌面环境保证。
3. 实际发行形式为 Windows/Linux **portable ZIP**、macOS **portable DMG**；macOS 安装说明是打开 DMG 并把 `WindTerm.app` 拖入 `Applications`。release 另给 Scoop/Chocolatey 命令，但它们不是本次四个官方上传资产，具体包身份和系统边界未知。
4. release 的 Linux 命令提到 `.tar.gz`，实际资产却是 `.zip`；Checksums 提到 macOS ZIP，实际资产却是 DMG。两处均保留为官方材料内部差异，不虚构缺失资产，也不把 macOS ZIP 校验值用于 DMG。
5. 固定 commit 没有完整 WindTerm 应用的顶层构建/打包工程；公开 `src/libssh` 的 CMake/CPack 配置属于第三方库，不能据此重建四个应用资产或推导其平台支持。
6. TermForge 当前只有有限 Windows x64 客户端原型；Linux 核心 CI 不是 Linux GUI 客户端，Windows 11/Server 2025 验证也没有覆盖官方 Windows 7/8/10 或 x86 范围。R2 使用本台账做候选差距输入；平台调查可并行，真实跨平台整合及完整验收归 R5 的后续全平台出口。

## 目标台账

### 官方平台、架构与最低边界

| ID | 官方声明与发行物事实 | 最低系统边界及未知 | 来源类型与精确来源 | TermForge 当前实现/验证 | 负责路标与后续验收 |
|---|---|---|---|---|---|
| PLAT-001 | Windows **x86_32**，portable ZIP；资产 `WindTerm_2.7.0_Windows_Portable_x86_32.zip` | 官方列出 Windows 7/8/10/11，故声明最低为 **Windows 7**；Service Pack、edition、Windows 8.1、Server 及各版本实测均未知 | 官方声明：[release Usage](https://github.com/kingToolbox/WindTerm/releases/tag/2.7.0)；发行物：[资产元数据](https://api.github.com/repos/kingToolbox/WindTerm/releases/assets/236583144) | **未实现、未验证**；现有 Windows 客户端仅 x64 | #23/#26 的历史调查供 #34 判断 32 位支持缺口；x86 产品范围待用户明确，不阻塞 Windows 11 x64 首个完整出口 |
| PLAT-002 | Windows **x86_64**，portable ZIP；资产 `WindTerm_2.7.0_Windows_Portable_x86_64.zip` | 官方列出 Windows 7/8/10/11，故声明最低为 **Windows 7**；Service Pack、edition、Windows 8.1、Server 及各版本实测均未知 | 官方声明：[release Usage](https://github.com/kingToolbox/WindTerm/releases/tag/2.7.0)；发行物：[资产元数据](https://api.github.com/repos/kingToolbox/WindTerm/releases/assets/236582459) | **部分**：有限 x64 原型与免安装 ZIP；只已验 Windows 11 本机和 Server 2025 CI。Windows 7/8/10 未验收，Server 2022 已知资源门槛未过 | R2 #34 选型输入及 Windows 开发版；R5 先验 Windows 11 x64，旧系统/更广矩阵由用户后续明确，不把当前验证范围写成官方范围 |
| PLAT-003 | macOS **x86_64**，portable DMG；资产 `WindTerm_2.7.0_Mac_Portable_x86_64.dmg`；说明为拖入 `Applications` | 官方列出 macOS 10.13/10.14/10.15/11/12，故声明最低为 **10.13**；具体 build、10.12 及更早、13 及更新版本和实机验证未知 | 官方声明/安装说明：[release Usage](https://github.com/kingToolbox/WindTerm/releases/tag/2.7.0)；发行物：[资产元数据](https://api.github.com/repos/kingToolbox/WindTerm/releases/assets/236583377) | **未实现、未验证** macOS 客户端 | R2 仅供 #26/#34 判断旧 macOS、窗口/输入/字体和打包缺口；可行性调查可并行；真实客户端整合及验收归 R5 Windows 出口之后，系统/架构由用户明确 |
| PLAT-004 | Linux **x86_64**，portable ZIP；资产 `WindTerm_2.7.0_Linux_Portable_x86_64.zip` | 官方声明 **Generic Linux（glibc 2.17+）**；具体发行版、内核、glibc 之外运行库、X11/Wayland/桌面环境和实机验证未知 | 官方声明：[release Usage](https://github.com/kingToolbox/WindTerm/releases/tag/2.7.0)；发行物：[资产元数据](https://api.github.com/repos/kingToolbox/WindTerm/releases/assets/236583277) | **未实现、未验证** Linux 客户端；只有 Linux 核心 CI，不证明 GUI/PTY 整合或可分发产品 | R2 仅供 #26/#34 判断 libc/显示/输入/打包缺口；可行性调查可并行；真实客户端整合及验收归 R5 Windows 出口之后，系统/架构由用户明确 |

固定 README 的 [Download](https://github.com/kingToolbox/WindTerm/blob/a8336c1d1e3dd1a981500dae8ef9eb42066de95f/README.md#download) 与 [GUI](https://github.com/kingToolbox/WindTerm/blob/a8336c1d1e3dd1a981500dae8ef9eb42066de95f/README.md#gui) 还明确声明 Linux/macOS/Windows binary 和三平台 GUI。这一声明由上面四行落实为 2.7.0 的具体资产；README 本身没有给出额外架构或更低系统版本。

### 安装路径、组件边界与公开构建边界

| ID | 目标事实或组件边界 | 来源类型与精确来源 | 当前实现/验证状态 | 负责路标与后续验收 |
|---|---|---|---|---|
| PLAT-005 | release 提供 `scoop bucket add extras` + `scoop install windterm` 和 `choco install windterm`。这是官方说明中的安装入口，不是额外的官方 release 资产；包版本、架构、维护者、依赖和最低系统均未知 | 官方声明：[release Installation](https://github.com/kingToolbox/WindTerm/releases/tag/2.7.0)；四个实际上传资产：[release API](https://api.github.com/repos/kingToolbox/WindTerm/releases/tags/2.7.0) | TermForge 未提供或验证这两个入口；未把第三方包管理器元数据当作官方目标事实 | R2/R3 决定 Windows 分发时另行核验；R5 验收所声明的安装/升级路径，不能以命令存在代替测试 |
| PLAT-006 | 固定公开 `ConPty.cpp` 定义 `CONPTY_MINIMAL_WINDOWS_VERSION 18309`，`isAvailable()` 检查该 build，失败文字为 Windows 10 below 1809；因此 **ConPTY 路径静态下限为 Windows 10 1809 / build 18309**。这不是整个 WindTerm 的最低系统，也不是 2.7.0 二进制运行观察；release 另称该版本暂以 WinPTY 替代 ConPTY | 源码静态事实：[`src/Pty/ConPty.cpp`](https://github.com/kingToolbox/WindTerm/blob/a8336c1d1e3dd1a981500dae8ef9eb42066de95f/src/Pty/ConPty.cpp)；官方 release 的 `[SHELL]` 说明：[2.7.0 release](https://github.com/kingToolbox/WindTerm/releases/tag/2.7.0) | 当前原型使用 ConPTY，已验 Windows 11/Server 2025；Windows 10 未验收，Server 2022 资源门槛未过。没有验证官方 2.7.0 的 WinPTY/ConPTY 选择行为 | 保留 #26 的历史比较，#34 根据原版观察提交后端与 PTY 边界方案供用户确认；R5 按确认的系统矩阵验收，不能把 1809 错写成全应用下限 |
| PLAT-007 | 固定公开树只有 `README.md`、`benchmark/`、`images/`、`src/`，没有完整应用入口或顶层构建/安装/打包工程。`src/libssh/CMakeLists.txt` 是 `project(libssh VERSION 0.9.5 LANGUAGES C)`，其 CPack 可生成 libssh ZIP/NSIS；这些不是 WindTerm 应用发行配置 | 源码树事实：[固定 commit 根目录](https://github.com/kingToolbox/WindTerm/tree/a8336c1d1e3dd1a981500dae8ef9eb42066de95f)、[递归 tree API](https://api.github.com/repos/kingToolbox/WindTerm/git/trees/a8336c1d1e3dd1a981500dae8ef9eb42066de95f?recursive=1)（本次返回 `truncated=false`）；构建配置：[`src/libssh/CMakeLists.txt`](https://github.com/kingToolbox/WindTerm/blob/a8336c1d1e3dd1a981500dae8ef9eb42066de95f/src/libssh/CMakeLists.txt)、[`src/libssh/CPackConfig.cmake`](https://github.com/kingToolbox/WindTerm/blob/a8336c1d1e3dd1a981500dae8ef9eb42066de95f/src/libssh/CPackConfig.cmake)、[`src/libssh/INSTALL`](https://github.com/kingToolbox/WindTerm/blob/a8336c1d1e3dd1a981500dae8ef9eb42066de95f/src/libssh/INSTALL) | 未从官方公开源码构建官方应用；TermForge 的独立可复建目标不受此阻塞，但不能宣称恢复官方构建或复用其完整源码 | R2 #26/#34 把依赖许可、平台构建和维护成本分开；R5 对 TermForge 自己声明的平台完成可复建发布和矩阵验收 |

### 未确认架构与版本范围

以下行是**显式未知项**，不是新增的官方支持声明，也不是放弃这些范围的决定。

| ID | 未知范围 | 来源类型与精确来源 | 当前实现/验证状态 | 负责路标与后续验收 |
|---|---|---|---|---|
| PLAT-008 | Windows ARM/ARM64，以及 x86/x64 之外架构；原生运行、仿真运行和最低系统均未知 | 官方声明与发行物事实：[固定 README](https://github.com/kingToolbox/WindTerm/blob/a8336c1d1e3dd1a981500dae8ef9eb42066de95f/README.md) 只声明 Windows；[release API](https://api.github.com/repos/kingToolbox/WindTerm/releases/tags/2.7.0) 四资产无 Windows ARM | 未实现、未验证 | R2 #26/#34 记录候选硬限制，不作支持承诺；是否纳入由用户明确，若纳入则在 R5 后续平台出口完成真实客户端验收 |
| PLAT-009 | macOS ARM64/Apple Silicon，包括原生与 Rosetta 路径；macOS x86 之外架构 | 官方声明与发行物事实：[release](https://github.com/kingToolbox/WindTerm/releases/tag/2.7.0) 只有 x86_64 和 10.13–12 声明；[macOS 资产元数据](https://api.github.com/repos/kingToolbox/WindTerm/releases/assets/236583377) 只有 x86_64 DMG，没有 ARM 资产或兼容声明 | 未实现、未验证 | 同 PLAT-008；不能从 x86_64 DMG 推断 ARM 原生或仿真通过 |
| PLAT-010 | Linux ARM64/AArch32、x86_32 及其他架构 | 官方声明与发行物事实：[release](https://github.com/kingToolbox/WindTerm/releases/tag/2.7.0) 只有 x86_64 和 glibc 2.17+ 声明；[Linux 资产元数据](https://api.github.com/repos/kingToolbox/WindTerm/releases/assets/236583277) 没有其他架构或系统边界 | 未实现、未验证；核心 CI 不覆盖这些客户端 | 同 PLAT-008；核心可编译不等于产品平台支持 |
| PLAT-011 | 未列出的旧/新系统、Windows Server/edition/Service Pack、Linux 发行版与显示栈 | 官方声明：[release](https://github.com/kingToolbox/WindTerm/releases/tag/2.7.0) 只给 PLAT-001–004 的枚举或 glibc 条件；未提及项均保持未知，不自行填具体版本 | 仅 FEATURES 所列有限 Windows 环境和 Linux 核心 CI 有验证 | R2 选型必须列环境缺口；R5 对用户确认范围做真实验收，其他系统/架构保持待明确，范围变化另留确认依据 |

## 2.7.0 官方资产快照

以下是 2026-10-24 访问 release API 时的全部四个上传资产。字节数来自资产元数据；SHA-256 来自同一 release 的 Checksums。API 的 `digest` 均为 `null`，所以没有把它当作校验值来源。

| 资产精确名称 | 字节数 | 形式 | release 给出的 SHA-256 | 精确链接 |
|---|---:|---|---|---|
| `WindTerm_2.7.0_Windows_Portable_x86_32.zip` | 28,769,873 | Windows x86 便携 ZIP | `d04b2742c33773c0a0a5761889ca599ad5c443224dbc6198fe0e9e8de84f62c7` | [元数据](https://api.github.com/repos/kingToolbox/WindTerm/releases/assets/236583144) · [下载](https://github.com/kingToolbox/WindTerm/releases/download/2.7.0/WindTerm_2.7.0_Windows_Portable_x86_32.zip) |
| `WindTerm_2.7.0_Windows_Portable_x86_64.zip` | 32,678,162 | Windows x64 便携 ZIP | `b05c3c7371340ff1302967247ffcd3a3ba27b237f72010e9157086143c453e50` | [元数据](https://api.github.com/repos/kingToolbox/WindTerm/releases/assets/236582459) · [下载](https://github.com/kingToolbox/WindTerm/releases/download/2.7.0/WindTerm_2.7.0_Windows_Portable_x86_64.zip) |
| `WindTerm_2.7.0_Mac_Portable_x86_64.dmg` | 29,737,609 | macOS x64 DMG；拖放 `.app` 安装 | **未知**：实际 DMG 没有对应 release 校验值 | [元数据](https://api.github.com/repos/kingToolbox/WindTerm/releases/assets/236583377) · [下载](https://github.com/kingToolbox/WindTerm/releases/download/2.7.0/WindTerm_2.7.0_Mac_Portable_x86_64.dmg) |
| `WindTerm_2.7.0_Linux_Portable_x86_64.zip` | 39,102,499 | Linux x64 便携 ZIP | `7797697eeb60239020512e2b549695a60c39b3fd01fe7ebb047fd50a2889103c` | [元数据](https://api.github.com/repos/kingToolbox/WindTerm/releases/assets/236583277) · [下载](https://github.com/kingToolbox/WindTerm/releases/download/2.7.0/WindTerm_2.7.0_Linux_Portable_x86_64.zip) |

资产名称中没有官方 Windows 安装 EXE/MSI、Linux `.tar.gz`/DEB/RPM/AppImage 或 macOS ZIP。这里只陈述本次 release 资产事实，不推断其他渠道或其他版本。GitHub 自动生成的源码归档也不等于可重建官方应用的发行物。

release 文本另列 `WindTerm_2.7.0_Mac_Portable_x86_64.zip` 的 SHA-256 `8c1bc1363e01df5b06f45019da934752f5f29e8603cc843bd38e59bdff121bf7`，但资产 API 没有该 ZIP；Linux 安装命令则写 `WindTerm_2.7.0_Linux_Portable_x86_64.tar.gz`，资产 API 同样没有该文件。缺失文件的可获得性和校验关系均未知。

## 已知限制与独立改进

- **参考版本已知限制：** release 的 Windows ZMODEM 非 ASCII 文件名、tmux Sender Pane/Sync Input 和嵌套限制属于功能边界，继续由 FEATURES/#19 处理；它们不删减本台账的平台行。ConPTY 的 build 18309 也只是组件边界，不能覆盖官方 Windows 7 应用声明。
- **证据限制：** 本次没有 WindTerm 运行观察，未检查包内所有二进制，也未验证任何旧系统、macOS、Linux 或 ARM。官方资产列表与说明的两处不一致原样保留。
- **独立改进：** 本切片不选择支持更多系统、ARM、更新 macOS、额外包格式或新安装器。TermForge 将来若增加参考版本未声明的能力，必须标为独立改进并单独验收，不能回写成 WindTerm 2.7.0 官方范围。

## 给 R2 选型的候选问题

### #26：核心路线与 ADR-0003

1. 候选核心及 PTY 适配能否分别覆盖 Windows x86/x64、Linux x64 和 macOS x64？对 Windows 7 所需非 ConPTY 路径与 ConPTY 1809+ 路径如何抽象、降级和差分验证？
2. 候选及其依赖实际要求的最低 Windows、macOS SDK/deployment target、glibc/libstdc++ 是什么？哪些要求会使 PLAT-001–004 出现硬缺口，能否以可维护的适配层解决？
3. 候选在 32 位、字节序/字长、原子操作、文件描述符/进程/resize 语义上有哪些假设？ARM 等未知架构是无法构建、未经测试，还是已有可验证支持？
4. Linux 核心 CI 之外，候选还缺哪些真实 PTY、TUI、输入和长期运行证据？不得以核心测试替代 PLAT-003/004 的客户端验收。

### #34：产品 GUI/平台与 ADR-0004

1. GUI 候选及其精确版本能否实际构建并部署到 Windows 7/8/10/11 x86/x64、macOS 10.13–12 x64、Linux glibc 2.17+ x64？若不能，缺口来自框架、渲染、输入法、字体、依赖还是打包工具？
2. 核心/历史/传输/呈现/平台适配接口能否隔离 ConPTY 与 WinPTY、Unix PTY、窗口/剪贴板/IME/DPI 差异，并保留当前 Windows 原型的可替换性？
3. 各平台便携 ZIP、macOS DMG 拖放安装及后续包管理器入口需要哪些依赖、签名、升级和资源策略？本切片不实现，但 ADR 必须给出验证责任。
4. 对 PLAT-008–011 的未知项，ADR 是明确延期、安排探针还是提出独立扩展？不得用当前 Windows x64 原型或单次 Linux CI 静默排除它们。

## 本次实际访问的一手来源

- [固定 commit README](https://github.com/kingToolbox/WindTerm/blob/a8336c1d1e3dd1a981500dae8ef9eb42066de95f/README.md) 及其 [raw 内容](https://raw.githubusercontent.com/kingToolbox/WindTerm/a8336c1d1e3dd1a981500dae8ef9eb42066de95f/README.md)
- [2.7.0 release](https://github.com/kingToolbox/WindTerm/releases/tag/2.7.0) · [release / 四资产 API](https://api.github.com/repos/kingToolbox/WindTerm/releases/tags/2.7.0) · 上表四个资产元数据 URL
- [2.7.0 tag ref API](https://api.github.com/repos/kingToolbox/WindTerm/git/ref/tags/2.7.0) · [固定 commit 根目录](https://github.com/kingToolbox/WindTerm/tree/a8336c1d1e3dd1a981500dae8ef9eb42066de95f) · [固定递归 tree API](https://api.github.com/repos/kingToolbox/WindTerm/git/trees/a8336c1d1e3dd1a981500dae8ef9eb42066de95f?recursive=1)
- 固定 commit 源码/配置：[`ConPty.cpp`](https://github.com/kingToolbox/WindTerm/blob/a8336c1d1e3dd1a981500dae8ef9eb42066de95f/src/Pty/ConPty.cpp)、[`UnixPty.cpp`](https://github.com/kingToolbox/WindTerm/blob/a8336c1d1e3dd1a981500dae8ef9eb42066de95f/src/Pty/UnixPty.cpp)、[`libssh/CMakeLists.txt`](https://github.com/kingToolbox/WindTerm/blob/a8336c1d1e3dd1a981500dae8ef9eb42066de95f/src/libssh/CMakeLists.txt)、[`libssh/CPackConfig.cmake`](https://github.com/kingToolbox/WindTerm/blob/a8336c1d1e3dd1a981500dae8ef9eb42066de95f/src/libssh/CPackConfig.cmake)、[`libssh/INSTALL`](https://github.com/kingToolbox/WindTerm/blob/a8336c1d1e3dd1a981500dae8ef9eb42066de95f/src/libssh/INSTALL)

`UnixPty.cpp` 的 UNIX/macOS 条件编译仅作为源码平台线索查阅，没有用来扩大官方发行矩阵；所有已确认发行范围仍以 PLAT-001–004 的 release 声明和实际资产为准。
