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
                    if (m_launcher->Visible()) {
                        m_launcher->Hide();
                    } else {
                        m_launcher->Show();
                    }
                });
            }
        });
    }
}
