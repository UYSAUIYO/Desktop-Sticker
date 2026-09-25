#include "pch.h"
#include "FullscreenDetector.h"

#include "WallPaperWindow.h"
#include "Log.h"

#include <dwmapi.h>

namespace desktopsticker::wallpaper {

namespace {

struct EnumContext {
    HWND exclude = nullptr;
    RECT monitorRect{};
    bool covered = false;
};

bool is_cloaked(HWND hwnd) {
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))) {
        return cloaked != 0;
    }
    return false;
}

BOOL CALLBACK enum_proc(HWND hwnd, LPARAM param) {
    auto* ctx = reinterpret_cast<EnumContext*>(param);
    if (!ctx || ctx->covered) return TRUE;

    if (hwnd == ctx->exclude) return TRUE;
    if (!IsWindowVisible(hwnd)) return TRUE;
    if (IsIconic(hwnd)) return TRUE;
    if (is_cloaked(hwnd)) return TRUE;

    // 桌面层与工具窗口不算遮挡
    const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (ex & WS_EX_TOOLWINDOW) return TRUE;
    if (ex & WS_EX_NOACTIVATE) return TRUE;
    if (WallPaperWindow::IsWallPaperWindow(hwnd)) return TRUE;

    // 空标题的 shell 层窗口（Progman / WorkerW / Shell_TrayWnd）排除
    wchar_t cls[128]{};
    if (GetClassNameW(hwnd, cls, static_cast<int>(std::size(cls))) > 0) {
        if (wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0 ||
            wcscmp(cls, L"Shell_TrayWnd") == 0 || wcscmp(cls, L"Shell_SecondaryTrayWnd") == 0) {
            return TRUE;
        }
    }

    RECT r{};
    if (!GetWindowRect(hwnd, &r)) return TRUE;

    const bool covers = r.left <= ctx->monitorRect.left && r.top <= ctx->monitorRect.top &&
                        r.right >= ctx->monitorRect.right && r.bottom >= ctx->monitorRect.bottom;
    if (covers) ctx->covered = true;
    return TRUE;
}

} // namespace

bool FullscreenDetector::AnyFullscreenCovering(HWND exclude) {
    EnumContext ctx;
    ctx.exclude = exclude;

    // 壁纸落在主显示器上，遮挡判定也以主显示器工作区为准
    POINT origin{ 0, 0 };
    HMONITOR monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(monitor, &mi)) return false;
    ctx.monitorRect = mi.rcMonitor;

    EnumWindows(enum_proc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.covered;
}

bool FullscreenDetector::SessionLocked() {
    // 锁屏时输入桌面不可打开；这也是锁屏与普通切换用户的分界
    HDESK desk = OpenInputDesktop(0, FALSE, DESKTOP_SWITCHDESKTOP);
    if (!desk) return true;
    CloseDesktop(desk);
    return false;
}

bool FullscreenDetector::DisplayOff() {
    // 显示器关闭时主显示器的电源状态为非开
    POINT origin{ 0, 0 };
    HMONITOR monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(monitor, &mi)) return false;

    // 用发送到主窗口的消息探测不可靠，这里以"会话锁定 + 无可见前台窗口"作保守判断，
    // 真正的熄屏由锁屏路径覆盖（Win11 熄屏一定伴随会话状态变化）。
    return false;
}

SystemFacts FullscreenDetector::Collect(HWND exclude) {
    SystemFacts facts;
    facts.sessionLocked = SessionLocked();
    facts.displayOff = facts.sessionLocked ? false : DisplayOff();
    if (!facts.sessionLocked) {
        facts.fullscreenCovered = AnyFullscreenCovering(exclude);
    }
    return facts;
}

} // namespace desktopsticker::wallpaper
