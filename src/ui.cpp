/*
 * clearC - 图形界面实现（Win32 / Common Controls）
 */
#include "ui.h"

#include "catalog.h"
#include "scan.h"
#include "util.h"

#include "resource.h"

#include <commctrl.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace clearc {
namespace {

constexpr int kFontBase = 13;
constexpr int kFontMin = 11;
constexpr int kFontMax = 26;
constexpr int kMaxSubclass = 32;
constexpr int kDefaultWidth = 940;
constexpr int kDefaultHeight = 660;
constexpr int kMinWidth = 780;
constexpr int kMinHeight = 560;

enum ControlId : int {
    ID_BTN_SCAN = 1001,
    ID_BTN_CLEAN = 1002,
    ID_BTN_ALL = 1003,
    ID_LISTVIEW = 1005,
    ID_PROGRESS = 1006,
    ID_STATUS = 1007,
    ID_SEL_LABEL = 1008,
    ID_COMBO_CAT = 1009,
    ID_BTN_ELEVATE = 1010,
};

struct UiState {
    HINSTANCE instance = nullptr;
    HWND hwnd = nullptr;
    HWND list = nullptr;
    HWND btnScan = nullptr;
    HWND btnClean = nullptr;
    HWND btnAll = nullptr;
    HWND btnElevate = nullptr;
    HWND comboCat = nullptr;
    HWND progress = nullptr;
    HWND status = nullptr;
    HWND selLabel = nullptr;
    HWND logo = nullptr;
    HWND brand = nullptr;

    HFONT font = nullptr;
    HFONT fontBold = nullptr;
    HICON icon = nullptr;
    HICON iconSm = nullptr;

    int dpi = 96;
    int fontPx = kFontBase;
    int wheelAccum = 0;

    std::vector<EntryState> states;
    std::vector<int> visible;  // 列表行号 -> states 下标
    int categoryFilter = -1;   // -1 = 全部
    bool elevated = false;
    bool populating = false;

    std::atomic<int> lastPercent{-1};
    std::atomic<bool> scanning{false};
    std::atomic<bool> cleaning{false};
    std::atomic<bool> cancel{false};

    std::mutex resultMtx;
    CleanOutcome lastClean;

    HANDLE scanThread = nullptr;
    HANDLE cleanThread = nullptr;

    HWND subWnd[kMaxSubclass] = {};
    WNDPROC subProc[kMaxSubclass] = {};
    int subCount = 0;
};

UiState g;

// ════════════════════════════════════════
//  DPI / 尺寸
// ════════════════════════════════════════

using GetDpiForWindowFn = UINT(WINAPI*)(HWND);

UINT WindowDpi(HWND hwnd) {
    static GetDpiForWindowFn getDpiForWindow = []() -> GetDpiForWindowFn {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (!user32) return nullptr;
        return reinterpret_cast<GetDpiForWindowFn>(
            reinterpret_cast<void*>(GetProcAddress(user32, "GetDpiForWindow")));
    }();
    if (getDpiForWindow) {
        UINT dpi = getDpiForWindow(hwnd);
        if (dpi != 0) return dpi;
    }
    HDC dc = GetDC(nullptr);
    UINT dpi = dc ? static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSX)) : 96;
    if (dc) ReleaseDC(nullptr, dc);
    return dpi == 0 ? 96 : dpi;
}

// 设计尺寸 -> 当前 DPI + 字号下的像素
int S(int value) {
    return MulDiv(MulDiv(value, g.dpi, 96), g.fontPx, kFontBase);
}

// 当前窗口所在显示器的工作区（与窗口坐标处于同一坐标空间）
RECT WorkAreaFor(HWND hwnd) {
    MONITORINFO info = {};
    info.cbSize = sizeof(info);
    HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
    if (monitor && GetMonitorInfoW(monitor, &info)) return info.rcWork;
    RECT fallback = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    return fallback;
}

// 高 DPI / 小屏下把窗口钳制进工作区，避免按钮与品牌标识超出屏幕
void ClampWindowToWorkArea(HWND hwnd) {
    if (!hwnd) return;
    RECT work = WorkAreaFor(hwnd);
    RECT windowRect;
    if (!GetWindowRect(hwnd, &windowRect)) return;

    const int workWidth = work.right - work.left;
    const int workHeight = work.bottom - work.top;
    int width = windowRect.right - windowRect.left;
    int height = windowRect.bottom - windowRect.top;
    const int maxWidth = workWidth - S(16);
    const int maxHeight = workHeight - S(48);
    if (maxWidth > 0 && width > maxWidth) width = maxWidth;
    if (maxHeight > 0 && height > maxHeight) height = maxHeight;

    const int x = work.left + (workWidth - width) / 2;
    const int y = work.top + (workHeight - height) / 2;
    if (x != windowRect.left || y != windowRect.top || width != windowRect.right - windowRect.left ||
        height != windowRect.bottom - windowRect.top) {
        SetWindowPos(hwnd, nullptr, x, y, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

// ════════════════════════════════════════
//  字体 / 子类化
// ════════════════════════════════════════

void ApplyFonts() {
    if (g.font) DeleteObject(g.font);
    if (g.fontBold) DeleteObject(g.fontBold);

    const int height = -MulDiv(g.fontPx, static_cast<int>(g.dpi), 96);
    g.font = CreateFontW(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH, L"Microsoft YaHei UI");
    g.fontBold = CreateFontW(height, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                             DEFAULT_PITCH, L"Microsoft YaHei UI");

    HWND normal[] = {g.list, g.btnAll, g.comboCat, g.selLabel, g.status, g.btnElevate};
    for (HWND control : normal) {
        if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
    }
    HWND bold[] = {g.btnScan, g.btnClean, g.brand};
    for (HWND control : bold) {
        if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(g.fontBold), TRUE);
    }
}

LRESULT CALLBACK WheelForwardProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_MOUSEWHEEL && (GET_KEYSTATE_WPARAM(wp) & MK_CONTROL)) {
        return SendMessageW(GetParent(hwnd), WM_MOUSEWHEEL, wp, lp);
    }
    for (int i = 0; i < g.subCount; ++i) {
        if (g.subWnd[i] == hwnd) return CallWindowProcW(g.subProc[i], hwnd, msg, wp, lp);
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void SubclassForWheel(HWND hwnd) {
    if (!hwnd || g.subCount >= kMaxSubclass) return;
    g.subWnd[g.subCount] = hwnd;
    g.subProc[g.subCount] =
        reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwnd, GWLP_WNDPROC));
    SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WheelForwardProc));
    ++g.subCount;
}

// ════════════════════════════════════════
//  界面状态刷新
// ════════════════════════════════════════

const EntryDef* Defs() {
    return Catalog(nullptr);
}

ULONGLONG SelectedBytes(int* selectedCount) {
    ULONGLONG bytes = 0;
    int count = 0;
    for (const auto& state : g.states) {
        if (state.exists && state.checked) {
            bytes += state.bytes;
            ++count;
        }
    }
    if (selectedCount) *selectedCount = count;
    return bytes;
}

void UpdateSelLabel() {
    int count = 0;
    ULONGLONG bytes = SelectedBytes(&count);
    std::wstring text = L"已选择: " + FormatSize(bytes) + L"（" + std::to_wstring(count) + L" 项）";
    SetWindowTextW(g.selLabel, text.c_str());
}

void UpdateAllCheckbox() {
    int visibleCount = static_cast<int>(g.visible.size());
    int checkedCount = 0;
    for (int entry : g.visible) {
        if (g.states[entry].checked) ++checkedCount;
    }
    int state = BST_UNCHECKED;
    if (visibleCount > 0 && checkedCount == visibleCount) state = BST_CHECKED;
    else if (checkedCount > 0) state = BST_INDETERMINATE;
    SendMessage(g.btnAll, BM_SETCHECK, state, 0);
}

std::wstring BuildStatusText(const std::wstring& prefix) {
    ULONGLONG total = 0;
    int items = 0;
    int adminBlocked = 0;
    int denied = 0;
    int links = 0;
    for (const auto& state : g.states) {
        if (!state.exists) continue;
        total += state.bytes;
        ++items;
        if (state.adminBlocked && state.bytes > 0) ++adminBlocked;
        if (state.denied) ++denied;
        if (state.skippedLinks > 0) ++links;
    }
    std::wstring text;
    if (!prefix.empty()) text = prefix + L" · ";
    text += L"可清理 " + std::to_wstring(items) + L" 项 · " + FormatSize(total);
    ULONGLONG freeBytes = QueryFreeSpace(L"C:\\");
    if (freeBytes > 0) text += L" · C: " + FormatSize(freeBytes) + L" 可用";
    if (adminBlocked > 0) text += L" · " + std::to_wstring(adminBlocked) + L" 需提权";
    if (denied > 0) text += L" · " + std::to_wstring(denied) + L" 不可读";
    if (links > 0) text += L" · " + std::to_wstring(links) + L" 链接跳过";
    return text;
}

void RefreshStatus(const std::wstring& prefix = L"") {
    SetWindowTextW(g.status, BuildStatusText(prefix).c_str());
}

// ════════════════════════════════════════
//  列表
// ════════════════════════════════════════

void PopulateList() {
    if (!g.list) return;
    g.populating = true;
    SendMessage(g.list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g.list);
    g.visible.clear();

    for (std::size_t i = 0; i < g.states.size(); ++i) {
        const EntryState& state = g.states[i];
        if (!state.exists) continue;
        if (g.categoryFilter >= 0 &&
            static_cast<int>(Defs()[i].category) != g.categoryFilter) {
            continue;
        }
        g.visible.push_back(static_cast<int>(i));
    }

    std::sort(g.visible.begin(), g.visible.end(), [](int a, int b) {
        const EntryState& left = g.states[static_cast<std::size_t>(a)];
        const EntryState& right = g.states[static_cast<std::size_t>(b)];
        if (left.bytes != right.bytes) return left.bytes > right.bytes;
        return wcscmp(Defs()[a].name, Defs()[b].name) < 0;
    });

    const EntryDef* defs = Defs();
    for (std::size_t row = 0; row < g.visible.size(); ++row) {
        const int entry = g.visible[row];
        const EntryDef& def = defs[entry];
        const EntryState& state = g.states[static_cast<std::size_t>(entry)];

        LVITEMW item = {};
        item.mask = LVIF_TEXT;
        item.iItem = static_cast<int>(row);
        item.pszText = const_cast<wchar_t*>(def.name);
        ListView_InsertItem(g.list, &item);

        std::wstring category = CategoryName(def.category);
        if (state.adminBlocked) category += L" · 需管理员";
        if (state.denied) category += L" · 不可读";
        if (state.skippedLinks > 0) category += L" · 链接跳过";
        ListView_SetItemText(g.list, static_cast<int>(row), 1, category.data());

        std::wstring size;
        if (state.bytes == 0) {
            if (state.denied) {
                size = state.adminBlocked ? L"需管理员" : L"无法读取";
            } else if (state.skippedLinks > 0 && state.dirs.empty() && state.fileList.empty()) {
                size = L"链接跳过";
            } else {
                size = L"0 B";
            }
        } else {
            size = FormatSize(state.bytes);
            if (state.denied) size += L"+";  // 仍有部分子目录无法读取
        }
        ListView_SetItemText(g.list, static_cast<int>(row), 2, size.data());

        std::wstring path;
        if (def.kind == EntryKind::RecycleBin) {
            path = L"（通过系统 API 清空）";
        } else if (def.kind == EntryKind::FileGlob) {
            if (!state.fileList.empty()) {
                path = state.fileList.front();
                if (state.fileList.size() > 1) {
                    path += L"  等 " + std::to_wstring(state.fileList.size()) + L" 个文件";
                }
            } else {
                path = L"（无匹配文件）";
            }
        } else if (!state.dirs.empty()) {
            path = state.dirs.front();
            if (state.dirs.size() > 1) {
                path += L"  等 " + std::to_wstring(state.dirs.size()) + L" 个位置";
            }
            if (state.skippedReparse > 0) {
                path += L"（含 " + std::to_wstring(state.skippedReparse) + L" 个链接已跳过）";
            }
        } else if (state.skippedLinks > 0) {
            path = L"（目标为链接，已跳过以避免误删外部数据）";
        }
        ListView_SetItemText(g.list, static_cast<int>(row), 3, path.data());

        ListView_SetCheckState(g.list, static_cast<int>(row), state.checked ? TRUE : FALSE);
    }

    SendMessage(g.list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g.list, nullptr, TRUE);
    g.populating = false;
    UpdateSelLabel();
    UpdateAllCheckbox();
}

// ════════════════════════════════════════
//  扫描 / 清理
// ════════════════════════════════════════

void SetBusyUi(bool busy) {
    EnableWindow(g.btnScan, busy ? FALSE : TRUE);
    EnableWindow(g.btnClean, busy ? FALSE : TRUE);
    EnableWindow(g.btnAll, busy ? FALSE : TRUE);
    EnableWindow(g.comboCat, busy ? FALSE : TRUE);
    EnableWindow(g.list, busy ? FALSE : TRUE);
    EnableWindow(g.btnElevate, busy ? FALSE : (g.elevated ? FALSE : TRUE));
}

DWORD WINAPI ScanThreadProc(LPVOID) {
    std::vector<EntryState> states = ResolveCatalog(g.elevated);
    {
        std::lock_guard<std::mutex> lock(g.resultMtx);
        for (std::size_t i = 0; i < states.size() && i < g.states.size(); ++i) {
            states[i].checked = g.states[i].checked;  // 保留用户已勾选状态
        }
    }

    ProgressFn progress = [](std::size_t done, std::size_t total) {
        const int percent = total > 0 ? static_cast<int>(done * 100 / total) : 100;
        if (g.lastPercent.exchange(percent) != percent) {
            PostMessage(g.hwnd, WM_SCAN_PROGRESS, static_cast<WPARAM>(percent), 0);
        }
    };
    ScanEntries(states, progress, g.cancel);

    {
        std::lock_guard<std::mutex> lock(g.resultMtx);
        g.states = std::move(states);
    }
    PostMessage(g.hwnd, WM_SCAN_DONE, 0, 0);
    return 0;
}

DWORD WINAPI CleanThreadProc(LPVOID param) {
    std::unique_ptr<std::vector<int>> selected(static_cast<std::vector<int>*>(param));

    std::vector<EntryState> states;
    {
        std::lock_guard<std::mutex> lock(g.resultMtx);
        states = g.states;
    }

    ProgressFn progress = [](std::size_t done, std::size_t total) {
        const int percent = total > 0 ? static_cast<int>(done * 100 / total) : 100;
        if (g.lastPercent.exchange(percent) != percent) {
            PostMessage(g.hwnd, WM_CLEAN_PROGRESS, static_cast<WPARAM>(percent), 0);
        }
    };
    CleanOutcome outcome =
        CleanEntries(states, *selected, g.elevated, progress, g.cancel);
    {
        std::lock_guard<std::mutex> lock(g.resultMtx);
        g.lastClean = outcome;
    }
    PostMessage(g.hwnd, WM_CLEAN_DONE, 0, 0);
    return 0;
}

void StopThread(HANDLE* handle, DWORD timeoutMs) {
    if (!*handle) return;
    WaitForSingleObject(*handle, timeoutMs);
    CloseHandle(*handle);
    *handle = nullptr;
}

void DoScan(HWND hwnd) {
    if (g.scanning || g.cleaning) return;
    g.scanning = true;
    g.cancel = false;
    g.lastPercent = -1;
    SetBusyUi(true);
    SetWindowTextW(g.status, L"正在扫描...");
    SendMessage(g.progress, PBM_SETPOS, 0, 0);

    StopThread(&g.scanThread, 1000);
    g.scanThread = CreateThread(nullptr, 0, ScanThreadProc, nullptr, 0, nullptr);
    if (!g.scanThread) {
        g.scanning = false;
        SetBusyUi(false);
        MessageBoxW(hwnd, L"无法创建扫描线程。", L"错误", MB_OK | MB_ICONERROR);
    }
}

void RelaunchElevated(HWND hwnd) {
    wchar_t exePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) return;
    HINSTANCE result =
        ShellExecuteW(hwnd, L"runas", exePath, nullptr, nullptr, SW_SHOWNORMAL);
    if (reinterpret_cast<INT_PTR>(result) > 32) {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    } else {
        MessageBoxW(hwnd, L"未能以管理员身份启动（可能已在 UAC 中取消）。", L"提示",
                    MB_OK | MB_ICONWARNING);
    }
}

void DoClean(HWND hwnd) {
    if (g.scanning || g.cleaning) return;

    std::vector<int> selected;
    ULONGLONG totalBytes = 0;
    int adminCount = 0;
    for (std::size_t i = 0; i < g.states.size(); ++i) {
        const EntryState& state = g.states[i];
        if (!state.exists || !state.checked) continue;
        if (state.bytes == 0 && state.files == 0) continue;  // 无内容，不必处理
        selected.push_back(static_cast<int>(i));
        totalBytes += state.bytes;
        if (state.needsAdmin && !g.elevated) ++adminCount;
    }

    if (selected.empty()) {
        MessageBoxW(hwnd, L"没有可清理的内容。请先「扫描缓存」，再勾选要清理的项目。",
                    L"提示", MB_OK | MB_ICONINFORMATION);
        return;
    }

    if (adminCount > 0 && !g.elevated) {
        std::wstring message = L"有 " + std::to_wstring(adminCount) +
                               L" 项需要管理员权限，普通权限下会被跳过。\n\n"
                               L"是否以管理员身份重新启动 clearC？\n"
                               L"（选择「否」将继续清理其余项目）";
        int result = MessageBoxW(hwnd, message.c_str(), L"需要管理员权限",
                                 MB_YESNOCANCEL | MB_ICONWARNING);
        if (result == IDCANCEL) return;
        if (result == IDYES) {
            RelaunchElevated(hwnd);
            return;
        }
    }

    std::wstring confirm = L"将清理 " + std::to_wstring(selected.size()) + L" 项，预计释放 " +
                           FormatSize(totalBytes) +
                           L"。\n\n建议先关闭浏览器 / IDE / 聊天工具，"
                           L"被占用的文件会自动跳过。\n\n是否继续？";
    if (MessageBoxW(hwnd, confirm.c_str(), L"确认清理",
                    MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
        return;
    }

    g.cleaning = true;
    g.cancel = false;
    g.lastPercent = -1;
    SetBusyUi(true);
    SetWindowTextW(g.status, L"正在清理...");
    SendMessage(g.progress, PBM_SETPOS, 0, 0);

    StopThread(&g.cleanThread, 1000);
    auto* payload = new std::vector<int>(std::move(selected));
    g.cleanThread = CreateThread(nullptr, 0, CleanThreadProc, payload, 0, nullptr);
    if (!g.cleanThread) {
        delete payload;
        g.cleaning = false;
        SetBusyUi(false);
        MessageBoxW(hwnd, L"无法创建清理线程。", L"错误", MB_OK | MB_ICONERROR);
    }
}

void SetAllVisible(bool checked) {
    g.populating = true;
    for (std::size_t row = 0; row < g.visible.size(); ++row) {
        const int entry = g.visible[row];
        g.states[static_cast<std::size_t>(entry)].checked = checked;
        ListView_SetCheckState(g.list, static_cast<int>(row), checked ? TRUE : FALSE);
    }
    g.populating = false;
    UpdateSelLabel();
    UpdateAllCheckbox();
}

void OpenEntryFolder(int entry) {
    const EntryDef& def = Defs()[entry];
    const EntryState& state = g.states[static_cast<std::size_t>(entry)];
    std::wstring target;
    if (def.kind == EntryKind::FileGlob && !state.fileList.empty()) {
        target = state.fileList.front();
        const std::size_t separator = target.find_last_of(L"\\/");
        if (separator != std::wstring::npos) target = target.substr(0, separator);
    } else if (def.kind == EntryKind::Directory && !state.dirs.empty()) {
        target = state.dirs.front();
    } else {
        MessageBoxW(g.hwnd, L"该项没有可直接打开的本地目录。", L"提示",
                    MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::wstring arguments = L"\"" + target + L"\"";
    ShellExecuteW(g.hwnd, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL);
}

// ════════════════════════════════════════
//  控件创建与布局
// ════════════════════════════════════════

void LayoutControls() {
    if (!g.hwnd || !g.list) return;
    RECT rc;
    GetClientRect(g.hwnd, &rc);
    const int w = rc.right - rc.left;
    const int h = rc.bottom - rc.top;
    const int pad = S(10);
    const int rowHeight = S(28);

    int x = pad;
    MoveWindow(g.btnScan, x, S(10), S(96), rowHeight, TRUE);
    x += S(96) + S(8);
    MoveWindow(g.btnAll, x, S(10), S(66), rowHeight, TRUE);
    x += S(66) + S(8);
    MoveWindow(g.comboCat, x, S(10), S(150), S(220), TRUE);
    x += S(150) + S(8);
    MoveWindow(g.btnElevate, x, S(10), S(140), rowHeight, TRUE);

    const int brandW = S(56);
    const int brandH = S(24);
    const int textX = w - pad - brandW;
    MoveWindow(g.brand, textX, S(12), brandW, brandH, TRUE);
    MoveWindow(g.logo, textX - S(8) - brandH, S(12), brandH, brandH, TRUE);

    const int lvTop = S(48);
    const int infoH = S(20);
    const int progressH = S(18);
    const int cleanH = S(32);
    const int bottom = infoH + S(4) + progressH + S(10) + cleanH + S(10);
    int lvBottom = h - bottom;
    if (lvBottom < lvTop + S(140)) lvBottom = lvTop + S(140);
    MoveWindow(g.list, pad, lvTop, w - 2 * pad, lvBottom - lvTop, TRUE);

    const int infoY = lvBottom + S(2);
    MoveWindow(g.selLabel, S(12), infoY, S(240), infoH, TRUE);
    MoveWindow(g.status, S(256), infoY, w - S(268), infoH, TRUE);
    const int progressY = infoY + infoH + S(4);
    MoveWindow(g.progress, pad, progressY, w - 2 * pad, progressH, TRUE);
    const int cleanY = progressY + progressH + S(10);
    MoveWindow(g.btnClean, w / 2 - S(62), cleanY, S(124), cleanH, TRUE);

    LVCOLUMNW column = {};
    column.mask = LVCF_WIDTH;
    column.cx = S(230);
    ListView_SetColumn(g.list, 0, &column);
    column.cx = S(140);
    ListView_SetColumn(g.list, 1, &column);
    column.cx = S(110);
    ListView_SetColumn(g.list, 2, &column);
    int pathWidth = w - 2 * pad - S(480) - S(24);
    column.cx = pathWidth > S(120) ? pathWidth : S(120);
    ListView_SetColumn(g.list, 3, &column);
}

void CreateControls(HWND hwnd) {
    g.btnScan = CreateWindowW(L"BUTTON", L"扫描缓存",
                              WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd,
                              reinterpret_cast<HMENU>(ID_BTN_SCAN), nullptr, nullptr);
    g.btnAll = CreateWindowW(L"BUTTON", L"全选",
                             WS_CHILD | WS_VISIBLE | BS_AUTO3STATE, 0, 0, 0, 0, hwnd,
                             reinterpret_cast<HMENU>(ID_BTN_ALL), nullptr, nullptr);
    g.comboCat = CreateWindowW(WC_COMBOBOXW, nullptr,
                               WS_CHILD | WS_VISIBLE | WS_VSCROLL | CBS_DROPDOWNLIST, 0, 0, 0,
                               0, hwnd, reinterpret_cast<HMENU>(ID_COMBO_CAT), nullptr, nullptr);
    SendMessageW(g.comboCat, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"全部分类"));
    for (int i = 0; i < static_cast<int>(Category::Count); ++i) {
        SendMessageW(g.comboCat, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(CategoryName(static_cast<Category>(i))));
    }
    SendMessageW(g.comboCat, CB_SETCURSEL, 0, 0);

    g.btnElevate = CreateWindowW(L"BUTTON", L"以管理员身份重启",
                                 WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd,
                                 reinterpret_cast<HMENU>(ID_BTN_ELEVATE), nullptr, nullptr);
    if (g.elevated) {
        SetWindowTextW(g.btnElevate, L"已获得管理员权限");
        EnableWindow(g.btnElevate, FALSE);
    }

    const int brandW = S(56);
    const int brandH = S(24);
    g.brand = CreateWindowW(L"STATIC", L"clearC",
                            WS_CHILD | WS_VISIBLE | SS_RIGHT | SS_CENTERIMAGE, 0, 0, brandW,
                            brandH, hwnd, nullptr, nullptr, nullptr);
    g.logo = CreateWindowW(L"STATIC", nullptr, WS_CHILD | WS_VISIBLE | SS_ICON, 0, 0, brandH,
                           brandH, hwnd, nullptr, nullptr, nullptr);
    SendMessageW(g.logo, STM_SETICON, reinterpret_cast<WPARAM>(g.icon), TRUE);

    g.list = CreateWindowW(WC_LISTVIEWW, nullptr,
                           WS_CHILD | WS_VISIBLE | WS_BORDER | WS_TABSTOP | LVS_REPORT |
                               LVS_SHOWSELALWAYS,
                           0, 0, 0, 0, hwnd, reinterpret_cast<HMENU>(ID_LISTVIEW), nullptr,
                           nullptr);
    ListView_SetExtendedListViewStyle(g.list, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT |
                                                  LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

    LVCOLUMNW column = {};
    column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    column.pszText = const_cast<wchar_t*>(L"缓存项");
    column.cx = S(230);
    column.iSubItem = 0;
    ListView_InsertColumn(g.list, 0, &column);
    column.pszText = const_cast<wchar_t*>(L"分类");
    column.cx = S(140);
    column.iSubItem = 1;
    ListView_InsertColumn(g.list, 1, &column);
    column.pszText = const_cast<wchar_t*>(L"大小");
    column.cx = S(110);
    column.iSubItem = 2;
    ListView_InsertColumn(g.list, 2, &column);
    column.pszText = const_cast<wchar_t*>(L"路径");
    column.cx = S(300);
    column.iSubItem = 3;
    ListView_InsertColumn(g.list, 3, &column);

    g.selLabel = CreateWindowW(L"STATIC", L"已选择: 0 B",
                               WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, hwnd, nullptr,
                               nullptr, nullptr);
    g.status = CreateWindowW(L"STATIC", L"就绪", WS_CHILD | WS_VISIBLE | SS_RIGHT, 0, 0, 0, 0,
                             hwnd, nullptr, nullptr, nullptr);
    g.progress = CreateWindowW(PROGRESS_CLASSW, nullptr, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0,
                               hwnd, reinterpret_cast<HMENU>(ID_PROGRESS), nullptr, nullptr);
    SendMessageW(g.progress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));

    g.btnClean = CreateWindowW(L"BUTTON", L"清理选中项",
                               WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 0, 0, 0, 0, hwnd,
                               reinterpret_cast<HMENU>(ID_BTN_CLEAN), nullptr, nullptr);
    EnableWindow(g.btnClean, FALSE);

    ApplyFonts();
    RefreshStatus();

    HWND children[] = {g.btnScan, g.btnAll, g.comboCat, g.btnElevate, g.list,
                       g.selLabel, g.status, g.progress, g.btnClean, g.logo, g.brand};
    for (HWND child : children) SubclassForWheel(child);

    LayoutControls();
}

// ════════════════════════════════════════
//  窗口过程
// ════════════════════════════════════════

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        g.hwnd = hwnd;
        CreateControls(hwnd);
        return 0;

    case WM_SIZE:
        LayoutControls();
        return 0;

    case WM_DPICHANGED: {
        UINT dpi = LOWORD(wp);
        if (dpi != 0) g.dpi = static_cast<int>(dpi);
        ApplyFonts();
        const RECT* suggested = reinterpret_cast<const RECT*>(lp);
        if (suggested) {
            SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        LayoutControls();
        ClampWindowToWorkArea(hwnd);
        return 0;
    }

    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lp);
        RECT work = WorkAreaFor(hwnd);
        const int workWidth = work.right - work.left;
        const int workHeight = work.bottom - work.top;
        int minWidth = S(kMinWidth);
        int minHeight = S(kMinHeight);
        if (minWidth > workWidth) minWidth = workWidth;
        if (minHeight > workHeight) minHeight = workHeight;
        info->ptMinTrackSize.x = minWidth;
        info->ptMinTrackSize.y = minHeight;
        return 0;
    }

    case WM_MOUSEWHEEL:
        if (GET_KEYSTATE_WPARAM(wp) & MK_CONTROL) {
            g.wheelAccum += GET_WHEEL_DELTA_WPARAM(wp);
            const int steps = g.wheelAccum / WHEEL_DELTA;
            if (steps != 0) {
                g.wheelAccum -= steps * WHEEL_DELTA;
                const int next = std::clamp(g.fontPx + steps, kFontMin, kFontMax);
                if (next != g.fontPx) {
                    g.fontPx = next;
                    ApplyFonts();
                    LayoutControls();
                    wchar_t buffer[64];
                    swprintf(buffer, 64, L"字号 %d px", g.fontPx);
                    SetWindowTextW(g.status, buffer);
                }
            }
            return 0;
        }
        break;

    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wp);
        SetBkMode(dc, TRANSPARENT);
        return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_BTN_SCAN:
            DoScan(hwnd);
            break;
        case ID_BTN_CLEAN:
            DoClean(hwnd);
            break;
        case ID_BTN_ALL:
            if (HIWORD(wp) == BN_CLICKED) {
                // 三态复选框：点击循环 未选 -> 全选 -> 半选；半选时再点视为清空
                const int check = static_cast<int>(SendMessageW(g.btnAll, BM_GETCHECK, 0, 0));
                if (check == BST_INDETERMINATE) {
                    SendMessageW(g.btnAll, BM_SETCHECK, BST_UNCHECKED, 0);
                    SetAllVisible(false);
                } else {
                    SetAllVisible(check == BST_CHECKED);
                }
            }
            break;
        case ID_BTN_ELEVATE:
            RelaunchElevated(hwnd);
            break;
        case ID_COMBO_CAT:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                const int index =
                    static_cast<int>(SendMessageW(g.comboCat, CB_GETCURSEL, 0, 0));
                g.categoryFilter = index <= 0 ? -1 : index - 1;
                PopulateList();
            }
            break;
        default:
            break;
        }
        return 0;

    case WM_NOTIFY: {
        auto* header = reinterpret_cast<NMHDR*>(lp);
        if (header->hwndFrom != g.list) break;

        if (header->code == LVN_ITEMCHANGED && !g.populating) {
            auto* item = reinterpret_cast<NMLISTVIEW*>(lp);
            if (item->uChanged & LVIF_STATE) {
                const int row = item->iItem;
                if (row >= 0 && row < static_cast<int>(g.visible.size())) {
                    const int entry = g.visible[static_cast<std::size_t>(row)];
                    const bool checked =
                        ListView_GetCheckState(g.list, row) != FALSE;
                    if (g.states[static_cast<std::size_t>(entry)].checked != checked) {
                        g.states[static_cast<std::size_t>(entry)].checked = checked;
                        UpdateSelLabel();
                        UpdateAllCheckbox();
                    }
                }
            }
        } else if (header->code == NM_DBLCLK) {
            auto* activate = reinterpret_cast<NMITEMACTIVATE*>(lp);
            const int row = activate->iItem;
            if (row >= 0 && row < static_cast<int>(g.visible.size())) {
                OpenEntryFolder(g.visible[static_cast<std::size_t>(row)]);
            }
        }
        break;
    }

    case WM_SCAN_PROGRESS: {
        const int percent = static_cast<int>(wp);
        SendMessage(g.progress, PBM_SETPOS, static_cast<WPARAM>(percent), 0);
        wchar_t buffer[64];
        swprintf(buffer, 64, L"正在扫描... %d%%", percent);
        SetWindowTextW(g.status, buffer);
        return 0;
    }

    case WM_SCAN_DONE:
        g.scanning = false;
        StopThread(&g.scanThread, 2000);
        SetBusyUi(false);
        SendMessage(g.progress, PBM_SETPOS, 100, 0);
        PopulateList();
        RefreshStatus(L"扫描完成");
        return 0;

    case WM_CLEAN_PROGRESS:
        SendMessage(g.progress, PBM_SETPOS, wp, 0);
        {
            wchar_t buffer[64];
            swprintf(buffer, 64, L"正在清理... %d%%", static_cast<int>(wp));
            SetWindowTextW(g.status, buffer);
        }
        return 0;

    case WM_CLEAN_DONE: {
        g.cleaning = false;
        StopThread(&g.cleanThread, 2000);
        SetBusyUi(false);
        SendMessage(g.progress, PBM_SETPOS, 100, 0);

        CleanOutcome outcome;
        {
            std::lock_guard<std::mutex> lock(g.resultMtx);
            outcome = g.lastClean;
        }
        std::wstring message =
            outcome.failed == 0 ? L"清理完成。\n\n" : L"清理结束：有项目未能删除。\n\n";
        message += L"释放空间：" + FormatSize(outcome.freedBytes) +
                   L"\n删除文件：" + FormatNumber(outcome.deletedFiles) + L" 个";
        if (outcome.failed > 0) {
            message += L"\n失败 " + std::to_wstring(outcome.failed) +
                       L" 项（可能被占用或无权限）";
        }
        if (outcome.entriesSkippedAdmin > 0) {
            message += L"\n跳过 " + std::to_wstring(outcome.entriesSkippedAdmin) +
                       L" 项（需要管理员）";
        }
        if (outcome.skippedLinks > 0) {
            message += L"\n跳过 " + std::to_wstring(outcome.skippedLinks) +
                       L" 项（目标为 junction/符号链接）";
        }
        if (!outcome.firstError.empty()) {
            message += L"\n\n首个失败：" + outcome.firstError;
        }
        MessageBoxW(hwnd, message.c_str(), L"清理结果", MB_OK | MB_ICONINFORMATION);

        RefreshStatus(L"清理完成");
        DoScan(hwnd);
        return 0;
    }

    case WM_CLOSE:
        if (g.scanning || g.cleaning) {
            if (MessageBoxW(hwnd, L"正在扫描或清理，确定要退出吗？", L"确认退出",
                            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) {
                return 0;
            }
            g.cancel = true;
        }
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        g.cancel = true;
        StopThread(&g.scanThread, 3000);
        StopThread(&g.cleanThread, 3000);
        if (g.font) DeleteObject(g.font);
        if (g.fontBold) DeleteObject(g.fontBold);
        if (g.icon) DestroyIcon(g.icon);
        if (g.iconSm) DestroyIcon(g.iconSm);
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int RunGui(HINSTANCE instance, int showCmd) {
    INITCOMMONCONTROLSEX controls = {};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    g.instance = instance;
    g.dpi = static_cast<int>(WindowDpi(nullptr));
    g.elevated = IsUserAdmin();
    g.states = ResolveCatalog(g.elevated);

    g.icon = reinterpret_cast<HICON>(LoadImageW(
        instance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, GetSystemMetrics(SM_CXICON),
        GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR));
    g.iconSm = reinterpret_cast<HICON>(LoadImageW(
        instance, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));

    const wchar_t* className = L"ClearCMainWindow";
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WndProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = className;
    windowClass.hIcon = g.icon;
    windowClass.hIconSm = g.iconSm;
    if (!RegisterClassExW(&windowClass)) return 1;

    RECT rect = {0, 0, S(kDefaultWidth), S(kDefaultHeight)};
    AdjustWindowRectEx(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0);
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    const int screenHeight = GetSystemMetrics(SM_CYSCREEN);
    const int x = (screenWidth - width) / 2;
    const int y = (screenHeight - height) / 2;

    std::wstring title = std::wstring(kAppTitle) + L" - " + kAppName + L" " + kVersion;
    g.hwnd = CreateWindowExW(0, className, title.c_str(), WS_OVERLAPPEDWINDOW, x, y, width,
                             height, nullptr, nullptr, instance, nullptr);
    if (!g.hwnd) return 1;

    ClampWindowToWorkArea(g.hwnd);
    ShowWindow(g.hwnd, showCmd);
    UpdateWindow(g.hwnd);
    DoScan(g.hwnd);

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}

}  // namespace clearc
