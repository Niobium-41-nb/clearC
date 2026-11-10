/*
 * clearC - C盘缓存清理工具
 * 公共定义：编码、版本、窗口消息、全局常量
 */
#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>

namespace clearc {

constexpr wchar_t kAppName[]  = L"clearC";
constexpr wchar_t kAppTitle[] = L"C盘清理工具";
constexpr wchar_t kVersion[]  = L"2.0.0";

// 后台线程 -> UI 线程的消息（WM_APP 段，避免与控件通知冲突）
enum : UINT {
    WM_SCAN_PROGRESS  = WM_APP + 201,  // wParam = 百分比
    WM_SCAN_DONE      = WM_APP + 202,
    WM_CLEAN_PROGRESS = WM_APP + 203,  // wParam = 百分比
    WM_CLEAN_DONE     = WM_APP + 204,
};

}  // namespace clearc
