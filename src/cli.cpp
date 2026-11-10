/*
 * clearC - 命令行模式
 *
 *   clearC.exe --list       列出全部清理项
 *   clearC.exe --scan       扫描并打印各清理项大小
 *   clearC.exe --json       与 --list / --scan 一起使用，输出 JSON
 *   clearC.exe --self-test  内置自检（在临时目录创建夹具，不触碰真实缓存）
 *   clearC.exe --version    版本
 *   clearC.exe --help       帮助
 */
#include "cli.h"

#include "catalog.h"
#include "scan.h"
#include "util.h"

#include <winioctl.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwctype>
#include <string>
#include <vector>

namespace clearc {
namespace {

// ── 控制台输出：控制台走 WriteConsoleW，重定向走 UTF-8 字节 ──
class ConsoleOut {
public:
    void Init() {
        HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);
        if (!handle || handle == INVALID_HANDLE_VALUE ||
            GetFileType(handle) == FILE_TYPE_UNKNOWN) {
            if (!AttachConsole(ATTACH_PARENT_PROCESS)) {
                AllocConsole();
            }
            handle = CreateFileW(L"CONOUT$", GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0,
                                 nullptr);
        }
        handle_ = handle;
        isConsole_ = handle_ && GetFileType(handle_) == FILE_TYPE_CHAR;
    }

    void Write(const std::wstring& text) {
        if (!handle_) return;
        if (isConsole_) {
            DWORD written = 0;
            WriteConsoleW(handle_, text.c_str(), static_cast<DWORD>(text.size()), &written,
                          nullptr);
        } else {
            const std::string utf8 = ToUtf8(text);
            DWORD written = 0;
            WriteFile(handle_, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
        }
    }

    void Line(const std::wstring& text = L"") { Write(text + L"\r\n"); }

private:
    HANDLE handle_ = nullptr;
    bool isConsole_ = false;
};

std::wstring ToLowerCopy(const std::wstring& s) {
    std::wstring out = s;
    for (auto& c : out) c = static_cast<wchar_t>(std::towlower(c));
    return out;
}

std::wstring JsonString(const std::wstring& value) {
    std::wstring out;
    out.reserve(value.size() + 8);
    out.push_back(L'"');
    for (wchar_t c : value) {
        switch (c) {
        case L'"':
            out += L"\\\"";
            break;
        case L'\\':
            out += L"\\\\";
            break;
        case L'\n':
            out += L"\\n";
            break;
        case L'\r':
            out += L"\\r";
            break;
        case L'\t':
            out += L"\\t";
            break;
        default:
            if (c < 0x20) {
                wchar_t buffer[8];
                swprintf(buffer, 8, L"\\u%04x", static_cast<unsigned>(c));
                out += buffer;
            } else {
                out.push_back(c);
            }
        }
    }
    out.push_back(L'"');
    return out;
}

void PrintHelp(ConsoleOut& out) {
    out.Line(L"clearC " + std::wstring(kVersion) + L" - C盘缓存清理工具");
    out.Line();
    out.Line(L"用法: clearC [选项]");
    out.Line(L"  无参数          启动图形界面");
    out.Line(L"  --scan          扫描并打印各清理项大小");
    out.Line(L"  --list          列出全部清理项与路径模板");
    out.Line(L"  --json          配合 --scan / --list，输出 JSON");
    out.Line(L"  --self-test     运行内置自检（临时夹具，不影响真实缓存）");
    out.Line(L"  --version       显示版本");
    out.Line(L"  --help          显示帮助");
    out.Line();
    out.Line(L"提示: 系统缓存项需要管理员权限；普通权限下会标注但跳过清理。");
}

int PrintVersion(ConsoleOut& out) {
    out.Line(std::wstring(kAppName) + L" " + kVersion);
    return 0;
}

int ListEntries(ConsoleOut& out, bool json) {
    const bool elevated = IsUserAdmin();
    std::vector<EntryState> states = ResolveCatalog(elevated);
    std::size_t count = 0;
    const EntryDef* defs = Catalog(&count);
    int existing = 0;
    for (const auto& state : states) {
        if (state.exists) ++existing;
    }

    auto kindName = [](EntryKind kind) -> const wchar_t* {
        switch (kind) {
        case EntryKind::FileGlob:
            return L"file";
        case EntryKind::RecycleBin:
            return L"recycle";
        default:
            return L"dir";
        }
    };

    if (json) {
        std::wstring text = L"{\"version\":\"" + std::wstring(kVersion) + L"\",\"elevated\":" +
                            (elevated ? L"true" : L"false") + L",\"total\":" +
                            std::to_wstring(count) + L",\"existing\":" +
                            std::to_wstring(existing) + L",\"items\":[";
        for (std::size_t i = 0; i < count; ++i) {
            const EntryDef& def = defs[i];
            if (i > 0) text += L",";
            text += L"{\"name\":" + JsonString(def.name) + L",\"category\":" +
                    JsonString(CategoryName(def.category)) + L",\"kind\":\"" + kindName(def.kind) +
                    L"\",\"paths\":" + JsonString(def.paths) + L",\"consequence\":" +
                    JsonString(ConsequenceOf(def)) + L",\"exists\":" +
                    (states[i].exists ? L"true" : L"false") + L",\"defaultChecked\":" +
                    (def.defaultChecked ? L"true" : L"false") + L",\"needsAdmin\":" +
                    (def.needsAdmin ? L"true" : L"false") + L",\"resolved\":" +
                    std::to_wstring(states[i].dirs.size() + states[i].fileList.size()) +
                    L",\"skippedLinks\":" + std::to_wstring(states[i].skippedLinks) + L"}";
        }
        text += L"]}";
        out.Line(text);
        return 0;
    }

    out.Line(std::wstring(L"clearC ") + kVersion + L" 清理项（共 " + std::to_wstring(count) +
             L" 项，本机存在 " + std::to_wstring(existing) + L" 项）");
    Category current = Category::Count;
    for (std::size_t i = 0; i < count; ++i) {
        const EntryDef& def = defs[i];
        if (def.category != current) {
            current = def.category;
            out.Line();
            out.Line(std::wstring(L"[") + CategoryName(current) + L"]");
        }
        std::wstring status = states[i].exists ? L"存在" : L"缺失";
        if (def.needsAdmin) status += L"，需管理员";
        if (def.kind == EntryKind::FileGlob) status += L"，文件通配";
        if (!def.defaultChecked) status += L"，默认不勾选";
        out.Line(L"  " + std::wstring(def.name) + L"  [" + status + L"]");
        out.Line(L"      路径模板: " + std::wstring(def.paths));
        if (states[i].exists && !states[i].dirs.empty()) {
            out.Line(L"      匹配目录: " + states[i].dirs.front() +
                     (states[i].dirs.size() > 1
                          ? L" 等 " + std::to_wstring(states[i].dirs.size()) + L" 个"
                          : L""));
        }
        if (states[i].exists && !states[i].fileList.empty()) {
            out.Line(L"      匹配文件: " + states[i].fileList.front() +
                     (states[i].fileList.size() > 1
                          ? L" 等 " + std::to_wstring(states[i].fileList.size()) + L" 个"
                          : L""));
        }
        if (states[i].skippedLinks > 0) {
            out.Line(L"      链接跳过: " + std::to_wstring(states[i].skippedLinks) + L" 个目标");
        }
        out.Line(L"      说明: " + std::wstring(def.note));
        out.Line(L"      清理后果: " + std::wstring(ConsequenceOf(def)));
    }
    return 0;
}

int ScanAll(ConsoleOut& out, bool json) {
    const bool elevated = IsUserAdmin();
    std::vector<EntryState> states = ResolveCatalog(elevated);
    std::size_t count = 0;
    const EntryDef* defs = Catalog(&count);

    if (!json) out.Line(L"正在扫描缓存目录...");
    std::atomic<bool> cancel{false};
    ScanEntries(states, nullptr, cancel);

    std::vector<int> order;
    order.reserve(states.size());
    for (std::size_t i = 0; i < states.size(); ++i) {
        if (states[i].exists) order.push_back(static_cast<int>(i));
    }
    std::sort(order.begin(), order.end(), [&states](int a, int b) {
        return states[static_cast<std::size_t>(a)].bytes >
               states[static_cast<std::size_t>(b)].bytes;
    });

    ULONGLONG totalBytes = 0;
    for (int index : order) totalBytes += states[static_cast<std::size_t>(index)].bytes;

    auto kindName = [](EntryKind kind) -> const wchar_t* {
        switch (kind) {
        case EntryKind::FileGlob:
            return L"file";
        case EntryKind::RecycleBin:
            return L"recycle";
        default:
            return L"dir";
        }
    };

    if (json) {
        std::wstring text = L"{\"version\":\"" + std::wstring(kVersion) + L"\",\"elevated\":" +
                            (elevated ? L"true" : L"false") + L",\"totalBytes\":" +
                            std::to_wstring(totalBytes) + L",\"items\":[";
        for (std::size_t i = 0; i < order.size(); ++i) {
            const int index = order[i];
            const EntryDef& def = defs[index];
            const EntryState& state = states[static_cast<std::size_t>(index)];
            if (i > 0) text += L",";
            text += L"{\"name\":" + JsonString(def.name) + L",\"category\":" +
                    JsonString(CategoryName(def.category)) + L",\"kind\":\"" + kindName(def.kind) +
                    L"\",\"bytes\":" + std::to_wstring(state.bytes) + L",\"files\":" +
                    std::to_wstring(state.files) + L",\"needsAdmin\":" +
                    (def.needsAdmin ? L"true" : L"false") + L",\"denied\":" +
                    (state.denied ? L"true" : L"false") + L",\"skippedLinks\":" +
                    std::to_wstring(state.skippedLinks) + L",\"skippedReparse\":" +
                    std::to_wstring(state.skippedReparse) + L",\"dirs\":" +
                    std::to_wstring(state.dirs.size()) + L",\"fileCount\":" +
                    std::to_wstring(state.fileList.size()) + L"}";
        }
        text += L"]}";
        out.Line(text);
        return 0;
    }

    out.Line();
    for (int index : order) {
        const EntryState& state = states[static_cast<std::size_t>(index)];
        const EntryDef& def = defs[index];
        std::wstring name = def.name;
        std::wstring suffix;
        if (def.needsAdmin && !elevated) suffix += L"  [需管理员]";
        if (state.denied) suffix += L"  [部分无法读取]";
        if (state.skippedLinks > 0) suffix += L"  [链接已跳过]";
        if (state.skippedReparse > 0) suffix += L"  [含链接子目录]";
        out.Line(L"  " + name + L"  " + FormatSize(state.bytes) + suffix);
    }
    out.Line();
    out.Line(L"合计可清理: " + FormatSize(totalBytes) + L"（" + std::to_wstring(order.size()) +
             L" 项）");
    ULONGLONG freeBytes = QueryFreeSpace(L"C:\\");
    if (freeBytes > 0) out.Line(L"C盘剩余空间: " + FormatSize(freeBytes));
    if (!elevated) out.Line(L"提示: 部分系统缓存需要管理员权限，图形界面中可一键提权重启。");
    return 0;
}

// ════════════════════════════════════════
//  自检
// ════════════════════════════════════════

struct TestRunner {
    ConsoleOut& out;
    int passed = 0;
    int failed = 0;
    int skipped = 0;

    void Check(bool condition, const std::wstring& name, const std::wstring& detail = L"") {
        if (condition) {
            ++passed;
            out.Line(L"  [PASS] " + name);
        } else {
            ++failed;
            out.Line(L"  [FAIL] " + name + (detail.empty() ? L"" : L"  ->  " + detail));
        }
    }

    void Skip(const std::wstring& name, const std::wstring& reason) {
        ++skipped;
        out.Line(L"  [SKIP] " + name + L"  ->  " + reason);
    }
};

bool MakeDirs(const std::wstring& path) {
    if (IsDirectory(path)) return true;
    const std::size_t separator = path.find_last_of(L"\\/");
    if (separator != std::wstring::npos && separator > 2) {
        if (!MakeDirs(path.substr(0, separator))) return false;
    }
    return CreateDirectoryW(ToLongPath(path).c_str(), nullptr) != FALSE || IsDirectory(path);
}

bool WriteTestFile(const std::wstring& path, std::size_t bytes, bool readonly = false) {
    HANDLE handle = CreateFileW(ToLongPath(path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    std::vector<char> buffer(bytes, 'x');
    DWORD written = 0;
    const bool ok = bytes == 0 ||
                    (WriteFile(handle, buffer.data(), static_cast<DWORD>(bytes), &written, nullptr) &&
                     written == bytes);
    CloseHandle(handle);
    if (ok && readonly) {
        SetFileAttributesW(ToLongPath(path).c_str(), FILE_ATTRIBUTE_READONLY);
    }
    return ok;
}

// 通过 FSCTL_SET_REPARSE_POINT 创建目录 junction（普通用户即可，不需要管理员）
struct MountPointReparseData {
    ULONG reparseTag;
    USHORT reparseDataLength;
    USHORT reserved;
    USHORT substituteNameOffset;
    USHORT substituteNameLength;
    USHORT printNameOffset;
    USHORT printNameLength;
    WCHAR pathBuffer[1];
};

bool CreateJunction(const std::wstring& link, const std::wstring& target) {
    if (!CreateDirectoryW(ToLongPath(link).c_str(), nullptr)) return false;
    HANDLE handle = CreateFileW(ToLongPath(link).c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                                nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;

    const std::wstring substitute = L"\\??\\" + target;
    const std::size_t substituteBytes = substitute.size() * sizeof(wchar_t);
    const std::size_t printBytes = target.size() * sizeof(wchar_t);
    const std::size_t dataLength = 8 + substituteBytes + sizeof(wchar_t) + printBytes +
                                   sizeof(wchar_t);
    std::vector<BYTE> buffer(8 + dataLength, 0);
    auto* data = reinterpret_cast<MountPointReparseData*>(buffer.data());
    data->reparseTag = IO_REPARSE_TAG_MOUNT_POINT;
    data->reparseDataLength = static_cast<USHORT>(dataLength);
    data->reserved = 0;
    data->substituteNameOffset = 0;
    data->substituteNameLength = static_cast<USHORT>(substituteBytes);
    data->printNameOffset = static_cast<USHORT>(substituteBytes + sizeof(wchar_t));
    data->printNameLength = static_cast<USHORT>(printBytes);
    std::memcpy(data->pathBuffer, substitute.c_str(), substituteBytes);
    std::memcpy(reinterpret_cast<BYTE*>(data->pathBuffer) + substituteBytes + sizeof(wchar_t),
                target.c_str(), printBytes);

    DWORD returned = 0;
    const BOOL ok = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, buffer.data(),
                                    static_cast<DWORD>(buffer.size()), nullptr, 0, &returned,
                                    nullptr);
    CloseHandle(handle);
    return ok != FALSE;
}

void RemoveTreeForFixture(const std::wstring& root) {
    if (!IsDirectory(root)) return;
    DeleteStats stats;
    std::atomic<bool> cancel{false};
    CleanDirectoryContents(root, stats, cancel);
    RemoveDirectoryW(ToLongPath(root).c_str());
}

int RunSelfTest(ConsoleOut& out) {
    TestRunner runner{out};
    out.Line(L"clearC 自检（临时目录夹具，不触碰真实缓存）");
    out.Line();

    // ── 纯函数 ──
    runner.Check(FormatSize(0) == L"0 B", L"FormatSize(0)");
    runner.Check(FormatSize(1023) == L"1023 B", L"FormatSize(1023)");
    runner.Check(FormatSize(1024) == L"1.0 KB", L"FormatSize(1KB)");
    runner.Check(FormatSize(1536) == L"1.5 KB", L"FormatSize(1.5KB)");
    runner.Check(FormatSize(1048576) == L"1.0 MB", L"FormatSize(1MB)");
    runner.Check(FormatSize(1073741824ULL) == L"1.00 GB", L"FormatSize(1GB)");

    const std::wstring temp = GetEnvW(L"TEMP");
    runner.Check(!temp.empty() && _wcsicmp(ExpandEnv(L"%TEMP%").c_str(), temp.c_str()) == 0,
                 L"ExpandEnv(%TEMP%)");
    runner.Check(TrimTrailingSlash(L"C:\\Temp\\") == L"C:\\Temp", L"TrimTrailingSlash 去尾斜杠");
    runner.Check(TrimTrailingSlash(L"C:\\") == L"C:\\", L"TrimTrailingSlash 保留盘符根");

    runner.Check(!IsSafeToClean(L""), L"安全判定: 拒绝空路径");
    runner.Check(!IsSafeToClean(L"C:\\"), L"安全判定: 拒绝盘符根");
    runner.Check(!IsSafeToClean(L"C:\\Windows"), L"安全判定: 拒绝 Windows 根");
    runner.Check(!IsSafeToClean(L"C:\\Windows\\System32"), L"安全判定: 拒绝 System32");
    runner.Check(!IsSafeToClean(GetEnvW(L"USERPROFILE")), L"安全判定: 拒绝用户目录根");
    runner.Check(!IsSafeToClean(GetEnvW(L"LOCALAPPDATA")), L"安全判定: 拒绝 LocalAppData 根");
    runner.Check(!IsSafeToClean(GetEnvW(L"USERPROFILE") + L"\\AppData"),
                 L"安全判定: 拒绝受保护目录的上级");
    runner.Check(!IsSafeToClean(GetEnvW(L"TEMP") + L"\\.."), L"安全判定: 拒绝 .. 归一化后的受保护目录");
    runner.Check(!IsSafeToClean(GetEnvW(L"SystemRoot")), L"安全判定: 拒绝 SystemRoot");
    runner.Check(!IsSafeToClean(GetEnvW(L"APPDATA")), L"安全判定: 拒绝 AppData 根");
    runner.Check(!IsSafeToClean(L"\\\\?\\C:\\Windows"), L"安全判定: 拒绝 \\\\?\\ 设备路径");
    runner.Check(!IsSafeToClean(L"\\\\server\\share\\cache"), L"安全判定: 拒绝 UNC 路径");
    runner.Check(IsSafeToClean(L"C:\\Windows\\Temp"), L"安全判定: 允许 Windows\\Temp");
    runner.Check(IsSafeToClean(temp), L"安全判定: 允许 %TEMP%");

    runner.Check(WildcardMatch(L"Cach*", L"Cache"), L"通配匹配: Cach* -> Cache");
    runner.Check(!WildcardMatch(L"Cach*", L"Code Cache"), L"通配匹配: 不匹配无关项");

    // ── 清理后果说明（点击名称时展示，必须逐项齐备）──
    std::size_t defCount = 0;
    const EntryDef* defs = Catalog(&defCount);
    bool allHaveText = true;
    bool allLongEnough = true;
    std::wstring firstMissing;
    for (std::size_t i = 0; i < defCount; ++i) {
        if (!defs[i].consequence || !*defs[i].consequence) {
            allHaveText = false;
            if (firstMissing.empty()) firstMissing = defs[i].name;
        }
        if (std::wstring(ConsequenceOf(defs[i])).size() < 12) allLongEnough = false;
    }
    runner.Check(defCount == 97, L"清理项目录共 97 项", std::to_wstring(defCount));
    runner.Check(allHaveText, L"每个清理项都有专属后果说明", firstMissing);
    runner.Check(allLongEnough, L"后果说明文案长度合理（非占位符）");
    const EntryDef probe = {L"临时条目", L"", Category::System, false, false, Risk::Caution,
                            L"", nullptr, EntryKind::Directory};
    runner.Check(std::wstring(ConsequenceOf(probe)).size() > 12,
                 L"后果说明兜底: 新增条目不会显示空白窗口");

    // ── 夹具 ──
    wchar_t pidText[32];
    swprintf(pidText, 32, L"%lu", static_cast<unsigned long>(GetCurrentProcessId()));
    const std::wstring root = JoinPath(temp, std::wstring(L"clearC-selftest-") + pidText);
    if (!MakeDirs(root)) {
        out.Line(L"无法创建临时夹具目录: " + root);
        return 1;
    }

    const std::wstring data = JoinPath(root, L"data");
    const std::wstring sub = JoinPath(data, L"sub");
    const std::wstring outside = JoinPath(root, L"outside");
    const std::wstring link = JoinPath(data, L"link");
    bool junctionCreated = false;
    bool fixtureOk = MakeDirs(sub) && MakeDirs(outside) &&
                     WriteTestFile(JoinPath(data, L"a.txt"), 100) &&
                     WriteTestFile(JoinPath(sub, L"b.txt"), 200) &&
                     WriteTestFile(JoinPath(sub, L"c.txt"), 300) &&
                     WriteTestFile(JoinPath(data, L"readonly.txt"), 40, true) &&
                     WriteTestFile(JoinPath(outside, L"sentinel.bin"), 500);
    runner.Check(fixtureOk, L"创建测试夹具");
    if (!fixtureOk) {
        RemoveTreeForFixture(root);
        return 1;
    }

    // ── 目录大小 ──
    std::atomic<bool> cancel{false};
    SizeInfo sizeInfo = DirSize(data, cancel);
    runner.Check(sizeInfo.bytes == 640, L"DirSize 统计字节数（100+200+300+40=640）",
                 std::to_wstring(sizeInfo.bytes));
    runner.Check(sizeInfo.files == 4, L"DirSize 统计文件数",
                 std::to_wstring(sizeInfo.files));

    // ── 通配展开 ──
    MakeDirs(JoinPath(root, L"glob\\A\\caches"));
    MakeDirs(JoinPath(root, L"glob\\B\\caches"));
    MakeDirs(JoinPath(root, L"glob\\C\\other"));
    const std::vector<std::wstring> globMatches =
        ExpandGlob(JoinPath(root, L"glob\\*\\caches"));
    runner.Check(globMatches.size() == 2, L"通配展开: \\*\\caches 命中 2 项",
                 std::to_wstring(globMatches.size()));
    const std::vector<std::wstring> templateMatches =
        ExpandTemplates(JoinPath(root, L"glob\\*\\caches") + L";" + JoinPath(root, L"glob\\C\\other"));
    runner.Check(templateMatches.size() == 3, L"多模板展开 + 去重",
                 std::to_wstring(templateMatches.size()));

    // ── 文件通配（缩略图/图标缓存语义：只删命中的文件） ──
    const std::wstring thumbs = JoinPath(root, L"thumbs");
    const std::wstring thumb1 = JoinPath(thumbs, L"thumbcache_1.db");
    const std::wstring thumb2 = JoinPath(thumbs, L"thumbcache_2.db");
    const std::wstring keepFile = JoinPath(thumbs, L"keep.txt");
    MakeDirs(thumbs);
    const bool thumbFixture = WriteTestFile(thumb1, 100) && WriteTestFile(thumb2, 200) &&
                              WriteTestFile(keepFile, 50);
    runner.Check(thumbFixture, L"创建文件通配夹具");
    if (thumbFixture) {
        const std::vector<std::wstring> fileMatches =
            ExpandFileTemplates(JoinPath(thumbs, L"thumbcache_*.db"));
        runner.Check(fileMatches.size() == 2, L"文件通配命中 2 个文件",
                     std::to_wstring(fileMatches.size()));
        ULONGLONG fileBytes = 0;
        for (const auto& file : fileMatches) fileBytes += FileSizeBytes(file);
        runner.Check(fileBytes == 300, L"文件通配统计字节数",
                     std::to_wstring(fileBytes));
        int deleted = 0;
        for (const auto& file : fileMatches) {
            if (DeleteFileForce(file)) ++deleted;
        }
        runner.Check(deleted == 2 && !FileExists(thumb1) && !FileExists(thumb2) &&
                         FileExists(keepFile),
                     L"文件通配型清理只删除命中的文件");
    }

    // ── junction 不跟随 ──
    if (CreateJunction(link, outside)) {
        junctionCreated = true;
        SizeInfo withLink = DirSize(data, cancel);
        runner.Check(withLink.bytes == 640, L"DirSize 跳过 junction（不重复统计外部目录）",
                     std::to_wstring(withLink.bytes));
        runner.Check(withLink.skippedReparse >= 1, L"DirSize 记录跳过的重解析点");
    } else {
        runner.Skip(L"junction 不跟随", L"当前环境无法创建 junction（不影响其他用例）");
    }

    // ── 缓存项自身是 junction（例如 %TEMP% 被重定向）：整体跳过，不清空目标 ──
    const std::wstring victim = JoinPath(root, L"victim");
    const std::wstring linkRoot = JoinPath(root, L"cache-link");
    MakeDirs(victim);
    WriteTestFile(JoinPath(victim, L"IMPORTANT.txt"), 123);
    if (CreateJunction(linkRoot, victim)) {
        DeleteStats linkStats;
        CleanDirectoryContents(linkRoot, linkStats, cancel);
        runner.Check(linkStats.skippedReparse >= 1,
                     L"目标本身是 junction 时整体跳过");
        runner.Check(FileExists(JoinPath(victim, L"IMPORTANT.txt")),
                     L"目标为 junction 时未清理其指向的用户数据");
        RemoveDirectoryW(ToLongPath(linkRoot).c_str());
    } else {
        runner.Skip(L"缓存项自身是 junction", L"当前环境无法创建 junction");
    }

    // ── 清理 ──
    DeleteStats stats;
    CleanDirectoryContents(data, stats, cancel);
    runner.Check(stats.bytes == 640, L"CleanDirectoryContents 删除字节数",
                 std::to_wstring(stats.bytes));
    runner.Check(stats.files == 4, L"CleanDirectoryContents 删除文件数",
                 std::to_wstring(stats.files));
    runner.Check(stats.failed == 0, L"CleanDirectoryContents 无失败",
                 stats.firstError);
    runner.Check(IsDirectory(data), L"清理后保留目标目录本身");
    runner.Check(!FileExists(JoinPath(data, L"a.txt")), L"清理后普通文件已删除");
    runner.Check(!FileExists(JoinPath(data, L"readonly.txt")), L"清理后只读文件已删除");
    if (junctionCreated) {
        runner.Check(IsDirectory(link) && IsReparsePoint(link), L"清理时保留 junction 本身");
        runner.Check(FileExists(JoinPath(outside, L"sentinel.bin")),
                     L"清理未删除 junction 指向的外部内容");
        RemoveDirectoryW(ToLongPath(link).c_str());
    }

    // ── 长路径（> MAX_PATH） ──
    std::wstring longPath = JoinPath(root, L"long");
    for (int i = 0; i < 18; ++i) longPath = JoinPath(longPath, L"0123456789abcdef");
    if (MakeDirs(longPath) && WriteTestFile(JoinPath(longPath, L"deep.bin"), 5)) {
        runner.Check(longPath.size() > 260, L"长路径夹具长度 > MAX_PATH",
                     std::to_wstring(longPath.size()));
        SizeInfo longInfo = DirSize(JoinPath(root, L"long"), cancel);
        runner.Check(longInfo.bytes == 5, L"长路径目录可统计",
                     std::to_wstring(longInfo.bytes));
        DeleteStats longStats;
        CleanDirectoryContents(JoinPath(root, L"long"), longStats, cancel);
        runner.Check(longStats.failed == 0 && !FileExists(JoinPath(longPath, L"deep.bin")),
                     L"长路径目录可清理", longStats.firstError);
    } else {
        runner.Skip(L"长路径支持", L"创建超长路径失败（可能被系统策略限制）");
    }

    // ── 回收站查询（只读，不清空） ──
    ULONGLONG recycleBytes = 0;
    ULONGLONG recycleItems = 0;
    if (QueryRecycleBin(L"C:\\", &recycleBytes, &recycleItems)) {
        runner.Check(true, L"回收站查询（C盘: " + FormatSize(recycleBytes) + L"，" +
                               std::to_wstring(recycleItems) + L" 项）");
    } else {
        runner.Skip(L"回收站查询", L"SHQueryRecycleBin 返回失败");
    }

    // ── 清理夹具 ──
    RemoveTreeForFixture(root);
    runner.Check(!IsDirectory(root), L"临时夹具已清理");

    out.Line();
    out.Line(L"自检结果: " + std::to_wstring(runner.passed) + L" 通过 / " +
             std::to_wstring(runner.failed) + L" 失败 / " + std::to_wstring(runner.skipped) +
             L" 跳过");
    return runner.failed == 0 ? 0 : 1;
}

}  // namespace

int RunCli(const std::vector<std::wstring>& args, HINSTANCE) {
    ConsoleOut out;
    out.Init();

    bool json = false;
    for (const auto& arg : args) {
        if (ToLowerCopy(arg) == L"--json" || ToLowerCopy(arg) == L"-json") json = true;
    }

    std::wstring command;
    for (const auto& arg : args) {
        const std::wstring lowered = ToLowerCopy(arg);
        if (lowered == L"--help" || lowered == L"-h" || lowered == L"/?" || lowered == L"/help") {
            PrintHelp(out);
            return 0;
        }
        if (lowered == L"--version" || lowered == L"-v") {
            return PrintVersion(out);
        }
        if (lowered == L"--list") {
            command = L"list";
            continue;
        }
        if (lowered == L"--scan") {
            command = L"scan";
            continue;
        }
        if (lowered == L"--self-test" || lowered == L"--selftest") {
            command = L"selftest";
            continue;
        }
        if (lowered == L"--json" || lowered == L"-json") continue;
        out.Line(L"未知参数: " + arg);
        out.Line();
        PrintHelp(out);
        return 2;
    }

    if (command == L"list") return ListEntries(out, json);
    if (command == L"scan") return ScanAll(out, json);
    if (command == L"selftest") return RunSelfTest(out);

    PrintHelp(out);
    return 0;
}

}  // namespace clearc
