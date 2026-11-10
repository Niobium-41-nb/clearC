/*
 * clearC - 通用工具层实现
 */
#include "util.h"

#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>

#include <cstdio>
#include <cstring>
#include <cwctype>

namespace clearc {
namespace {

constexpr wchar_t kDevicePrefix[]    = L"\\\\?\\";
constexpr wchar_t kDeviceUncPrefix[] = L"\\\\?\\UNC\\";
constexpr std::size_t kMaxDepth      = 128;  // 防御异常深的目录树导致栈溢出

bool HasWildcard(const std::wstring& s) {
    return s.find_first_of(L"*?") != std::wstring::npos;
}

std::wstring FullPath(const std::wstring& p) {
    DWORD n = GetFullPathNameW(p.c_str(), 0, nullptr, nullptr);
    if (n == 0) return p;
    std::wstring buf(n, L'\0');
    DWORD m = GetFullPathNameW(p.c_str(), n, buf.data(), nullptr);
    if (m == 0 || m > n) return p;
    buf.resize(m);
    return buf;
}

// 去掉 \\?\ 或 \\?\UNC\ 前缀，便于做“危险路径”比较
std::wstring StripDevicePrefix(const std::wstring& p) {
    if (p.rfind(kDeviceUncPrefix, 0) == 0) return L"\\\\" + p.substr(8);
    if (p.rfind(kDevicePrefix, 0) == 0) return p.substr(4);
    return p;
}

bool PathEquals(const std::wstring& a, const std::wstring& b) {
    return _wcsicmp(a.c_str(), b.c_str()) == 0;
}

// target 是否是 root 本身，或 root 的上级目录（删上级会把 root 一起删掉）
bool IsSelfOrAncestorOf(const std::wstring& target, const std::wstring& root) {
    if (PathEquals(target, root)) return true;
    if (root.size() > target.size() + 1 && root[target.size()] == L'\\' &&
        _wcsnicmp(root.c_str(), target.c_str(), target.size()) == 0) {
        return true;
    }
    return false;
}

std::wstring DescribeWin32(DWORD err) {
    wchar_t* buf = nullptr;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                 FORMAT_MESSAGE_IGNORE_INSERTS,
                             nullptr, err, 0, reinterpret_cast<LPWSTR>(&buf), 0, nullptr);
    std::wstring s;
    if (n && buf) {
        s.assign(buf, n);
        while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n' || s.back() == L' ')) s.pop_back();
    }
    if (buf) LocalFree(buf);
    if (s.empty()) s = L"Windows 错误 " + std::to_wstring(err);
    return s;
}

void RecordFailure(DeleteStats& stats, const std::wstring& path, DWORD err) {
    ++stats.failed;
    if (stats.firstError.empty()) {
        stats.firstError = path + L"（" + DescribeWin32(err) + L"）";
    }
}

void DirSizeRec(const std::wstring& dir, SizeInfo& info, const std::atomic<bool>& cancel,
                std::size_t depth) {
    if (cancel.load(std::memory_order_relaxed) || depth > kMaxDepth) return;

    WIN32_FIND_DATAW fd;
    std::wstring pattern = ToLongPath(JoinPath(dir, L"*"));
    HANDLE h = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch,
                                nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_ACCESS_DENIED) info.denied = true;
        return;
    }

    do {
        if (cancel.load(std::memory_order_relaxed)) break;
        if (fd.cFileName[0] == L'.' &&
            (fd.cFileName[1] == 0 || (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0))) {
            continue;
        }
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                ++info.skippedReparse;  // 不跟随 junction / 符号链接
                continue;
            }
            ++info.dirs;
            DirSizeRec(JoinPath(dir, fd.cFileName), info, cancel, depth + 1);
        } else {
            info.bytes += (static_cast<ULONGLONG>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
            ++info.files;
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

void DeleteChildren(const std::wstring& dir, DeleteStats& stats, const std::atomic<bool>& cancel,
                    std::size_t depth) {
    if (cancel.load(std::memory_order_relaxed) || depth > kMaxDepth) return;

    WIN32_FIND_DATAW fd;
    std::wstring pattern = ToLongPath(JoinPath(dir, L"*"));
    HANDLE h = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch,
                                nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND) {
            RecordFailure(stats, dir, err);
        }
        return;
    }

    do {
        if (cancel.load(std::memory_order_relaxed)) break;
        if (fd.cFileName[0] == L'.' &&
            (fd.cFileName[1] == 0 || (fd.cFileName[1] == L'.' && fd.cFileName[2] == 0))) {
            continue;
        }
        std::wstring full = JoinPath(dir, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                ++stats.skippedReparse;  // 保留链接，避免删除链接指向的外部内容
                continue;
            }
            DeleteChildren(full, stats, cancel, depth + 1);
            if (RemoveDirectoryW(ToLongPath(full).c_str())) {
                ++stats.dirs;
            } else {
                DWORD err = GetLastError();
                if (err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND) {
                    RecordFailure(stats, full, err);
                }
            }
        } else {
            ULONGLONG size = (static_cast<ULONGLONG>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
            std::wstring longFull = ToLongPath(full);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY) {
                SetFileAttributesW(longFull.c_str(), fd.dwFileAttributes & ~FILE_ATTRIBUTE_READONLY);
            }
            if (DeleteFileW(longFull.c_str())) {
                stats.bytes += size;
                ++stats.files;
            } else {
                DWORD err = GetLastError();
                if (err != ERROR_FILE_NOT_FOUND && err != ERROR_PATH_NOT_FOUND) {
                    RecordFailure(stats, full, err);
                }
            }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

void ExpandGlobRec(const std::wstring& base, const std::vector<std::wstring>& comps,
                   std::size_t idx, std::vector<std::wstring>& out, std::size_t maxResults,
                   bool filesOnly) {
    if (out.size() >= maxResults) return;
    if (idx >= comps.size()) {
        if (filesOnly) {
            if (FileExists(base) && !IsDirectory(base)) out.push_back(base);
        } else if (IsDirectory(base)) {
            out.push_back(base);
        }
        return;
    }

    const std::wstring& comp = comps[idx];
    if (!HasWildcard(comp)) {
        ExpandGlobRec(JoinPath(base, comp), comps, idx + 1, out, maxResults, filesOnly);
        return;
    }

    const bool last = (idx + 1 == comps.size());
    std::wstring dir = base.empty() ? L"." : base;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW(ToLongPath(JoinPath(dir, L"*")).c_str(), FindExInfoBasic, &fd,
                                FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (out.size() >= maxResults) break;
        const bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        // 文件通配的末段只收文件；其余情况（中间段、目录模式末段）只收目录
        if (last && filesOnly) {
            if (isDir) continue;
        } else if (!isDir) {
            continue;
        }
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;  // 不跟随链接
        if (!WildcardMatch(comp.c_str(), fd.cFileName)) continue;
        ExpandGlobRec(JoinPath(base, fd.cFileName), comps, idx + 1, out, maxResults, filesOnly);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

std::vector<std::wstring> SplitPathComponents(const std::wstring& path) {
    std::vector<std::wstring> comps;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == L'\\' || path[i] == L'/') {
            if (i > start) comps.push_back(path.substr(start, i - start));
            start = i + 1;
        }
    }
    return comps;
}

bool AddUnique(std::vector<std::wstring>& list, const std::wstring& value) {
    for (const auto& item : list) {
        if (PathEquals(item, value)) return false;
    }
    list.push_back(value);
    return true;
}

}  // namespace

// ════════════════════════════════════════
//  环境变量与路径
// ════════════════════════════════════════

std::wstring GetEnvW(const wchar_t* name) {
    DWORD n = GetEnvironmentVariableW(name, nullptr, 0);
    if (n == 0) return L"";
    std::wstring s(n, L'\0');
    DWORD m = GetEnvironmentVariableW(name, s.data(), n);
    if (m == 0 || m >= n) return L"";
    s.resize(m);
    return s;
}

std::wstring ExpandEnv(const std::wstring& s) {
    if (s.empty()) return s;
    DWORD n = ExpandEnvironmentStringsW(s.c_str(), nullptr, 0);
    if (n == 0) return s;
    std::wstring out(n, L'\0');
    DWORD m = ExpandEnvironmentStringsW(s.c_str(), out.data(), n);
    if (m == 0 || m > n) return s;
    out.resize(m - 1);  // 返回值包含结尾 NUL
    return out;
}

std::wstring TrimTrailingSlash(const std::wstring& path) {
    std::wstring p = path;
    while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
    if (p.size() == 3 && p[1] == L':' && (p[2] == L'\\' || p[2] == L'/')) p[2] = L'\\';
    return p;
}

std::wstring NormalizePath(const std::wstring& path) {
    if (path.empty()) return L"";
    std::wstring p = path;
    for (auto& c : p) {
        if (c == L'/') c = L'\\';
    }
    p = TrimTrailingSlash(p);
    if (p.empty()) return L"";

    // 设备路径（\\?\ / \\.\）不做 GetFullPathName 归一化，保持原样
    const bool device = p.rfind(kDevicePrefix, 0) == 0 || p.rfind(L"\\\\.\\", 0) == 0;
    if (!device) {
        // 关键：绝对路径也要归一化，否则 "C:\...\Temp\.." 这类路径可以绕过安全判定
        std::wstring full = FullPath(p);
        if (!full.empty()) p = full;
    }
    return TrimTrailingSlash(p);
}

std::wstring ToLongPath(const std::wstring& path) {
    if (path.empty()) return path;
    if (path.rfind(kDevicePrefix, 0) == 0 || path.rfind(L"\\\\.\\", 0) == 0) return path;
    if (path.rfind(L"\\\\", 0) == 0) return std::wstring(kDeviceUncPrefix) + path.substr(2);
    if (path.size() >= 3 && std::iswalpha(path[0]) && path[1] == L':' && path[2] == L'\\') {
        return std::wstring(kDevicePrefix) + path;
    }
    return path;
}

std::wstring JoinPath(const std::wstring& dir, const std::wstring& name) {
    if (dir.empty()) return name;
    if (name.empty()) return dir;
    if (dir.back() == L'\\' || dir.back() == L'/') return dir + name;
    return dir + L"\\" + name;
}

bool IsDirectory(const std::wstring& path) {
    if (path.empty()) return false;
    DWORD attr = GetFileAttributesW(ToLongPath(path).c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool FileExists(const std::wstring& path) {
    if (path.empty()) return false;
    return GetFileAttributesW(ToLongPath(path).c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool IsReparsePoint(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(ToLongPath(path).c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

// ════════════════════════════════════════
//  安全判定
// ════════════════════════════════════════

bool IsSafeToClean(const std::wstring& input) {
    if (input.empty()) return false;

    // 先挡住 "C:" / "C:\" 这类“盘符根”的原始写法：归一化会把 "C:" 变成 C 盘当前目录
    std::wstring raw = TrimTrailingSlash(input);
    for (auto& c : raw) {
        if (c == L'/') c = L'\\';
    }
    // UNC（\\server\share）与设备路径（\\?\ / \\.\）不属于 clearC 的清理范围；
    // 设备路径还会跳过 GetFullPathName 归一化，必须在入口直接拒绝。
    if (raw.rfind(L"\\\\", 0) == 0) return false;
    if (raw.size() == 2 && raw[1] == L':') return false;
    if (raw.size() == 3 && raw[1] == L':' && raw[2] == L'\\') return false;

    std::wstring p = NormalizePath(input);
    if (p.empty()) return false;
    p = StripDevicePrefix(p);

    // 归一化后必须仍是“本地盘符绝对路径”
    if (p.size() < 3 || !std::iswalpha(p[0]) || p[1] != L':' || p[2] != L'\\') return false;

    // 盘符根 / 过短路径 / 残留通配符
    if (p.size() <= 3) return false;
    if (p.find_first_of(L"*?") != std::wstring::npos) return false;

    // 受保护根：删除它们（或它们的上级）会破坏系统/丢失用户数据
    static const wchar_t* kLiteralRoots[] = {
        L"C:\\Windows",
        L"C:\\Windows\\System32",
        L"C:\\Windows\\SysWOW64",
        L"C:\\Windows\\WinSxS",
        L"C:\\Windows\\Fonts",
        L"C:\\Windows\\assembly",
        L"C:\\Windows\\servicing",
        L"C:\\Windows\\System32\\config",
        L"C:\\Program Files",
        L"C:\\Program Files (x86)",
        L"C:\\ProgramData",
        L"C:\\Users",
        L"C:\\System Volume Information",
        L"C:\\Recovery",
        L"C:\\PerfLogs",
    };

    std::vector<std::wstring> roots;
    for (const wchar_t* root : kLiteralRoots) roots.push_back(NormalizePath(root));

    const wchar_t* envNames[] = {
        L"SystemDrive", L"SystemRoot", L"windir", L"ProgramFiles", L"ProgramFiles(x86)",
        L"ProgramData", L"USERPROFILE",    L"LOCALAPPDATA", L"APPDATA",
        L"PUBLIC",
    };
    for (const wchar_t* name : envNames) {
        std::wstring value = GetEnvW(name);
        if (!value.empty()) roots.push_back(NormalizePath(value));
    }
    // 用户个人目录：桌面/文档/下载/OneDrive
    std::wstring profile = GetEnvW(L"USERPROFILE");
    if (!profile.empty()) {
        for (const wchar_t* sub : {L"Desktop", L"Documents", L"Downloads", L"Pictures",
                                   L"Videos", L"Music", L"OneDrive", L"AppData"}) {
            roots.push_back(NormalizePath(JoinPath(profile, sub)));
        }
    }

    for (const auto& root : roots) {
        if (root.empty() || root.size() <= 3) continue;
        if (IsSelfOrAncestorOf(p, root)) return false;
    }
    return true;
}

bool IsUserAdmin() {
    BOOL isAdmin = FALSE;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    PSID adminGroup = nullptr;
    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin != FALSE;
}

// ════════════════════════════════════════
//  通配展开
// ════════════════════════════════════════

bool WildcardMatch(const wchar_t* pattern, const wchar_t* text) {
    return PathMatchSpecW(text, pattern) != FALSE;
}

namespace {

std::vector<std::wstring> ExpandGlobInternal(const std::wstring& pattern, std::size_t maxResults,
                                             bool filesOnly) {
    std::vector<std::wstring> out;
    std::wstring p = NormalizePath(pattern);
    if (p.empty()) return out;

    // UNC 路径不展开通配（项目内的清理目标都是本地盘符路径）
    if (p.rfind(L"\\\\", 0) == 0 && p.rfind(kDevicePrefix, 0) != 0) {
        const bool match = filesOnly ? (FileExists(p) && !IsDirectory(p)) : IsDirectory(p);
        if (match) out.push_back(p);
        return out;
    }

    std::vector<std::wstring> comps = SplitPathComponents(p);
    if (comps.empty()) return out;
    ExpandGlobRec(L"", comps, 0, out, maxResults, filesOnly);
    return out;
}

std::vector<std::wstring> ExpandTemplatesInternal(const std::wstring& spec, std::size_t maxResults,
                                                  bool filesOnly) {
    std::vector<std::wstring> result;
    std::size_t start = 0;
    while (start <= spec.size()) {
        std::size_t pos = spec.find(L';', start);
        if (pos == std::wstring::npos) pos = spec.size();
        std::wstring item = spec.substr(start, pos - start);
        start = pos + 1;

        // 去掉首尾空白（模板常量里为了可读性偶尔会留空格）
        while (!item.empty() && (item.front() == L' ' || item.front() == L'\t')) {
            item.erase(item.begin());
        }
        while (!item.empty() && (item.back() == L' ' || item.back() == L'\t')) item.pop_back();
        if (item.empty()) continue;

        std::wstring expanded = ExpandEnv(item);
        std::vector<std::wstring> matches = ExpandGlobInternal(expanded, maxResults, filesOnly);
        for (const auto& m : matches) {
            if (!IsSafeToClean(m)) continue;  // 双保险：通配展开结果也必须通过安全判定
            AddUnique(result, m);
            if (result.size() >= maxResults) return result;
        }
        if (pos == spec.size()) break;
    }
    return result;
}

}  // namespace

std::vector<std::wstring> ExpandGlob(const std::wstring& pattern, std::size_t maxResults) {
    return ExpandGlobInternal(pattern, maxResults, false);
}

std::vector<std::wstring> ExpandFileGlob(const std::wstring& pattern, std::size_t maxResults) {
    return ExpandGlobInternal(pattern, maxResults, true);
}

std::vector<std::wstring> ExpandTemplates(const std::wstring& spec, std::size_t maxResults) {
    return ExpandTemplatesInternal(spec, maxResults, false);
}

std::vector<std::wstring> ExpandFileTemplates(const std::wstring& spec, std::size_t maxResults) {
    return ExpandTemplatesInternal(spec, maxResults, true);
}

// ════════════════════════════════════════
//  统计与删除
// ════════════════════════════════════════

SizeInfo DirSize(const std::wstring& path, const std::atomic<bool>& cancel) {
    SizeInfo info;
    if (path.empty() || cancel.load(std::memory_order_relaxed)) return info;
    if (!IsDirectory(path)) return info;
    if (IsReparsePoint(path)) {
        ++info.skippedReparse;  // 顶层本身就是链接：不跟随
        return info;
    }
    DirSizeRec(path, info, cancel, 0);
    return info;
}

void CleanDirectoryContents(const std::wstring& rawDir, DeleteStats& stats,
                            const std::atomic<bool>& cancel) {
    if (rawDir.empty()) return;
    std::wstring dir = NormalizePath(rawDir);
    if (!IsDirectory(dir)) return;
    if (IsReparsePoint(dir)) {
        ++stats.skippedReparse;
        return;
    }
    if (!IsSafeToClean(dir)) {
        ++stats.failed;
        if (stats.firstError.empty()) {
            stats.firstError = L"拒绝清理不安全路径: " + dir;
        }
        return;
    }
    DeleteChildren(dir, stats, cancel, 0);
}

ULONGLONG FileSizeBytes(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (!GetFileAttributesExW(ToLongPath(path).c_str(), GetFileExInfoStandard, &data)) return 0;
    return (static_cast<ULONGLONG>(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
}

bool DeleteFileForce(const std::wstring& path) {
    std::wstring longPath = ToLongPath(path);
    const DWORD attributes = GetFileAttributesW(longPath.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) return false;
    if (attributes & FILE_ATTRIBUTE_DIRECTORY) return false;
    if (attributes & FILE_ATTRIBUTE_READONLY) {
        SetFileAttributesW(longPath.c_str(), attributes & ~FILE_ATTRIBUTE_READONLY);
    }
    return DeleteFileW(longPath.c_str()) != FALSE;
}

bool QueryRecycleBin(const wchar_t* root, ULONGLONG* bytes, ULONGLONG* items) {
    SHQUERYRBINFO info = {};
    info.cbSize = sizeof(info);
    if (FAILED(SHQueryRecycleBinW(root, &info))) return false;
    if (bytes) *bytes = static_cast<ULONGLONG>(info.i64Size);
    if (items) *items = static_cast<ULONGLONG>(info.i64NumItems);
    return true;
}

bool EmptyRecycleBin(const wchar_t* root, ULONGLONG* bytesFreed, DWORD* errorCode) {
    ULONGLONG before = 0;
    ULONGLONG items = 0;
    QueryRecycleBin(root, &before, &items);
    HRESULT hr = SHEmptyRecycleBinW(nullptr, root,
                                    SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
    if (bytesFreed) *bytesFreed = before;
    if (errorCode) *errorCode = (hr == S_OK) ? 0 : static_cast<DWORD>(hr);
    // 空回收站时 Shell 可能返回失败，视为已完成（无可清理内容）
    if (hr == S_OK) return true;
    return before == 0 && items == 0;
}

ULONGLONG QueryFreeSpace(const wchar_t* rootPath) {
    ULONGLONG freeForCaller = 0;
    ULONGLONG total = 0;
    ULONGLONG totalFree = 0;
    if (!GetDiskFreeSpaceExW(rootPath, reinterpret_cast<PULARGE_INTEGER>(&freeForCaller),
                             reinterpret_cast<PULARGE_INTEGER>(&total),
                             reinterpret_cast<PULARGE_INTEGER>(&totalFree))) {
        return 0;
    }
    return freeForCaller;
}

// ════════════════════════════════════════
//  字符串
// ════════════════════════════════════════

std::wstring FormatSize(ULONGLONG b) {
    wchar_t buf[64];
    if (b < 1024ULL) {
        swprintf(buf, 64, L"%llu B", static_cast<unsigned long long>(b));
    } else if (b < 1024ULL * 1024ULL) {
        swprintf(buf, 64, L"%.1f KB", b / 1024.0);
    } else if (b < 1024ULL * 1024ULL * 1024ULL) {
        swprintf(buf, 64, L"%.1f MB", b / (1024.0 * 1024.0));
    } else {
        swprintf(buf, 64, L"%.2f GB", b / (1024.0 * 1024.0 * 1024.0));
    }
    return buf;
}

std::wstring FormatNumber(ULONGLONG value) {
    wchar_t buf[64];
    swprintf(buf, 64, L"%llu", static_cast<unsigned long long>(value));
    return buf;
}

std::string ToUtf8(const std::wstring& s) {
    if (s.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0,
                                nullptr, nullptr);
    if (n <= 0) return std::string();
    std::string out(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), n, nullptr,
                        nullptr);
    return out;
}

}  // namespace clearc
