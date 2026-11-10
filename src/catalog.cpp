/*
 * clearC - 清理项目录实现
 *
 * 条目来源与安全分级依据见 docs/cache-candidates.md（本机逐项实测）
 */
#include "catalog.h"

namespace clearc {
namespace {

// 目录型清理项；路径支持 ';' 多模板、%ENV% 与通配符
#define CACHE_DIR(NAME, PATHS, CAT, ADMIN, DEFAULT_ON, RISK, NOTE) \
    { NAME, PATHS, Category::CAT, ADMIN, DEFAULT_ON, Risk::RISK, NOTE, EntryKind::Directory }

// 文件通配型清理项：只删除命中的文件本身，不会清空整个父目录
#define CACHE_FILE(NAME, PATHS, CAT, ADMIN, DEFAULT_ON, RISK, NOTE) \
    { NAME, PATHS, Category::CAT, ADMIN, DEFAULT_ON, Risk::RISK, NOTE, EntryKind::FileGlob }

const EntryDef kEntries[] = {
    // ── 包管理器缓存 ──
    CACHE_DIR(L"uv 包缓存",             L"%LOCALAPPDATA%\\uv\\cache",                      Package, false, true,  Safe,    L"uv 下载/构建缓存，自动重建"),
    CACHE_DIR(L"pip 缓存",              L"%LOCALAPPDATA%\\pip\\cache",                     Package, false, true,  Safe,    L"pip wheel 缓存，自动重建"),
    CACHE_DIR(L"Conda 包缓存",          L"%USERPROFILE%\\.conda\\pkgs;%LOCALAPPDATA%\\conda\\conda\\pkgs", Package, false, true, Safe, L"conda 已解压包缓存"),
    CACHE_DIR(L"NuGet 全局包",          L"%USERPROFILE%\\.nuget\\packages",                Package, false, true,  Safe,    L"已还原的 NuGet 包，需要时重新还原"),
    CACHE_DIR(L"NuGet HTTP 缓存",       L"%LOCALAPPDATA%\\NuGet\\v3-cache;%LOCALAPPDATA%\\NuGet\\plugins-cache", Package, false, true, Safe, L"NuGet 源响应缓存"),
    CACHE_DIR(L"npm 缓存",              L"%LOCALAPPDATA%\\npm-cache;%APPDATA%\\npm-cache",  Package, false, true,  Safe,    L"npm tarball 与 _npx 缓存（新版在 LocalAppData）"),
    CACHE_DIR(L"yarn 缓存",             L"%LOCALAPPDATA%\\Yarn\\Cache;%LOCALAPPDATA%\\Yarn\\Berry\\cache", Package, false, true, Safe, L"yarn 1 / Berry 下载缓存"),
    CACHE_DIR(L"pnpm 存储（v8）",       L"%LOCALAPPDATA%\\pnpm-store",                     Package, false, true,  Safe,    L"pnpm 内容寻址存储"),
    CACHE_DIR(L"pnpm 存储（v9+）",      L"%LOCALAPPDATA%\\pnpm\\store",                    Package, false, true,  Safe,    L"pnpm 新版本存储目录"),
    CACHE_DIR(L"pnpm 元数据缓存",       L"%LOCALAPPDATA%\\pnpm-cache",                     Package, false, true,  Safe,    L"registry 元数据缓存，自动重建"),
    CACHE_DIR(L"vcpkg 下载缓存",        L"%LOCALAPPDATA%\\vcpkg\\archives",                Package, false, true,  Safe,    L"vcpkg 下载归档"),
    CACHE_DIR(L"vcpkg 构建树",          L"%LOCALAPPDATA%\\vcpkg\\buildtrees",              Package, false, true,  Safe,    L"vcpkg 构建中间产物"),
    CACHE_DIR(L"Go 构建缓存",           L"%LOCALAPPDATA%\\go-build",                       Package, false, true,  Safe,    L"go build 编译缓存"),
    CACHE_DIR(L"Go 模块下载缓存",       L"%USERPROFILE%\\go\\pkg\\mod\\cache",             Package, false, true,  Safe,    L"go mod 下载缓存（源码模块保留）"),
    CACHE_DIR(L"Cargo 注册表缓存",      L"%USERPROFILE%\\.cargo\\registry\\cache",         Package, false, true,  Safe,    L"crates.io 包缓存"),
    CACHE_DIR(L"Cargo Git 缓存",        L"%USERPROFILE%\\.cargo\\git\\db",                 Package, false, true,  Safe,    L"git 依赖裸仓库缓存"),
    CACHE_DIR(L"Maven 本地仓库",        L"%USERPROFILE%\\.m2\\repository",                 Package, false, false, Caution, L"删除后首次构建需重新下载全部依赖"),
    CACHE_DIR(L"Composer 缓存",         L"%LOCALAPPDATA%\\Composer\\cache",                Package, false, true,  Safe,    L"PHP Composer 包缓存"),
    CACHE_DIR(L"Deno 依赖缓存",         L"%LOCALAPPDATA%\\deno",                           Package, false, true,  Safe,    L"Deno 远程模块缓存"),
    CACHE_DIR(L"Bun 安装缓存",          L"%USERPROFILE%\\.bun\\install\\cache",            Package, false, true,  Safe,    L"Bun 包缓存"),
    CACHE_DIR(L"Chocolatey 安装缓存",   L"%LOCALAPPDATA%\\Temp\\chocolatey",               Package, false, true,  Safe,    L"choco 安装包临时目录"),
    CACHE_DIR(L"Scoop 下载缓存",        L"%USERPROFILE%\\scoop\\cache",                    Package, false, true,  Safe,    L"scoop 安装包缓存"),
    CACHE_DIR(L"Conan 2 包缓存",        L"%USERPROFILE%\\.conan2\\p",                      Package, false, true,  Safe,    L"conan 2 本地包缓存"),
    CACHE_DIR(L"node-gyp 缓存",         L"%LOCALAPPDATA%\\node-gyp",                       Package, false, true,  Safe,    L"Node 原生模块编译缓存，重新编译时重建"),

    // ── 构建缓存 ──
    CACHE_DIR(L"Gradle 缓存",           L"%USERPROFILE%\\.gradle\\caches",                 Build,   false, true,  Safe,    L"Gradle 依赖与构建缓存"),
    CACHE_DIR(L"Gradle 分发缓存",       L"%USERPROFILE%\\.gradle\\wrapper",                Build,   false, false, Caution, L"删除后重新下载 Gradle 发行包"),
    CACHE_DIR(L"CMake 缓存",            L"%LOCALAPPDATA%\\CMake",                           Build,   false, true,  Safe,    L"CMake 下载的模块与缓存"),
    CACHE_DIR(L"ccache 缓存",           L"%LOCALAPPDATA%\\ccache",                          Build,   false, true,  Safe,    L"ccache 编译缓存"),

    // ── 编辑器 / IDE 缓存 ──
    CACHE_DIR(L"VSCode Cache",              L"%APPDATA%\\Code\\Cache",                     Ide, false, true, Safe, L"VSCode 缓存"),
    CACHE_DIR(L"VSCode CachedData",         L"%APPDATA%\\Code\\CachedData",                Ide, false, true, Safe, L"VSCode 扩展宿主缓存"),
    CACHE_DIR(L"VSCode CachedExtensions",   L"%APPDATA%\\Code\\CachedExtensions;%APPDATA%\\Code\\CachedExtensionVSIXs", Ide, false, true, Safe, L"VSCode 扩展 VSIX 缓存"),
    CACHE_DIR(L"VSCode CachedProfileData",  L"%APPDATA%\\Code\\CachedProfileData;%APPDATA%\\Code\\CachedProfilesData", Ide, false, true, Safe, L"VSCode 配置文件缓存"),
    CACHE_DIR(L"VSCode GPUCache",           L"%APPDATA%\\Code\\GPUCache",                  Ide, false, true, Safe, L"VSCode GPU 渲染缓存"),
    CACHE_DIR(L"VSCode 日志",               L"%APPDATA%\\Code\\logs",                      Ide, false, true, Safe, L"VSCode 运行日志"),
    CACHE_DIR(L"VSCode C++ 缓存",           L"%APPDATA%\\Code\\User\\globalStorage\\ms-vscode.cpptools", Ide, false, true, Safe, L"cpptools 数据库与缓存"),
    CACHE_DIR(L"VSCode 崩溃转储",           L"%APPDATA%\\Code\\Crashpad",                  Ide, false, true, Safe, L"崩溃报告，自动重建"),
    CACHE_DIR(L"VSCode 工作区状态",         L"%APPDATA%\\Code\\User\\workspaceStorage",    Ide, false, false, Caution, L"工作区布局与扩展缓存，重开自动重建（含 ipch）"),
    CACHE_DIR(L"VSCode 全局扩展存储",       L"%APPDATA%\\Code\\User\\globalStorage",       Ide, false, false, Caution, L"部分扩展会丢本地状态或需重新登录"),
    CACHE_DIR(L"VSCode WebStorage",         L"%APPDATA%\\Code\\WebStorage",                Ide, false, false, Caution, L"webview 存储，面板会重新加载"),
    CACHE_DIR(L"VSCode Partitions",         L"%APPDATA%\\Code\\Partitions",                Ide, false, false, Caution, L"多 profile 的浏览器数据（含 localStorage）"),
    CACHE_DIR(L"VSCode 本地历史",           L"%APPDATA%\\Code\\User\\History",             Ide, false, false, Caution, L"文件本地历史，用户可能还需要撤销"),
    CACHE_DIR(L"Cursor 缓存",               L"%APPDATA%\\Cursor\\Cache;%APPDATA%\\Cursor\\CachedData;%APPDATA%\\Cursor\\GPUCache;%APPDATA%\\Cursor\\logs", Ide, false, true, Safe, L"Cursor 编辑器缓存"),
    CACHE_DIR(L"Trae CN 缓存",              L"%APPDATA%\\Trae CN\\Cache;%APPDATA%\\Trae CN\\CachedData;%APPDATA%\\Trae CN\\GPUCache;%APPDATA%\\Trae CN\\logs;%APPDATA%\\Trae CN\\WebStorage;%APPDATA%\\Trae CN\\Partitions", Ide, false, false, Caution, L"VSCode 系编辑器缓存，建议先退出"),
    CACHE_DIR(L"JetBrains 缓存",            L"%LOCALAPPDATA%\\JetBrains\\*\\caches",        Ide, false, true, Safe, L"IDEA/PyCharm 等缓存"),
    CACHE_DIR(L"JetBrains 索引",            L"%LOCALAPPDATA%\\JetBrains\\*\\index",         Ide, false, true, Safe, L"代码索引，重新打开项目时重建"),
    CACHE_DIR(L"JetBrains 日志",            L"%LOCALAPPDATA%\\JetBrains\\*\\log",           Ide, false, true, Safe, L"IDE 运行日志"),
    CACHE_DIR(L"Visual Studio 组件缓存",    L"%LOCALAPPDATA%\\Microsoft\\VisualStudio\\*\\ComponentModelCache", Ide, false, true, Safe, L"VS 组件模型缓存"),
    CACHE_DIR(L"Visual Studio 安装缓存",    L"%ProgramData%\\Microsoft\\VisualStudio\\Packages", Ide, true, false, Caution, L"VS 安装包缓存；若被重定向为 junction 会自动跳过"),
    CACHE_DIR(L"Package Cache（全机）",     L"%ProgramData%\\Package Cache",               Ide, true,  false, Caution, L"安装器缓存，修复/卸载时会重新下载"),
    CACHE_DIR(L"Package Cache（用户级）",   L"%LOCALAPPDATA%\\Package Cache",              Ide, false, false, Caution, L"用户级安装器缓存"),
    CACHE_DIR(L"Android Studio 缓存",       L"%LOCALAPPDATA%\\Google\\AndroidStudio*\\caches", Ide, false, true, Safe, L"Android Studio 缓存"),

    // ── 浏览器缓存 ──
    CACHE_DIR(L"Edge 浏览器缓存",
              L"%LOCALAPPDATA%\\Microsoft\\Edge\\User Data\\*\\Cache;"
              L"%LOCALAPPDATA%\\Microsoft\\Edge\\User Data\\*\\Code Cache;"
              L"%LOCALAPPDATA%\\Microsoft\\Edge\\User Data\\*\\GPUCache;"
              L"%LOCALAPPDATA%\\Microsoft\\Edge\\User Data\\*\\ShaderCache;"
              L"%LOCALAPPDATA%\\Microsoft\\Edge\\User Data\\*\\GrShaderCache;"
              L"%LOCALAPPDATA%\\Microsoft\\Edge\\User Data\\*\\Service Worker\\CacheStorage",
              Browser, false, true, Safe, L"建议先关闭 Edge"),
    CACHE_DIR(L"Edge 组件缓存",              L"%LOCALAPPDATA%\\Microsoft\\Edge\\User Data\\component_crx_cache", Browser, false, true, Safe, L"组件包，自动重新下载"),
    CACHE_DIR(L"Chrome 浏览器缓存",
              L"%LOCALAPPDATA%\\Google\\Chrome\\User Data\\*\\Cache;"
              L"%LOCALAPPDATA%\\Google\\Chrome\\User Data\\*\\Code Cache;"
              L"%LOCALAPPDATA%\\Google\\Chrome\\User Data\\*\\GPUCache;"
              L"%LOCALAPPDATA%\\Google\\Chrome\\User Data\\*\\ShaderCache;"
              L"%LOCALAPPDATA%\\Google\\Chrome\\User Data\\*\\GrShaderCache;"
              L"%LOCALAPPDATA%\\Google\\Chrome\\User Data\\*\\Service Worker\\CacheStorage",
              Browser, false, true, Safe, L"建议先关闭 Chrome"),
    CACHE_DIR(L"Chrome 组件缓存",            L"%LOCALAPPDATA%\\Google\\Chrome\\User Data\\component_crx_cache", Browser, false, true, Safe, L"组件包，自动重新下载"),
    CACHE_DIR(L"Chrome 优化指南模型",        L"%LOCALAPPDATA%\\Google\\Chrome\\User Data\\optimization_guide_model_store", Browser, false, true, Safe, L"优化指南模型，自动重新下载"),
    CACHE_DIR(L"Firefox 浏览器缓存",         L"%LOCALAPPDATA%\\Mozilla\\Firefox\\Profiles\\*\\cache2", Browser, false, true, Safe, L"建议先关闭 Firefox"),
    CACHE_DIR(L"系统网络缓存（INetCache）",  L"%LOCALAPPDATA%\\Microsoft\\Windows\\INetCache", Browser, false, true, Safe, L"WinINet 临时文件缓存"),

    // ── 系统缓存 ──
    CACHE_DIR(L"系统临时文件",          L"%TEMP%",                                        System, false, true,  Safe,    L"用户临时目录内容（占用中的文件会跳过）"),
    CACHE_DIR(L"Windows 临时文件",      L"%SystemRoot%\\Temp",                            System, true,  true,  Safe,    L"系统临时目录，需管理员"),
    CACHE_DIR(L"Windows 更新缓存",      L"%SystemRoot%\\SoftwareDistribution\\Download",  System, true,  true,  Safe,    L"已下载的更新包，可重新下载"),
    CACHE_DIR(L"传递优化缓存",          L"%SystemRoot%\\SoftwareDistribution\\DeliveryOptimization;%SystemRoot%\\ServiceProfiles\\NetworkService\\AppData\\Local\\Microsoft\\Windows\\DeliveryOptimization", System, true, true, Safe, L"更新分发缓存，需管理员"),
    CACHE_DIR(L"Windows 预取",          L"%SystemRoot%\\Prefetch",                        System, true,  true,  Safe,    L"预读文件，系统会重新生成"),
    CACHE_DIR(L"错误报告缓存",          L"%LOCALAPPDATA%\\Microsoft\\Windows\\WER",        System, false, true,  Safe,    L"Windows 错误报告队列"),
    CACHE_DIR(L"崩溃转储",              L"%LOCALAPPDATA%\\CrashDumps",                     System, false, true,  Safe,    L"应用崩溃转储文件"),
    CACHE_DIR(L"DirectX 着色器缓存",    L"%LOCALAPPDATA%\\D3DSCache",                      System, false, true,  Safe,    L"游戏/图形着色器缓存"),
    CACHE_DIR(L"NVIDIA 着色器缓存",     L"%LOCALAPPDATA%\\NVIDIA\\DXCache;%LOCALAPPDATA%\\NVIDIA\\GLCache", System, false, true, Safe, L"NVIDIA 显卡着色器缓存"),
    CACHE_DIR(L"AMD 着色器缓存",        L"%LOCALAPPDATA%\\AMD\\DxCache;%LOCALAPPDATA%\\AMD\\DX9Cache;%LOCALAPPDATA%\\AMD\\DxcCache;%LOCALAPPDATA%\\AMD\\OglCache;%LOCALAPPDATA%\\AMD\\VkCache;%LOCALAPPDATA%\\AMD\\CN", System, false, true, Safe, L"AMD 显卡着色器缓存"),
    CACHE_DIR(L"内核报告",              L"%SystemRoot%\\LiveKernelReports",                System, true,  true,  Safe,    L"内核转储报告，需管理员"),
    CACHE_DIR(L"小型内存转储",          L"%SystemRoot%\\Minidump",                         System, true,  true,  Safe,    L"蓝屏小转储，需管理员"),
    CACHE_DIR(L"CBS 日志",              L"%SystemRoot%\\Logs\\CBS",                        System, true,  false, Caution, L"组件安装日志，排障时可能需要"),
    CACHE_DIR(L"系统日志",              L"%SystemRoot%\\Logs;%SystemRoot%\\System32\\LogFiles", System, true, false, Caution, L"排障可能需要，需管理员"),
    CACHE_DIR(L"更新协调器日志",        L"%ProgramData%\\USOPrivate;%ProgramData%\\USOShared", System, true, false, Caution, L"更新历史/日志，可重建，需管理员"),
    CACHE_FILE(L"缩略图缓存",           L"%LOCALAPPDATA%\\Microsoft\\Windows\\Explorer\\thumbcache_*.db", System, false, true, Safe, L"重启资源管理器后自动重建"),
    CACHE_FILE(L"图标缓存",             L"%LOCALAPPDATA%\\Microsoft\\Windows\\Explorer\\iconcache_*.db", System, false, true, Safe, L"重启资源管理器后自动重建"),
    CACHE_DIR(L"Squirrel 更新临时文件", L"%LOCALAPPDATA%\\SquirrelTemp",                   System, false, true,  Safe,    L"Electron 应用更新残留"),
    CACHE_DIR(L"Google 更新器 CRX 缓存", L"%ProgramFiles(x86)%\\Google\\GoogleUpdater\\crx_cache", System, true, false, Safe, L"Chrome 组件缓存，提权后可删，自动重下"),
    CACHE_DIR(L"UWP 应用缓存",          L"%LOCALAPPDATA%\\Packages\\*\\LocalCache;%LOCALAPPDATA%\\Packages\\*\\TempState", System, false, false, Caution, L"商店应用缓存，清理后应用需重建"),
    CACHE_DIR(L"旧版 Windows 文件",     L"%SystemDrive%\\Windows.old",                     System, true,  false, Caution, L"升级前的旧系统，删除后无法回退"),
    CACHE_DIR(L"Windows 升级临时文件",  L"%SystemDrive%\\$WINDOWS.~BT;%SystemDrive%\\$WINDOWS.~WS", System, true, false, Caution, L"升级过程残留，需管理员"),
    { L"回收站", L"", Category::System, false, true, Risk::Safe, L"清空 C 盘回收站（其他盘符不受影响）", EntryKind::RecycleBin },

    // ── 应用缓存 ──
    CACHE_DIR(L"Teams 缓存",            L"%APPDATA%\\Microsoft\\Teams\\Cache;%APPDATA%\\Microsoft\\Teams\\Code Cache;%APPDATA%\\Microsoft\\Teams\\GPUCache", App, false, true, Safe, L"建议先退出 Teams"),
    CACHE_DIR(L"Slack 缓存",            L"%APPDATA%\\Slack\\Cache;%APPDATA%\\Slack\\Code Cache;%APPDATA%\\Slack\\GPUCache", App, false, true, Safe, L"建议先退出 Slack"),
    CACHE_DIR(L"Discord 缓存",          L"%APPDATA%\\discord\\Cache;%APPDATA%\\discord\\Code Cache;%APPDATA%\\discord\\GPUCache", App, false, true, Safe, L"建议先退出 Discord"),
    CACHE_DIR(L"微信缓存（不含聊天记录）", L"%USERPROFILE%\\Documents\\WeChat Files\\*\\FileStorage\\Cache;%USERPROFILE%\\Documents\\xwechat_files\\*\\cache", App, false, false, Caution, L"仅清缓存，不触碰聊天记录"),
    CACHE_DIR(L"网易云音乐缓存",        L"%LOCALAPPDATA%\\Netease\\CloudMusic\\Cache",      App, false, false, Caution, L"在线歌曲缓存，离线歌曲不在此目录"),
    CACHE_DIR(L"Spotify 缓存",          L"%LOCALAPPDATA%\\Spotify\\Storage",               App, false, false, Caution, L"离线歌曲缓存"),
    CACHE_DIR(L"Adobe 媒体缓存",        L"%APPDATA%\\Adobe\\Common\\Media Cache Files;%APPDATA%\\Adobe\\Common\\Media Cache", App, false, false, Caution, L"Premiere/AE 媒体缓存"),
    CACHE_DIR(L"WPS 组件缓存",          L"%APPDATA%\\kingsoft\\wps\\addons;%APPDATA%\\kingsoft\\wps_international\\addons", App, false, false, Caution, L"关闭 WPS 后清理；组件会按需重新下载"),
    CACHE_DIR(L"Steam html 缓存",       L"%LOCALAPPDATA%\\Steam\\htmlcache",               App, false, true,  Safe,    L"内嵌浏览器缓存，自动重建"),
    CACHE_DIR(L"QQ 频道缓存",           L"%LOCALAPPDATA%\\Tencent\\QQGuild",               App, false, false, Caution, L"QQ 组件缓存，建议先退出 QQ"),

    // ── 开发工具缓存 ──
    CACHE_DIR(L"Playwright 浏览器",     L"%LOCALAPPDATA%\\ms-playwright",                  Dev, false, false, Caution, L"删除后需重新执行 playwright install"),
    CACHE_DIR(L"Puppeteer 浏览器缓存",  L"%USERPROFILE%\\.cache\\puppeteer",               Dev, false, false, Caution, L"删除后需重新下载 Chromium"),
    CACHE_DIR(L"Playwright MCP 浏览器", L"%LOCALAPPDATA%\\ms-playwright-mcp",              Dev, false, true,  Safe,    L"自动重新下载"),
    CACHE_DIR(L"CLI 工具缓存",          L"%USERPROFILE%\\.cache\\huggingface;%USERPROFILE%\\.cache\\chrome-devtools-mcp;%USERPROFILE%\\.cache\\selenium", Dev, false, false, Caution, L"模型/工具下载缓存，可重新下载"),
    CACHE_DIR(L"Docker 日志",           L"%LOCALAPPDATA%\\Docker\\log",                    Dev, false, true,  Safe,    L"仅日志；镜像/容器见文档危险项，不做删除"),
    CACHE_DIR(L"WinGet 缓存",           L"%LOCALAPPDATA%\\Microsoft\\WinGet\\Cache",       Dev, false, true,  Safe,    L"WinGet 下载缓存"),
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
