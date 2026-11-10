/*
 * clearC - 通用工具层
 * 路径规范化、长路径、通配展开、目录统计、安全删除、提权判断、格式化
 */
#pragma once

#include "common.h"

#include <atomic>
#include <cstddef>
#include <string>
#include <vector>

namespace clearc {

// ── 环境变量与路径 ──
std::wstring GetEnvW(const wchar_t* name);
std::wstring ExpandEnv(const std::wstring& s);
std::wstring TrimTrailingSlash(const std::wstring& path);
std::wstring NormalizePath(const std::wstring& path);
std::wstring ToLongPath(const std::wstring& path);
std::wstring JoinPath(const std::wstring& dir, const std::wstring& name);

bool IsDirectory(const std::wstring& path);
bool FileExists(const std::wstring& path);
bool IsReparsePoint(const std::wstring& path);

// ── 安全判定：拒绝盘符根、系统关键目录、用户目录根等危险目标 ──
bool IsSafeToClean(const std::wstring& path);
bool IsUserAdmin();

// ── 通配展开 ──
// 支持路径中任意一段出现 * 或 ?，例如 %LOCALAPPDATA%\JetBrains\*\caches
std::vector<std::wstring> ExpandGlob(const std::wstring& pattern, std::size_t maxResults = 256);
// 文件通配（最后一段匹配文件而非目录），例如 ...\Explorer\thumbcache_*.db
std::vector<std::wstring> ExpandFileGlob(const std::wstring& pattern, std::size_t maxResults = 256);
// 支持用 ';' 分隔多个模板，自动去重（大小写不敏感）
std::vector<std::wstring> ExpandTemplates(const std::wstring& spec, std::size_t maxResults = 256);
std::vector<std::wstring> ExpandFileTemplates(const std::wstring& spec,
                                              std::size_t maxResults = 256);
bool WildcardMatch(const wchar_t* pattern, const wchar_t* text);

// ── 统计与删除 ──
struct SizeInfo {
    ULONGLONG bytes = 0;
    ULONGLONG files = 0;
    ULONGLONG dirs = 0;
    ULONGLONG skippedReparse = 0;
    bool denied = false;  // 枚举时遇到拒绝访问（普通权限下常见）
};

struct DeleteStats {
    ULONGLONG bytes = 0;
    ULONGLONG files = 0;
    ULONGLONG dirs = 0;
    ULONGLONG failed = 0;
    ULONGLONG skippedReparse = 0;
    std::wstring firstError;
};

// 统计目录大小；不跟随 junction/符号链接（重解析点），遇到即跳过并计数
SizeInfo DirSize(const std::wstring& path, const std::atomic<bool>& cancel);

// 只清空目录内容，保留目录本身；不删除重解析点目录；失败会累计并记录首个错误
void CleanDirectoryContents(const std::wstring& dir, DeleteStats& stats,
                            const std::atomic<bool>& cancel);

// 单个文件：去只读后删除（用于文件通配型清理项）
bool DeleteFileForce(const std::wstring& path);
ULONGLONG FileSizeBytes(const std::wstring& path);

// ── 回收站（通过 Shell API，而非直接删 $Recycle.Bin） ──
// root 为盘符根（如 L"C:\\"）；传 nullptr 表示所有盘符
bool QueryRecycleBin(const wchar_t* root, ULONGLONG* bytes, ULONGLONG* items);
bool EmptyRecycleBin(const wchar_t* root, ULONGLONG* bytesFreed, DWORD* errorCode);

ULONGLONG QueryFreeSpace(const wchar_t* rootPath);

// ── 字符串 ──
std::wstring FormatSize(ULONGLONG bytes);
std::wstring FormatNumber(ULONGLONG value);
std::string ToUtf8(const std::wstring& s);

}  // namespace clearc
