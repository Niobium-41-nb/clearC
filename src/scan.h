/*
 * clearC - 扫描 / 清理引擎
 */
#pragma once

#include "common.h"
#include "util.h"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace clearc {

// 单个清理项在运行期的状态
struct EntryState {
    std::vector<std::wstring> dirs;      // 已解析、通过安全检查的目录
    std::vector<std::wstring> fileList;  // 文件通配型清理项命中的文件
    bool exists = false;                 // 是否至少有一个目标存在
    bool checked = false;                // 用户勾选状态
    bool adminBlocked = false;           // 需要管理员但当前未提权
    bool needsAdmin = false;             // 该项需要管理员权限
    bool recycleBin = false;             // 回收站特殊项
    bool fileMode = false;               // 文件通配型（删除文件本身）
    bool denied = false;                 // 枚举时被拒绝访问，大小未知
    ULONGLONG skippedLinks = 0;          // 因目标是 junction/符号链接而跳过
    ULONGLONG bytes = 0;
    ULONGLONG files = 0;
    ULONGLONG skippedReparse = 0;
};

struct CleanOutcome {
    ULONGLONG freedBytes = 0;
    ULONGLONG deletedFiles = 0;
    ULONGLONG deletedDirs = 0;
    ULONGLONG failed = 0;
    ULONGLONG skippedReparse = 0;
    ULONGLONG skippedLinks = 0;
    int entriesCleaned = 0;
    int entriesSkippedAdmin = 0;
    std::wstring firstError;
};

// done: 已完成任务数，total: 任务总数
using ProgressFn = std::function<void(std::size_t done, std::size_t total)>;

// 并行统计所有已解析目标的大小；结果写回 states[i]
void ScanEntries(std::vector<EntryState>& states, const ProgressFn& progress,
                 const std::atomic<bool>& cancel);

// 依次清理选中的项（回收站调用 Shell API，文件项删除文件本身）
CleanOutcome CleanEntries(std::vector<EntryState>& states, const std::vector<int>& selected,
                          bool elevated, const ProgressFn& progress,
                          const std::atomic<bool>& cancel);

}  // namespace clearc
