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
        // 主窗口被关（WM_CLOSE / close_app.ps1）时同样要走完整清理：
        // Closed 里显式停模块并退出，别指望 Application 的析构链
        Closed([this](IInspectable const&, IInspectable const&) {
            ShutdownModules();
            try { Application::Current().Exit(); } catch (...) {}
        });
        AddTrayIcon();
    }

    void MainWindow::ShutdownModules()
    {
        // 顺序：磁贴（用户可见的销毁+图标还原）→ 壁纸（停渲染/音频线程）→ 资源管理器。
        // 三者的 Shutdown 都幂等，之后析构链再触发一次也无害。
        if (m_modulesShutDown) return;
        m_modulesShutDown = true;
        try {
            if (m_host && m_host->Module()) m_host->Module()->Shutdown();
        } catch (...) {}
        try {
            if (m_host && m_host->WallPaper()) m_host->WallPaper()->Shutdown();
        } catch (...) {}
        try {
            if (m_host && m_host->ResMon()) m_host->ResMon()->Shutdown();
        } catch (...) {}
    }

    void MainWindow::AttachSettings(desktopsticker::app::SettingsController* settings)
    {
        m_settings = settings;
    }

    void MainWindow::ShowTrayWarning(const wchar_t* text)
    {
        // 托盘气泡：初始化失败时用户唯一的可见反馈（主窗口常驻隐藏，无其他 UI）
        if (!m_hwnd || !m_trayAdded) return;
        NOTIFYICONDATAW nid{};
        nid.cbSize = sizeof(nid);
        nid.hWnd = m_hwnd;
        nid.uID = 1;
        nid.uFlags = NIF_INFO;
        nid.dwInfoFlags = NIIF_WARNING;
        wcscpy_s(nid.szInfoTitle, L"Desktop Sticker");
        wcsncpy_s(nid.szInfo, text, _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &nid);
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
            // 磁贴显隐（干净桌面模式）：勾选 = 磁贴正在显示
            const bool tilesHidden = (m_host && m_host->Module() && m_host->Module()->TilesHidden());
            AppendMenuW(menu, MF_STRING | (tilesHidden ? 0u : MF_CHECKED), 5, L"显示磁贴");
            AppendMenuW(menu, MF_STRING, 2, L"恢复桌面");
            // 资源管理器不可用（缺 DLL / WebView2 运行时缺失）时置灰而不是隐藏，
            // 让用户能看到"功能存在但不可用"
            const bool resmonOk = (m_host && m_host->ResMon() && m_host->ResMon()->Available());
            AppendMenuW(menu, MF_STRING | (resmonOk ? 0u : MF_GRAYED), 4, L"资源管理器");
            AppendMenuW(menu, MF_STRING, 3, L"退出");
            POINT pt{};
            GetCursorPos(&pt);
            // TrackPopupMenu 前必须把前台焦点交给托盘窗口，否则点击菜单外菜单不会消失
            SetForegroundWindow(m_hwnd);
            int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hwnd, nullptr);
            DestroyMenu(menu);
            PostMessageW(m_hwnd, WM_NULL, 0, 0);

            try {
                if (cmd == 1 && m_settings) {
                    m_settings->Show();
                } else if (cmd == 2 && m_host && m_host->Module()) {
                    m_host->Module()->RestoreDesktop();
                } else if (cmd == 5 && m_host && m_host->Module()) {
                    m_host->Module()->SetTilesHidden(!m_host->Module()->TilesHidden());
                } else if (cmd == 4 && resmonOk) {
                    m_host->ResMon()->Show();
                } else if (cmd == 3) {
                    // 显式退出 WinUI 应用（仅 WM_CLOSE 不会结束应用）。
                    // Exit() 的收尾不会可靠走到模块析构（实测磁贴残留、壁纸线程存活），
                    // 必须先在这里显式停模块，把磁贴销毁、图标还原做完。
                    ShutdownModules();
                    Application::Current().Exit();
                }
            } catch (...) {
                // 托盘回调运行在原生窗口过程里：任何异常都会直接闪退进程
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
