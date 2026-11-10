# clearC 缓存候选目录实测清单

> **调查机**：`VANADIUM\14530`（**非管理员**，完整性级别 Medium），Windows，C: 已用 242.6 GiB / **剩余 57.2 GiB**，D:/E: 均有空闲。
> **实测时间**：2026-11-10。**方法**：`Get-ChildItem -LiteralPath <path> -Recurse -Force -ErrorAction SilentlyContinue | Measure-Object Length -Sum`，含隐藏/系统文件；1 GiB = 1024 MiB。占用中/无权限被跳过的文件**不计入**，因此"临时目录/运行中应用的缓存"实际偏小。
> **安全声明**：本次调查只读，未删除/移动任何文件；仅生成本文档。

---

## 0. 结论摘要

1. 本机真正的大头（可直接或谨慎回收）：**WPS 组件缓存 11.24 GiB**、**VSCode workspaceStorage 5.84 GiB**、**VS 安装包缓存 3.89 GiB**、**Docker vhdx 3.66 GiB**、**VSCode 扩展 3.73 GiB**、**WPS 旧版本目录 3.36 GiB**、**npm 缓存 2.58 GiB**、**Trae CN 2.75 GiB**、**`~/.cache` 1.98 GiB**、**Package Cache 1.80 GiB**、**VSCode globalStorage 1.58 GiB**、**Playwright 1.53 GiB**、**pnpm 合计 2.56 GiB**。
2. 现有 catalog 共 **70 条**（69 个 `CACHE_DIR` + 1 个回收站项，`build/scan.json` 实际命中 34 项），其中约 **14 项在本机为 0/不存在**，属"未安装即空"的**正常**情况，条目本身无需删除；但发现 **4 处真实问题**（见 §6）：npm 路径写错（0 vs 2.58 GiB）、pnpm-cache 漏项（1.04 GiB）、VS 安装包缓存 3.89 GiB 被统计成 0、Delivery Optimization 路径不存在。
3. 建议**新增约 26 条**（含 3 处修正），其中 **约 15 条为 Safe 且可默认勾选**（新增部分约 1.7 GiB 增量收益）；最大收益的 6 项均为 Caution，应默认不勾选并在 UI 配文案。
4. 本机 **不是 NVIDIA 显卡，是 AMD**：`%LOCALAPPDATA%\NVIDIA\*` 全部为 0，而 AMD 着色器缓存 257 MiB → 目录模板应与显卡厂商解耦。

---

## 1. 环境与权限事实（决定 `needsAdmin` 标注）

| 检查 | 结果 | 结论 |
|---|---|---|
| `whoami` | `vanadium\14530` | 普通用户 |
| `[WindowsPrincipal]::IsInRole(Administrator)` | `False` | **当前非管理员** |
| `whoami /groups` 完整性级别 | `Medium Mandatory Level (S-1-16-8192)` | 未提权 |
| `net session` | `System error 5. 拒绝访问` | 非管理员（提权后才返回列表） |

ACL 实测（只读检查，未写文件）：

| 路径 | 当前用户有效权限 | 需管理员 |
|---|---|---|
| `%LOCALAPPDATA%\*`、`%APPDATA%\*`、`%USERPROFILE%\*` | `VANADIUM\14530 : FullControl` | ❌ 否 |
| `%TEMP%`（=`%LOCALAPPDATA%\Temp`） | 本人 FullControl | ❌ 否 |
| `C:\Windows\Temp` | Users 仅 `CreateFiles, CreateDirectories, Traverse`（**只能删自己创建的文件**） | ⚠️ **部分** |
| `C:\Windows\Prefetch` | `Get-Acl` 直接 `unauthorized operation`，枚举被拒 | ✅ 是 |
| `C:\Windows\SoftwareDistribution\Download`、`C:\Windows\Logs`、`C:\Windows\System32\LogFiles` | Users 仅 `ReadAndExecute` | ✅ 是 |
| `C:\Windows\System32\winevt\Logs` | Authenticated Users 仅 `Read` | ✅ 是 |
| `C:\ProgramData\Microsoft\Windows\WER`、`USOShared`、`Package Cache`、`Microsoft\VisualStudio\Packages` | Users 仅读 / 仅 `Write`（不能删） | ✅ 是 |
| `C:\Windows\Installer`、`C:\$Recycle.Bin`（顶层） | Users 仅 `ReadAndExecute` | ✅ 是 |
| `C:\Windows\ServiceProfiles\NetworkService\...\DeliveryOptimization` | **直接 Access denied** | ✅ 是 |
| `C:\Windows\ServiceProfiles\LocalService\...\FontCache` | Access denied | ✅ 是 |

**回收站补充**：`C:\$Recycle.Bin` 顶层为只读，但**每个用户自己的 `S-1-5-21-...` 子目录由该用户 FullControl**，因此"清空当前用户回收站"无需提权（现有 `EntryKind::RecycleBin` 走 Shell API 是对的）；清空**其他用户/其他盘**的回收站才需管理员。

**多用户**：`C:\Users` 下仅 `14530` 有配置（`Default`/`Public`/`All Users` 为系统项）→ 程序一律按**当前用户 profile** 展开，禁止写死用户名。

---

## 2. 逐项实测清单（按分类；"来源"列标注 现有/新增）

> 大小 = 实测值；"0" 表示目录不存在或为空。"匹配"列的 `n 目录/n 文件` 指通配命中数。

### 2.1 包管理器 / 构建工具

| # | 清理项 | 路径模板 | 来源 | 实测大小 | 需管理员 | 风险 | 默认勾选 | 备注（重建方式） |
|---|---|---|---|---|---|---|---|---|
| 1 | npm 缓存 | `%LOCALAPPDATA%\npm-cache` | **修正** | **2.58 GiB**（171,620 文件；`_npx` 2.02 GiB + `_cacache` 517 MiB） | ❌ | 安全 | ✅ | 自动重新下载；现有模板写成 `%APPDATA%\npm-cache`→本机 0 |
| 2 | pnpm 存储 v9 | `%LOCALAPPDATA%\pnpm\store` | 现有 | **1.52 GiB**（70,517） | ❌ | 安全 | ✅ | `pnpm install` 重建 |
| 3 | pnpm 元数据缓存 | `%LOCALAPPDATA%\pnpm-cache` | **新增** | **1.04 GiB**（5,746） | ❌ | 安全 | ✅ | 重新拉取 registry 元数据 |
| 4 | pip 缓存 | `%LOCALAPPDATA%\pip\Cache` | 现有 | 77.9 MiB（42） | ❌ | 安全 | ✅ | 重新下载 wheel（本机 `Cache` 大写，Win 不敏感） |
| 5 | Gradle 缓存 | `%USERPROFILE%\.gradle\caches` | 现有 | **610 MiB**（4,763） | ❌ | 安全 | ✅ | 首次构建重下依赖（较慢） |
| 6 | Gradle 分发缓存 | `%USERPROFILE%\.gradle\wrapper` | **新增** | 290 MiB（943） | ❌ | 谨慎 | ❌ | 删除后重新下载 gradle 发行包 |
| 7 | Maven 本地仓库 | `%USERPROFILE%\.m2\repository` | 现有 | **480 MiB**（7,142） | ❌ | 谨慎 | ❌ | 删后首次构建全量重下，慢 |
| 8 | JetBrains 索引（见 2.2） | — | — | — | — | — | — | — |
| 9 | node-gyp 缓存 | `%LOCALAPPDATA%\node-gyp` | **新增** | 110 MiB（5,096） | ❌ | 安全 | ✅ | 重新编译时重建 |
| 10 | Chocolatey HTTP 缓存 | `C:\ProgramData\ChocolateyHttpCache` | 现有(变体) | 0.2 MiB（9） | ✅ | 安全 | ✅ | 重新下载 |
| 11 | Chocolatey lib | `C:\ProgramData\chocolatey\lib` | 现有(变体) | 13.2 MiB（26） | ✅ | 谨慎 | ❌ | 包本体，卸载需重装 |
| 12 | Scoop apps | `%USERPROFILE%\scoop\apps` | 现有(cache 为空) | 10.4 MiB（1） | ❌ | 谨慎 | ❌ | `cache` 子目录本机不存在 |
| 13 | Go 构建缓存 | `%LOCALAPPDATA%\go-build` | 现有 | 0（11 文件，1.5 KiB） | ❌ | 安全 | ✅ | **本机 GOCACHE=D:\GoData\cache**，C 盘为空 |
| 14 | Go 模块缓存 | `%USERPROFILE%\go\pkg\mod\cache` | 现有 | 0（不存在） | ❌ | 安全 | ✅ | 本机未用默认 GOMODCACHE |
| 15 | Cargo 注册表/Git | `%USERPROFILE%\.cargo\registry\cache`、`\.cargo\git\db` | 现有 | 0（不存在；`.cargo` 仅 bin 180 MiB） | ❌ | 安全 | ✅ | 重新 `cargo fetch` |
| 16 | Conda 包缓存 | `%USERPROFILE%\.conda\pkgs`；`%LOCALAPPDATA%\conda\conda\pkgs` | 现有 | 0（C 盘无） | ❌ | 安全 | ✅ | **本机 conda 在 D 盘**：`D:\anaconda3\pkgs` 5.16 GiB、`D:\conda\pkgs` 1.46 GiB（非 C 盘，需单独盘符支持） |
| 17 | NuGet 全局包 / HTTP 缓存 | `%USERPROFILE%\.nuget\packages`、`%LOCALAPPDATA%\NuGet\v3-cache` | 现有 | 0（空目录） | ❌ | 安全 | ✅ | 重新 restore |
| 18 | yarn 缓存 | `%LOCALAPPDATA%\Yarn\Cache`、`..\Berry\cache` | 现有 | 0（空目录） | ❌ | 安全 | ✅ | Yarn 装在 `E:\Program Files (x86)\Yarn` |
| 19 | vcpkg archives/buildtrees | `%LOCALAPPDATA%\vcpkg\archives`、`..\buildtrees` | 现有 | 0（未安装） | ❌ | 安全 | ✅ | 如安装，实际根在 `%VCPKG_ROOT%` |
| 20 | uv / Composer / Deno / Bun / Conan / ccache / CMake / WinGet | 对应现有模板 | 现有 | 0（均未安装/空） | ❌ | 安全 | ✅ | 保留条目即可，无副作用 |

### 2.2 IDE / 编辑器

| # | 清理项 | 路径模板 | 来源 | 实测大小 | 需管理员 | 风险 | 默认勾选 | 备注 |
|---|---|---|---|---|---|---|---|---|
| 21 | VSCode 工作区状态 | `%APPDATA%\Code\User\workspaceStorage` | **新增** | **5.84 GiB**（74,909） | ❌ | 谨慎 | ❌ | 丢工作区布局/扩展工作区缓存（含 cpptools ipch），重开自动重建 |
| 22 | VSCode 全局扩展存储 | `%APPDATA%\Code\User\globalStorage` | **新增** | **1.58 GiB**（19,301；`ms-dotnettools.vscode-dotnet-runtime` 1.03 GiB 为下载的运行时安装包） | ❌ | 谨慎 | ❌ | 部分扩展会丢本地状态/需重新登录 |
| 23 | VSCode WebStorage | `%APPDATA%\Code\WebStorage` | **新增** | **1.47 GiB**（2,512） | ❌ | 谨慎 | ❌ | webview/内置浏览器存储，部分面板需重新加载 |
| 24 | VSCode 崩溃转储 | `%APPDATA%\Code\Crashpad` | **新增** | 381 MiB（12） | ❌ | 安全 | ✅ | 崩溃报告，自动重建 |
| 25 | VSCode Partitions | `%APPDATA%\Code\Partitions` | **新增** | 310 MiB（2,051） | ❌ | 谨慎 | ❌ | 多 profile 的浏览器数据（含 localStorage） |
| 26 | VSCode 本地历史 | `%APPDATA%\Code\User\History` | **新增** | 276 MiB（65,257） | ❌ | 谨慎 | ❌ | 文件"本地历史"撤销记录，用户可能要用 |
| 27 | VSCode 扩展 VSIX 缓存 | `%APPDATA%\Code\CachedExtensionVSIXs` | 现有 | 207 MiB（21） | ❌ | 安全 | ✅ | 自动重下 |
| 28 | VSCode Cache/CachedData/GPUCache/logs | 现有模板 | 现有 | 6.1 / 13.9 / 1.6 / 0.8 MiB | ❌ | 安全 | ✅ | 均自动重建 |
| 29 | VSCode CachedProfileData | `%APPDATA%\Code\CachedProfileData` | 现有→**修正** | 0（实际目录名是 **`CachedProfilesData`** 2.4 MiB） | ❌ | 安全 | ✅ | 模板应同时写 `CachedProfileData;CachedProfilesData` |
| 30 | VSCode C++ 缓存 | `%APPDATA%\Code\User\globalStorage\ms-vscode.cpptools` | 现有 | 0（不存在，cpptools 数据落在 workspaceStorage） | ❌ | 安全 | ✅ | 保留无害；本机为 0 |
| 31 | VSCode 扩展目录 | `%USERPROFILE%\.vscode\extensions` | **新增（不入目录）** | **3.73 GiB**（80,514） | ❌ | 危险 | ❌ | **不是缓存**：删=所有扩展消失，需重装重登（见 §4） |
| 32 | Trae CN（VSCode 系） | `%APPDATA%\Trae CN\Cache;CachedData;GPUCache;logs;WebStorage;Partitions` | **新增（可选）** | 合计 **2.75 GiB**（root 2,810.7 MiB） | ❌ | 谨慎 | ❌ | 同 VSCode 语义；同类还有 `Qoder CN`、`.lingma` 等 |
| 33 | JetBrains 索引 | `%LOCALAPPDATA%\JetBrains\*\index` | 现有 | **480 MiB**（1 产品：PyCharmCE2024.1） | ❌ | 安全 | ✅ | 重开项目重建索引 |
| 34 | JetBrains 缓存 | `%LOCALAPPDATA%\JetBrains\*\caches` | 现有 | 157 MiB（1 产品） | ❌ | 安全 | ✅ | 自动重建 |
| 35 | JetBrains 日志 | `%LOCALAPPDATA%\JetBrains\*\log` | 现有 | 2.0 MiB | ❌ | 安全 | ✅ | — |
| 36 | Visual Studio 组件缓存 | `%LOCALAPPDATA%\Microsoft\VisualStudio\*\ComponentModelCache` | 现有 | 12.6 MiB（2 个实例） | ❌ | 安全 | ✅ | 自动重建 |
| 37 | VS 安装包缓存 | `%ProgramData%\Microsoft\VisualStudio\Packages` | 现有 | **3.89 GiB**（1,898） | ✅ | 谨慎 | ❌ | ⚠️ **scan.json 报 0，需复核**（见 §7） |
| 38 | Package Cache（全机安装器缓存） | `C:\ProgramData\Package Cache` | **新增** | **1.80 GiB**（493） | ✅ | 谨慎 | ❌ | 修复/卸载时会重新下载；删前建议先确认无待修复安装 |
| 39 | Package Cache（用户级） | `%LOCALAPPDATA%\Package Cache` | **新增** | 68.8 MiB（33） | ❌ | 谨慎 | ❌ | 同上 |
| 40 | Cursor | `%APPDATA%\Cursor\*` | 现有 | 0（未安装；`~/.cursor` 仅 5.5 MiB） | ❌ | 安全 | ✅ | 保留 |
| 41 | Android Studio / Qt / CMake | 现有模板 | 现有 | 0（未安装） | ❌ | 安全 | ✅ | 保留 |

### 2.3 浏览器

| # | 清理项 | 路径模板 | 来源 | 实测大小 | 需管理员 | 风险 | 默认勾选 | 备注 |
|---|---|---|---|---|---|---|---|---|
| 42 | Edge 缓存合集 | `...\Edge\User Data\*\Cache`、`Code Cache`、`GPUCache`、`Service Worker\CacheStorage` | 现有 | 合计 **368 MiB**（Cache 70.2 / Code Cache 35.9 / GPUCache 0.5 / SW·CacheStorage 261.5） | ❌ | 安全 | ✅ | 2 个 profile（`Default` 1.20 GiB、`Profile 1` 8.5 MiB）；Edge 整根 1.67 GiB 其余是用户数据 |
| 43 | Edge 组件 CRX 缓存 | `...\Edge\User Data\component_crx_cache` | **新增** | 199 MiB（27） | ❌ | 安全 | ✅ | 组件自动重下 |
| 44 | Chrome 缓存合集 | `...\Chrome\User Data\*\Cache`、`Code Cache`、`GPUCache`、`SW\CacheStorage` | 现有 | 合计 **12.7 MiB**（Code Cache 11.1 / GPUCache 1.6 / Cache 0 / SW 0） | ❌ | 安全 | ✅ | 1 个 profile（Default 160 MiB） |
| 45 | Chrome 组件 CRX 缓存 | `...\Chrome\User Data\component_crx_cache` | **新增** | 40.0 MiB（21） | ❌ | 安全 | ✅ | — |
| 46 | Chrome 优化指南模型 | `...\Chrome\User Data\optimization_guide_model_store` | **新增** | 48.7 MiB（28） | ❌ | 安全 | ✅ | 自动重下 |
| 47 | Firefox cache2 | `%LOCALAPPDATA%\Mozilla\Firefox\Profiles\*\cache2` | 现有 | 0（1 profile，空） | ❌ | 安全 | ✅ | Firefox 数据在 `%APPDATA%\Mozilla` 77.7 MiB（不是缓存） |
| 48 | INetCache | `%LOCALAPPDATA%\Microsoft\Windows\INetCache` | 现有 | 0.2 MiB（21） | ❌ | 安全 | ✅ | — |
| 49 | Edge 扩展与活动记录 | `...\User Data\Default\Extensions`、`ExtensionActivityEdge`(318 MiB 单文件) | **不入** | 216 + 318 MiB | ❌ | 危险 | ❌ | 非缓存，删=扩展丢失/活动记录丢失 |

### 2.4 系统

| # | 清理项 | 路径模板 | 来源 | 实测大小 | 需管理员 | 风险 | 默认勾选 | 备注 |
|---|---|---|---|---|---|---|---|---|
| 50 | 用户临时文件 | `%TEMP%` | 现有 | 54.3 MiB（318） | ❌ | 安全 | ✅ | 占用中文件跳过；实测偏小 |
| 51 | Windows 临时文件 | `C:\Windows\Temp` | 现有 | 7.7 MiB（134） | ⚠️ 部分 | 安全 | ✅ | Users 只有 CreateFiles，**别人创建的文件删不掉** |
| 52 | Windows 更新缓存 | `C:\Windows\SoftwareDistribution\Download` | 现有 | 32.4 MiB（9） | ✅ | 安全 | ✅ | 本机很小（刚清过/未更新） |
| 53 | 传递优化缓存 | `%SystemRoot%\SoftwareDistribution\DeliveryOptimization` | 现有→**修正** | **路径不存在**；真实路径被拒（见 §6） | ✅ | 安全 | ✅ | 真实路径：`C:\Windows\ServiceProfiles\NetworkService\AppData\Local\Microsoft\Windows\DeliveryOptimization` |
| 54 | Windows 预取 | `C:\Windows\Prefetch` | 现有 | **无法统计**（枚举被拒） | ✅ | 安全 | ✅ | 普通权限下必须显示"需管理员"，不要显示 0 B |
| 55 | 错误报告 | `C:\ProgramData\Microsoft\Windows\WER`、`%LOCALAPPDATA%\Microsoft\Windows\WER` | 现有 | 0（1 空项） | ✅ | 安全 | ✅ | 本机无积压 |
| 56 | 崩溃转储 | `%LOCALAPPDATA%\CrashDumps`、`C:\Windows\Minidump`、`LiveKernelReports` | 现有 | 0 | ✅(系统路径) | 安全 | ✅ | 本机无 |
| 57 | DirectX 着色器缓存 | `%LOCALAPPDATA%\D3DSCache` | 现有 | 2.8 MiB（72） | ❌ | 安全 | ✅ | 自动重建 |
| 58 | AMD 着色器缓存 | `%LOCALAPPDATA%\AMD\DxCache;DX9Cache;DxcCache;OglCache;VkCache;CN` | **新增** | 合计 **257 MiB**（OglCache 156.6 / DxCache 60.1 / CN 22.2 / DX9Cache 13.8 / DxcCache 3.4 / VkCache 0.8） | ❌ | 安全 | ✅ | **本机为 AMD 显卡**；NVIDIA 模板在本机全为 0 |
| 59 | 缩略图缓存（**文件**通配） | `%LOCALAPPDATA%\Microsoft\Windows\Explorer\thumbcache_*.db` | **新增** | **80.2 MiB**（15 文件） | ❌ | 安全 | ✅ | 需重启 Explorer 生效，之后自动重建 |
| 60 | 图标缓存（**文件**通配） | `%LOCALAPPDATA%\Microsoft\Windows\Explorer\iconcache_*.db` | **新增** | 4.0 MiB（15 文件） | ❌ | 安全 | ✅ | 同上 |
| 61 | Squirrel/Electron 更新临时目录 | `%LOCALAPPDATA%\SquirrelTemp` | **新增** | **314 MiB**（4,646） | ❌ | 安全 | ✅ | 更新残留，随时可删 |
| 62 | 系统日志 | `C:\Windows\Logs`、`C:\Windows\System32\LogFiles` | **新增** | 92.8 + 71.6 = **164 MiB** | ✅ | 谨慎 | ❌ | 排障时可能要；CBS 子目录现有条目已覆盖 |
| 63 | 更新协调器日志 | `C:\ProgramData\USOPrivate`、`USOShared` | **新增** | 7.9 + 80.4 = **88 MiB** | ✅ | 谨慎 | ❌ | 更新历史/日志，可重建 |
| 64 | 事件日志 | `C:\Windows\System32\winevt\Logs` | **不入** | **500 MiB**（411） | ✅ | 危险 | ❌ | 删需停 EventLog 服务，破坏审计（见 §4） |
| 65 | Google 更新器 CRX 缓存 | `C:\Program Files (x86)\Google\GoogleUpdater\crx_cache` | **新增** | **523 MiB**（4 文件） | ✅ | 安全 | ❌(需提权) | Program Files 只读，提权后可删，自动重下 |
| 66 | UWP LocalCache/TempState | `%LOCALAPPDATA%\Packages\*\LocalCache`、`*\TempState` | 现有 | 45.5 + 6.1 MiB（147/147 目录） | ❌ | 谨慎 | ❌ | 现有条目风险等级合理 |
| 67 | UWP LocalState | `%LOCALAPPDATA%\Packages\*\LocalState` | **不入** | **892 MiB**（148 目录） | ❌ | 危险 | ❌ | 应用**用户数据**（登录/存档），非缓存 |
| 68 | Windows.old / `$WINDOWS.~BT` / `~WS` | 现有模板 | 现有 | 0（本机不存在） | ✅ | 谨慎 | ❌ | — |
| 69 | 回收站 C: / D: / E: | 现有 RecycleBin 项 | 现有 | C 5.0 MiB（80）/ D 5.5 MiB（7）/ E 16.5 MiB（100） | ❌（本人）/✅（他人盘） | 安全 | ✅ | 现有实现只清当前盘，建议按盘符列出 |
| 70 | 字体缓存 | `C:\Windows\System32\FNTCACHE.DAT` 0.7 MiB；`C:\Windows\ServiceProfiles\LocalService\...\FontCache` 拒绝访问 | **不入** | ≈0.7 MiB | ✅ | 危险 | ❌ | 需停 FontCache 服务，收益极低（见 §4） |
| 71 | Windows\Installer | `C:\Windows\Installer` | **不入** | **7.88 GiB**（1,012） | ✅ | 危险 | ❌ | 删=程序无法修复/卸载（见 §4） |
| 72 | WinSxS | `C:\Windows\WinSxS` | **不入** | 未统计（仅枚举到 29,340 个顶层项） | ✅ | 危险 | ❌ | 只能用 `DISM /StartComponentCleanup` |
| 73 | pagefile / hiberfil | `C:\pagefile.sys` 18.0 GiB、`C:\hiberfil.sys` 11.1 GiB | **不入** | 29.1 GiB | ✅ | 危险 | ❌ | 属系统设置（`powercfg /h off`），不是缓存 |

### 2.5 常用应用

| # | 清理项 | 路径模板 | 来源 | 实测大小 | 需管理员 | 风险 | 默认勾选 | 备注 |
|---|---|---|---|---|---|---|---|---|
| 74 | WPS 组件缓存 | `%APPDATA%\kingsoft\wps\addons;%APPDATA%\kingsoft\wps_international\addons` | **新增** | **11.24 GiB**（`wps\addons` 4.80 GiB：pool 2.93+data 1.87；`wps_international\addons` 6.44 GiB：pool 4.00+data 2.44） | ❌ | 谨慎 | ❌ | 关闭 WPS 后删；首次用某功能会重新下载组件 |
| 75 | WPS 旧版本目录 | `%LOCALAPPDATA%\Kingsoft\WPS Office\<旧版本号>` | **不入（手工）** | 合计 3.36 GiB（12.1.0.28043 1.69 GiB / .23542 1.53 GiB / .21915 104 MiB / .21541 30 MiB） | ❌ | 谨慎 | ❌ | **不能做通配项**：必须白名单保留当前安装版本，否则删掉 WPS 本体 |
| 76 | Steam htmlcache | `%LOCALAPPDATA%\Steam\htmlcache` | **新增** | 48.5 MiB（410） | ❌ | 安全 | ✅ | 自动重建；本机 Steam 未装在 C 盘（`shadercache` 不存在） |
| 77 | QQ 频道缓存 | `%LOCALAPPDATA%\Tencent\QQGuild` | **新增（可选）** | 380 MiB（124） | ❌ | 谨慎 | ❌ | QQ 组件缓存，可能含少量数据 |
| 78 | Feishu / Teams / Slack / Discord / 微信 / QQ 文档 / 钉钉 / 网易云 / Adobe / Spotify | 现有模板 | 现有 | 0（本机未安装或非默认路径） | — | — | — | ⚠️ Feishu 实际在 `%LOCALAPPDATA%\Feishu\app` 1.54 GiB，**那是程序本体，禁止采集** |

### 2.6 开发辅助

| # | 清理项 | 路径模板 | 来源 | 实测大小 | 需管理员 | 风险 | 默认勾选 | 备注 |
|---|---|---|---|---|---|---|---|---|
| 79 | Playwright 浏览器 | `%LOCALAPPDATA%\ms-playwright` | 现有 | **1.53 GiB**（3,123） | ❌ | 谨慎 | ❌ | 需 `playwright install` 重下（现有已标谨慎） |
| 80 | Puppeteer 浏览器 | `%USERPROFILE%\.cache\puppeteer` | 现有 | **1.65 GiB**（944） | ❌ | 谨慎 | ❌ | 重下 Chromium |
| 81 | Playwright MCP 浏览器 | `%LOCALAPPDATA%\ms-playwright-mcp` | **新增** | 95.9 MiB（1,069） | ❌ | 安全 | ✅ | 自动重下 |
| 82 | 其他 CLI 缓存 | `%USERPROFILE%\.cache\huggingface`、`.cache\chrome-devtools-mcp`、`.cache\selenium` | **新增（可选）** | 141.6 + 39.0 + 34.1 = **215 MiB** | ❌ | 谨慎 | ❌ | 逐个可重下；`~/.cache` 合计 1.98 GiB |
| 83 | Docker Desktop 数据 | `%LOCALAPPDATA%\Docker\wsl\disk\docker_data.vhdx` 3.54 GiB、`...\wsl\main\ext4.vhdx` 119 MiB | **不入** | **3.66 GiB** | ❌ | 危险 | ❌ | 运行中无法删；删=丢镜像/容器（见 §4）。`%LOCALAPPDATA%\Docker\log` 79.8 MiB 可删（安全） |
| 84 | WSL 发行版 vhdx | 本机 `Ubuntu-26.04` 位于 **E:\WSL\Ubuntu-26.04**（注册表 Lxss） | **不入** | 不在 C 盘 | — | 危险 | ❌ | 用 `wsl --export/--unregister`；C 盘仅有 `docker-desktop` 数据 |
| 85 | Android AVD / Android SDK | `%USERPROFILE%\.android\avd`、`%LOCALAPPDATA%\Android\Sdk` | 现有(IDE 段) | 0（未安装） | ❌ | 危险 | ❌ | 现有条目未覆盖 AVD，建议明确排除 |

---

## 3. 通配路径明细（目录通配 vs 文件通配）

| 模板 | 类型 | 匹配数 | 总大小 | 说明 |
|---|---|---|---|---|
| `%LOCALAPPDATA%\JetBrains\*\caches` | 目录通配 | 1 目录 | 157 MiB | 按产品名匹配 |
| `%LOCALAPPDATA%\JetBrains\*\index` | 目录通配 | 1 | 480 MiB | — |
| `%LOCALAPPDATA%\JetBrains\*\log` | 目录通配 | 1 | 2.0 MiB | — |
| `%APPDATA%\Microsoft\VisualStudio\*\ComponentModelCache` | 目录通配 | 2 | 12.6 MiB | VS 多实例 |
| `%LOCALAPPDATA%\Microsoft\Edge\User Data\*\Cache` | 目录通配 | 2（Default, Profile 1） | 70.2 MiB | **必须挂在 `\Cache` 等子目录后**，不能 `User Data\*` |
| `...\Edge\User Data\*\Code Cache` | 目录通配 | 2 | 35.9 MiB | 路径含空格 |
| `...\Edge\User Data\*\GPUCache` | 目录通配 | 2 | 0.5 MiB | — |
| `...\Edge\User Data\*\Service Worker\CacheStorage` | 目录通配 | 2 | 261.5 MiB | **本机 Edge 最大可清缓存** |
| `...\Chrome\User Data\*\Cache` / `*\Code Cache` / `*\GPUCache` | 目录通配 | 1（Default） | 0 / 11.1 / 1.6 MiB | Chrome `Default\Cache` 为空 |
| `%LOCALAPPDATA%\Packages\*\LocalCache` | 目录通配 | 147 目录 | 45.5 MiB | UWP |
| `%LOCALAPPDATA%\Packages\*\TempState` | 目录通配 | 147 | 6.1 MiB | — |
| `%LOCALAPPDATA%\Packages\*\AC\Temp` | 目录通配 | 184 | ≈0 | 多为空壳 |
| `%LOCALAPPDATA%\Packages\*\LocalState` | 目录通配 | 148 | **892 MiB** | **危险：用户数据**，不要采集 |
| `%LOCALAPPDATA%\Microsoft\Windows\Explorer\thumbcache_*.db` | **文件通配** | 15 文件 | 80.2 MiB | 单个 db 文件名带通配 |
| `%LOCALAPPDATA%\Microsoft\Windows\Explorer\iconcache_*.db` | **文件通配** | 15 文件 | 4.0 MiB | 同上 |
| `%USERPROFILE%\.vscode\extensions\ms-vscode.cpptools-*` | 目录通配 | 3 | 270 MiB | 扩展本体，非缓存 |
| `%LOCALAPPDATA%\Google\AndroidStudio*\caches` | 目录通配 | 0 | — | 未安装 |

> 现有 `ExpandTemplates()` 对"文件名内通配"（`thumbcache_*.db`）需要支持"命中文件"而非"命中目录后清空整个目录"；否则要么匹配失败，要么误删整个 `Explorer` 目录（该目录 86.7 MiB，含 `iconcache`、`thumbcache` 与设置）。**这是本次唯一需要扩展清理语义的点。**

---

## 4. 危险项与陷阱（看起来能删，实际不能）

| # | 目录/对象 | 本机实测 | 为什么危险 | 替代做法 |
|---|---|---|---|---|
| D1 | `C:\Windows\Installer` | 7.88 GiB | 所有 MSI 补丁缓存；删后程序**无法修复/卸载/升级**，且无法安全重建 | 用 `PatchCleaner` 类工具按 orphan 清单清理；不要进目录 |
| D2 | `C:\Windows\WinSxS` | 未统计 | 组件存储与硬链接，手删必坏系统 | `DISM /Online /Cleanup-Image /StartComponentCleanup` |
| D3 | `winevt\Logs` | 500 MiB | 事件日志需停 EventLog 服务才能删，破坏审计与排障 | 只 `wevtutil cl` 指定日志（需管理员），不做文件删除 |
| D4 | `%LOCALAPPDATA%\Docker`（vhdx） | 3.66 GiB | 运行中的镜像/容器/卷；删=数据全丢且 Docker 需重置 | `docker system prune -a --volumes`，或 Docker Desktop"清理磁盘" |
| D5 | WSL `ext4.vhdx` | 不在 C 盘 | 发行版根文件系统，删=发行版丢失 | `wsl --export` 备份后 `wsl --unregister` |
| D6 | `%APPDATA%\Code\User` 根 / `globalStorage` / `Partitions` | 7.71 / 1.58 / 0.31 GiB | 含设置、登录态、扩展数据；删根=IDE 重置 | 只删明确子目录（workspaceStorage/Crashpad/History），并在文案说明"会丢工作区状态" |
| D7 | `%USERPROFILE%\.vscode\extensions` | 3.73 GiB | 扩展本体，非缓存；删=全部扩展消失需重装重登 | 不采集；或仅提示"用 VS Code 自带扩展清理" |
| D8 | `%APPDATA%\kingsoft\*` 的 addons | 11.24 GiB | 是 WPS 插件/组件池，不是纯缓存；删后部分功能按钮首次点击会失败并重下；必须确认 WPS 全部退出（`wps.exe`、`ksolaunch.exe`、`wpscloudsvr.exe`） | 标"谨慎/需关闭 WPS"，默认不勾选 |
| D9 | `%LOCALAPPDATA%\Kingsoft\WPS Office\<版本号>` | 3.36 GiB | 版本号目录里**含当前运行版本**，通配删除=WPS 打不开 | 白名单保留当前版本；不做通配项 |
| D10 | `%LOCALAPPDATA%\Feishu\app` | 1.54 GiB | 程序本体（不是缓存） | 不采集 |
| D11 | `%LOCALAPPDATA%\Packages\*\LocalState` | 892 MiB | UWP 用户数据（登录/存档/配置） | 不采集 |
| D12 | `%APPDATA%\Microsoft\Edge\User Data` 根、`Default\Extensions`、`ExtensionActivityEdge` | 1.67 GiB / 216 MiB / 318 MiB | 用户数据、扩展、活动记录 | 只清 `Cache`/`Code Cache`/`GPUCache`/`Service Worker\CacheStorage`/`component_crx_cache` |
| D13 | 微信 `Documents\WeChat Files` / `xwechat_files`、QQ `Documents\Tencent Files` | 本机不存在 | 聊天记录=用户数据；本机路径为空不代表其他机器为空 | 现有模板只清 `FileStorage\Cache`/`xwechat_files\*\cache` 是正确做法，**保持 Caution 且默认不勾**，并在 UI 明示"不含聊天记录" |
| D14 | 字体缓存 `FNTCACHE.DAT` / `ServiceProfiles\LocalService\...\FontCache` | 0.7 MiB / 拒访问 | 需停 FontCache 服务，删后短时字体渲染异常，收益≈0 | 不采集 |
| D15 | `C:\Windows\Temp` | 7.7 MiB | Users 仅 CreateFiles，**别人/Antivirus 创建的文件删不掉**，会大量失败 | 标"部分需管理员"，删失败要计数而不是报错 |
| D16 | `C:\Windows\Prefetch` | 枚举被拒 | **非管理员枚举也被拒**，不能显示 0 B | 标 needsAdmin，未提权时显示"需管理员"占位 |
| D17 | pagefile / hiberfil | 29.1 GiB | 系统设置，不是缓存 | 说明文字引导：`powercfg /h off`、虚拟内存设置 |
| D18 | 旧版 Windows 升级残留 `$WINDOWS.~BT/~WS`、Windows.old | 本机不存在 | 现有 Caution 合理；Windows.old 删后无法回退 | 保持默认不勾 |

**"删了会自动重建"速查**：VSCode Cache/CachedData/GPUCache/Crashpad/CachedExtensionVSIXs、浏览器 Cache/Code Cache/GPUCache/component_crx、JetBrains caches/index/log、Gradle/Maven/pip/npm/pnpm 缓存、D3DSCache 与 GPU 着色器缓存、缩略图/图标缓存（重启 Explorer 后）、SquirrelTemp、Windows\temp 与 %TEMP%（下次运行）、WER/Minidump（下次崩溃）、Windows Update Download（下次检查更新）。
**"需要重新下载/重新登录"**：Playwright/Puppeteer 浏览器、VS/Package Cache（修复时）、WPS addons（按功能）、Android SDK/AVD、VSCode globalStorage 内的扩展运行时。

---

## 5. 可直接落地的 Entry 清单（沿用现有 `EntryDef` 宏，按收益排序）

```cpp
    // ── 包管理器：修正 + 补充 ──
    // 修正：npm 在 Windows 的默认缓存是 %LOCALAPPDATA%，不是 %APPDATA%（本机实测 2.58 GiB vs 0）
    CACHE_DIR(L"npm 缓存",            L"%LOCALAPPDATA%\\npm-cache",          Package, false, true,  Safe,    L"npm tarball 与 _npx 缓存，自动重建"),
    CACHE_DIR(L"pnpm 元数据缓存",     L"%LOCALAPPDATA%\\pnpm-cache",         Package, false, true,  Safe,    L"registry 元数据缓存，自动重建"),
    CACHE_DIR(L"node-gyp 缓存",       L"%LOCALAPPDATA%\\node-gyp",           Package, false, true,  Safe,    L"Node 原生模块编译缓存"),
    CACHE_DIR(L"Gradle 分发缓存",     L"%USERPROFILE%\\.gradle\\wrapper",     Build,   false, false, Caution, L"删除后重新下载 Gradle 发行包"),

    // ── IDE：VSCode 补齐（现有只覆盖了 Cache/CachedData/VSIX/GPUCache/logs） ──
    CACHE_DIR(L"VSCode 工作区状态",   L"%APPDATA%\\Code\\User\\workspaceStorage", Ide, false, false, Caution, L"工作区布局与扩展缓存，重开自动重建"),
    CACHE_DIR(L"VSCode 全局扩展存储", L"%APPDATA%\\Code\\User\\globalStorage",     Ide, false, false, Caution, L"部分扩展会丢本地状态或需重新登录"),
    CACHE_DIR(L"VSCode WebStorage",   L"%APPDATA%\\Code\\WebStorage",             Ide, false, false, Caution, L"webview 存储，面板会重新加载"),
    CACHE_DIR(L"VSCode 崩溃转储",     L"%APPDATA%\\Code\\Crashpad",               Ide, false, true,  Safe,    L"崩溃报告，自动重建"),
    CACHE_DIR(L"VSCode 本地历史",     L"%APPDATA%\\Code\\User\\History",          Ide, false, false, Caution, L"文件本地历史，用户可能仍需撤销"),
    CACHE_DIR(L"VSCode Partitions",   L"%APPDATA%\\Code\\Partitions",             Ide, false, false, Caution, L"多 profile 浏览器数据"),
    CACHE_DIR(L"Package Cache（用户级）", L"%LOCALAPPDATA%\\Package Cache",       Ide, false, false, Caution, L"安装器缓存，修复时会重新下载"),
    CACHE_DIR(L"Package Cache（全机）",   L"%ProgramData%\\Package Cache",        Ide, true,  false, Caution, L"安装器缓存，需管理员"),
    CACHE_DIR(L"Trae CN 缓存",        L"%APPDATA%\\Trae CN\\Cache;%APPDATA%\\Trae CN\\CachedData;%APPDATA%\\Trae CN\\GPUCache;%APPDATA%\\Trae CN\\logs;%APPDATA%\\Trae CN\\WebStorage;%APPDATA%\\Trae CN\\Partitions", Ide, false, false, Caution, L"VSCode 系编辑器缓存，建议先退出"),

    // ── 浏览器：组件缓存 ──
    CACHE_DIR(L"Edge 组件缓存",       L"%LOCALAPPDATA%\\Microsoft\\Edge\\User Data\\component_crx_cache",   Browser, false, true, Safe, L"组件包，自动重下"),
    CACHE_DIR(L"Chrome 组件缓存",     L"%LOCALAPPDATA%\\Google\\Chrome\\User Data\\component_crx_cache",     Browser, false, true, Safe, L"组件包，自动重下"),
    CACHE_DIR(L"Chrome 优化模型缓存", L"%LOCALAPPDATA%\\Google\\Chrome\\User Data\\optimization_guide_model_store", Browser, false, true, Safe, L"优化指南模型，自动重下"),

    // ── 系统：补齐 ──
    CACHE_DIR(L"缩略图缓存",          L"%LOCALAPPDATA%\\Microsoft\\Windows\\Explorer\\thumbcache_*.db", System, false, true, Safe, L"文件通配；重启资源管理器后自动重建"),
    CACHE_DIR(L"图标缓存",            L"%LOCALAPPDATA%\\Microsoft\\Windows\\Explorer\\iconcache_*.db",   System, false, true, Safe, L"文件通配；重启资源管理器后自动重建"),
    CACHE_DIR(L"Squirrel 更新临时文件", L"%LOCALAPPDATA%\\SquirrelTemp",           System, false, true,  Safe,    L"Electron 应用更新残留"),
    CACHE_DIR(L"AMD 着色器缓存",      L"%LOCALAPPDATA%\\AMD\\DxCache;%LOCALAPPDATA%\\AMD\\DX9Cache;%LOCALAPPDATA%\\AMD\\DxcCache;%LOCALAPPDATA%\\AMD\\OglCache;%LOCALAPPDATA%\\AMD\\VkCache;%LOCALAPPDATA%\\AMD\\CN", System, false, true, Safe, L"AMD 显卡着色器缓存，自动重建"),
    CACHE_DIR(L"系统日志",            L"%SystemRoot%\\Logs;%SystemRoot%\\System32\\LogFiles", System, true, false, Caution, L"排障可能需要，需管理员"),
    CACHE_DIR(L"更新协调器日志",      L"%ProgramData%\\USOPrivate;%ProgramData%\\USOShared",   System, true, false, Caution, L"更新日志，可重建，需管理员"),
    CACHE_DIR(L"Google 更新器缓存",   L"%ProgramFiles(x86)%\\Google\\GoogleUpdater\\crx_cache", System, true, false, Safe, L"需管理员（Program Files 只读）"),
    // 修正：传递优化真实路径
    CACHE_DIR(L"传递优化缓存",        L"%SystemRoot%\\ServiceProfiles\\NetworkService\\AppData\\Local\\Microsoft\\Windows\\DeliveryOptimization", System, true, true, Safe, L"更新分发缓存，需管理员"),
    // 修正：VSCode profile 缓存实际目录名带 s
    CACHE_DIR(L"VSCode CachedProfileData", L"%APPDATA%\\Code\\CachedProfileData;%APPDATA%\\Code\\CachedProfilesData", Ide, false, true, Safe, L"VSCode 配置文件缓存"),

    // ── 应用 ──
    CACHE_DIR(L"WPS 组件缓存",        L"%APPDATA%\\kingsoft\\wps\\addons;%APPDATA%\\kingsoft\\wps_international\\addons", App, false, false, Caution, L"关闭 WPS 后清理；功能组件按需重新下载"),
    CACHE_DIR(L"Steam html 缓存",     L"%LOCALAPPDATA%\\Steam\\htmlcache",        App, false, true,  Safe,    L"内嵌浏览器缓存，自动重建"),
    CACHE_DIR(L"QQ 频道缓存",         L"%LOCALAPPDATA%\\Tencent\\QQGuild",        App, false, false, Caution, L"QQ 组件缓存，建议先退出 QQ"),

    // ── 开发辅助 ──
    CACHE_DIR(L"Playwright MCP 浏览器", L"%LOCALAPPDATA%\\ms-playwright-mcp",     Dev, false, true,  Safe,    L"自动重新下载"),
    CACHE_DIR(L"CLI 工具缓存",        L"%USERPROFILE%\\.cache\\huggingface;%USERPROFILE%\\.cache\\chrome-devtools-mcp;%USERPROFILE%\\.cache\\selenium", Dev, false, false, Caution, L"模型/工具下载缓存，可重下"),
    CACHE_DIR(L"Docker 日志",         L"%LOCALAPPDATA%\\Docker\\log",             Dev, false, true,  Safe,    L"仅日志；镜像/容器见危险项，不做删除"),
```

> 与现有设计一致：`Risk::Safe` 才 `defaultChecked=true`；`needsAdmin=true` 时未提权只统计不删除。

---

## 6. 需要修正的现有条目（本机实测证据）

| 现有条目 | 现有模板 | 实测问题 | 建议 |
|---|---|---|---|
| npm 缓存 | `%APPDATA%\npm-cache` | 本机该路径 0；真实缓存在 `%LOCALAPPDATA%\npm-cache` **2.58 GiB**（`_npx` 2.02 GiB） | 改 `%LOCALAPPDATA%\npm-cache`（可同时保留旧模板，兼容旧版 npm） |
| pnpm 存储（v8） | `%LOCALAPPDATA%\pnpm-store` | 不存在；v9 实际是 `%LOCALAPPDATA%\pnpm\store` ✓ + 另一处 `pnpm-cache` **1.04 GiB** 未覆盖 | 增加 `pnpm-cache` 条目 |
| pnpm 存储（v9+） | `%LOCALAPPDATA%\pnpm\store` | 正确，1.52 GiB | 保留 |
| Visual Studio 安装缓存 | `%ProgramData%\Microsoft\VisualStudio\Packages` | 本机实测 **3.89 GiB / 1,898 文件**，但 `build/scan.json` 报 `bytes:0, files:0` | 复核该路径是否被权限枚举逻辑跳过（其余 needsAdmin 项如 WU 缓存能正常统计） |
| 传递优化缓存 | `%SystemRoot%\SoftwareDistribution\DeliveryOptimization` | **路径不存在**；真实路径在 `ServiceProfiles\NetworkService\...`，普通权限 Access denied | 改真实路径并标 needsAdmin |
| VSCode CachedProfileData | `%APPDATA%\Code\CachedProfileData` | 不存在；实际目录名 **`CachedProfilesData`**（2.4 MiB） | 双模板 |
| NVIDIA 着色器缓存 | `%LOCALAPPDATA%\NVIDIA\DXCache;GLCache` | 本机为 0（AMD 显卡）；AMD 着色器缓存 257 MiB 未覆盖 | 保留 NVIDIA + 新增 AMD 条目 |
| Go 构建/模块缓存、Conda、uv、yarn、vcpkg、cargo、Composer、Deno、Bun、Conan、ccache、CMake、Android Studio、Firefox、Teams/Slack/Discord/微信/网易云/Spotify/Adobe | 现有模板 | 本机均 0（未安装/环境变量改到 D 盘：`GOCACHE=D:\GoData\cache`、conda 在 `D:\anaconda3`、Yarn 在 E:） | 条目本身无需删，但**文案**应写"未安装则显示为空" |
| 微信 | `...\WeChat Files\*` | 本机不存在；模板只清 `FileStorage\Cache` 是正确且必要的保守做法 | 保留 Caution + 默认不勾 |

---

## 7. 路径模板上的坑（写程序时逐个踩过）

1. **环境变量**：只用 `%LOCALAPPDATA%`/`%APPDATA%`/`%USERPROFILE%`/`%TEMP%`/`%SystemRoot%`/`%SystemDrive%`/`%ProgramData%`；**不要**出现 `C:\Users\14530`。注意 `%SystemRoot%\Temp` ≠ `%TEMP%`，两者需分开两项（一个需管理员）。
2. **大小写**：Windows 不区分大小写（本机实际是 `pip\Cache`、`Explorer\thumbcache_*.db`），模板写 `cache` 也能命中；但**遍历/比较时**不要用大小写敏感字符串比较。
3. **空格**：`Package Cache`、`Code Cache`、`Service Worker`、`Temporary Internet Files`、`Program Files (x86)`、`C:\Program Files` 均含空格；展开后的路径**只能**作为 API 参数（`FindFirstFileExW`/`GetFileAttributesW`），不要拼到命令行里（现有实现全部走 Win32 API，OK）。
4. **通配语义**：`*` 只匹配一段；`User Data\*\Cache` 安全（只命中 Cache 目录），但 `User Data\*` 会命中 `component_crx_cache`、`Extensions`、`Local Storage` 等 → **禁止把通配放在会删除的模板末尾**。当前 `ExpandTemplates` 对 `thumbcache_*.db` 这类**文件名通配**需要"匹配文件"语义，否则会退化成"匹配目录"。
5. **OneDrive 重定向**：本机 `Documents/Desktop/Pictures/Downloads` 均**未**重定向（无 ReparsePoint，`User Shell Folders` 指向本地）；但其他机器开启"已知文件夹移动"后，微信/QQ 记录会落在 `%USERPROFILE%\OneDrive\Documents\...`，硬编码 `%USERPROFILE%\Documents` 会漏清或清错。**建议**：读 `HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\User Shell Folders` 的 `Personal` 再拼子路径，且只删已知 cache 子目录。
6. **多用户**：只按当前用户展开；`C:\Users\Public`、`Default`、`All Users` 一律不碰；将来支持"所有用户"需提权并单独确认。
7. **重解析点（junction/symlink）**：遍历时**不要跟随**（避免跨盘误删）；本机 `uv`、`.cargo`、`JetBrains` 均非 junction，但用户可能把 npm/pnpm store 指到 D 盘（`pnpm config set store-dir`），跟随会扩大删除范围。
8. **非 C 盘工具**：`GOCACHE=D:\GoData\cache`、conda `D:\anaconda3\pkgs`（5.16 GiB）、Yarn/Node/Maven/Go 均在 D:/E:、WSL Ubuntu 在 `E:\WSL`。现有条目的"0"不代表路径错；若将来要"清 D 盘"，路径需按盘符参数化。
9. **长路径**：`%LOCALAPPDATA%\Packages\<包名>\LocalState\...` 可能超 260 字符；建议扫描/删除统一加 `\\?\` 前缀（现有实现需确认）。
10. **占用中文件**：`%TEMP%`、浏览器 `Cache`、`log` 常被占用 → 失败要"跳过并计数"，不能整体报错；同时这些目录实测值会偏小（本次 `%TEMP%` 仅 54.3 MiB 即因 318 个可枚举文件之外的被占用文件被跳过）。
11. **需管理员的判定不能只看路径前缀**：`C:\Windows\Temp` 是"部分可删"（Users 有 CreateFiles 但无 Delete），`C:\ProgramData\USOShared` Users 有 `Write` 但无删除权限；一律按 **ACL 实测**或"尝试打开句柄失败"判定，而不是硬编码 `C:\Windows\* → admin`。

---

## 8. 测量方法与局限

- 统一使用 `Get-ChildItem -Recurse -Force -ErrorAction SilentlyContinue | Measure-Object Length -Sum`；单目录耗时 0.1–7 s，最大 `%LOCALAPPDATA%\Packages` 10,786 文件/6 s。
- **未完整统计**：`C:\Windows\WinSxS`（仅枚举 29,340 个顶层项，未统计总大小）、`C:\Windows\Prefetch`（枚举 Access denied）、`C:\Windows\ServiceProfiles\LocalService|NetworkService` 下 FontCache/DeliveryOptimization（Access denied）、Docker vhdx 内部（只计文件大小，未挂载）。
- **只统计了当前用户**与 C 盘；D:/E: 仅抽样（回收站、conda、WSL、Yarn），未做全盘扫描。
- 大小为**单次快照**；浏览器/IDE/临时目录随运行波动，`scan.json` 与本文档的差异主要来自"运行状态"和"通配展开层次"（例如 Edge 现有条目 386 MB vs 本文 368 MiB，差异来自 `ShaderCache`/`GrShaderCache` 的匹配情况）。
