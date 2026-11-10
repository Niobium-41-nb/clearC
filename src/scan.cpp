/*
 * clearC - 扫描 / 清理引擎实现
 *
 * 扫描：任务队列 + 少量工作线程（目录遍历是 I/O 密集型，8 条线程已能打满 SSD）
 * 清理：单线程依次处理，避免并发删除引发的占用/权限抖动，同时便于报告失败项
 */
#include "scan.h"

#include <mutex>

namespace clearc {
namespace {

// clearC 面向 C 盘，回收站也只清理 C 盘（其他盘符的回收站不受影响）
constexpr wchar_t kRecycleRoot[] = L"C:\\";

enum class TaskKind { Directory, File, RecycleBin };

struct ScanTask {
    int entry = -1;
    TaskKind kind = TaskKind::Directory;
    std::wstring path;
};

struct ScanShared {
    std::vector<ScanTask> tasks;
    std::atomic<std::size_t> next{0};
    std::atomic<std::size_t> done{0};
    std::mutex mtx;
    std::vector<ULONGLONG> bytes;
    std::vector<ULONGLONG> files;
    std::vector<ULONGLONG> skipped;
    std::vector<bool> denied;
    const std::atomic<bool>* cancel = nullptr;
    ProgressFn progress;
};

DWORD WINAPI ScanWorkerProc(LPVOID param) {
    auto* shared = static_cast<ScanShared*>(param);
    for (;;) {
        std::size_t index = shared->next.fetch_add(1, std::memory_order_relaxed);
        if (index >= shared->tasks.size()) break;

        const ScanTask& task = shared->tasks[index];
        ULONGLONG bytes = 0;
        ULONGLONG files = 0;
        ULONGLONG skipped = 0;
        bool denied = false;
        if (task.kind == TaskKind::RecycleBin) {
            ULONGLONG rbBytes = 0;
            ULONGLONG rbItems = 0;
            if (QueryRecycleBin(kRecycleRoot, &rbBytes, &rbItems)) {
                bytes = rbBytes;
                files = rbItems;
            }
        } else if (task.kind == TaskKind::File) {
            if (FileExists(task.path) && !IsDirectory(task.path)) {
                bytes = FileSizeBytes(task.path);
                files = 1;
            }
        } else {
            SizeInfo info = DirSize(task.path, *shared->cancel);
            bytes = info.bytes;
            files = info.files;
            skipped = info.skippedReparse;
            denied = info.denied;
        }

        {
            std::lock_guard<std::mutex> lock(shared->mtx);
            const std::size_t i = static_cast<std::size_t>(task.entry);
            shared->bytes[i] += bytes;
            shared->files[i] += files;
            shared->skipped[i] += skipped;
            if (denied) shared->denied[i] = true;
        }

        std::size_t done = shared->done.fetch_add(1, std::memory_order_relaxed) + 1;
        if (shared->progress) shared->progress(done, shared->tasks.size());
    }
    return 0;
}

std::size_t WorkerCount(std::size_t taskCount) {
    SYSTEM_INFO si = {};
    GetSystemInfo(&si);
    std::size_t cpus = si.dwNumberOfProcessors > 0 ? si.dwNumberOfProcessors : 1;
    std::size_t workers = cpus < 8 ? cpus : 8;
    if (workers < 1) workers = 1;
    if (workers > taskCount) workers = taskCount;
    return workers;
}

bool CancelRequested(const std::atomic<bool>& cancel) {
    return cancel.load(std::memory_order_relaxed);
}

}  // namespace

void ScanEntries(std::vector<EntryState>& states, const ProgressFn& progress,
                 const std::atomic<bool>& cancel) {
    const std::size_t count = states.size();
    for (auto& state : states) {
        state.bytes = 0;
        state.files = 0;
        state.skippedReparse = 0;
        state.denied = false;
    }
    if (count == 0) return;

    ScanShared shared;
    shared.bytes.assign(count, 0);
    shared.files.assign(count, 0);
    shared.skipped.assign(count, 0);
    shared.denied.assign(count, false);
    shared.cancel = &cancel;
    shared.progress = progress;

    for (std::size_t i = 0; i < count; ++i) {
        if (!states[i].exists) continue;
        if (states[i].recycleBin) {
            ScanTask task;
            task.entry = static_cast<int>(i);
            task.kind = TaskKind::RecycleBin;
            shared.tasks.push_back(std::move(task));
            continue;
        }
        if (states[i].fileMode) {
            for (const auto& file : states[i].fileList) {
                ScanTask task;
                task.entry = static_cast<int>(i);
                task.kind = TaskKind::File;
                task.path = file;
                shared.tasks.push_back(std::move(task));
            }
            continue;
        }
        for (const auto& dir : states[i].dirs) {
            ScanTask task;
            task.entry = static_cast<int>(i);
            task.kind = TaskKind::Directory;
            task.path = dir;
            shared.tasks.push_back(std::move(task));
        }
    }

    if (shared.tasks.empty()) {
        if (progress) progress(1, 1);
        return;
    }

    const std::size_t workers = WorkerCount(shared.tasks.size());
    std::vector<HANDLE> threads;
    threads.reserve(workers);
    for (std::size_t i = 0; i < workers; ++i) {
        HANDLE thread = CreateThread(nullptr, 0, ScanWorkerProc, &shared, 0, nullptr);
        if (thread) threads.push_back(thread);
    }
    if (threads.empty()) {
        // 线程创建失败时退化为同步执行
        ScanWorkerProc(&shared);
    } else {
        WaitForMultipleObjects(static_cast<DWORD>(threads.size()), threads.data(), TRUE, INFINITE);
        for (HANDLE thread : threads) CloseHandle(thread);
    }

    for (std::size_t i = 0; i < count; ++i) {
        states[i].bytes = shared.bytes[i];
        states[i].files = shared.files[i];
        states[i].skippedReparse = shared.skipped[i];
        states[i].denied = shared.denied[i];
    }
    if (progress) progress(shared.tasks.size(), shared.tasks.size());
}

CleanOutcome CleanEntries(std::vector<EntryState>& states, const std::vector<int>& selected,
                          bool elevated, const ProgressFn& progress,
                          const std::atomic<bool>& cancel) {
    CleanOutcome outcome;
    const std::size_t total = selected.size();
    if (total == 0) return outcome;

    std::size_t processed = 0;
    for (int index : selected) {
        if (index < 0 || static_cast<std::size_t>(index) >= states.size()) continue;
        if (CancelRequested(cancel)) break;
        EntryState& state = states[static_cast<std::size_t>(index)];

        if (state.needsAdmin && !elevated) {
            ++outcome.entriesSkippedAdmin;
            ++processed;
            if (progress) progress(processed, total);
            continue;
        }

        if (state.recycleBin) {
            ULONGLONG freed = 0;
            DWORD error = 0;
            if (EmptyRecycleBin(kRecycleRoot, &freed, &error)) {
                outcome.freedBytes += freed;
                outcome.deletedFiles += state.files;
                ++outcome.entriesCleaned;
            } else {
                ++outcome.failed;
                if (outcome.firstError.empty()) {
                    outcome.firstError = L"清空回收站失败（错误码 " + std::to_wstring(error) + L"）";
                }
            }
        } else if (state.fileMode) {
            DeleteStats stats;
            for (const auto& file : state.fileList) {
                if (CancelRequested(cancel)) break;
                const ULONGLONG size = FileSizeBytes(file);
                if (DeleteFileForce(file)) {
                    stats.bytes += size;
                    ++stats.files;
                } else {
                    ++stats.failed;
                    if (stats.firstError.empty()) {
                        stats.firstError = L"无法删除文件: " + file;
                    }
                }
            }
            outcome.freedBytes += stats.bytes;
            outcome.deletedFiles += stats.files;
            outcome.failed += stats.failed;
            if (!stats.firstError.empty() && outcome.firstError.empty()) {
                outcome.firstError = stats.firstError;
            }
            ++outcome.entriesCleaned;
        } else {
            DeleteStats stats;
            for (const auto& dir : state.dirs) {
                if (CancelRequested(cancel)) break;
                CleanDirectoryContents(dir, stats, cancel);
            }
            outcome.freedBytes += stats.bytes;
            outcome.deletedFiles += stats.files;
            outcome.deletedDirs += stats.dirs;
            outcome.failed += stats.failed;
            outcome.skippedReparse += stats.skippedReparse;
            outcome.skippedLinks += state.skippedLinks;
            if (!stats.firstError.empty() && outcome.firstError.empty()) {
                outcome.firstError = stats.firstError;
            }
            ++outcome.entriesCleaned;
        }

        ++processed;
        if (progress) progress(processed, total);
    }
    return outcome;
}

}  // namespace clearc
