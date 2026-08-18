#include "pch.h"
#include "SettingsController.h"

#include <winrt/Microsoft.UI.Windowing.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;

namespace desktopsticker::app {

SettingsController::SettingsController(Host* host) : host_(host) {}

void SettingsController::EnsureWindow() {
    if (window_) return;

    window_ = Window();

    auto scroll = ScrollViewer();
    auto root = StackPanel();
    root.Padding(ThicknessHelper::FromLengths(28, 28, 28, 28));
    root.Spacing(12);
    root.MaxWidth(560);

    auto title = TextBlock();
    title.Text(L"Desktop Sticker 设置");
    title.Style(Application::Current().Resources().Lookup(box_value(L"TitleTextBlockStyle")).as<Style>());
    root.Children().Append(title);

    auto sectionHeader = [&](const wchar_t* text) {
        auto t = TextBlock();
        t.Text(text);
        t.Style(Application::Current().Resources().Lookup(box_value(L"SubtitleTextBlockStyle")).as<Style>());
        t.Margin(ThicknessHelper::FromLengths(0, 8, 0, 0));
        root.Children().Append(t);
    };

    sectionHeader(L"搜索设置");
    searchDesktopSwitch_ = ToggleSwitch();
    searchDesktopSwitch_.Header(box_value(L"搜索桌面内容"));
    searchDesktopSwitch_.Toggled([this](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { SaveConfig(); });
    root.Children().Append(searchDesktopSwitch_);

    searchKnownFoldersSwitch_ = ToggleSwitch();
    searchKnownFoldersSwitch_.Header(box_value(L"搜索文档/下载/图片/视频/音乐"));
    searchKnownFoldersSwitch_.Toggled([this](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { SaveConfig(); });
    root.Children().Append(searchKnownFoldersSwitch_);

    followThemeSwitch_ = ToggleSwitch();
    followThemeSwitch_.Header(box_value(L"跟随系统深浅色主题"));
    followThemeSwitch_.Toggled([this](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { SaveConfig(); });
    root.Children().Append(followThemeSwitch_);

    sectionHeader(L"热键设置");
    hotkeyModeCombo_ = ComboBox();
    {
        auto item1 = ComboBoxItem();
        item1.Content(box_value(L"双击空格"));
        item1.Tag(box_value(L"double-space"));
        auto item2 = ComboBoxItem();
        item2.Content(box_value(L"Alt + Space"));
        item2.Tag(box_value(L"custom"));
        hotkeyModeCombo_.Items().Append(item1);
        hotkeyModeCombo_.Items().Append(item2);
    }
    hotkeyModeCombo_.SelectionChanged([this](winrt::Windows::Foundation::IInspectable const&, SelectionChangedEventArgs const&) { SaveConfig(); });
    root.Children().Append(hotkeyModeCombo_);

    sectionHeader(L"磁贴设置");
    columnSpacingBox_ = NumberBox();
    columnSpacingBox_.Header(box_value(L"磁贴列间距（像素）"));
    columnSpacingBox_.Minimum(32);
    columnSpacingBox_.Maximum(96);
    columnSpacingBox_.SmallChange(2);
    columnSpacingBox_.ValueChanged([this](winrt::Windows::Foundation::IInspectable const&, NumberBoxValueChangedEventArgs const&) { SaveConfig(); });
    root.Children().Append(columnSpacingBox_);

    rowSpacingBox_ = NumberBox();
    rowSpacingBox_.Header(box_value(L"磁贴行间距（像素）"));
    rowSpacingBox_.Minimum(40);
    rowSpacingBox_.Maximum(120);
    rowSpacingBox_.SmallChange(2);
    rowSpacingBox_.ValueChanged([this](winrt::Windows::Foundation::IInspectable const&, NumberBoxValueChangedEventArgs const&) { SaveConfig(); });
    root.Children().Append(rowSpacingBox_);

    sectionHeader(L"应用管理");
    auto addRow = StackPanel();
    addRow.Orientation(Orientation::Horizontal);
    addRow.Spacing(8);

    appPathBox_ = TextBox();
    appPathBox_.Width(400);
    appPathBox_.PlaceholderText(L"例如 C:\\Program Files\\App\\app.exe");
    addRow.Children().Append(appPathBox_);

    auto addButton = Button();
    addButton.Content(box_value(L"添加"));
    addButton.Click([this](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
        auto text = appPathBox_.Text();
        if (!text.empty()) {
            host_->Module()->AddApp(text.c_str());
            RefreshApps();
            appPathBox_.Text(L"");
        }
    });
    addRow.Children().Append(addButton);
    root.Children().Append(addRow);

    appList_ = ListView();
    appList_.MaxHeight(200);
    root.Children().Append(appList_);

    auto removeButton = Button();
    removeButton.Content(box_value(L"移除选中"));
    removeButton.Click([this](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
        auto selected = appList_.SelectedItem();
        if (selected) {
            host_->Module()->RemoveApp(selected.as<TextBlock>().Text().c_str());
            RefreshApps();
        }
    });
    root.Children().Append(removeButton);

    sectionHeader(L"系统");
    auto restoreButton = Button();
    restoreButton.Content(box_value(L"恢复桌面图标"));
    restoreButton.Click([this](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
        if (host_ && host_->Module()) host_->Module()->RestoreDesktop();
    });
    root.Children().Append(restoreButton);

    scroll.Content(root);
    window_.Content(scroll);
    window_.Title(L"Desktop Sticker 设置");

    // 从配置加载
    auto cfg = host_->Module()->GetConfig();
    searchDesktopSwitch_.IsOn(cfg.searchDesktop);
    searchKnownFoldersSwitch_.IsOn(cfg.searchKnownFolders);
    followThemeSwitch_.IsOn(cfg.followSystemTheme);
    hotkeyModeCombo_.SelectedIndex(cfg.hotkeyMode == L"custom" ? 1 : 0);
    columnSpacingBox_.Value(static_cast<double>(cfg.zoneColumnSpacing));
    rowSpacingBox_.Value(static_cast<double>(cfg.zoneRowSpacing));
    RefreshApps();
}

void SettingsController::Show() {
    EnsureWindow();
    if (!window_) return;
    visible_ = true;
    window_.AppWindow().Show();
    window_.Activate();
}

void SettingsController::Hide() {
    if (!window_) return;
    visible_ = false;
    // 用 Hide 而不是 Close：Close 会销毁 Window，再次 Show 会崩溃
    window_.AppWindow().Hide();
}

void SettingsController::RefreshApps() {
    appList_.Items().Clear();
    for (const auto& app : host_->Module()->GetApps()) {
        auto tb = TextBlock();
        tb.Text(app);
        appList_.Items().Append(tb);
    }
}

void SettingsController::SaveConfig() {
    auto cfg = host_->Module()->GetConfig();
    cfg.searchDesktop = searchDesktopSwitch_.IsOn();
    cfg.searchKnownFolders = searchKnownFoldersSwitch_.IsOn();
    cfg.followSystemTheme = followThemeSwitch_.IsOn();

    auto item = hotkeyModeCombo_.SelectedItem().try_as<ComboBoxItem>();
    if (item) {
        auto tag = item.Tag().as<Windows::Foundation::IPropertyValue>().GetString();
        cfg.hotkeyMode = tag == L"custom" ? L"custom" : L"double-space";
    }
    if (columnSpacingBox_) cfg.zoneColumnSpacing = static_cast<int>(columnSpacingBox_.Value());
    if (rowSpacingBox_) cfg.zoneRowSpacing = static_cast<int>(rowSpacingBox_.Value());
    host_->Module()->SetConfig(cfg);
}

} // namespace desktopsticker::app
