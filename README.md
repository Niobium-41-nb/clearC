<img src="assets/logo.png" width="88" alt="clearC logo" />

# clearC - C盘缓存清理工具

Windows 原生桌面应用，一键扫描并清理 C 盘各类缓存，释放磁盘空间。

## 功能

- **97 个清理项，覆盖 7 大类**（`clearC.exe --list` 查看完整清单）：
  - **包管理器（24）**：uv、pip、Conda、NuGet（全局包 / HTTP 缓存）、npm、yarn、pnpm（store / 元数据）、vcpkg、Go（构建 / 模块）、Cargo（注册表 / Git）、Maven、Composer、Deno、Bun、Chocolatey、Scoop、Conan、node-gyp
  - **构建缓存（4）**：Gradle（依赖 / 发行包）、CMake、ccache
  - **编辑器 / IDE（23）**：VSCode 全套缓存（含工作区状态、全局扩展存储、WebStorage、Partitions、本地历史、Crashpad）、Cursor、Trae CN、JetBrains（缓存 / 索引 / 日志）、Visual Studio（组件缓存 / 安装缓存）、Package Cache（全机 / 用户级）、Android Studio
  - **浏览器（7）**：Edge / Chrome（缓存、组件 CRX、优化模型）、Firefox、系统网络缓存（INetCache）
  - **系统缓存（23）**：临时文件、Windows Temp、更新缓存、传递优化、预取、错误报告、崩溃转储、DirectX / NVIDIA / AMD 着色器缓存、内核报告、内存转储、CBS / 系统 / 更新协调器日志、缩略图 / 图标缓存（文件通配）、Squirrel 更新残留、Google 更新器缓存、UWP 应用缓存、Windows.old、升级临时文件、C 盘回收站
  - **应用缓存（10）**：Teams、Slack、Discord、WPS 组件、Steam html、QQ 频道、微信（仅缓存，不含聊天记录）、网易云音乐、Spotify、Adobe 媒体缓存
  - **开发工具（6）**：Playwright / Puppeteer / Playwright MCP 浏览器、CLI 工具缓存、Docker 日志、WinGet 缓存
- **多线程并行扫描**：8 条工作线程统计目录大小，实时进度；本机 64 项命中、38 GB 可清理内容约 12 秒完成（视磁盘与缓存规模而定）
- **分类筛选 + 按大小排序**：列表含“分类”列，可按分类过滤；扫描后按占用空间从大到小排列
- **安全护栏**：拒绝盘符根、系统关键目录、用户目录根等危险目标；不跟随 junction / 符号链接（重定向项显示“链接跳过”）；支持 `\\?\` 超长路径
- **管理员分级**：需要提权的系统项标注“需管理员”，普通权限下自动跳过；界面提供“以管理员身份重启”
- **无法读取可见**：普通权限下无法枚举的目录（如 Prefetch）显示“无法读取 / 需管理员”，不会误报 0 B
- **失败统计**：被占用 / 无权限的文件不会静默丢失，清理结果报告释放空间、失败数、跳过数与首个失败原因
- **谨慎项默认不勾选**：Maven 本地仓库、VS / Package Cache、VSCode 工作区状态、WPS 组件、Windows.old 等默认关闭
- **界面**：Ctrl + 鼠标滚轮调整字号（11–26px）、Per-Monitor DPI 适配、窗口可缩放、双击行打开目录

> 默认勾选 70 个“安全”项；其余 27 项为谨慎项，需要你确认后手动勾选。

## 使用

1. 双击 `clearC.exe`（启动后自动扫描）。
2. 扫描完成后按需增减（可先用“分类”下拉框过滤，再点“全选”）。
3. 点击“清理选中项”，确认后清理；如有系统项需要管理员权限，会提示一键提权重启。
4. 清理完成后自动重新扫描，刷新剩余空间。

> 建议清理前关闭浏览器 / IDE / 聊天工具；被占用的文件会自动跳过并在结果中统计。
> 若某个缓存目录被 junction / 符号链接重定向（例如 VS 安装缓存指向其他盘），程序会显示“链接跳过”而不清理，避免误删链接目标。

## 命令行

无参数启动图形界面；带参数进入命令行模式，便于脚本与自动化验证：

```bat
clearC.exe --scan             :: 扫描并打印各清理项大小
clearC.exe --scan --json      :: 机器可读 JSON（含 denied / skippedLinks / kind 字段）
clearC.exe --list             :: 列出全部清理项、路径模板与解析结果
clearC.exe --list --json      :: JSON 清单
clearC.exe --self-test        :: 内置自检（只在临时目录创建夹具，不碰真实缓存）
clearC.exe --version
clearC.exe --help
```

自检覆盖：大小格式化、路径归一化、危险路径判定、目录/文件通配、目录统计、只读文件删除、junction 不跟随、**缓存项自身为 junction 时整体跳过**、超长路径、回收站查询。退出码 0 表示全部通过，可直接用于 CI。

## 安全说明

- 只删除缓存目录的**内容**，保留目录本身；不删除配置、文档、聊天记录等用户数据。
- `IsSafeToClean` 会拒绝：盘符根、`C:\Windows`、`System32`、`SysWOW64`、`WinSxS`、`Program Files`、`ProgramData`、`Users`、用户目录根、桌面 / 文档 / 下载 / OneDrive 等，以及这些目录的上级路径（`..` 已归一化，无法绕过）。
- 遇到 junction / 符号链接（重解析点）时**跳过不跟随**；缓存项自身是链接时整项跳过，避免删除链接指向的其他磁盘或用户数据。
- 文件通配项（缩略图 / 图标缓存）只删除命中的文件，不会清空父目录。
- 回收站通过 Shell API（`SHEmptyRecycleBin`）清空，且只作用于 C 盘。
- 清理前有确认弹窗并展示预计释放空间；清理后有结果汇总（释放空间、删除文件数、失败数、跳过数）。

## 编译

### 环境要求

- MSYS2 + MinGW-w64（ucrt64 工具链），或 Visual Studio 2019+

### 一键编译

```bat
build.bat          :: 编译（自动定位 PATH / D:\msys64 / C:\msys64 下的 g++）
build.bat release  :: 编译并 strip 符号
build.bat run      :: 编译并启动
build.bat clean    :: 清理 build\ 与 clearC.exe
```

### 手动编译（MSYS2 MinGW-w64）

```bash
windres resource.rc -o build/resource.o
g++ -std=c++17 -O2 -DUNICODE -D_UNICODE -municode -mwindows -finput-charset=UTF-8 -I. \
    -o clearC.exe \
    src/main.cpp src/ui.cpp src/scan.cpp src/catalog.cpp src/util.cpp src/cli.cpp \
    build/resource.o -lcomctl32 -lshlwapi -lshell32 -static
```

### Visual Studio（未在 MSVC 下验证）

```cmd
rc /fo build\resource.res resource.rc
cl /std:c++17 /O2 /utf-8 /EHsc /DUNICODE /D_UNICODE /I. ^
   src\main.cpp src\ui.cpp src\scan.cpp src\catalog.cpp src\util.cpp src\cli.cpp ^
   build\resource.res /link ^
   user32.lib comctl32.lib shlwapi.lib shell32.lib ^
   /SUBSYSTEM:WINDOWS /ENTRY:wWinMainCRTStartup
```

## 项目结构

```
src/
  common.h        公共定义（版本、窗口消息）
  util.h/.cpp     环境变量与路径规范化、长路径、目录/文件通配、统计、安全删除、提权判断
  catalog.h/.cpp  清理项目录（97 项，7 类；支持 %ENV%、通配、文件通配）
  scan.h/.cpp     并行扫描 / 清理引擎
  ui.h/.cpp       Win32 图形界面
  cli.h/.cpp      命令行模式与内置自检
  main.cpp        入口（GUI / CLI 分发）
docs/
  audit.md              旧版 clearC.cpp 的审计报告（56 项问题、实测证据）
  cache-candidates.md   本机缓存目录逐项实测清单（大小、权限、风险、路径坑）
assets/           标识与程序图标
tools/            标识生成脚本
build.bat         一键编译脚本
```

## 开发文档

- [docs/audit.md](docs/audit.md)：旧实现审计（junction 跟随、MAX_PATH 截断、失败吞掉、构建脚本误报等），含可执行复现与验收清单。
- [docs/cache-candidates.md](docs/cache-candidates.md)：本机候选缓存目录实测（大小 / ACL / 风险 / 危险项 / 通配与路径陷阱）。
- [docs/verify.md](docs/verify.md)：2.0.0 独立验收报告（构建 / CLI / GUI / 安全 / 一致性，49+2 项自检与增量复核 PASS）。

## 图标与标识

| 文件 | 用途 |
| --- | --- |
| `assets/logo.svg` | 矢量母版 |
| `assets/logo.png` | 位图标识（文档、宣传用） |
| `assets/clearC.ico` | 程序图标（16–256px 多尺寸），由 `resource.rc` 嵌入 exe |

标识由脚本生成，改 `tools/gen_logo.py` 顶部的设计参数后重跑即可（需要 Python + Pillow）：

```bash
python tools/gen_logo.py
```

## 许可证

MIT License
