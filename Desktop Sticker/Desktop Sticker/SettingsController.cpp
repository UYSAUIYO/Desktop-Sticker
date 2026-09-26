#include "pch.h"
#include "SettingsController.h"

#include "AppLog.h"
#include "WindowChrome.h"

// 壁纸后端的"能力"是纯函数（谁能调速/谁能出声/谁是自呈现型），
// 放在 header-only 的 BackendKind.h 里，EXE 侧直接复用，不必给模块接口加 vtable
#include <desktopsticker/wallpaper/BackendKind.h>
#include <desktopsticker/wallpaper/DecodePath.h>

#include <winrt/Microsoft.UI.Text.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <shobjidl.h>

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

// 播放方式下拉的档位与顺序；填项 / 回读 / 回显三处共用一份，避免各自写错顺序
constexpr desktopsticker::DecodePath kDecodePaths[] = {
    desktopsticker::DecodePath::Auto,
    desktopsticker::DecodePath::FfmpegHardware,
    desktopsticker::DecodePath::MediaFoundationD3d,
    desktopsticker::DecodePath::Cpu,
};
constexpr int kDecodePathCount = static_cast<int>(sizeof(kDecodePaths) / sizeof(kDecodePaths[0]));

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
        // 计时器必须在窗口销毁前停掉：否则下一次 Tick 会访问已销毁的控件
        if (playbackTimer_) {
            playbackTimer_.Stop();
            playbackTimer_ = nullptr;
        }
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

    // 两个二级页面的内容栈：贴纸（既有设置）与桌面壁纸，由左侧 NavigationView 切换。
    // 不引入 Frame/Page 导航（需要 XAML 页面文件），改用 NavigationView + 两个 ScrollViewer
    // 换 Content，保持本文件"全代码式构建"的风格。
    auto makePageRoot = [&]() {
        auto sp = StackPanel();
        sp.Spacing(4);
        sp.MaxWidth(1000);
        sp.Padding(ThicknessHelper::FromLengths(36, 4, 36, 36));
        sp.HorizontalAlignment(HorizontalAlignment::Center);
        if (!micaOk) {
            sp.Background(dark ? Solid(0xFF, 0x20, 0x20, 0x20) : Solid(0xFF, 0xF3, 0xF3, 0xF3));
        }
        return sp;
    };
    auto stickerRoot = makePageRoot();
    auto wallPaperRoot = makePageRoot();

    auto stickerScroll = ScrollViewer();
    stickerScroll.Content(stickerRoot);
    auto wallPaperScroll = ScrollViewer();
    wallPaperScroll.Content(wallPaperRoot);

    // 下面既有卡片构建代码全部指向贴纸页
    auto& root = stickerRoot;

    window_.Title(L"Desktop Sticker 设置");

    // 尺寸按系统 DPI 换算为物理像素（AppWindow::Resize 使用物理像素）；多留出左侧导航宽度
    const float scale = static_cast<float>(GetDpiForSystem()) / 96.0f;
    window_.AppWindow().Resize({static_cast<int>(1120 * scale), static_cast<int>(760 * scale)});
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
    pageTitle.Text(L"贴纸");
    pageTitle.FontSize(28);
    pageTitle.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
    pageTitle.Margin(ThicknessHelper::FromLengths(0, 16, 0, 4));
    root.Children().Append(pageTitle);

    auto section = [&](StackPanel const& into, const wchar_t* text) {
        auto t = TextBlock();
        t.Text(text);
        t.FontSize(16);
        t.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
        t.Margin(ThicknessHelper::FromLengths(0, 26, 0, 8));
        into.Children().Append(t);
    };
    auto group = [&](StackPanel const& into) {
        auto sp = StackPanel();
        sp.Spacing(4);
        into.Children().Append(sp);
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
    section(stickerRoot, L"搜索");
    auto searchGroup = group(stickerRoot);

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
    section(stickerRoot, L"热键");
    auto hotkeyGroup = group(stickerRoot);

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
    section(stickerRoot, L"磁贴");
    auto tileGroup = group(stickerRoot);

    zoneCardsCombo_ = ComboBox();
    zoneCardsCombo_.MinWidth(180);
    zoneCardsCombo_.PlaceholderText(L"选择每列卡片数");
    {
        auto item4 = ComboBoxItem();
        item4.Content(box_value(L"4 张 / 列"));
        auto item5 = ComboBoxItem();
        item5.Content(box_value(L"5 张 / 列"));
        zoneCardsCombo_.Items().Append(item4);
        zoneCardsCombo_.Items().Append(item5);
    }
    placeRight(makeCard(tileGroup, L"\uE713", L"每列卡片数",
                        L"一列纵向排几张卡片；卡片高度自动铺满到任务栏上沿，改变后立即重排"),
               zoneCardsCombo_);
    zoneCardsCombo_.SelectionChanged([this](winrt::Windows::Foundation::IInspectable const&, SelectionChangedEventArgs const&) { SaveConfig(); });

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
    section(stickerRoot, L"应用管理");
    auto appGroup = group(stickerRoot);

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

    // —— 小组件 ——
    section(stickerRoot, L"小组件");
    auto widgetGroup = group(stickerRoot);

    clockSwitch_ = ToggleSwitch();
    placeRight(makeCard(widgetGroup, L"\uE823", L"桌面时钟",
                        L"在桌面顶部中间显示时间、日出日落与当前小时天气"),
               clockSwitch_);
    clockSwitch_.Toggled([this](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { SaveConfig(); });

    // ============ 第二个二级页面：桌面壁纸 ============
    // 本块构建的内容全部挂在 wallPaperRoot 上，由左侧导航切换显示。
    {
        auto wpTitle = TextBlock();
        wpTitle.Text(L"桌面壁纸");
        wpTitle.FontSize(28);
        wpTitle.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
        wpTitle.Margin(ThicknessHelper::FromLengths(0, 16, 0, 4));
        wallPaperRoot.Children().Append(wpTitle);
    }
    section(wallPaperRoot, L"播放设置");
    auto wallPaperGroup = group(wallPaperRoot);

    wallPaperSwitch_ = ToggleSwitch();
    placeRight(makeCard(wallPaperGroup, L"\uE786", L"启用动态壁纸",
                        L"把视频作为桌面壁纸播放，位于桌面图标与分区卡片之下"),
               wallPaperSwitch_);
    wallPaperSwitch_.Toggled([this](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { SaveWallPaper(); });

    wallPaperVariantCombo_ = ComboBox();
    wallPaperVariantCombo_.MinWidth(170);
    {
        auto v0 = ComboBoxItem();
        v0.Content(box_value(L"原画"));
        auto v1 = ComboBoxItem();
        v1.Content(box_value(L"均衡副本"));
        auto v2 = ComboBoxItem();
        v2.Content(box_value(L"省电副本"));
        wallPaperVariantCombo_.Items().Append(v0);
        wallPaperVariantCombo_.Items().Append(v1);
        wallPaperVariantCombo_.Items().Append(v2);
    }
    placeRight(makeCard(wallPaperGroup, L"\uE9D9", L"播放档位",
                        L"原画优先；所选副本不存在时自动回落到原画"),
               wallPaperVariantCombo_);
    wallPaperVariantCombo_.SelectionChanged([this](winrt::Windows::Foundation::IInspectable const&, SelectionChangedEventArgs const&) { SaveWallPaper(); });

    wallPaperSpeedCombo_ = ComboBox();
    wallPaperSpeedCombo_.MinWidth(170);
    {
        // 规格 §11：0.25×–4× 档位；调速由渲染侧的 frame_advance_policy 缩放播放节奏
        const double speeds[] = { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0 };
        for (double s : speeds) {
            auto entry = ComboBoxItem();
            wchar_t text[32]{};
            swprintf_s(text, L"%g\u00D7", s);
            entry.Content(box_value(text));
            wallPaperSpeedCombo_.Items().Append(entry);
        }
    }
    placeRight(makeCard(wallPaperGroup, L"\uE916", L"播放速度",
                        L"对视频/动图/图片序列生效；网页的节奏由页面自己决定，故不可调"),
               wallPaperSpeedCombo_);
    wallPaperSpeedCombo_.SelectionChanged([this](winrt::Windows::Foundation::IInspectable const&, SelectionChangedEventArgs const&) { SaveWallPaper(); });

    wallPaperAudioSwitch_ = ToggleSwitch();
    placeRight(makeCard(wallPaperGroup, L"\uE767", L"播放声音",
                        L"默认关闭；只有带音轨的视频和网页才会出声"),
               wallPaperAudioSwitch_);
    wallPaperAudioSwitch_.Toggled([this](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { SaveWallPaper(); });

    wallPaperDecodePathCombo_ = ComboBox();
    wallPaperDecodePathCombo_.MinWidth(230);
    {
        // 四档：自动 / FFmpeg+Vulkan 硬解 / MF+D3D11 硬解 / CPU 软解。
        // 名字与顺序必须与 DecodePath 的持久化映射一致（见 DecodePath.h）。
        for (auto p : kDecodePaths) {
            auto entry = ComboBoxItem();
            entry.Content(box_value(desktopsticker::wallpaper::decode_path_name(p)));
            wallPaperDecodePathCombo_.Items().Append(entry);
        }
    }
    placeRight(makeCard(wallPaperGroup, L"\uE950", L"播放方式",
                        L"换一条解码/呈现路径，切换后立刻重开（不必重启）；某条路不可用会自动回落"),
               wallPaperDecodePathCombo_);
    wallPaperDecodePathCombo_.SelectionChanged([this](winrt::Windows::Foundation::IInspectable const&, SelectionChangedEventArgs const&) { SaveWallPaper(); });

    // 当前播放状态：整宽一行，由计时器每秒刷新
    {
        auto border = Border();
        border.CornerRadius(CornerRadiusHelper::FromUniformRadius(8));
        border.Background(cardBg);
        border.BorderBrush(cardStroke);
        border.BorderThickness(ThicknessHelper::FromLengths(1, 1, 1, 1));
        border.Padding(ThicknessHelper::FromLengths(20, 12, 20, 12));

        auto stack = StackPanel();
        stack.Spacing(4);
        auto header = TextBlock();
        header.Text(L"当前播放状态");
        header.FontSize(14);
        stack.Children().Append(header);

        wallPaperPlaybackText_ = TextBlock();
        wallPaperPlaybackText_.FontSize(12);
        wallPaperPlaybackText_.Foreground(textSecondary);
        wallPaperPlaybackText_.TextWrapping(TextWrapping::Wrap);
        wallPaperPlaybackText_.Text(L"—");
        stack.Children().Append(wallPaperPlaybackText_);

        border.Child(stack);
        wallPaperGroup.Children().Append(border);
    }

    {
        // 音量：滑块 + 百分比文字，右对齐成一组
        auto panel = StackPanel();
        panel.Orientation(Orientation::Horizontal);
        panel.Spacing(8);
        panel.VerticalAlignment(VerticalAlignment::Center);

        wallPaperVolumeSlider_ = Slider();
        wallPaperVolumeSlider_.Width(140);
        wallPaperVolumeSlider_.Minimum(0);
        wallPaperVolumeSlider_.Maximum(100);
        wallPaperVolumeSlider_.StepFrequency(5);
        panel.Children().Append(wallPaperVolumeSlider_);

        wallPaperVolumeLabel_ = TextBlock();
        wallPaperVolumeLabel_.FontSize(12);
        wallPaperVolumeLabel_.VerticalAlignment(VerticalAlignment::Center);
        panel.Children().Append(wallPaperVolumeLabel_);

        placeRight(makeCard(wallPaperGroup, L"\uE995", L"音量",
                            L"网页壁纸的音量由页面自己控制，这里只对视频生效"),
                   panel);
        wallPaperVolumeSlider_.ValueChanged([this](winrt::Windows::Foundation::IInspectable const&,
                                                  Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& e) {
            if (wallPaperVolumeLabel_) {
                wallPaperVolumeLabel_.Text(std::to_wstring(static_cast<int>(e.NewValue())) + L"%");
            }
            SaveWallPaper();
        });
    }

    wallPaperPauseFullscreenSwitch_ = ToggleSwitch();
    placeRight(makeCard(wallPaperGroup, L"\uE740", L"全屏时暂停",
                        L"检测到覆盖整个屏幕的应用时停止播放，切回桌面自动恢复"),
               wallPaperPauseFullscreenSwitch_);
    wallPaperPauseFullscreenSwitch_.Toggled([this](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { SaveWallPaper(); });

    wallPaperPauseLockSwitch_ = ToggleSwitch();
    placeRight(makeCard(wallPaperGroup, L"\uE72E", L"锁屏或息屏时暂停",
                        L"会话锁定或显示器关闭时停止播放"),
               wallPaperPauseLockSwitch_);
    wallPaperPauseLockSwitch_.Toggled([this](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { SaveWallPaper(); });

    wallPaperUserPauseSwitch_ = ToggleSwitch();
    placeRight(makeCard(wallPaperGroup, L"\uE769", L"手动暂停",
                        L"临时停止播放，不影响其他设置"),
               wallPaperUserPauseSwitch_);
    wallPaperUserPauseSwitch_.Toggled([this](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { SaveWallPaper(); });

    // 壁纸库卡片：整宽（列表 + 操作 + 状态），不走 makeCard 的左右两列布局
    {
        auto border = Border();
        border.CornerRadius(CornerRadiusHelper::FromUniformRadius(8));
        border.Background(cardBg);
        border.BorderBrush(cardStroke);
        border.BorderThickness(ThicknessHelper::FromLengths(1, 1, 1, 1));
        border.Padding(ThicknessHelper::FromLengths(20, 12, 20, 12));

        auto stack = StackPanel();
        stack.Spacing(8);

        auto header = TextBlock();
        header.Text(L"壁纸库");
        header.FontSize(14);
        stack.Children().Append(header);

        wallPaperGrid_ = GridView();
        wallPaperGrid_.Height(300);
        wallPaperGrid_.SelectionMode(ListViewSelectionMode::Single);
        stack.Children().Append(wallPaperGrid_);
        wallPaperGrid_.SelectionChanged([this](winrt::Windows::Foundation::IInspectable const&, SelectionChangedEventArgs const&) { SaveWallPaper(); });

        auto buttons = StackPanel();
        buttons.Orientation(Orientation::Horizontal);
        buttons.Spacing(8);

        wallPaperImportButton_ = Button();
        wallPaperImportButton_.Content(box_value(L"导入文件…"));
        wallPaperImportButton_.MinWidth(104);
        buttons.Children().Append(wallPaperImportButton_);
        wallPaperImportButton_.Click([this](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { ImportWallPaper(); });

        // 目录型来源（图片序列 / 网页 / 着色器）只能选文件夹，类型由目录内容自动判定
        wallPaperImportFolderButton_ = Button();
        wallPaperImportFolderButton_.Content(box_value(L"导入文件夹…"));
        wallPaperImportFolderButton_.MinWidth(116);
        buttons.Children().Append(wallPaperImportFolderButton_);
        wallPaperImportFolderButton_.Click([this](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { ImportWallPaperFolder(); });

        wallPaperRemoveButton_ = Button();
        wallPaperRemoveButton_.Content(box_value(L"删除选中"));
        wallPaperRemoveButton_.MinWidth(92);
        buttons.Children().Append(wallPaperRemoveButton_);
        wallPaperRemoveButton_.Click([this](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { RemoveSelectedWallPaper(); });

        wallPaperVariantButton_ = Button();
        wallPaperVariantButton_.Content(box_value(L"重新生成副本"));
        wallPaperVariantButton_.MinWidth(116);
        buttons.Children().Append(wallPaperVariantButton_);
        wallPaperVariantButton_.Click([this](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { RegenerateSelectedVariant(); });

        stack.Children().Append(buttons);

        auto buttons2 = StackPanel();
        buttons2.Orientation(Orientation::Horizontal);
        buttons2.Spacing(8);

        wallPaperChangeRootButton_ = Button();
        wallPaperChangeRootButton_.Content(box_value(L"更改存储位置…"));
        wallPaperChangeRootButton_.MinWidth(132);
        buttons2.Children().Append(wallPaperChangeRootButton_);
        wallPaperChangeRootButton_.Click([this](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { ChangeWallPaperRoot(); });

        // 用户要放自己的 .frag / .gltf / JSON 参数时，直接打开库根目录最省事
        wallPaperOpenConfigButton_ = Button();
        wallPaperOpenConfigButton_.Content(box_value(L"打开壁纸目录"));
        wallPaperOpenConfigButton_.MinWidth(132);
        buttons2.Children().Append(wallPaperOpenConfigButton_);
        wallPaperOpenConfigButton_.Click([this](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { OpenWallPaperConfigDir(); });

        stack.Children().Append(buttons2);

        wallPaperStatus_ = TextBlock();
        wallPaperStatus_.FontSize(12);
        wallPaperStatus_.Foreground(textSecondary);
        wallPaperStatus_.TextWrapping(TextWrapping::Wrap);
        stack.Children().Append(wallPaperStatus_);

        border.Child(stack);
        wallPaperGroup.Children().Append(border);
    }

    // —— 系统 ——
    section(stickerRoot, L"系统");
    auto sysGroup = group(stickerRoot);

    auto restoreButton = Button();
    restoreButton.Content(box_value(L"恢复"));
    restoreButton.MinWidth(80);
    placeRight(makeCard(sysGroup, L"\uE72C", L"恢复桌面图标",
                        L"把被收纳的图标放回桌面原始位置并重新显示"),
               restoreButton);
    restoreButton.Click([this](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) {
        if (host_ && host_->Module()) host_->Module()->RestoreDesktop();
    });

    // ============ 左侧导航：贴纸 / 桌面壁纸 ============
    {
        auto nav = NavigationView();
        nav.PaneDisplayMode(NavigationViewPaneDisplayMode::Left);
        nav.IsBackButtonVisible(NavigationViewBackButtonVisible::Collapsed);
        nav.IsPaneToggleButtonVisible(false); // 固定展开，避免窄窗时折叠成图标条
        nav.IsSettingsVisible(false);
        nav.OpenPaneLength(188);

        auto makeNavItem = [&](const wchar_t* glyph, const wchar_t* label, const wchar_t* tag) {
            auto item = NavigationViewItem();
            item.Content(box_value(label));
            item.Tag(box_value(tag));
            auto icon = FontIcon();
            icon.Glyph(glyph);
            icon.FontFamily(Media::FontFamily(L"Segoe Fluent Icons"));
            item.Icon(icon);
            nav.MenuItems().Append(item);
            return item;
        };
        auto stickerItem = makeNavItem(L"\uE790", L"贴纸", L"sticker");
        makeNavItem(L"\uE786", L"桌面壁纸", L"wallpaper");

        nav.Content(stickerScroll);
        nav.SelectedItem(stickerItem);

        // 切换页面：只换 Content，两个页面的控件都保持存活（避免重建丢失壁纸状态）
        nav.SelectionChanged([stickerScroll, wallPaperScroll](
                                 winrt::Windows::Foundation::IInspectable const& sender,
                                 NavigationViewSelectionChangedEventArgs const&) {
            auto view = sender.try_as<NavigationView>();
            if (!view) return;
            std::wstring tag;
            if (auto nvi = view.SelectedItem().try_as<NavigationViewItem>()) {
                tag = unbox_value_or<hstring>(nvi.Tag(), L"").c_str();
            }
            view.Content(tag == L"wallpaper"
                             ? (winrt::Windows::Foundation::IInspectable)wallPaperScroll
                             : (winrt::Windows::Foundation::IInspectable)stickerScroll);
        });

        window_.Content(nav);
    }

    // 从配置加载
    auto cfg = host_->Module()->GetConfig();
    searchDesktopSwitch_.IsOn(cfg.searchDesktop);
    searchKnownFoldersSwitch_.IsOn(cfg.searchKnownFolders);
    startMenuSwitch_.IsOn(cfg.searchStartMenu);
    followThemeSwitch_.IsOn(cfg.followSystemTheme);
    clockSwitch_.IsOn(cfg.showClock);
    hotkeyModeCombo_.SelectedIndex(cfg.hotkeyMode == L"custom" ? 1 : 0);
    zoneCardsCombo_.SelectedIndex(cfg.zoneColumnCards >= 5 ? 1 : 0);
    columnSpacingBox_.Value(static_cast<double>(cfg.zoneColumnSpacing));
    rowSpacingBox_.Value(static_cast<double>(cfg.zoneRowSpacing));
    RefreshApps();
    RefreshWallPaperControls(); // 壁纸状态由模块持有，窗口重建后必须重新拉取

    // 帧率是"活的"，只能靠定时器刷；1 秒一次足够，且只在窗口存活期间跑
    playbackTimer_ = winrt::Microsoft::UI::Xaml::DispatcherTimer();
    playbackTimer_.Interval(std::chrono::milliseconds(1000));
    playbackTimer_.Tick([this](auto&&, auto&&) { RefreshPlaybackText(); });
    playbackTimer_.Start();
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
    if (clockSwitch_) cfg.showClock = clockSwitch_.IsOn();

    auto item = hotkeyModeCombo_.SelectedItem().try_as<ComboBoxItem>();
    if (item) {
        auto tag = item.Tag().as<Windows::Foundation::IPropertyValue>().GetString();
        cfg.hotkeyMode = tag == L"custom" ? L"custom" : L"double-space";
    }
    if (zoneCardsCombo_) {
        cfg.zoneColumnCards = zoneCardsCombo_.SelectedIndex() == 1 ? 5 : 4;
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

// ---- 动态壁纸 ----
// 模块在后台线程回调，宿主已通过 DispatcherQueue 切回 UI 线程。
// 标题栏 X 会销毁 XAML Window，因此这里必须先判断窗口是否仍然有效。

void SettingsController::RefreshWallPaper() {
    if (!window_ || closed_) return;
    RefreshWallPaperControls();
}

void SettingsController::RefreshWallPaperControls() {
    auto* wp = host_ ? host_->WallPaper() : nullptr;
    if (!wallPaperSwitch_) return;

    wallpaperLoading_ = true; // 抑制控件事件回写配置
    const bool available = (wp != nullptr) && wp->Available();

    wallPaperSwitch_.IsEnabled(available);
    if (wallPaperPauseFullscreenSwitch_) wallPaperPauseFullscreenSwitch_.IsEnabled(available);
    if (wallPaperPauseLockSwitch_) wallPaperPauseLockSwitch_.IsEnabled(available);
    if (wallPaperUserPauseSwitch_) wallPaperUserPauseSwitch_.IsEnabled(available);
    if (wallPaperVariantCombo_) wallPaperVariantCombo_.IsEnabled(available);
    if (wallPaperSpeedCombo_) wallPaperSpeedCombo_.IsEnabled(available);
    if (wallPaperAudioSwitch_) wallPaperAudioSwitch_.IsEnabled(available);
    if (wallPaperVolumeSlider_) wallPaperVolumeSlider_.IsEnabled(available);
    if (wallPaperGrid_) wallPaperGrid_.IsEnabled(available);
    if (wallPaperImportButton_) wallPaperImportButton_.IsEnabled(available);
    if (wallPaperImportFolderButton_) wallPaperImportFolderButton_.IsEnabled(available);
    if (wallPaperRemoveButton_) wallPaperRemoveButton_.IsEnabled(available);
    if (wallPaperVariantButton_) wallPaperVariantButton_.IsEnabled(available);
    if (wallPaperChangeRootButton_) wallPaperChangeRootButton_.IsEnabled(available);
    if (wallPaperOpenConfigButton_) wallPaperOpenConfigButton_.IsEnabled(available);

    if (!available) {
        wallPaperSwitch_.IsOn(false);
        if (wallPaperStatus_) {
            wallPaperStatus_.Text(L"动态壁纸不可用（组件缺失，或存储位置校验未通过；详见 debug.log）");
        }
        if (wallPaperGrid_) wallPaperGrid_.Items().Clear();
        if (wallPaperDecodePathCombo_) wallPaperDecodePathCombo_.IsEnabled(false);
        wallpaperLoading_ = false;
        RefreshPlaybackText();   // 这里会显示"壁纸模块不可用"，别让上一次的帧率留在界面上
        return;
    }

    const auto settings = wp->GetSettings();
    wallPaperSwitch_.IsOn(settings.enabled);
    if (wallPaperPauseFullscreenSwitch_) wallPaperPauseFullscreenSwitch_.IsOn(settings.pauseOnFullscreen);
    if (wallPaperPauseLockSwitch_) wallPaperPauseLockSwitch_.IsOn(settings.pauseOnLock);
    if (wallPaperUserPauseSwitch_) wallPaperUserPauseSwitch_.IsOn(wp->IsUserPaused());
    if (wallPaperVariantCombo_) {
        const int index = settings.preferred == VariantKind::PowerSaver ? 2
                        : settings.preferred == VariantKind::Balanced ? 1 : 0;
        wallPaperVariantCombo_.SelectedIndex(index);
    }
    if (wallPaperDecodePathCombo_) {
        int index = 0;
        for (int i = 0; i < kDecodePathCount; ++i) {
            if (kDecodePaths[i] == settings.decodePath) { index = i; break; }
        }
        wallPaperDecodePathCombo_.SelectedIndex(index);
        wallPaperDecodePathCombo_.IsEnabled(available);
    }
    if (wallPaperSpeedCombo_) {
        // 与 EnsureWindow 里的档位数组一致；找不到就落到 1×
        static const double kSpeeds[] = { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0 };
        int index = 3;
        for (int i = 0; i < 8; ++i) {
            if (std::abs(kSpeeds[i] - settings.speed) < 0.01) { index = i; break; }
        }
        wallPaperSpeedCombo_.SelectedIndex(index);
    }
    if (wallPaperAudioSwitch_) wallPaperAudioSwitch_.IsOn(settings.audioEnabled);
    if (wallPaperVolumeSlider_) {
        wallPaperVolumeSlider_.Value(settings.audioVolume * 100.0f);
        if (wallPaperVolumeLabel_) {
            wallPaperVolumeLabel_.Text(std::to_wstring(
                static_cast<int>(settings.audioVolume * 100.0f + 0.5f)) + L"%");
        }
    }

    // 按当前壁纸的类型决定哪些控件有意义（规格 §11：不适用就置灰并说明）
    BackendKind activeKind = BackendKind::Video;
    bool hasActive = false;
    {
        const auto items = wp->ListItems();
        for (const auto& it : items) {
            if (it.id == settings.activeId) { activeKind = it.kind; hasActive = true; break; }
        }
    }
    if (hasActive) {
        if (wallPaperVariantCombo_) wallPaperVariantCombo_.IsEnabled(desktopsticker::wallpaper::backend_kind_supports_variants(activeKind));
        if (wallPaperVariantButton_) wallPaperVariantButton_.IsEnabled(desktopsticker::wallpaper::backend_kind_supports_variants(activeKind));
        if (wallPaperSpeedCombo_) wallPaperSpeedCombo_.IsEnabled(desktopsticker::wallpaper::backend_kind_supports_speed(activeKind));
        // 没有音轨的类型（动图/序列/着色器）置灰；网页能出声但音量不归我们管
        const bool audioOk = desktopsticker::wallpaper::backend_kind_has_audio(activeKind);
        if (wallPaperAudioSwitch_) wallPaperAudioSwitch_.IsEnabled(audioOk);
        if (wallPaperVolumeSlider_) {
            wallPaperVolumeSlider_.IsEnabled(audioOk && activeKind == BackendKind::Video);
        }
    }

    if (wallPaperGrid_) {
        wallPaperGrid_.Items().Clear();
        const auto items = wp->ListItems();
        const auto rootPath = std::filesystem::path(settings.libraryRoot);
        // 未生成封面时的占位底色（本函数与 EnsureWindow 不共享局部配色变量）
        const auto placeholder = Solid(0x38, 0x80, 0x80, 0x80);

        for (const auto& item : items) {
            // 缩略图直接读库里生成好的 poster.png（比把 HICON 转成 WinUI 图像源简单可靠），
            // 尚未生成时用底色占位，避免网格出现空洞。
            auto frame = Border();
            frame.Width(196);
            frame.Height(110);
            frame.CornerRadius(CornerRadiusHelper::FromUniformRadius(6));
            frame.Background(placeholder);

            const auto poster = rootPath / L"media" / item.id / L"poster.png";
            std::error_code ec;
            if (std::filesystem::is_regular_file(poster, ec)) {
                auto image = Microsoft::UI::Xaml::Controls::Image();
                image.Stretch(Microsoft::UI::Xaml::Media::Stretch::UniformToFill);
                auto bitmap = Microsoft::UI::Xaml::Media::Imaging::BitmapImage();
                bitmap.UriSource(winrt::Windows::Foundation::Uri(poster.wstring()));
                image.Source(bitmap);
                frame.Child(image);
            }

            auto label = TextBlock();
            // 类型标签：一眼能看出这条是视频还是网页/序列（规格 §11）
            label.Text(item.name + L" · " + desktopsticker::wallpaper::backend_kind_name(item.kind) +
                       (item.hasBalanced || item.hasPowerSaver ? L" · 有副本" : L""));
            label.FontSize(12);
            label.MaxWidth(196);
            label.TextTrimming(Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);

            auto tile = StackPanel();
            tile.Spacing(4);
            tile.Tag(box_value(item.id));   // Tag 挂在 tile 上，供选中项读取 id
            tile.Children().Append(frame);
            tile.Children().Append(label);

            wallPaperGrid_.Items().Append(tile);
        }

        // 选中当前壁纸
        for (uint32_t i = 0; i < wallPaperGrid_.Items().Size(); ++i) {
            auto tile = wallPaperGrid_.Items().GetAt(i).try_as<FrameworkElement>();
            if (!tile) continue;
            const auto id = unbox_value_or<hstring>(tile.Tag(), L"");
            if (std::wstring(id.c_str()) == settings.activeId) {
                wallPaperGrid_.SelectedIndex(static_cast<int>(i));
                break;
            }
        }
    }

    if (wallPaperStatus_) {
        const auto items = wp->ListItems();
        wallPaperStatus_.Text(L"存储位置：" + settings.libraryRoot +
                              L"\n共 " + std::to_wstring(items.size()) + L" 个壁纸");
    }
    wallpaperLoading_ = false;
    RefreshPlaybackText();   // 控件刷新时顺手刷一次回显，不必等下一个 Tick
}

// 设置页要能回答："现在到底走的哪条路、多少帧"。请求的路径与实际后端分开显示 ——
// 请求的那条路可能因为环境不可用而回落，只显示选择值会骗人。
void SettingsController::RefreshPlaybackText() {
    if (!window_ || closed_ || !wallPaperPlaybackText_) return;

    auto* wp = host_ ? host_->WallPaper() : nullptr;
    if (!wp || !wp->Available()) {
        wallPaperPlaybackText_.Text(L"壁纸模块不可用（详见 debug.log）");
        return;
    }

    const auto st = wp->PlaybackStatus();
    std::wstring text = L"设置：" + st.requestedPath + L"\n实际：" +
                        (st.backend.empty() ? L"-" : st.backend);
    if (st.playing && st.fps > 0.0) {
        // 一位小数自己拼，不依赖 printf 家族（EXE 的 pch 没包 <cstdio>）
        const long long tenths = static_cast<long long>(st.fps * 10.0 + 0.5);
        text += L"\n实测 " + std::to_wstring(tenths / 10) + L"." +
                std::to_wstring(tenths % 10) + L" fps · 帧 " + std::to_wstring(st.width) +
                L"×" + std::to_wstring(st.height);
    } else if (st.playing) {
        text += L"\n正在启动…";
    } else {
        text += L"\n未在播放（已暂停、未启用，或刚切换完正在启动）";
    }
    wallPaperPlaybackText_.Text(text);
}

void SettingsController::SaveWallPaper() {
    if (wallpaperLoading_) return;
    auto* wp = host_ ? host_->WallPaper() : nullptr;
    if (!wp || !wp->Available()) return;

    auto settings = wp->GetSettings();
    if (wallPaperSwitch_) settings.enabled = wallPaperSwitch_.IsOn();
    if (wallPaperPauseFullscreenSwitch_) settings.pauseOnFullscreen = wallPaperPauseFullscreenSwitch_.IsOn();
    if (wallPaperPauseLockSwitch_) settings.pauseOnLock = wallPaperPauseLockSwitch_.IsOn();
    if (wallPaperVariantCombo_) {
        const int index = wallPaperVariantCombo_.SelectedIndex();
        settings.preferred = index == 2 ? VariantKind::PowerSaver
                           : index == 1 ? VariantKind::Balanced
                                        : VariantKind::Original;
    }
    if (wallPaperSpeedCombo_) {
        static const double kSpeeds[] = { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0 };
        const int index = wallPaperSpeedCombo_.SelectedIndex();
        if (index >= 0 && index < 8) settings.speed = kSpeeds[index];
    }
    if (wallPaperDecodePathCombo_) {
        const int index = wallPaperDecodePathCombo_.SelectedIndex();
        if (index >= 0 && index < kDecodePathCount) settings.decodePath = kDecodePaths[index];
    }
    if (wallPaperAudioSwitch_) settings.audioEnabled = wallPaperAudioSwitch_.IsOn();
    if (wallPaperVolumeSlider_) {
        settings.audioVolume = static_cast<float>(wallPaperVolumeSlider_.Value() / 100.0);
    }
    if (wallPaperGrid_) {
        const int sel = wallPaperGrid_.SelectedIndex();
        if (sel >= 0 && static_cast<uint32_t>(sel) < wallPaperGrid_.Items().Size()) {
            auto tile = wallPaperGrid_.Items().GetAt(static_cast<uint32_t>(sel)).try_as<FrameworkElement>();
            if (tile) {
                settings.activeId = unbox_value_or<hstring>(tile.Tag(), L"").c_str();
            }
        }
    }
    wp->SetSettings(settings);
    if (wallPaperUserPauseSwitch_) wp->SetUserPaused(wallPaperUserPauseSwitch_.IsOn());
}

std::wstring SettingsController::SelectedWallPaperId() const {
    if (!wallPaperGrid_) return {};
    const int sel = wallPaperGrid_.SelectedIndex();
    if (sel < 0 || static_cast<uint32_t>(sel) >= wallPaperGrid_.Items().Size()) return {};
    auto tile = wallPaperGrid_.Items().GetAt(static_cast<uint32_t>(sel)).try_as<FrameworkElement>();
    if (!tile) return {};
    return unbox_value_or<hstring>(tile.Tag(), L"").c_str();
}

void SettingsController::ImportWallPaper() {
    auto* wp = host_ ? host_->WallPaper() : nullptr;
    if (!wp || !wp->Available()) return;

    // 用 Win32 通用文件对话框：解包 WinUI3 下无需额外的 WinRT 拾取器 interop 初始化
    winrt::com_ptr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(dialog.put())))) {
        AppLog("wallpaper", "CoCreateInstance(FileOpenDialog) failed");
        return;
    }

    const COMDLG_FILTERSPEC filters[] = {
        { L"视频 / 动图 / 图片", L"*.mp4;*.mkv;*.mov;*.avi;*.webm;*.wmv;*.m4v;*.mpg;*.mpeg;*.ts"
                                 L";*.gif;*.webp;*.apng;*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff" },
        { L"所有文件", L"*.*" },
    };
    dialog->SetFileTypes(2, filters);
    dialog->SetTitle(L"选择壁纸文件（类型自动识别）");
    dialog->SetOptions(FOS_FILEMUSTEXIST | FOS_PATHMUSTEXIST);
    if (FAILED(dialog->Show(nullptr))) return; // 用户取消

    winrt::com_ptr<IShellItem> item;
    if (FAILED(dialog->GetResult(item.put()))) return;
    PWSTR picked = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &picked))) return;
    std::wstring path(picked);
    CoTaskMemFree(picked);

    std::wstring id;
    if (!wp->Import(path, id)) {
        if (wallPaperStatus_) wallPaperStatus_.Text(L"导入失败，详见 %APPDATA%\\DesktopSticker\\debug.log");
        return;
    }
    // 导入是后台线程回报的，这里先本地刷新一次，事件回调稍后还会再刷
    RefreshWallPaperControls();
}

// 目录型来源：图片文件夹 → 图片序列，含 index.html → 网页，含 .frag/.gltf → 着色器。
// 具体是哪一种交给模块里的 classify_directory 判定，UI 不猜。
void SettingsController::ImportWallPaperFolder() {
    auto* wp = host_ ? host_->WallPaper() : nullptr;
    if (!wp || !wp->Available()) return;

    winrt::com_ptr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(dialog.put())))) {
        AppLog("wallpaper", "CoCreateInstance(FileOpenDialog) failed");
        return;
    }
    dialog->SetTitle(L"选择壁纸文件夹（图片序列 / 网页 / 着色器）");
    dialog->SetOptions(FOS_PICKFOLDERS | FOS_PATHMUSTEXIST);
    if (FAILED(dialog->Show(nullptr))) return;

    winrt::com_ptr<IShellItem> item;
    if (FAILED(dialog->GetResult(item.put()))) return;
    PWSTR picked = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &picked))) return;
    std::wstring path(picked);
    CoTaskMemFree(picked);

    std::wstring id;
    if (!wp->Import(path, id)) {
        if (wallPaperStatus_) {
            wallPaperStatus_.Text(L"导入文件夹失败：目录里没有可识别的内容"
                                  L"（图片序列/含 index.html 的网页/含 .frag 或 .gltf 的着色器）");
        }
        return;
    }
    RefreshWallPaperControls();
}

void SettingsController::OpenWallPaperConfigDir() {
    auto* wp = host_ ? host_->WallPaper() : nullptr;
    if (!wp || !wp->Available()) return;

    const std::wstring root = wp->GetSettings().libraryRoot;
    if (root.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    ShellExecuteW(nullptr, L"open", root.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void SettingsController::RemoveSelectedWallPaper() {
    auto* wp = host_ ? host_->WallPaper() : nullptr;
    if (!wp || !wp->Available()) return;

    const std::wstring id = SelectedWallPaperId();
    if (id.empty()) {
        if (wallPaperStatus_) wallPaperStatus_.Text(L"请先在列表中选择一个壁纸");
        return;
    }
    wp->Remove(id);
    RefreshWallPaperControls();
}

void SettingsController::RegenerateSelectedVariant() {
    auto* wp = host_ ? host_->WallPaper() : nullptr;
    if (!wp || !wp->Available()) return;

    const std::wstring id = SelectedWallPaperId();
    if (id.empty()) {
        if (wallPaperStatus_) wallPaperStatus_.Text(L"请先在列表中选择一个壁纸");
        return;
    }

    // 档位选"原画"时没有副本可生成，按均衡档生成
    auto settings = wp->GetSettings();
    const auto kind = settings.preferred == VariantKind::Original ? VariantKind::Balanced
                                                                 : settings.preferred;
    if (wp->RegenerateVariant(id, kind)) {
        if (wallPaperStatus_) wallPaperStatus_.Text(L"已在后台生成性能副本，完成后列表会自动刷新…");
    } else {
        if (wallPaperStatus_) wallPaperStatus_.Text(L"生成副本失败：缺少 tools\\ffmpeg\\ffmpeg.exe 或条目不存在");
    }
}

void SettingsController::ChangeWallPaperRoot() {
    auto* wp = host_ ? host_->WallPaper() : nullptr;
    if (!wp || !wp->Available()) return;

    winrt::com_ptr<IFileOpenDialog> dialog;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(dialog.put())))) {
        return;
    }
    dialog->SetTitle(L"选择新的壁纸库位置");
    dialog->SetOptions(FOS_PICKFOLDERS | FOS_PATHMUSTEXIST | FOS_FORCEFILESYSTEM);
    if (FAILED(dialog->Show(nullptr))) return; // 用户取消

    winrt::com_ptr<IShellItem> item;
    if (FAILED(dialog->GetResult(item.put()))) return;
    PWSTR picked = nullptr;
    if (FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &picked))) return;
    const std::wstring path(picked);
    CoTaskMemFree(picked);

    // 复制 + 逐文件校验 + 切换记录；旧位置保留不删（可回滚）
    if (!wp->ChangeLibraryRoot(path)) {
        if (wallPaperStatus_) wallPaperStatus_.Text(L"更改存储位置失败（目标不可写或复制校验不通过），详见 debug.log");
        return;
    }
    RefreshWallPaperControls();
}

} // namespace desktopsticker::app