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
    bool loading_ = false; // 初始化控件赋值期间抑制 SaveConfig，防止把未初始化值写进配置
    bool closed_ = false;  // 标题栏 X 已销毁 XAML Window，下次 Show 需全量重建
    winrt::Microsoft::UI::Xaml::Window window_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch searchDesktopSwitch_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch searchKnownFoldersSwitch_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch startMenuSwitch_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch followThemeSwitch_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::ComboBox hotkeyModeCombo_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::NumberBox columnSpacingBox_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::NumberBox rowSpacingBox_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::TextBox appPathBox_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::ListView appList_{ nullptr };
};

} // namespace desktopsticker::app
