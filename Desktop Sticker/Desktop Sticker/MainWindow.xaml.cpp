#include "pch.h"
#include "MainWindow.xaml.h"
#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include <commctrl.h>
#include <microsoft.ui.xaml.window.h>
#include <shellapi.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::Desktop_Sticker::implementation
{
    MainWindow::~MainWindow()
    {
        RemoveTrayIcon();
        if (m_hwnd) {
            RemoveWindowSubclass(m_hwnd, TraySubclassProc, 1);
        }
    }

    void MainWindow::AttachHost(desktopsticker::app::Host* host, desktopsticker::app::SettingsController* settings)
    {
        m_host = host;
        m_settings = settings;
        HWND hwnd = nullptr;
        this->m_inner.as<::IWindowNative>()->get_WindowHandle(&hwnd);
        m_hwnd = hwnd;
        SetWindowSubclass(m_hwnd, TraySubclassProc, 1, reinterpret_cast<DWORD_PTR>(this));
        AddTrayIcon();
    }

    void MainWindow::AttachSettings(desktopsticker::app::SettingsController* settings)
    {
        m_settings = settings;
    }

    void MainWindow::AddTrayIcon()
    {
        if (!m_hwnd || m_trayAdded) return;
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = m_hwnd;
        nid.uID = 1;
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = WM_APP + 1;
        nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        wcscpy_s(nid.szTip, L"Desktop Sticker");
        m_trayAdded = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    }

    void MainWindow::RemoveTrayIcon()
    {
        if (!m_hwnd || !m_trayAdded) return;
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = m_hwnd;
        nid.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        m_trayAdded = false;
    }

    void MainWindow::OnTrayMessage(WPARAM wParam, LPARAM lParam)
    {
        if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_LBUTTONUP) {
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, 1, L"设置");
            AppendMenuW(menu, MF_STRING, 2, L"恢复桌面");
            AppendMenuW(menu, MF_STRING, 3, L"退出");
            POINT pt{};
            GetCursorPos(&pt);
            // TrackPopupMenu 前必须把前台焦点交给托盘窗口，否则点击菜单外菜单不会消失
            SetForegroundWindow(m_hwnd);
            int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hwnd, nullptr);
            DestroyMenu(menu);
            PostMessageW(m_hwnd, WM_NULL, 0, 0);

            if (cmd == 1 && m_settings) {
                m_settings->Show();
            } else if (cmd == 2 && m_host && m_host->Module()) {
                m_host->Module()->RestoreDesktop();
            } else if (cmd == 3) {
                // 显式退出 WinUI 应用（仅 WM_CLOSE 不会结束应用）
                Application::Current().Exit();
            }
        }
    }

    LRESULT CALLBACK MainWindow::TraySubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                                  UINT_PTR id, DWORD_PTR data)
    {
        auto* self = reinterpret_cast<MainWindow*>(data);
        if (msg == WM_APP + 1 && self) {
            self->OnTrayMessage(wp, lp);
            return 0;
        }
        return DefSubclassProc(hwnd, msg, wp, lp);
    }

    int32_t MainWindow::MyProperty()
    {
        throw hresult_not_implemented();
    }

    void MainWindow::MyProperty(int32_t /* value */)
    {
        throw hresult_not_implemented();
    }
}
