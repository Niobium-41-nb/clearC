/*
 * clearC - 清理项目录实现
 *
 * 条目来源与安全分级依据见 docs/cache-candidates.md（本机逐项实测）
 */
#include "catalog.h"

namespace clearc {
namespace {

// 目录型清理项；路径支持 ';' 多模板、%ENV% 与通配符
// CONSEQ 描述清理后的具体后果，界面点击名称时展示，见 consequence 字段
#define CACHE_DIR(NAME, PATHS, CAT, ADMIN, DEFAULT_ON, RISK, NOTE, CONSEQ) \
    { NAME, PATHS, Category::CAT, ADMIN, DEFAULT_ON, Risk::RISK, NOTE, CONSEQ, \
      EntryKind::Directory }

// 文件通配型清理项：只删除命中的文件本身，不会清空整个父目录
#define CACHE_FILE(NAME, PATHS, CAT, ADMIN, DEFAULT_ON, RISK, NOTE, CONSEQ) \
    { NAME, PATHS, Category::CAT, ADMIN, DEFAULT_ON, Risk::RISK, NOTE, CONSEQ, \
      EntryKind::FileGlob }

const EntryDef kEntries[] = {
    // ── 包管理器缓存 ──
    CACHE_DIR(L"uv 包缓存",             L"%LOCALAPPDATA%\\uv\\cache",                      Package, false, true,  Safe,    L"uv 下载/构建缓存，自动重建",
              L"清理后 uv 会在下次安装或构建时重新下载依赖并重建缓存，首次相关命令明显变慢且需要网络；已创建的虚拟环境、项目文件和已安装的包不受影响。"),
    CACHE_DIR(L"pip 缓存",              L"%LOCALAPPDATA%\\pip\\cache",                     Package, false, true,  Safe,    L"pip wheel 缓存，自动重建",
              L"清理后 pip 下载的 wheel 与 http 缓存需要重新获取，首次安装变慢；已安装的包不受影响。离线环境或内网源不可用时会安装失败。"),
    CACHE_DIR(L"Conda 包缓存",          L"%USERPROFILE%\\.conda\\pkgs;%LOCALAPPDATA%\\conda\\conda\\pkgs", Package, false, true, Safe, L"conda 已解压包缓存",
              L"清理后 conda 已解压的包缓存被移除，已创建的环境仍能正常运行；下次创建环境或安装包时需重新下载并解压，首次操作变慢。"),
    CACHE_DIR(L"NuGet 全局包",          L"%USERPROFILE%\\.nuget\\packages",                Package, false, true,  Safe,    L"已还原的 NuGet 包，需要时重新还原",
              L"清理后已还原的包被移除，下次打开或生成项目时会从包源重新还原，首次明显变慢；项目文件不受影响，离线或内网源不可用时会还原失败。"),
    CACHE_DIR(L"NuGet HTTP 缓存",       L"%LOCALAPPDATA%\\NuGet\\v3-cache;%LOCALAPPDATA%\\NuGet\\plugins-cache", Package, false, true, Safe, L"NuGet 源响应缓存",
              L"清理后 NuGet 需重新向包源请求索引与响应缓存，首次还原略慢；包源凭据、配置和已还原的包不受影响。"),
    CACHE_DIR(L"npm 缓存",              L"%LOCALAPPDATA%\\npm-cache;%APPDATA%\\npm-cache",  Package, false, true,  Safe,    L"npm tarball 与 _npx 缓存（新版在 LocalAppData）",
              L"清理后 npm 的 tarball 与 _npx 缓存需重新下载，首次安装变慢；已存在的 node_modules 与项目文件不受影响，离线状态下 npx / npm install 可能失败。"),
    CACHE_DIR(L"yarn 缓存",             L"%LOCALAPPDATA%\\Yarn\\Cache;%LOCALAPPDATA%\\Yarn\\Berry\\cache", Package, false, true, Safe, L"yarn 1 / Berry 下载缓存",
              L"清理后 yarn 1 / Berry 的下载缓存需重新获取，首次安装变慢；项目依赖与 yarn.lock 不受影响，离线安装可能无法进行。"),
    CACHE_DIR(L"pnpm 存储（v8）",       L"%LOCALAPPDATA%\\pnpm-store",                     Package, false, true,  Safe,    L"pnpm 内容寻址存储",
              L"清理后内容寻址存储被移除，已有项目中依赖该存储的 node_modules 链接可能失效，需重新执行 pnpm install；会重新占用磁盘并需要网络。"),
    CACHE_DIR(L"pnpm 存储（v9+）",      L"%LOCALAPPDATA%\\pnpm\\store",                    Package, false, true,  Safe,    L"pnpm 新版本存储目录",
              L"清理后新版 pnpm 的内容寻址存储被移除，已有项目的 node_modules 链接可能失效，需重新执行 pnpm install 并重新下载全部依赖。"),
    CACHE_DIR(L"pnpm 元数据缓存",       L"%LOCALAPPDATA%\\pnpm-cache",                     Package, false, true,  Safe,    L"registry 元数据缓存，自动重建",
              L"清理后 registry 元数据会重新拉取，首次安装略慢；已下载的包体和项目文件不受影响。"),
    CACHE_DIR(L"vcpkg 下载缓存",        L"%LOCALAPPDATA%\\vcpkg\\archives",                Package, false, true,  Safe,    L"vcpkg 下载归档",
              L"清理后 vcpkg 下载的源码归档需重新下载，首次构建变慢且需要网络；已安装的库与 vcpkg.json 清单不受影响。"),
    CACHE_DIR(L"vcpkg 构建树",          L"%LOCALAPPDATA%\\vcpkg\\buildtrees",              Package, false, true,  Safe,    L"vcpkg 构建中间产物",
              L"清理后未完成的构建中间产物被移除，正在进行的 vcpkg install 可能需要从头重来；已安装的库与项目不受影响。"),
    CACHE_DIR(L"Go 构建缓存",           L"%LOCALAPPDATA%\\go-build",                       Package, false, true,  Safe,    L"go build 编译缓存",
              L"清理后 go build 需要重新编译所有包，首次全量构建明显变慢；源码、模块缓存与构建结果配置不受影响。"),
    CACHE_DIR(L"Go 模块下载缓存",       L"%USERPROFILE%\\go\\pkg\\mod\\cache",             Package, false, true,  Safe,    L"go mod 下载缓存（源码模块保留）",
              L"清理后模块下载缓存被移除，下次构建需重新下载模块；源码模块本体保留，离线环境或私有仓库不可达时构建可能失败。"),
    CACHE_DIR(L"Cargo 注册表缓存",      L"%USERPROFILE%\\.cargo\\registry\\cache",         Package, false, true,  Safe,    L"crates.io 包缓存",
              L"清理后已下载的 crate 归档需重新从注册表拉取，首次构建变慢；Cargo.lock 与项目源码不受影响，离线构建会失败。"),
    CACHE_DIR(L"Cargo Git 缓存",        L"%USERPROFILE%\\.cargo\\git\\db",                 Package, false, true,  Safe,    L"git 依赖裸仓库缓存",
              L"清理后 git 依赖的裸仓库需重新克隆，首次构建变慢；依赖内网或私有仓库时可能需要重新认证，项目源码不受影响。"),
    CACHE_DIR(L"Maven 本地仓库",        L"%USERPROFILE%\\.m2\\repository",                 Package, false, false, Caution, L"删除后首次构建需重新下载全部依赖",
              L"清理后所有依赖需从远程仓库重新下载，首次构建耗时显著增加；离线、内网仓库或已下线的 SNAPSHOT 版本可能无法恢复，项目代码不受影响。建议只在依赖可重新获取时勾选。"),
    CACHE_DIR(L"Composer 缓存",         L"%LOCALAPPDATA%\\Composer\\cache",                Package, false, true,  Safe,    L"PHP Composer 包缓存",
              L"清理后 PHP 包归档需重新下载，首次 composer install 变慢；项目 vendor 目录与 composer.lock 不受影响。"),
    CACHE_DIR(L"Deno 依赖缓存",         L"%LOCALAPPDATA%\\deno",                           Package, false, true,  Safe,    L"Deno 远程模块缓存",
              L"清理后远程模块需重新下载并重新编译，首次运行脚本变慢，离线运行会失败；本地源码与权限配置不受影响。"),
    CACHE_DIR(L"Bun 安装缓存",          L"%USERPROFILE%\\.bun\\install\\cache",            Package, false, true,  Safe,    L"Bun 包缓存",
              L"清理后 Bun 的包缓存需重新下载，首次 install 变慢；node_modules 与项目文件不受影响。"),
    CACHE_DIR(L"Chocolatey 安装缓存",   L"%LOCALAPPDATA%\\Temp\\chocolatey",               Package, false, true,  Safe,    L"choco 安装包临时目录",
              L"清理后 choco 的安装包临时文件被移除，重复安装同一版本时可能需重新下载；已安装的软件不受影响。"),
    CACHE_DIR(L"Scoop 下载缓存",        L"%USERPROFILE%\\scoop\\cache",                    Package, false, true,  Safe,    L"scoop 安装包缓存",
              L"清理后 scoop 的安装包缓存需重新下载，重复安装同一版本时变慢；已安装的应用与其数据不受影响。"),
    CACHE_DIR(L"Conan 2 包缓存",        L"%USERPROFILE%\\.conan2\\p",                      Package, false, true,  Safe,    L"conan 2 本地包缓存",
              L"清理后 Conan 本地包需重新构建或从远端重新拉取，首次构建明显变慢；配方、profile 与配置不受影响。"),
    CACHE_DIR(L"node-gyp 缓存",         L"%LOCALAPPDATA%\\node-gyp",                       Package, false, true,  Safe,    L"Node 原生模块编译缓存，重新编译时重建",
              L"清理后 Node 原生模块编译时会重新下载 Node 头文件，首次编译变慢且需要网络；已编译好的模块与项目不受影响。"),

    // ── 构建缓存 ──
    CACHE_DIR(L"Gradle 缓存",           L"%USERPROFILE%\\.gradle\\caches",                 Build,   false, true,  Safe,    L"Gradle 依赖与构建缓存",
              L"清理后 Gradle 的依赖与构建缓存需重新下载、重新执行任务，首次构建明显变慢；项目源码与 wrapper 配置不受影响。"),
    CACHE_DIR(L"Gradle 分发缓存",       L"%USERPROFILE%\\.gradle\\wrapper",                Build,   false, false, Caution, L"删除后重新下载 Gradle 发行包",
              L"清理后各项目会重新下载对应版本的 Gradle 发行包，首次构建需要网络且耗时较长；断网或无法访问分发服务时构建会失败。项目源码不受影响。"),
    CACHE_DIR(L"CMake 缓存",            L"%LOCALAPPDATA%\\CMake",                           Build,   false, true,  Safe,    L"CMake 下载的模块与缓存",
              L"清理后 CMake 下载的模块与缓存需重新获取，首次配置略慢；项目源码与已生成的构建目录不受影响。"),
    CACHE_DIR(L"ccache 缓存",           L"%LOCALAPPDATA%\\ccache",                          Build,   false, true,  Safe,    L"ccache 编译缓存",
              L"清理后编译缓存全部失效，首次全量编译会明显变慢；源码、构建产物与编译配置不受影响。"),

    // ── 编辑器 / IDE 缓存 ──
    CACHE_DIR(L"VSCode Cache",              L"%APPDATA%\\Code\\Cache",                     Ide, false, true, Safe, L"VSCode 缓存",
              L"清理后 VSCode 会重新生成页面缓存，首次启动或打开窗口略慢；扩展、用户设置与已打开的文件不受影响。"),
    CACHE_DIR(L"VSCode CachedData",         L"%APPDATA%\\Code\\CachedData",                Ide, false, true, Safe, L"VSCode 扩展宿主缓存",
              L"清理后扩展宿主的缓存代码会在下次启动时重建，首次启动略慢；扩展本体与用户设置不受影响。"),
    CACHE_DIR(L"VSCode CachedExtensions",   L"%APPDATA%\\Code\\CachedExtensions;%APPDATA%\\Code\\CachedExtensionVSIXs", Ide, false, true, Safe, L"VSCode 扩展 VSIX 缓存",
              L"清理后已缓存的 VSIX 需重新下载，离线状态下安装或更新扩展可能失败；已安装的扩展与设置不受影响。"),
    CACHE_DIR(L"VSCode CachedProfileData",  L"%APPDATA%\\Code\\CachedProfileData;%APPDATA%\\Code\\CachedProfilesData", Ide, false, true, Safe, L"VSCode 配置文件缓存",
              L"清理后配置档缓存会重建，首次切换配置档略慢；配置档本身的设置与扩展清单不受影响。"),
    CACHE_DIR(L"VSCode GPUCache",           L"%APPDATA%\\Code\\GPUCache",                  Ide, false, true, Safe, L"VSCode GPU 渲染缓存",
              L"清理后 GPU 渲染缓存会重建，首次显示窗口可能短暂卡顿；界面设置与用户数据不受影响。"),
    CACHE_DIR(L"VSCode 日志",               L"%APPDATA%\\Code\\logs",                      Ide, false, true, Safe, L"VSCode 运行日志",
              L"清理后历史运行日志不可再查阅，排查扩展或编辑器故障时会缺少记录；新日志会自动生成，设置与扩展不受影响。"),
    CACHE_DIR(L"VSCode C++ 缓存",           L"%APPDATA%\\Code\\User\\globalStorage\\ms-vscode.cpptools", Ide, false, true, Safe, L"cpptools 数据库与缓存",
              L"清理后 cpptools 的符号数据库需重新索引，大型项目首次的代码跳转与补全明显变慢；代码与 c_cpp_properties.json 配置不受影响。"),
    CACHE_DIR(L"VSCode 崩溃转储",           L"%APPDATA%\\Code\\Crashpad",                  Ide, false, true, Safe, L"崩溃报告，自动重建",
              L"清理后历史崩溃报告不可再用于排查问题；编辑器运行、扩展与用户数据不受影响，新的报告会自动生成。"),
    CACHE_DIR(L"VSCode 工作区状态",         L"%APPDATA%\\Code\\User\\workspaceStorage",    Ide, false, false, Caution, L"工作区布局与扩展缓存，重开自动重建（含 ipch）",
              L"清理后工作区布局、面板状态与部分扩展的本地缓存丢失，下次打开工作区会重新初始化；磁盘上的文件内容不受影响，但某些扩展的临时状态无法恢复。"),
    CACHE_DIR(L"VSCode 全局扩展存储",       L"%APPDATA%\\Code\\User\\globalStorage",       Ide, false, false, Caution, L"部分扩展会丢本地状态或需重新登录",
              L"清理后部分扩展会丢失本地状态，可能出现需要重新登录账号、重新授权的情况，首次启动需重新配置；扩展本体与 settings.json 不受影响。"),
    CACHE_DIR(L"VSCode WebStorage",         L"%APPDATA%\\Code\\WebStorage",                Ide, false, false, Caution, L"webview 存储，面板会重新加载",
              L"清理后 webview 的本地存储被清空，相关面板会重新加载，部分面板的登录态或界面状态需要重建；扩展本体与项目文件不受影响。"),
    CACHE_DIR(L"VSCode Partitions",         L"%APPDATA%\\Code\\Partitions",                Ide, false, false, Caution, L"多 profile 的浏览器数据（含 localStorage）",
              L"清理后各 profile 的浏览器数据（含 localStorage）丢失，依赖 webview 的扩展可能需要重新登录；主配置、设置与文件不受影响。"),
    CACHE_DIR(L"VSCode 本地历史",           L"%APPDATA%\\Code\\User\\History",             Ide, false, false, Caution, L"文件本地历史，用户可能还需要撤销",
              L"清理后文件的本地历史记录不可恢复，无法再通过「时间线」找回之前的版本；当前磁盘上的文件内容完全不受影响。若近期靠本地历史撤销过改动，请勿勾选。"),
    CACHE_DIR(L"Cursor 缓存",               L"%APPDATA%\\Cursor\\Cache;%APPDATA%\\Cursor\\CachedData;%APPDATA%\\Cursor\\GPUCache;%APPDATA%\\Cursor\\logs", Ide, false, true, Safe, L"Cursor 编辑器缓存",
              L"清理后 Cursor 会重建页面、代码与 GPU 缓存，首次启动略慢；建议先退出 Cursor，否则被占用的文件会自动跳过。设置、扩展与项目不受影响。"),
    CACHE_DIR(L"Trae CN 缓存",              L"%APPDATA%\\Trae CN\\Cache;%APPDATA%\\Trae CN\\CachedData;%APPDATA%\\Trae CN\\GPUCache;%APPDATA%\\Trae CN\\logs;%APPDATA%\\Trae CN\\WebStorage;%APPDATA%\\Trae CN\\Partitions", Ide, false, false, Caution, L"VSCode 系编辑器缓存，建议先退出",
              L"清理后 Trae CN 的 webview 存储与分区数据会重建，部分面板可能需要重新登录；建议先退出程序，设置与项目文件不受影响。"),
    CACHE_DIR(L"JetBrains 缓存",            L"%LOCALAPPDATA%\\JetBrains\\*\\caches",        Ide, false, true, Safe, L"IDEA/PyCharm 等缓存",
              L"清理后 IDE 会重建项目缓存，首次打开项目变慢；建议先退出 IDE，代码索引需另行重建。项目源码与配置不受影响。"),
    CACHE_DIR(L"JetBrains 索引",            L"%LOCALAPPDATA%\\JetBrains\\*\\index",         Ide, false, true, Safe, L"代码索引，重新打开项目时重建",
              L"清理后代码索引需全量重建，大型项目首次打开时补全、搜索与跳转会暂时不可用并明显变慢；源码与设置不受影响。"),
    CACHE_DIR(L"JetBrains 日志",            L"%LOCALAPPDATA%\\JetBrains\\*\\log",           Ide, false, true, Safe, L"IDE 运行日志",
              L"清理后历史 IDE 日志不可再查阅，排查插件或 IDE 故障时会缺少依据；新日志会自动生成。"),
    CACHE_DIR(L"Visual Studio 组件缓存",    L"%LOCALAPPDATA%\\Microsoft\\VisualStudio\\*\\ComponentModelCache", Ide, false, true, Safe, L"VS 组件模型缓存",
              L"清理后 Visual Studio 会在下次启动时重建组件模型缓存，首次启动变慢；解决方案、项目与用户设置不受影响。"),
    CACHE_DIR(L"Visual Studio 安装缓存",    L"%ProgramData%\\Microsoft\\VisualStudio\\Packages", Ide, true, false, Caution, L"VS 安装包缓存；若被重定向为 junction 会自动跳过",
              L"清理后修复、修改或卸载 Visual Studio 及安装器组件时会重新下载安装包，断网或源失效时可能无法完成修复；已安装的功能不受影响。若该目录被重定向为 junction，本工具会自动跳过。"),
    CACHE_DIR(L"Package Cache（全机）",     L"%ProgramData%\\Package Cache",               Ide, true,  false, Caution, L"安装器缓存，修复/卸载时会重新下载",
              L"清理后修复或卸载依赖这些安装包的程序时会重新下载，断网或源失效时可能无法修复；已安装程序本身仍可运行。"),
    CACHE_DIR(L"Package Cache（用户级）",   L"%LOCALAPPDATA%\\Package Cache",              Ide, false, false, Caution, L"用户级安装器缓存",
              L"清理后修复或卸载用户级安装的程序时会重新下载安装包，断网时可能无法修复；程序本身与用户数据不受影响。"),
    CACHE_DIR(L"Android Studio 缓存",       L"%LOCALAPPDATA%\\Google\\AndroidStudio*\\caches", Ide, false, true, Safe, L"Android Studio 缓存",
              L"清理后 Android Studio 会重建缓存并在下次打开时重新索引，首次启动变慢；项目源码与已下载的 SDK 不受影响。"),

    // ── 浏览器缓存 ──
    CACHE_DIR(L"Edge 浏览器缓存",
              L"%LOCALAPPDATA%\\Microsoft\\Edge\\User Data\\*\\Cache;"
              L"%LOCALAPPDATA%\\Microsoft\\Edge\\User Data\\*\\Code Cache;"
              L"%LOCALAPPDATA%\\Microsoft\\Edge\\User Data\\*\\GPUCache;"
              L"%LOCALAPPDATA%\\Microsoft\\Edge\\User Data\\*\\ShaderCache;"
              L"%LOCALAPPDATA%\\Microsoft\\Edge\\User Data\\*\\GrShaderCache;"
              L"%LOCALAPPDATA%\\Microsoft\\Edge\\User Data\\*\\Service Worker\\CacheStorage",
              Browser, false, true, Safe, L"建议先关闭 Edge",
              L"清理后网页图片、脚本等资源需重新下载，首次访问网站变慢；建议先关闭 Edge，否则被占用的文件会自动跳过。收藏夹、历史记录、密码、Cookie 与登录态均不在此目录，不受影响。"),
    CACHE_DIR(L"Edge 组件缓存",              L"%LOCALAPPDATA%\\Microsoft\\Edge\\User Data\\component_crx_cache", Browser, false, true, Safe, L"组件包，自动重新下载",
              L"清理后 Edge 的组件包会自动重新下载，个别内置功能首次启用略慢；浏览数据与设置不受影响。"),
    CACHE_DIR(L"Chrome 浏览器缓存",
              L"%LOCALAPPDATA%\\Google\\Chrome\\User Data\\*\\Cache;"
              L"%LOCALAPPDATA%\\Google\\Chrome\\User Data\\*\\Code Cache;"
              L"%LOCALAPPDATA%\\Google\\Chrome\\User Data\\*\\GPUCache;"
              L"%LOCALAPPDATA%\\Google\\Chrome\\User Data\\*\\ShaderCache;"
              L"%LOCALAPPDATA%\\Google\\Chrome\\User Data\\*\\GrShaderCache;"
              L"%LOCALAPPDATA%\\Google\\Chrome\\User Data\\*\\Service Worker\\CacheStorage",
              Browser, false, true, Safe, L"建议先关闭 Chrome",
              L"清理后网页图片、脚本等资源需重新下载，首次访问网站变慢；建议先关闭 Chrome，否则被占用的文件会自动跳过。收藏夹、历史记录、密码、Cookie 与登录态均不在此目录，不受影响。"),
    CACHE_DIR(L"Chrome 组件缓存",            L"%LOCALAPPDATA%\\Google\\Chrome\\User Data\\component_crx_cache", Browser, false, true, Safe, L"组件包，自动重新下载",
              L"清理后 Chrome 的组件包会自动重新下载，个别内置功能首次启用略慢；浏览数据与设置不受影响。"),
    CACHE_DIR(L"Chrome 优化指南模型",        L"%LOCALAPPDATA%\\Google\\Chrome\\User Data\\optimization_guide_model_store", Browser, false, true, Safe, L"优化指南模型，自动重新下载",
              L"清理后优化指南模型会自动重新下载，相关智能功能短时间内不可用；浏览数据与设置不受影响。"),
    CACHE_DIR(L"Firefox 浏览器缓存",         L"%LOCALAPPDATA%\\Mozilla\\Firefox\\Profiles\\*\\cache2", Browser, false, true, Safe, L"建议先关闭 Firefox",
              L"清理后网页资源需重新下载，首次访问变慢；建议先关闭 Firefox，否则被占用的文件会自动跳过。收藏、历史、密码与登录态不受影响。"),
    CACHE_DIR(L"系统网络缓存（INetCache）",  L"%LOCALAPPDATA%\\Microsoft\\Windows\\INetCache", Browser, false, true, Safe, L"WinINet 临时文件缓存",
              L"清理后使用 WinINet 的程序需重新获取网络资源，首次访问略慢；浏览器主缓存、收藏夹与登录态不受影响。"),

    // ── 系统缓存 ──
    CACHE_DIR(L"系统临时文件",          L"%TEMP%",                                        System, false, true,  Safe,    L"用户临时目录内容（占用中的文件会跳过）",
              L"清理后用户临时目录中的文件被删除，正在运行的程序若依赖其中文件可能出现异常（占用中的文件会自动跳过）；建议先关闭不需要的程序。文档、配置与已安装软件不受影响。"),
    CACHE_DIR(L"Windows 临时文件",      L"%SystemRoot%\\Temp",                            System, true,  true,  Safe,    L"系统临时目录，需管理员",
              L"清理后系统临时目录被清空，部分安装程序或服务的临时文件会被移除；正在使用中的文件会自动跳过，需要管理员权限。已安装的软件不受影响。"),
    CACHE_DIR(L"Windows 更新缓存",      L"%SystemRoot%\\SoftwareDistribution\\Download",  System, true,  true,  Safe,    L"已下载的更新包，可重新下载",
              L"清理后已下载的更新包需重新下载，下次检查更新变慢并占用带宽；已安装的更新不受影响，但通过缓存卸载或回滚某个更新可能不再可行。"),
    CACHE_DIR(L"传递优化缓存",          L"%SystemRoot%\\SoftwareDistribution\\DeliveryOptimization;%SystemRoot%\\ServiceProfiles\\NetworkService\\AppData\\Local\\Microsoft\\Windows\\DeliveryOptimization", System, true, true, Safe, L"更新分发缓存，需管理员",
              L"清理后本机不再作为更新分发的缓存来源，局域网内其他设备获取更新可能变慢；已安装的更新与系统功能不受影响，需要管理员权限。"),
    CACHE_DIR(L"Windows 预取",          L"%SystemRoot%\\Prefetch",                        System, true,  true,  Safe,    L"预读文件，系统会重新生成",
              L"清理后系统启动与程序启动的预读数据会重新积累，短期内首次启动略慢；不影响系统功能，需要管理员权限。"),
    CACHE_DIR(L"错误报告缓存",          L"%LOCALAPPDATA%\\Microsoft\\Windows\\WER",        System, false, true,  Safe,    L"Windows 错误报告队列",
              L"清理后尚未上报的错误报告队列会丢失，历史崩溃信息不可再查阅；不影响系统运行，新的报告会自动生成。"),
    CACHE_DIR(L"崩溃转储",              L"%LOCALAPPDATA%\\CrashDumps",                     System, false, true,  Safe,    L"应用崩溃转储文件",
              L"清理后应用崩溃转储文件不可再用于排障；不影响应用运行，新的崩溃会重新生成转储。"),
    CACHE_DIR(L"DirectX 着色器缓存",    L"%LOCALAPPDATA%\\D3DSCache",                      System, false, true,  Safe,    L"游戏/图形着色器缓存",
              L"清理后游戏与图形程序首次运行会重新编译着色器，可能出现短暂卡顿；不影响画质、驱动与游戏设置。"),
    CACHE_DIR(L"NVIDIA 着色器缓存",     L"%LOCALAPPDATA%\\NVIDIA\\DXCache;%LOCALAPPDATA%\\NVIDIA\\GLCache", System, false, true, Safe, L"NVIDIA 显卡着色器缓存",
              L"清理后游戏与图形程序首次运行会重新编译着色器，可能出现短暂卡顿；不影响驱动、画质与游戏设置。"),
    CACHE_DIR(L"AMD 着色器缓存",        L"%LOCALAPPDATA%\\AMD\\DxCache;%LOCALAPPDATA%\\AMD\\DX9Cache;%LOCALAPPDATA%\\AMD\\DxcCache;%LOCALAPPDATA%\\AMD\\OglCache;%LOCALAPPDATA%\\AMD\\VkCache;%LOCALAPPDATA%\\AMD\\CN", System, false, true, Safe, L"AMD 显卡着色器缓存",
              L"清理后游戏与图形程序首次运行会重新编译着色器，可能出现短暂卡顿；不影响驱动、画质与游戏设置。"),
    CACHE_DIR(L"内核报告",              L"%SystemRoot%\\LiveKernelReports",                System, true,  true,  Safe,    L"内核转储报告，需管理员",
              L"清理后内核转储报告不可再用于排查蓝屏或硬件问题；不影响系统运行，需要管理员权限。"),
    CACHE_DIR(L"小型内存转储",          L"%SystemRoot%\\Minidump",                         System, true,  true,  Safe,    L"蓝屏小转储，需管理员",
              L"清理后蓝屏小转储不可再用于分析历史崩溃；不影响系统运行，需要管理员权限。"),
    CACHE_DIR(L"CBS 日志",              L"%SystemRoot%\\Logs\\CBS",                        System, true,  false, Caution, L"组件安装日志，排障时可能需要",
              L"清理后组件安装日志不可再查阅，排查 Windows 更新或组件故障时会缺少依据；系统功能不受影响，需要管理员权限。"),
    CACHE_DIR(L"系统日志",              L"%SystemRoot%\\Logs;%SystemRoot%\\System32\\LogFiles", System, true, false, Caution, L"排障可能需要，需管理员",
              L"清理后系统日志被清空，排障与审计时可能缺少记录；系统功能不受影响，需要管理员权限。"),
    CACHE_DIR(L"更新协调器日志",        L"%ProgramData%\\USOPrivate;%ProgramData%\\USOShared", System, true, false, Caution, L"更新历史/日志，可重建，需管理员",
              L"清理后 Windows 更新的历史与协调器日志被移除，排查更新失败时缺少依据；更新功能不受影响，需要管理员权限。"),
    CACHE_FILE(L"缩略图缓存",           L"%LOCALAPPDATA%\\Microsoft\\Windows\\Explorer\\thumbcache_*.db", System, false, true, Safe, L"重启资源管理器后自动重建",
              L"清理后资源管理器会重建缩略图，首次浏览图片或视频文件夹时显示变慢，可能需要重启资源管理器；原始图片文件完全不受影响，本项只删除 thumbcache_*.db。"),
    CACHE_FILE(L"图标缓存",             L"%LOCALAPPDATA%\\Microsoft\\Windows\\Explorer\\iconcache_*.db", System, false, true, Safe, L"重启资源管理器后自动重建",
              L"清理后资源管理器会重建图标，桌面与文件夹图标可能短暂显示为通用图标，重启资源管理器或注销后恢复；程序文件与快捷方式不受影响。"),
    CACHE_DIR(L"Squirrel 更新临时文件", L"%LOCALAPPDATA%\\SquirrelTemp",                   System, false, true,  Safe,    L"Electron 应用更新残留",
              L"清理后 Electron 应用的更新残留被移除，正在进行的更新可能需要重新下载；已安装的应用与其数据不受影响。"),
    CACHE_DIR(L"Google 更新器 CRX 缓存", L"%ProgramFiles(x86)%\\Google\\GoogleUpdater\\crx_cache", System, true, false, Safe, L"Chrome 组件缓存，提权后可删，自动重下",
              L"清理后 Chrome 组件会重新下载，个别功能首次启用略慢；浏览器与更新器配置不受影响，需要管理员权限。"),
    CACHE_DIR(L"UWP 应用缓存",          L"%LOCALAPPDATA%\\Packages\\*\\LocalCache;%LOCALAPPDATA%\\Packages\\*\\TempState", System, false, false, Caution, L"商店应用缓存，清理后应用需重建",
              L"清理后商店应用的本地缓存需重新生成，部分应用可能丢失临时状态或需要重新登录；应用本体与账户数据通常不受影响，建议先关闭相关应用。"),
    CACHE_DIR(L"旧版 Windows 文件",     L"%SystemDrive%\\Windows.old",                     System, true,  false, Caution, L"升级前的旧系统，删除后无法回退",
              L"清理后无法再回退到升级前的 Windows 版本，也无法从中找回旧系统里的个人文件；当前系统和数据不受影响。请确认不再需要回退再勾选，本操作不可撤销，需要管理员权限。"),
    CACHE_DIR(L"Windows 升级临时文件",  L"%SystemDrive%\\$WINDOWS.~BT;%SystemDrive%\\$WINDOWS.~WS", System, true, false, Caution, L"升级过程残留，需管理员",
              L"清理后升级过程的临时文件被移除，无法再用于排查升级失败；若升级流程仍在进行可能影响该流程，需要管理员权限。当前系统不受影响。"),
    { L"回收站", L"", Category::System, false, true, Risk::Safe, L"清空 C 盘回收站（其他盘符不受影响）",
              L"清空后 C 盘回收站里的文件将被永久删除，无法再通过「还原」找回；其他盘符的回收站不受影响。请先确认回收站中没有还需要的文件。", EntryKind::RecycleBin },

    // ── 应用缓存 ──
    CACHE_DIR(L"Teams 缓存",            L"%APPDATA%\\Microsoft\\Teams\\Cache;%APPDATA%\\Microsoft\\Teams\\Code Cache;%APPDATA%\\Microsoft\\Teams\\GPUCache", App, false, true, Safe, L"建议先退出 Teams",
              L"清理后 Teams 需重新加载页面资源，首次启动略慢，建议先退出 Teams，否则被占用的文件会自动跳过；聊天记录、账号与设置不受影响。"),
    CACHE_DIR(L"Slack 缓存",            L"%APPDATA%\\Slack\\Cache;%APPDATA%\\Slack\\Code Cache;%APPDATA%\\Slack\\GPUCache", App, false, true, Safe, L"建议先退出 Slack",
              L"清理后 Slack 需重新加载页面资源，首次启动略慢，建议先退出 Slack，否则被占用的文件会自动跳过；消息记录、账号与设置不受影响。"),
    CACHE_DIR(L"Discord 缓存",          L"%APPDATA%\\discord\\Cache;%APPDATA%\\discord\\Code Cache;%APPDATA%\\discord\\GPUCache", App, false, true, Safe, L"建议先退出 Discord",
              L"清理后 Discord 需重新加载页面资源，首次启动略慢，建议先退出 Discord，否则被占用的文件会自动跳过；消息记录、账号与设置不受影响。"),
    CACHE_DIR(L"微信缓存（不含聊天记录）", L"%USERPROFILE%\\Documents\\WeChat Files\\*\\FileStorage\\Cache;%USERPROFILE%\\Documents\\xwechat_files\\*\\cache", App, false, false, Caution, L"仅清缓存，不触碰聊天记录",
              L"清理后微信的图片、文件缓存需重新加载或重新下载，首次查看略慢；聊天记录、收藏与账号数据不在清理范围内，不受影响。建议先退出微信。"),
    CACHE_DIR(L"网易云音乐缓存",        L"%LOCALAPPDATA%\\Netease\\CloudMusic\\Cache",      App, false, false, Caution, L"在线歌曲缓存，离线歌曲不在此目录",
              L"清理后在线歌曲需重新缓冲或下载，短时间内播放变慢；已下载的离线歌曲、歌单与账号数据不在此目录，不受影响。"),
    CACHE_DIR(L"Spotify 缓存",          L"%LOCALAPPDATA%\\Spotify\\Storage",               App, false, false, Caution, L"离线歌曲缓存",
              L"清理后已缓存的歌曲需重新下载，标记为离线可用的内容会失效并需要重新下载；歌单、账号与设置不受影响。"),
    CACHE_DIR(L"Adobe 媒体缓存",        L"%APPDATA%\\Adobe\\Common\\Media Cache Files;%APPDATA%\\Adobe\\Common\\Media Cache", App, false, false, Caution, L"Premiere/AE 媒体缓存",
              L"清理后 Premiere / After Effects 需重新生成峰值、预览与一致性文件，首次打开工程明显变慢；工程文件与原始素材不受影响。"),
    CACHE_DIR(L"WPS 组件缓存",          L"%APPDATA%\\kingsoft\\wps\\addons;%APPDATA%\\kingsoft\\wps_international\\addons", App, false, false, Caution, L"关闭 WPS 后清理；组件会按需重新下载",
              L"清理后 WPS 的组件会按需重新下载，某些功能首次启用略慢，断网时可能无法加载；文档与账号数据不受影响，清理前需先关闭 WPS。"),
    CACHE_DIR(L"Steam html 缓存",       L"%LOCALAPPDATA%\\Steam\\htmlcache",               App, false, true,  Safe,    L"内嵌浏览器缓存，自动重建",
              L"清理后 Steam 内嵌网页需重新加载，商店与社区页面首次打开略慢；已安装游戏、存档与库文件不受影响。"),
    CACHE_DIR(L"QQ 频道缓存",           L"%LOCALAPPDATA%\\Tencent\\QQGuild",               App, false, false, Caution, L"QQ 组件缓存，建议先退出 QQ",
              L"清理后 QQ 频道的组件缓存需重新生成，首次进入略慢；聊天记录与账号数据不受影响，建议先退出 QQ。"),

    // ── 开发工具缓存 ──
    CACHE_DIR(L"Playwright 浏览器",     L"%LOCALAPPDATA%\\ms-playwright",                  Dev, false, false, Caution, L"删除后需重新执行 playwright install",
              L"清理后必须重新执行 playwright install 下载浏览器，重装完成前自动化脚本无法运行且需要网络；测试代码与配置不受影响。"),
    CACHE_DIR(L"Puppeteer 浏览器缓存",  L"%USERPROFILE%\\.cache\\puppeteer",               Dev, false, false, Caution, L"删除后需重新下载 Chromium",
              L"清理后需要重新下载 Chromium，重装完成前 Puppeteer 脚本无法运行；项目代码与配置不受影响。"),
    CACHE_DIR(L"Playwright MCP 浏览器", L"%LOCALAPPDATA%\\ms-playwright-mcp",              Dev, false, true,  Safe,    L"自动重新下载",
              L"清理后 MCP 浏览器会自动重新下载，首次调用变慢并需要网络；项目配置与测试代码不受影响。"),
    CACHE_DIR(L"CLI 工具缓存",          L"%USERPROFILE%\\.cache\\huggingface;%USERPROFILE%\\.cache\\chrome-devtools-mcp;%USERPROFILE%\\.cache\\selenium", Dev, false, false, Caution, L"模型/工具下载缓存，可重新下载",
              L"清理后 huggingface 模型、chrome-devtools-mcp、selenium 等下载缓存需重新获取，首次使用变慢且需要网络，模型较大时耗时明显；项目代码不受影响。"),
    CACHE_DIR(L"Docker 日志",           L"%LOCALAPPDATA%\\Docker\\log",                    Dev, false, true,  Safe,    L"仅日志；镜像/容器见文档危险项，不做删除",
              L"清理后 Docker 的历史日志不可再查阅，排查容器问题时会缺少依据；镜像、容器、卷与数据卷完全不在清理范围内，不受影响。"),
    CACHE_DIR(L"WinGet 缓存",           L"%LOCALAPPDATA%\\Microsoft\\WinGet\\Cache",       Dev, false, true,  Safe,    L"WinGet 下载缓存",
              L"清理后 WinGet 的安装包下载缓存需重新获取，重复安装同一软件时变慢；已安装的软件与其配置不受影响。"),
};

#undef CACHE_DIR
#undef CACHE_FILE

const wchar_t* kCategoryNames[] = {
    L"包管理器",
    L"构建缓存",
    L"编辑器/IDE",
    L"浏览器",
    L"系统缓存",
    L"应用缓存",
    L"开发工具",
};

}  // namespace

const EntryDef* Catalog(std::size_t* count) {
    if (count) *count = sizeof(kEntries) / sizeof(kEntries[0]);
    return kEntries;
}

const wchar_t* CategoryName(Category category) {
    const int index = static_cast<int>(category);
    if (index < 0 || index >= static_cast<int>(Category::Count)) return L"其他";
    return kCategoryNames[index];
}

// 目录里的每一项都写有专属后果说明；兜底用于将来新增条目时避免出现空白窗口
const wchar_t* ConsequenceOf(const EntryDef& def) {
    if (def.consequence && *def.consequence) return def.consequence;
    if (def.risk == Risk::Caution) {
        return L"该项为谨慎项，清理后可能需要重新下载、重新登录，或无法恢复部分历史数据；"
               L"请确认不再需要这些内容后再勾选。用户文档与配置不在清理范围内。";
    }
    return L"该项是可自动重建的缓存，清理后程序会在下次使用时重新生成，首次使用可能变慢；"
           L"用户配置、文档与已安装的程序不受影响。";
}

std::vector<EntryState> ResolveCatalog(bool elevated) {
    std::size_t count = 0;
    const EntryDef* defs = Catalog(&count);

    std::vector<EntryState> states(count);
    for (std::size_t i = 0; i < count; ++i) {
        const EntryDef& def = defs[i];
        EntryState& state = states[i];
        state.checked = def.defaultChecked;
        state.needsAdmin = def.needsAdmin;
        state.adminBlocked = def.needsAdmin && !elevated;
        state.recycleBin = def.kind == EntryKind::RecycleBin;
        state.fileMode = def.kind == EntryKind::FileGlob;

        if (def.kind == EntryKind::RecycleBin) {
            state.exists = true;
            continue;
        }

        if (def.kind == EntryKind::FileGlob) {
            state.fileList = ExpandFileTemplates(def.paths);
            state.exists = !state.fileList.empty();
            continue;
        }

        for (const auto& dir : ExpandTemplates(def.paths)) {
            // 目标本身是 junction / 符号链接：默认跳过，避免清理到链接指向的外部数据
            if (IsReparsePoint(dir)) {
                ++state.skippedLinks;
                continue;
            }
            state.dirs.push_back(dir);
        }
        state.exists = !state.dirs.empty() || state.skippedLinks > 0;
    }
    return states;
}

}  // namespace clearc
