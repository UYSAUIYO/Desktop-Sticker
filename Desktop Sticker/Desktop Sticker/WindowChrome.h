#pragma once
#include <windows.h>

namespace desktopsticker::app {

// 不在任务栏显示 + 常驻置顶（压过其他置顶 overlay；显示时还会再补一次 HWND_TOPMOST）
inline void MakeTopmostToolWindow(HWND hwnd) {
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE,
                      GetWindowLongPtrW(hwnd, GWL_EXSTYLE) | WS_EX_TOOLWINDOW | WS_EX_TOPMOST);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

// 去掉系统/DWM 边框白边：改 WS_POPUP 并强制 Win11 圆角
inline void ApplyBorderlessRounded(HWND hwnd) {
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    style &= ~(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU);
    style |= WS_POPUP;
    SetWindowLongPtrW(hwnd, GWL_STYLE, style);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

    using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, void*, DWORD);
    static DwmSetWindowAttributeFn dwmSet = []() -> DwmSetWindowAttributeFn {
        HMODULE dwm = GetModuleHandleW(L"dwmapi.dll");
        return dwm ? reinterpret_cast<DwmSetWindowAttributeFn>(
                         GetProcAddress(dwm, "DwmSetWindowAttribute"))
                   : nullptr;
    }();
    if (dwmSet) {
        UINT pref = 2; // DWMWCP_ROUND
        dwmSet(hwnd, 33 /*DWMWA_WINDOW_CORNER_PREFERENCE*/, &pref, sizeof(pref));
    }
}

} // namespace desktopsticker::app
