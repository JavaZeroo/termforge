# 工作区 UI 字体来源

`RobotoMono-Regular.ttf` 从 [Google Fonts 的 RobotoMono 官方仓库](https://github.com/googlefonts/RobotoMono)独立取得，未使用 WindTerm 发行物中的字体。固定上游提交为 [`895ec691990d041dd727c7b5afa3ce56525d98e6`](https://github.com/googlefonts/RobotoMono/tree/895ec691990d041dd727c7b5afa3ce56525d98e6)，取得日期为 2026-10-23。这里只随附未修改的静态 Regular 字体及同一提交的许可文件；完整可编辑字体源、构建脚本在固定上游树中。

| 文件 | 上游固定原始文件 | 字节数 | SHA-256 |
|---|---|---:|---|
| `RobotoMono-Regular.ttf` | [fonts/ttf/RobotoMono-Regular.ttf](https://raw.githubusercontent.com/googlefonts/RobotoMono/895ec691990d041dd727c7b5afa3ce56525d98e6/fonts/ttf/RobotoMono-Regular.ttf) | 125748 | `af0bff7599c3df3831755c16e39b3c496df74b8c8d8a1161b14dc8461be17cb4` |
| `OFL.txt` | [OFL.txt](https://raw.githubusercontent.com/googlefonts/RobotoMono/895ec691990d041dd727c7b5afa3ce56525d98e6/OFL.txt) | 4395 | `50ab8dd54680d3473f649c9db86fece88434d097c7834475c1c72d2f8c429215` |

版权所有者与许可见随附 [OFL.txt](OFL.txt)。该文件授权字体随软件分发，要求保留版权声明及完整许可，字体本身继续适用 SIL Open Font License 1.1。TermForge 的 MIT 许可不覆盖此字体。字体和许可均按上游原始字节保存，没有子集化、转换或改名。

构建不访问网络。Windows `termforge_shell` 的构建步骤把本目录的字体、许可与本文复制到可执行文件旁的 `assets/fonts/`；分发可执行文件时须一并保留该目录。`WorkspaceFont` 从可执行文件目录解析路径，以 [`AddFontResourceExW(..., FR_PRIVATE, ...)`](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-addfontresourceexw)仅对本进程加载，释放 HFONT 后卸载。字体不存在、加载失败或 [`GetTextFaceW`](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-gettextfacew) 返回的实际 face 不是 `Roboto Mono` 时明确失败，不静默替换。

当前仅用于工作区 UI，GDI 字符高度为 12（96 逻辑 DPI 下的 9pt）。同名字体不证明与 WindTerm 所带版本的字形或像素完全一致；GDI 与 Qt 的字体选择、测量与栅格化也可能不同。终端 Renderer 的字体与网格未受此资源变更影响，尚未加入 Nerd Font、逐字形 fallback 或真实跨屏 DPI 处理。
