#include "pch.h"
#include "desktopsticker/MouseInputForwarder.h"

#include <commctrl.h>
#include <cstdlib>

namespace desktopsticker {

namespace {
MouseInputForwarder* g_forwarder = nullptr;
} // namespace

MouseInputForwarder::MouseInputForwarder(DesktopShellIntegration* shell, ZoneResolver resolver)
    : shell_(shell), resolveZone_(std::move(resolver)) {}

MouseInputForwarder::~MouseInputForwarder() {
    Stop();
}

void MouseInputForwarder::Start() {
    if (hook_) return;
    g_forwarder = this;
    hook_ = SetWindowsHookExW(WH_MOUSE_LL, MouseHookProc,
                              GetModuleHandleW(L"DesktopSticker.Features.dll"), 0);
}

void MouseInputForwarder::Stop() {
    if (hook_) {
        UnhookWindowsHookEx(hook_);
        hook_ = nullptr;
    }
    if (g_forwarder == this) g_forwarder = nullptr;
}

LRESULT CALLBACK MouseInputForwarder::MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        auto* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
        auto* self = g_forwarder;
        if (self && info) {
            const POINT pt = info->pt;
            ZoneWindow* zone = self->resolveZone_ ? self->resolveZone_(pt) : nullptr; // 已过滤隐藏分区
            if (zone && !zone->IsEmbedded()) {
                // 降级模式（分区未嵌入桌面、处在最底层）：系统把输入路由给上层窗口，
                // 这里转发给分区并吞掉原始事件；嵌入模式下系统直接命中分区子窗口，
                // 绝不能重复转发，否则每次点击被处理两次（双击标题会折叠又展开）
                const UINT msg = static_cast<UINT>(wParam);
                if (msg == WM_LBUTTONDOWN || msg == WM_LBUTTONUP || msg == WM_RBUTTONUP) {
                    return self->ForwardMouseToZone(zone, msg, pt);
                }
            } else if (!zone && wParam == WM_LBUTTONDOWN) {
                self->DetectDesktopBlankDoubleClick(pt); // 双击桌面空白 → 干净桌面
            }
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT MouseInputForwarder::ForwardMouseToZone(ZoneWindow* zone, UINT msg, const POINT& pt) {
    // 系统只为命中窗口合成 WM_LBUTTONDBLCLK，最底层分区收不到，双击在这里手动合成
    bool dblclk = false;
    if (msg == WM_LBUTTONDOWN) {
        const DWORD now = GetTickCount();
        if (forwardDownZone_ == zone &&
            now - forwardDownTick_ <= GetDoubleClickTime() &&
            std::abs(pt.x - forwardDownPt_.x) <= GetSystemMetrics(SM_CXDOUBLECLK) / 2 &&
            std::abs(pt.y - forwardDownPt_.y) <= GetSystemMetrics(SM_CYDOUBLECLK) / 2) {
            dblclk = true;
            forwardDownZone_ = nullptr;
        } else {
            forwardDownZone_ = zone;
            forwardDownTick_ = now;
            forwardDownPt_ = pt;
        }
    }
    POINT client = pt;
    ScreenToClient(zone->Hwnd(), &client);
    WPARAM wp = 0;
    if (msg == WM_LBUTTONDOWN || dblclk) wp = MK_LBUTTON;
    if (msg == WM_RBUTTONUP) wp = MK_RBUTTON;
    // PostMessage 而非 SendMessage：绝不能在 hook 回调里弹菜单/做 IO，
    // 否则超过 LowLevelHooksTimeout 后系统会静默摘除 hook，之后所有转发失效
    PostMessageW(zone->Hwnd(), dblclk ? WM_LBUTTONDBLCLK : msg, wp, MAKELPARAM(client.x, client.y));
    return 1; // 吞掉原始事件，避免桌面同时响应
}

void MouseInputForwarder::DetectDesktopBlankDoubleClick(const POINT& pt) {
    const DWORD now = GetTickCount();
    const bool dblclk =
        blankDownTick_ != 0 &&
        now - blankDownTick_ <= GetDoubleClickTime() &&
        std::abs(pt.x - blankDownPt_.x) <= GetSystemMetrics(SM_CXDOUBLECLK) / 2 &&
        std::abs(pt.y - blankDownPt_.y) <= GetSystemMetrics(SM_CYDOUBLECLK) / 2;
    blankDownTick_ = now;
    blankDownPt_ = pt;
    if (!dblclk) return;

    // 确认双击发生在桌面层（Progman/WorkerW/listView），而不是其他应用窗口上
    HWND hit = WindowFromPoint(pt);
    if (!hit) return;
    const HWND root = GetAncestor(hit, GA_ROOT);
    const auto& w = shell_->Windows();
    if (root != w.workerw && root != w.progman && hit != w.listView) return;

    // 干净桌面模式：分区已隐藏、图标列表已隐藏，直接恢复
    if (isCleanMode && isCleanMode()) {
        if (onBlankDesktopDoubleClick) onBlankDesktopDoubleClick();
        return;
    }
    if (!w.listView || !IsWindow(w.listView) || !IsWindowVisible(w.listView)) return;

    // LVM_HITTEST 的 LVHITTESTINFO* 必须位于 Explorer 进程内存，否则 Explorer 崩溃
    DWORD pid = 0;
    GetWindowThreadProcessId(w.listView, &pid);
    HANDLE hProc = pid ? OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE, FALSE, pid) : nullptr;
    int index = -1;
    if (hProc) {
        LPVOID remote = VirtualAllocEx(hProc, nullptr, sizeof(LVHITTESTINFO),
                                       MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (remote) {
            POINT client = pt;
            ScreenToClient(w.listView, &client);
            LVHITTESTINFO ht{};
            ht.pt = client;
            WriteProcessMemory(hProc, remote, &ht, sizeof(ht), nullptr);
            DWORD_PTR hitResult = 0;
            SendMessageTimeoutW(w.listView, LVM_HITTEST, 0, reinterpret_cast<LPARAM>(remote),
                                SMTO_ABORTIFHUNG, 500, &hitResult);
            ReadProcessMemory(hProc, remote, &ht, sizeof(ht), nullptr);
            index = ht.iItem;
            VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
        }
        CloseHandle(hProc);
    }
    if (index == -1 && onBlankDesktopDoubleClick) {
        onBlankDesktopDoubleClick(); // 双击空白处（不是图标）
    }
}

} // namespace desktopsticker
