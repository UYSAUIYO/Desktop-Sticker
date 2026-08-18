#pragma once

#include "App.xaml.g.h"
#include <memory>

#include "Host.h"
#include "LauncherController.h"
#include "SettingsController.h"

namespace winrt::Desktop_Sticker::implementation
{
    struct App : AppT<App>
    {
        App();

        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&);

    private:
        winrt::Microsoft::UI::Xaml::Window window{ nullptr };
        std::unique_ptr<desktopsticker::app::Host> m_host;
        std::unique_ptr<desktopsticker::app::LauncherController> m_launcher;
        std::unique_ptr<desktopsticker::app::SettingsController> m_settings;
        winrt::Microsoft::UI::Dispatching::DispatcherQueue m_dispatcher{ nullptr };
    };
}
