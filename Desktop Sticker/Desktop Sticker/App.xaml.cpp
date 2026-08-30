#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"

#include <winrt/Microsoft.UI.Dispatching.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;

namespace winrt::Desktop_Sticker::implementation
{
    App::App()
    {
#if defined _DEBUG && !defined DISABLE_XAML_GENERATED_BREAK_ON_UNHANDLED_EXCEPTION
        UnhandledException([](IInspectable const&, UnhandledExceptionEventArgs const& e)
        {
            if (IsDebuggerPresent())
            {
                auto errorMessage = e.Message();
                __debugbreak();
            }
        });
#endif
    }

    void App::OnLaunched([[maybe_unused]] LaunchActivatedEventArgs const& e)
    {
        // 单实例保护：多个实例同时移动桌面图标会导致图标来回闪烁
        static HANDLE s_singleInstanceMutex = CreateMutexW(nullptr, TRUE, L"Local\\DesktopSticker.SingleInstance");
        if (!s_singleInstanceMutex || GetLastError() == ERROR_ALREADY_EXISTS) {
            Application::Current().Exit();
            return;
        }

        m_host = std::make_unique<desktopsticker::app::Host>();
        m_host->LoadFeatures();
        m_host->Start();

        // 主窗口只作为托盘宿主，创建后立即隐藏
        auto mainWindow = make<MainWindow>();
        auto impl = winrt::get_self<implementation::MainWindow>(mainWindow);
        impl->AttachHost(m_host.get(), nullptr);
        window = mainWindow;
        window.Activate();
        ShowWindow(impl->Hwnd(), SW_HIDE);

        m_launcher = std::make_unique<desktopsticker::app::LauncherController>(m_host.get());
        m_settings = std::make_unique<desktopsticker::app::SettingsController>(m_host.get());
        impl->AttachSettings(m_settings.get());

        m_dispatcher = window.DispatcherQueue();
        m_host->SetHotkeyCallback([this]() {
            if (m_dispatcher) {
                m_dispatcher.TryEnqueue([this]() {
                    // 面板打开（含焦点在面板上）时双击空格不再关闭——避免搜索框输入
                    // 空格/手抖多敲一下导致面板闪现即关；关闭只走 Esc
                    if (!m_launcher->Visible()) {
                        m_launcher->Show();
                    }
                });
            }
        });
    }
}
