#pragma once

#include "MainWindow.g.h"
#include <functional>
#include <map>

#include "Host.h"
#include "SettingsController.h"

namespace winrt::Desktop_Sticker::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow()
        {
            // Xaml objects should not call InitializeComponent during construction.
            // See https://github.com/microsoft/cppwinrt/tree/master/nuget#initializecomponent
        }

        ~MainWindow();

        void AttachHost(desktopsticker::app::Host* host, desktopsticker::app::SettingsController* settings);
        void AttachSettings(desktopsticker::app::SettingsController* settings);
        void ShowTrayWarning(const wchar_t* text); // 初始化失败等异常的用户可见提示
        HWND Hwnd() const { return m_hwnd; }

        int32_t MyProperty();
        void MyProperty(int32_t value);

        void OnTrayMessage(WPARAM wParam, LPARAM lParam);

    private:
        static LRESULT CALLBACK TraySubclassProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                                 UINT_PTR id, DWORD_PTR data);

        void AddTrayIcon();
        void RemoveTrayIcon();

        desktopsticker::app::Host* m_host = nullptr;
        desktopsticker::app::SettingsController* m_settings = nullptr;
        HWND m_hwnd = nullptr;
        bool m_trayAdded = false;
    };
}

namespace winrt::Desktop_Sticker::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
