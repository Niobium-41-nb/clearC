/*
 * clearC - C盘缓存清理工具 (Win32 原生桌面应用)
 *
 * 编译 (MSYS2 MinGW-w64):
 *   windres resource.rc -o resource.o
 *   g++ -std=c++17 -O2 -o clearC.exe clearC.cpp resource.o -mwindows -lcomctl32 -lshlwapi -lole32 -luuid -static
 *
 * 清理项:
 *   uv / pip / Conda / NuGet / npm / yarn / pnpm / vcpkg 包缓存
 *   VSCode 全部缓存 (Cache, CachedData, CachedExtensions, GPUCache, cpptools, logs)
 *   CMake 缓存, 系统临时文件, Windows 更新/预取/错误报告
 */

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <commctrl.h>
#include <shlobj.h>
#include <shlwapi.h>

#include "resource.h"

#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <cstdio>

#if defined(_MSC_VER)
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
#endif

// ─── 自定义消息 ───
enum {
    WM_SCAN_UPDATE   = WM_USER + 200,
    WM_SCAN_PROGRESS = WM_USER + 201,
    WM_SCAN_DONE     = WM_USER + 202,
    WM_CLEAN_UPDATE  = WM_USER + 203,
    WM_CLEAN_DONE    = WM_USER + 204,
};

// ─── 控件 ID ───
enum {
    ID_BTN_SCAN  = 1001,
    ID_BTN_CLEAN = 1002,
    ID_BTN_ALL   = 1003,
    ID_LISTVIEW  = 1005,
    ID_PROGRESS  = 1006,
    ID_STATUS    = 1007,
    ID_SEL_LABEL = 1008,
};

// ─── 常量 ───
static constexpr int WIN_W = 760;
static constexpr int WIN_H = 540;
static constexpr int MAX_ENTRIES = 32;

// ─── 缓存项 ───
struct CacheEntry {
    wchar_t name[128];
    wchar_t path[MAX_PATH];
    ULONGLONG size;
    bool exists;
    int lvIndex;  // ListView 中的索引，-1 表示不显示
};

// ─── 全局状态 ───
static CacheEntry g_entries[MAX_ENTRIES];
static int g_count = 0;
static std::mutex g_mtx;
static std::atomic<bool> g_scanning{false};
static std::atomic<bool> g_cleaning{false};

static HWND g_hWnd      = nullptr;
static HWND g_hListView  = nullptr;
static HWND g_hBtnScan   = nullptr;
static HWND g_hBtnClean  = nullptr;
static HWND g_hBtnAll    = nullptr;
static HWND g_hBtnNone   = nullptr;
static HWND g_hProgress  = nullptr;
static HWND g_hStatus    = nullptr;
static HWND g_hSelLabel  = nullptr;
static HWND g_hLogo      = nullptr;
static HWND g_hBrand     = nullptr;
static HFONT g_hFont     = nullptr;
static HFONT g_hFontBold = nullptr;
static HICON g_hIcon     = nullptr;
static HICON g_hIconSm   = nullptr;

// ─── 顶栏品牌标识尺寸 ───
static constexpr int BRAND_ICON = 24;
static constexpr int BRAND_TEXT_W = 56;
static constexpr int BRAND_GAP = 8;

// ════════════════════════════════════════
//  工具函数
// ════════════════════════════════════════

static std::wstring GetEnv(const wchar_t* name) {
    DWORD n = GetEnvironmentVariableW(name, nullptr, 0);
    if (n == 0) return L"";
    std::wstring s(n - 1, L'\0');
    GetEnvironmentVariableW(name, &s[0], n);
    return s;
}

static std::wstring FormatSize(ULONGLONG b) {
    wchar_t buf[64];
    if (b < 1024ULL)
        swprintf(buf, 64, L"%llu B", (unsigned long long)b);
    else if (b < 1048576ULL)
        swprintf(buf, 64, L"%.1f KB", b / 1024.0);
    else if (b < 1073741824ULL)
        swprintf(buf, 64, L"%.1f MB", b / 1048576.0);
    else
        swprintf(buf, 64, L"%.2f GB", b / 1073741824.0);
    return buf;
}

static bool IsDir(const wchar_t* p) {
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

static ULONGLONG CalcDirSize(const std::wstring& dir) {
    ULONGLONG total = 0;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;
        std::wstring full = dir + L"\\" + fd.cFileName;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            total += CalcDirSize(full);
        else
            total += ((ULONGLONG)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return total;
}

static void DeleteRecursive(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return;

    if (attr & FILE_ATTRIBUTE_DIRECTORY) {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((path + L"\\*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (wcscmp(fd.cFileName, L".") != 0 && wcscmp(fd.cFileName, L"..") != 0)
                    DeleteRecursive(path + L"\\" + fd.cFileName);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        if (attr & FILE_ATTRIBUTE_READONLY)
            SetFileAttributesW(path.c_str(), attr & ~FILE_ATTRIBUTE_READONLY);
        RemoveDirectoryW(path.c_str());
    } else {
        if (attr & FILE_ATTRIBUTE_READONLY)
            SetFileAttributesW(path.c_str(), attr & ~FILE_ATTRIBUTE_READONLY);
        DeleteFileW(path.c_str());
    }
}

static void CleanDirContents(const std::wstring& dir) {
    if (!IsDir(dir.c_str())) return;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (wcscmp(fd.cFileName, L".") != 0 && wcscmp(fd.cFileName, L"..") != 0)
            DeleteRecursive(dir + L"\\" + fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

// ════════════════════════════════════════
//  缓存项管理
// ════════════════════════════════════════

static void AddEntry(const wchar_t* name, const std::wstring& path) {
    if (g_count >= MAX_ENTRIES) return;
    auto& e = g_entries[g_count++];
    wcsncpy(e.name, name, 127);
    e.name[127] = 0;
    wcsncpy(e.path, path.c_str(), MAX_PATH - 1);
    e.path[MAX_PATH - 1] = 0;
    e.size = 0;
    e.exists = false;
    e.lvIndex = -1;
}

static void InitEntries() {
    std::wstring lad = GetEnv(L"LOCALAPPDATA");
    std::wstring ad  = GetEnv(L"APPDATA");
    std::wstring up  = GetEnv(L"USERPROFILE");
    std::wstring tmp = GetEnv(L"TEMP");

    // ── 包管理器缓存 ──
    AddEntry(L"uv 包缓存",             lad + L"\\uv\\cache");
    AddEntry(L"pip 缓存",               lad + L"\\pip\\cache");
    AddEntry(L"Conda 包缓存",           up  + L"\\.conda\\pkgs");
    AddEntry(L"NuGet 包缓存",           up  + L"\\.nuget\\packages");
    AddEntry(L"npm 缓存",               ad  + L"\\npm-cache");
    AddEntry(L"yarn 缓存",              lad + L"\\Yarn\\Cache");
    AddEntry(L"pnpm store",             lad + L"\\pnpm-store");
    AddEntry(L"vcpkg 下载缓存",         lad + L"\\vcpkg\\archives");

    // ── VSCode 缓存 ──
    AddEntry(L"VSCode Cache",                ad + L"\\Code\\Cache");
    AddEntry(L"VSCode CachedData",           ad + L"\\Code\\CachedData");
    AddEntry(L"VSCode CachedExtensions",     ad + L"\\Code\\CachedExtensions");
    AddEntry(L"VSCode CachedProfileData",    ad + L"\\Code\\CachedProfileData");
    AddEntry(L"VSCode GPUCache",             ad + L"\\Code\\GPUCache");
    AddEntry(L"VSCode logs",                 ad + L"\\Code\\logs");
    AddEntry(L"VSCode C++ (cpptools)",       ad + L"\\Code\\User\\globalStorage\\ms-vscode.cpptools");

    // ── 编译器 & 构建缓存 ──
    AddEntry(L"CMake 缓存",              lad + L"\\CMake");

    // ── 系统缓存 ──
    AddEntry(L"系统临时文件",             tmp);
    AddEntry(L"Windows 更新缓存",         L"C:\\Windows\\SoftwareDistribution\\Download");
    AddEntry(L"Windows 预取",             L"C:\\Windows\\Prefetch");
    AddEntry(L"错误报告缓存",             lad + L"\\Microsoft\\Windows\\WER");
}

// ════════════════════════════════════════
//  扫描线程
// ════════════════════════════════════════

static DWORD WINAPI ScanThread(LPVOID) {
    // 计算存在的项数
    int existCount = 0;
    for (int i = 0; i < g_count; i++)
        if (g_entries[i].lvIndex >= 0) existCount++;

    int scanned = 0;
    for (int i = 0; i < g_count; i++) {
        if (!g_scanning) return 0;
        if (g_entries[i].lvIndex < 0) continue;  // 跳过不存在的项目

        std::wstring path(g_entries[i].path);
        ULONGLONG size = CalcDirSize(path);

        {
            std::lock_guard<std::mutex> lk(g_mtx);
            g_entries[i].exists = true;
            g_entries[i].size = size;
        }

        PostMessage(g_hWnd, WM_SCAN_UPDATE, (WPARAM)i, 0);
        scanned++;
        PostMessage(g_hWnd, WM_SCAN_PROGRESS,
                    (WPARAM)(scanned * 100 / existCount), 0);
    }
    PostMessage(g_hWnd, WM_SCAN_DONE, 0, 0);
    return 0;
}

// ════════════════════════════════════════
//  清理线程
// ════════════════════════════════════════

static DWORD WINAPI CleanThread(LPVOID) {
    // 收集 ListView 中选中的项（通过 lvIndex 映射回 g_entries）
    std::vector<int> selEntryIdx;
    int lvCount = ListView_GetItemCount(g_hListView);
    for (int lv = 0; lv < lvCount; lv++) {
        if (ListView_GetCheckState(g_hListView, lv)) {
            // 找到对应的 g_entries 索引
            for (int i = 0; i < g_count; i++) {
                if (g_entries[i].lvIndex == lv) {
                    selEntryIdx.push_back(i);
                    break;
                }
            }
        }
    }

    if (selEntryIdx.empty()) {
        PostMessage(g_hWnd, WM_CLEAN_DONE, 0, 0);
        return 0;
    }

    int total = (int)selEntryIdx.size();
    for (int k = 0; k < total; k++) {
        if (!g_cleaning) return 0;
        int i = selEntryIdx[k];
        CleanDirContents(std::wstring(g_entries[i].path));
        PostMessage(g_hWnd, WM_CLEAN_UPDATE,
                    (WPARAM)((k + 1) * 100 / total), 0);
    }
    PostMessage(g_hWnd, WM_CLEAN_DONE, 0, 0);
    return 0;
}

// ════════════════════════════════════════
//  UI 辅助
// ════════════════════════════════════════

static ULONGLONG GetSelectedSize() {
    ULONGLONG total = 0;
    std::lock_guard<std::mutex> lk(g_mtx);
    int lvCount = ListView_GetItemCount(g_hListView);
    for (int lv = 0; lv < lvCount; lv++) {
        if (ListView_GetCheckState(g_hListView, lv)) {
            // 找到对应的 g_entries 索引
            for (int i = 0; i < g_count; i++) {
                if (g_entries[i].lvIndex == lv) {
                    total += g_entries[i].size;
                    break;
                }
            }
        }
    }
    return total;
}

static void UpdateSelLabel() {
    ULONGLONG sz = GetSelectedSize();
    std::wstring text = L"已选择: " + FormatSize(sz);
    SetWindowTextW(g_hSelLabel, text.c_str());
}

static void SetAllChecks(bool checked) {
    int lvCount = ListView_GetItemCount(g_hListView);
    for (int lv = 0; lv < lvCount; lv++)
        ListView_SetCheckState(g_hListView, lv, checked ? TRUE : FALSE);
    SendMessage(g_hBtnAll, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    UpdateSelLabel();
}

static void DoScan() {
    if (g_scanning || g_cleaning) return;
    g_scanning = true;
    EnableWindow(g_hBtnScan, FALSE);
    EnableWindow(g_hBtnClean, FALSE);
    SetWindowTextW(g_hStatus, L"正在扫描...");
    SendMessage(g_hProgress, PBM_SETPOS, 0, 0);

    ListView_DeleteAllItems(g_hListView);
    int lvIdx = 0;
    for (int i = 0; i < g_count; i++) {
        // 只添加存在的缓存项
        if (!IsDir(g_entries[i].path)) {
            g_entries[i].lvIndex = -1;
            continue;
        }

        g_entries[i].lvIndex = lvIdx;
        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.iItem = lvIdx;
        item.pszText = g_entries[i].name;
        ListView_InsertItem(g_hListView, &item);
        ListView_SetItemText(g_hListView, lvIdx, 1, (LPWSTR)L"扫描中...");
        ListView_SetItemText(g_hListView, lvIdx, 2, (LPWSTR)g_entries[i].path);
        lvIdx++;
    }

    CreateThread(nullptr, 0, ScanThread, nullptr, 0, nullptr);
}

static void DoClean() {
    if (g_scanning || g_cleaning) return;

    ULONGLONG sz = GetSelectedSize();
    if (sz == 0) {
        MessageBoxW(g_hWnd, L"请先扫描并勾选要清理的项目。", L"提示",
                    MB_OK | MB_ICONWARNING);
        return;
    }

    std::wstring msg = L"确定要清理选中的缓存项吗？\n\n"
                       L"将释放约 " + FormatSize(sz) + L" 空间。\n"
                       L"请确保 VSCode 等程序已关闭。";
    if (MessageBoxW(g_hWnd, msg.c_str(), L"确认清理",
                    MB_YESNO | MB_ICONQUESTION) != IDYES)
        return;

    g_cleaning = true;
    EnableWindow(g_hBtnScan, FALSE);
    EnableWindow(g_hBtnClean, FALSE);
    SetWindowTextW(g_hStatus, L"正在清理...");
    SendMessage(g_hProgress, PBM_SETPOS, 0, 0);

    CreateThread(nullptr, 0, CleanThread, nullptr, 0, nullptr);
}

// ════════════════════════════════════════
//  控件创建
// ════════════════════════════════════════

static void LayoutControls(int w, int h) {
    int lvBottom = h - 110;
    MoveWindow(g_hListView, 10, 48, w - 20, lvBottom - 48, TRUE);

    int textX = w - 10 - BRAND_TEXT_W;
    MoveWindow(g_hBrand, textX, 12, BRAND_TEXT_W, BRAND_ICON, TRUE);
    MoveWindow(g_hLogo, textX - BRAND_GAP - BRAND_ICON, 12, BRAND_ICON, BRAND_ICON, TRUE);

    int infoY = lvBottom + 2;
    MoveWindow(g_hSelLabel, 12, infoY, 200, 20, TRUE);
    MoveWindow(g_hStatus, 220, infoY, w - 230, 20, TRUE);
    MoveWindow(g_hProgress, 10, infoY + 24, w - 20, 18, TRUE);
    MoveWindow(g_hBtnClean, w / 2 - 55, infoY + 48, 110, 32, TRUE);

    LVCOLUMNW col = {};
    col.mask = LVCF_WIDTH;
    col.cx = w - 350;
    ListView_SetColumn(g_hListView, 2, &col);
}

static void CreateControls(HWND hWnd) {
    RECT rc;
    GetClientRect(hWnd, &rc);
    int w = rc.right;

    g_hFont = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    g_hFontBold = CreateFontW(-13, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");

    // ── 工具栏按钮 ──
    g_hBtnScan = CreateWindowW(L"BUTTON", L"扫描缓存",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        10, 10, 90, 28, hWnd, (HMENU)(INT_PTR)ID_BTN_SCAN, nullptr, nullptr);

    g_hBtnAll = CreateWindowW(L"BUTTON", L"全选",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        110, 10, 55, 28, hWnd, (HMENU)(INT_PTR)ID_BTN_ALL, nullptr, nullptr);

    // ── 品牌标识 ──
    int textX = w - 10 - BRAND_TEXT_W;
    g_hBrand = CreateWindowW(L"STATIC", L"clearC",
        WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE,
        textX, 12, BRAND_TEXT_W, BRAND_ICON, hWnd, nullptr, nullptr, nullptr);

    g_hLogo = CreateWindowW(L"STATIC", nullptr,
        WS_CHILD | WS_VISIBLE | SS_ICON,
        textX - BRAND_GAP - BRAND_ICON, 12, BRAND_ICON, BRAND_ICON, hWnd, nullptr, nullptr, nullptr);
    SendMessageW(g_hLogo, STM_SETICON, (WPARAM)g_hIcon, TRUE);

    // ── ListView ──
    g_hListView = CreateWindowW(WC_LISTVIEWW, L"",
        WS_CHILD | WS_VISIBLE | WS_BORDER | LVS_REPORT | LVS_SHOWSELALWAYS,
        10, 48, w - 20, 340, hWnd, (HMENU)(INT_PTR)ID_LISTVIEW, nullptr, nullptr);

    ListView_SetExtendedListViewStyle(g_hListView,
        LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

    LVCOLUMNW col = {};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;

    col.pszText = (LPWSTR)L"缓存项";
    col.cx = 200;
    col.iSubItem = 0;
    ListView_InsertColumn(g_hListView, 0, &col);

    col.pszText = (LPWSTR)L"大小";
    col.cx = 100;
    col.iSubItem = 1;
    ListView_InsertColumn(g_hListView, 1, &col);

    col.pszText = (LPWSTR)L"路径";
    col.cx = w - 350;
    col.iSubItem = 2;
    ListView_InsertColumn(g_hListView, 2, &col);

    // ── 信息行 ──
    g_hSelLabel = CreateWindowW(L"STATIC", L"已选择: 0 B",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        12, 395, 200, 20, hWnd, (HMENU)(INT_PTR)ID_SEL_LABEL, nullptr, nullptr);

    g_hStatus = CreateWindowW(L"STATIC", L"就绪 - 点击「扫描缓存」开始",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        220, 395, w - 230, 20, hWnd, (HMENU)(INT_PTR)ID_STATUS, nullptr, nullptr);

    // ── 进度条 ──
    g_hProgress = CreateWindowW(PROGRESS_CLASSW, L"",
        WS_CHILD | WS_VISIBLE,
        10, 420, w - 20, 18, hWnd, (HMENU)(INT_PTR)ID_PROGRESS, nullptr, nullptr);

    // ── 清理按钮 ──
    g_hBtnClean = CreateWindowW(L"BUTTON", L"清理选中项",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        w / 2 - 55, 448, 110, 32, hWnd, (HMENU)(INT_PTR)ID_BTN_CLEAN, nullptr, nullptr);
    EnableWindow(g_hBtnClean, FALSE);

    // ── 设置字体 ──
    HWND ctrls[] = {
        g_hBtnScan, g_hBtnAll, g_hBtnClean,
        g_hSelLabel, g_hStatus
    };
    for (auto c : ctrls)
        SendMessageW(c, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    SendMessageW(g_hBtnScan, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);
    SendMessageW(g_hBtnClean, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);
    SendMessageW(g_hBrand, WM_SETFONT, (WPARAM)g_hFontBold, TRUE);
}

// ════════════════════════════════════════
//  窗口过程
// ════════════════════════════════════════

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        CreateControls(hWnd);
        return 0;

    case WM_SIZE:
        LayoutControls(LOWORD(lp), HIWORD(lp));
        return 0;

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wp;
        SetBkMode(hdc, TRANSPARENT);
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    }

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_BTN_SCAN:  DoScan();  break;
        case ID_BTN_CLEAN: DoClean(); break;
        case ID_BTN_ALL:
            if (HIWORD(wp) == BN_CLICKED) {
                bool checked = (SendMessage(g_hBtnAll, BM_GETCHECK, 0, 0) == BST_CHECKED);
                SetAllChecks(checked);
            }
            break;
        }
        return 0;

    case WM_NOTIFY: {
        NMHDR* nm = (NMHDR*)lp;
        if (nm->hwndFrom == g_hListView && nm->code == LVN_ITEMCHANGED) {
            NMLISTVIEW* nmlv = (NMLISTVIEW*)lp;
            if (nmlv->uChanged & LVIF_STATE) {
                UpdateSelLabel();
                // 同步全选复选框状态
                int lvCount = ListView_GetItemCount(g_hListView);
                int checkedCount = 0;
                for (int lv = 0; lv < lvCount; lv++)
                    if (ListView_GetCheckState(g_hListView, lv))
                        checkedCount++;
                bool allChecked = (checkedCount == lvCount && lvCount > 0);
                bool noneChecked = (checkedCount == 0);
                SendMessage(g_hBtnAll, BM_SETCHECK,
                    allChecked ? BST_CHECKED : (noneChecked ? BST_UNCHECKED : BST_INDETERMINATE), 0);
            }
        }
        break;
    }

    case WM_SCAN_UPDATE: {
        int idx = (int)wp;
        std::lock_guard<std::mutex> lk(g_mtx);
        if (idx >= 0 && idx < g_count && g_entries[idx].lvIndex >= 0) {
            std::wstring s = FormatSize(g_entries[idx].size);
            ListView_SetItemText(g_hListView, g_entries[idx].lvIndex, 1, (LPWSTR)s.c_str());
        }
        return 0;
    }

    case WM_SCAN_PROGRESS:
        SendMessage(g_hProgress, PBM_SETPOS, (WPARAM)wp, 0);
        return 0;

    case WM_SCAN_DONE:
        g_scanning = false;
        EnableWindow(g_hBtnScan, TRUE);
        EnableWindow(g_hBtnClean, TRUE);
        SendMessage(g_hProgress, PBM_SETPOS, 100, 0);
        SetWindowTextW(g_hStatus, L"扫描完成");
        UpdateSelLabel();
        return 0;

    case WM_CLEAN_UPDATE:
        SendMessage(g_hProgress, PBM_SETPOS, (WPARAM)wp, 0);
        return 0;

    case WM_CLEAN_DONE:
        g_cleaning = false;
        EnableWindow(g_hBtnScan, TRUE);
        EnableWindow(g_hBtnClean, TRUE);
        SendMessage(g_hProgress, PBM_SETPOS, 100, 0);
        SetWindowTextW(g_hStatus, L"清理完成！");
        MessageBoxW(hWnd, L"清理完成！", L"提示", MB_OK | MB_ICONINFORMATION);
        DoScan();
        return 0;

    case WM_DESTROY:
        if (g_hFont) DeleteObject(g_hFont);
        if (g_hFontBold) DeleteObject(g_hFontBold);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

// ════════════════════════════════════════
//  入口
// ════════════════════════════════════════

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nShow) {
    INITCOMMONCONTROLSEX icex = {};
    icex.dwSize = sizeof(icex);
    icex.dwICC = ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icex);

    InitEntries();

    // 用 SM_CX*ICON 而非固定 32/16，高 DPI 下才能取到 ICO 里对应的尺寸而不是被拉伸
    g_hIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                    GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
    g_hIconSm = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                    GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);

    const wchar_t* CLS = L"ClearCWnd";
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = CLS;
    wc.hIcon = g_hIcon;
    wc.hIconSm = g_hIconSm;
    RegisterClassExW(&wc);

    int sx = GetSystemMetrics(SM_CXSCREEN);
    int sy = GetSystemMetrics(SM_CYSCREEN);
    int x = (sx - WIN_W) / 2;
    int y = (sy - WIN_H) / 2;

    RECT rc = {0, 0, WIN_W, WIN_H};
    DWORD style = WS_OVERLAPPEDWINDOW;
    AdjustWindowRect(&rc, style, FALSE);

    g_hWnd = CreateWindowExW(0, CLS, L"C盘清理工具",
        style, x, y, rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hInst, nullptr);

    ShowWindow(g_hWnd, nShow);
    UpdateWindow(g_hWnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    if (g_hIcon) DestroyIcon(g_hIcon);
    if (g_hIconSm) DestroyIcon(g_hIconSm);
    return (int)msg.wParam;
}
