#pragma once
#include <string>
#include <vector>

#include "Host.h"

namespace desktopsticker::app {

class SettingsController {
public:
    explicit SettingsController(Host* host);

    void Show();
    void Hide();
    bool Visible() const { return visible_; }

private:
    void EnsureWindow();
    void RefreshApps();
    void SaveConfig();

    Host* host_ = nullptr;
    bool visible_ = false;
    winrt::Microsoft::UI::Xaml::Window window_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch searchDesktopSwitch_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch searchKnownFoldersSwitch_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch followThemeSwitch_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::ComboBox hotkeyModeCombo_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::TextBox appPathBox_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::ListView appList_{ nullptr };
};

} // namespace desktopsticker::app
