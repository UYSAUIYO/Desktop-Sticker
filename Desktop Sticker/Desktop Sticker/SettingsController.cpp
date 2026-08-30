#include "pch.h"
#include "SettingsController.h"

#include "AppLog.h"
#include "WindowChrome.h"

#include <winrt/Microsoft.UI.Text.h>
#include <winrt/Microsoft.UI.Windowing.h>

#include <algorithm>
#include <cmath>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Input;
using namespace Microsoft::UI::Xaml::Media;

namespace desktopsticker::app {

namespace {

SolidColorBrush Solid(BYTE a, BYTE r, BYTE g, BYTE b) {
    return SolidColorBrush(winrt::Windows::UI::Color{a, r, g, b});
}

} // namespace

SettingsController::SettingsController(Host* host) : host_(host) {}

void SettingsController::EnsureWindow() {
    if (window_) {
        if (!closed_) return;
        // 上次被标题栏 X / WM_CLOSE 销毁：失效对象必须释放后全量重建，
        // 对已销毁的 XAML Window 调 Show 会卡死并抛异常（曾导致再次打开闪退）
        window_ = nullptr;
        closed_ = false;
    }
    loading_ = true; // 下面逐个赋值会触发 Toggled/SelectionChanged/ValueChanged → SaveConfig
    window_ = Window();
    // 点 X 会销毁 XAML Window 而非隐藏：订阅 Closed 记录状态，Show 时重建
    window_.Closed([this](auto&&, auto&&) {
        closed_ = true;
        visible_ = false;
    });
    desktopsticker::app::AppLog("settings", "EnsureWindow begin");

    // Win11 设置页同款 Mica 背景材质（低版本运行时不支持时退回纯色底）
    bool micaOk = false;
    try {
        window_.SystemBackdrop(winrt::Microsoft::UI::Xaml::Media::MicaBackdrop());
        micaOk = true;
    } catch (...) {
        desktopsticker::app::AppLog("settings", "MicaBackdrop failed, fallback solid");
    }
    desktopsticker::app::AppLog("settings", micaOk ? "EnsureWindow mica ok" : "EnsureWindow mica off");

    const bool dark = IsSystemDarkMode();
    // 系统设置页卡片配色：Mica 之上叠一层更亮的圆角卡片，描边几乎不可见
    const auto cardBg = dark ? Solid(0x0B, 0xFF, 0xFF, 0xFF) : Solid(0x9C, 0xFF, 0xFF, 0xFF);
    const auto cardStroke = dark ? Solid(0x12, 0xFF, 0xFF, 0xFF) : Solid(0x0F, 0x00, 0x00, 0x00);
    const auto textSecondary = dark ? Solid(0x97, 0xFF, 0xFF, 0xFF) : Solid(0x8A, 0x00, 0x00, 0x00);

    auto scroll = ScrollViewer();
    auto root = StackPanel();
    root.Spacing(4);
    root.MaxWidth(1000);
    root.Padding(ThicknessHelper::FromLengths(36, 4, 36, 36));
    root.HorizontalAlignment(HorizontalAlignment::Center);
    if (!micaOk) {
        root.Background(dark ? Solid(0xFF, 0x20, 0x20, 0x20) : Solid(0xFF, 0xF3, 0xF3, 0xF3));
    }
    scroll.Content(root);
    window_.Content(scroll);
    window_.Title(L"Desktop Sticker 设置");
    // 尺寸按系统 DPI 换算为物理像素（AppWindow::Resize 使用物理像素）
    const float scale = static_cast<float>(GetDpiForSystem()) / 96.0f;
    window_.AppWindow().Resize({static_cast<int>(980 * scale), static_cast<int>(720 * scale)});
    // 深色下显式给标题栏上色（解包应用的标题栏不一定跟随应用主题）
    if (dark) {
        auto tb = window_.AppWindow().TitleBar();
        const winrt::Windows::UI::Color white{0xFF, 0xFF, 0xFF, 0xFF};
        const winrt::Windows::UI::Color transparent{0x00, 0x00, 0x00, 0x00};
        const winrt::Windows::UI::Color hoverBg{0x20, 0xFF, 0xFF, 0xFF};
        tb.ForegroundColor(white);
        tb.BackgroundColor(transparent);
        tb.ButtonForegroundColor(white);
        tb.ButtonBackgroundColor(transparent);
        tb.ButtonHoverForegroundColor(white);
        tb.ButtonHoverBackgroundColor(hoverBg);
    }

    // —— 构建辅助：分区标题 / 分组容器 / 设置卡片（左：图标+标题+描述，右：控件） ——
    auto pageTitle = TextBlock();
    pageTitle.Text(L"设置");
    pageTitle.FontSize(28);
    pageTitle.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
    pageTitle.Margin(ThicknessHelper::FromLengths(0, 16, 0, 4));
    root.Children().Append(pageTitle);

    auto section = [&](const wchar_t* text) {
        auto t = TextBlock();
        t.Text(text);
        t.FontSize(16);
        t.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
        t.Margin(ThicknessHelper::FromLengths(0, 26, 0, 8));
        root.Children().Append(t);
    };
    auto group = [&]() {
        auto sp = StackPanel();
        sp.Spacing(4);
        root.Children().Append(sp);
        return sp;
    };

    // 一张设置卡片：返回外层 Border（已含左列文本），右列控件由调用方 append 进 grid 第 1 列
    auto makeCard = [&](StackPanel const& into, const wchar_t* glyph,
                        const wchar_t* title, const wchar_t* desc) {
        auto border = Border();
        border.CornerRadius(CornerRadiusHelper::FromUniformRadius(8));
        border.Background(cardBg);
        border.BorderBrush(cardStroke);
        border.BorderThickness(ThicknessHelper::FromLengths(1, 1, 1, 1));
        border.Padding(ThicknessHelper::FromLengths(20, 12, 20, 12));
        border.MinHeight(72);

        auto grid = Grid();
        auto c0 = ColumnDefinition();
        c0.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        auto c1 = ColumnDefinition();
        c1.Width(GridLengthHelper::Auto());
        grid.ColumnDefinitions().Append(c0);
        grid.ColumnDefinitions().Append(c1);

        auto left = StackPanel();
        left.Orientation(Orientation::Horizontal);
        left.Spacing(16);
        left.VerticalAlignment(VerticalAlignment::Center);
        if (glyph && *glyph) {
            auto icon = FontIcon();
            icon.Glyph(glyph);
            icon.FontSize(20);
            icon.FontFamily(Media::FontFamily(L"Segoe Fluent Icons"));
            icon.VerticalAlignment(VerticalAlignment::Center);
            left.Children().Append(icon);
        }
        auto texts = StackPanel();
        texts.Spacing(2);
        texts.VerticalAlignment(VerticalAlignment::Center);
        auto tb = TextBlock();
        tb.Text(title);
        tb.FontSize(14);
        texts.Children().Append(tb);
        if (desc && *desc) {
            auto db = TextBlock();
            db.Text(desc);
            db.FontSize(12);
            db.Foreground(textSecondary);
            db.TextWrapping(TextWrapping::Wrap);
            texts.Children().Append(db);
        }
        left.Children().Append(texts);
        Grid::SetColumn(left, 0);
        grid.Children().Append(left);

        border.Child(grid); // Border 用 Child（WinUI3 无 Content）
        into.Children().Append(border);
        return grid;
    };
    auto placeRight = [](Grid const& grid, FrameworkElement const& ctl) {
        ctl.VerticalAlignment(VerticalAlignment::Center);
        ctl.Margin(ThicknessHelper::FromLengths(16, 8, 0, 8));
        Grid::SetColumn(ctl, 1);
        grid.Children().Append(ctl);
    };

    // —— 搜索设置 ——
    section(L"搜索");
    auto searchGroup = group();

    searchDesktopSwitch_ = ToggleSwitch();
    placeRight(makeCard(searchGroup, L"\uE721", L"搜索桌面内容",
                        L"在搜索结果中包含桌面上的文件与快捷方式"),
               searchDesktopSwitch_);
    searchDesktopSwitch_.Toggled([this](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { SaveConfig(); });

    searchKnownFoldersSwitch_ = ToggleSwitch();
    placeRight(makeCard(searchGroup, L"\uE8B7", L"搜索已知文件夹",
                        L"文档、下载、图片、视频、音乐"),
               searchKnownFoldersSwitch_);
    searchKnownFoldersSwitch_.Toggled([this](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { SaveConfig(); });

    startMenuSwitch_ = ToggleSwitch();
    placeRight(makeCard(searchGroup, L"\uE80F", L"搜索开始菜单应用",
                        L"索引开始菜单里的应用快捷方式"),
               startMenuSwitch_);
    startMenuSwitch_.Toggled([this](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { SaveConfig(); });

    followThemeSwitch_ = ToggleSwitch();
    placeRight(makeCard(searchGroup, L"\uE793", L"跟随系统深浅色主题",
                        L"窗口外观随 Windows 主题变化"),
               followThemeSwitch_);
    followThemeSwitch_.Toggled([this](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { SaveConfig(); });

    // —— 热键设置 ——
    section(L"热键");
    auto hotkeyGroup = group();

    hotkeyModeCombo_ = ComboBox();
    hotkeyModeCombo_.MinWidth(180);
    hotkeyModeCombo_.PlaceholderText(L"选择唤醒方式");
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
    placeRight(makeCard(hotkeyGroup, L"\uE765", L"唤醒方式",
                        L"唤起搜索面板的快捷键"),
               hotkeyModeCombo_);
    hotkeyModeCombo_.SelectionChanged([this](winrt::Windows::Foundation::IInspectable const&, SelectionChangedEventArgs const&) { SaveConfig(); });

    // —— 磁贴设置 ——
    section(L"磁贴");
    auto tileGroup = group();

    columnSpacingBox_ = NumberBox();
    columnSpacingBox_.Width(160);
    columnSpacingBox_.Minimum(32);
    columnSpacingBox_.Maximum(96);
    columnSpacingBox_.SmallChange(2);
    columnSpacingBox_.SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Compact);
    placeRight(makeCard(tileGroup, L"\uE713", L"磁贴列间距（像素）",
                        L"磁贴之间的横向距离，图标会随列宽自动居中"),
               columnSpacingBox_);
    columnSpacingBox_.ValueChanged([this](winrt::Windows::Foundation::IInspectable const&, NumberBoxValueChangedEventArgs const&) { SaveConfig(); });

    rowSpacingBox_ = NumberBox();
    rowSpacingBox_.Width(160);
    rowSpacingBox_.Minimum(40);
    rowSpacingBox_.Maximum(120);
    rowSpacingBox_.SmallChange(2);
    rowSpacingBox_.SpinButtonPlacementMode(NumberBoxSpinButtonPlacementMode::Compact);
    placeRight(makeCard(tileGroup, L"\uE713", L"磁贴行间距（像素）",
                        L"磁贴之间的纵向距离"),
               rowSpacingBox_);
    rowSpacingBox_.ValueChanged([this](winrt::Windows::Foundation::IInspectable const&, NumberBoxValueChangedEventArgs const&) { SaveConfig(); });

    // —— 应用管理 ——
    section(L"应用管理");
    auto appGroup = group();

    appPathBox_ = TextBox();
    appPathBox_.Width(300);
    appPathBox_.PlaceholderText(L"例如 C:\\Program Files\\App\\app.exe");
    auto addButton = Button();
    addButton.Content(box_value(L"添加"));
    addButton.MinWidth(80);
    auto addBox = StackPanel();
    addBox.Orientation(Orientation::Horizontal);
    addBox.Spacing(8);
    addBox.Children().Append(appPathBox_);
    addBox.Children().Append(addButton);
    {
        auto addGrid = makeCard(appGroup, L"\uE710", L"手动添加应用",
                                L"输入程序或文件的完整路径后点击添加");
        placeRight(addGrid, addBox);
    }
    addButton.Click([this](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
        auto text = appPathBox_.Text();
        if (!text.empty()) {
            host_->Module()->AddApp(text.c_str());
            RefreshApps();
            appPathBox_.Text(L"");
        }
    });
    appPathBox_.KeyDown([this](winrt::Windows::Foundation::IInspectable const&, KeyRoutedEventArgs const& e) {
        if (e.Key() == Windows::System::VirtualKey::Enter && !appPathBox_.Text().empty()) {
            host_->Module()->AddApp(appPathBox_.Text().c_str());
            RefreshApps();
            appPathBox_.Text(L"");
        }
    });

    appList_ = ListView();
    appList_.Width(340);
    appList_.MaxHeight(200);
    auto removeButton = Button();
    removeButton.Content(box_value(L"移除选中"));
    removeButton.MinWidth(80);
    removeButton.HorizontalAlignment(HorizontalAlignment::Right);
    removeButton.Margin(ThicknessHelper::FromLengths(0, 8, 0, 0));
    auto listBox = StackPanel();
    listBox.Spacing(0);
    listBox.Children().Append(appList_);
    listBox.Children().Append(removeButton);
    {
        auto listGrid = makeCard(appGroup, L"\uE77B", L"已添加应用",
                                 L"手动添加的搜索结果来源");
        placeRight(listGrid, listBox);
    }
    removeButton.Click([this](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
        auto selected = appList_.SelectedItem();
        if (selected) {
            host_->Module()->RemoveApp(selected.as<TextBlock>().Text().c_str());
            RefreshApps();
        }
    });

    // —— 系统 ——
    section(L"系统");
    auto sysGroup = group();

    auto restoreButton = Button();
    restoreButton.Content(box_value(L"恢复"));
    restoreButton.MinWidth(80);
    placeRight(makeCard(sysGroup, L"\uE72C", L"恢复桌面图标",
                        L"把被收纳的图标放回桌面原始位置并重新显示"),
               restoreButton);
    restoreButton.Click([this](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
        if (host_ && host_->Module()) host_->Module()->RestoreDesktop();
    });

    // 从配置加载
    auto cfg = host_->Module()->GetConfig();
    searchDesktopSwitch_.IsOn(cfg.searchDesktop);
    searchKnownFoldersSwitch_.IsOn(cfg.searchKnownFolders);
    startMenuSwitch_.IsOn(cfg.searchStartMenu);
    followThemeSwitch_.IsOn(cfg.followSystemTheme);
    hotkeyModeCombo_.SelectedIndex(cfg.hotkeyMode == L"custom" ? 1 : 0);
    columnSpacingBox_.Value(static_cast<double>(cfg.zoneColumnSpacing));
    rowSpacingBox_.Value(static_cast<double>(cfg.zoneRowSpacing));
    RefreshApps();
    loading_ = false;
}

void SettingsController::Show() {
    try {
        EnsureWindow();
        if (!window_) return;
        visible_ = true;
        window_.AppWindow().Show();
        window_.Activate();
    } catch (...) {
        // 托盘菜单等原生路径会调用这里：异常不允许穿过窗口过程（会直接闪退）
        desktopsticker::app::AppLog("settings", "Show FAILED (exception)");
    }
}

void SettingsController::Hide() {
    try {
        if (!window_) return;
        visible_ = false;
        // 用 Hide 而不是 Close：Close 会销毁 Window，再次 Show 会崩溃
        window_.AppWindow().Hide();
    } catch (...) {
        desktopsticker::app::AppLog("settings", "Hide FAILED (exception)");
    }
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
    // 初始化赋值期间不回写：控件尚未全部就绪，会把默认/垃圾值写进 config.json
    if (loading_ || !host_ || !host_->Module()) return;
    auto cfg = host_->Module()->GetConfig();
    cfg.searchDesktop = searchDesktopSwitch_.IsOn();
    cfg.searchKnownFolders = searchKnownFoldersSwitch_.IsOn();
    if (startMenuSwitch_) cfg.searchStartMenu = startMenuSwitch_.IsOn();
    cfg.followSystemTheme = followThemeSwitch_.IsOn();

    auto item = hotkeyModeCombo_.SelectedItem().try_as<ComboBoxItem>();
    if (item) {
        auto tag = item.Tag().as<Windows::Foundation::IPropertyValue>().GetString();
        cfg.hotkeyMode = tag == L"custom" ? L"custom" : L"double-space";
    }
    if (columnSpacingBox_) {
        const double v = columnSpacingBox_.Value(); // 空输入时为 NaN，强转 int 是 UB
        if (!std::isnan(v)) cfg.zoneColumnSpacing = std::clamp(static_cast<int>(v), 32, 96);
    }
    if (rowSpacingBox_) {
        const double v = rowSpacingBox_.Value();
        if (!std::isnan(v)) cfg.zoneRowSpacing = std::clamp(static_cast<int>(v), 40, 120);
    }
    host_->Module()->SetConfig(cfg);
}

} // namespace desktopsticker::app
