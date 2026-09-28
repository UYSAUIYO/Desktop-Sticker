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

// 播放速度档位与顺序；滑块刻度 / 填项 / 持久化三处共用一份
constexpr double kSpeeds[] = { 0.25, 0.5, 0.75, 1.0, 1.5, 2.0, 3.0, 4.0 };

// "166 MB" 风格的条目大小，和参考截图的元信息一致
std::wstring FormatSize(uint64_t bytes) {
    if (bytes == 0) return L"0 B";
    const wchar_t* units[] = { L"B", L"KB", L"MB", L"GB", L"TB" };
    double v = static_cast<double>(bytes);
    int u = 0;
    while (v >= 1024.0 && u < 4) { v /= 1024.0; ++u; }
    wchar_t text[32]{};
    swprintf_s(text, v >= 100.0 || u == 0 ? L"%.0f %s" : L"%.1f %s", v, units[u]);
    return text;
}

// 库条目的 poster.png 转 WinUI 图像源；未生成封面返回空，由调用方显示占位
winrt::Windows::Foundation::IInspectable MakePosterSource(const std::filesystem::path& root,
                                                          const desktopsticker::WallPaperItem& item) {
    const auto poster = root / L"media" / item.id / L"poster.png";
    std::error_code ec;
    if (!std::filesystem::is_regular_file(poster, ec)) return nullptr;
    try {
        auto bitmap = Microsoft::UI::Xaml::Media::Imaging::BitmapImage();
        bitmap.UriSource(winrt::Windows::Foundation::Uri(poster.wstring()));
        return bitmap;
    } catch (...) {
        return nullptr; // 单张封面读不出来不能让设置页整体崩
    }
}

// 能力 id → 中文名（与编辑器面板声明的 id 一致）。未知 id 原样显示。
std::wstring PermissionDisplayName(const std::wstring& id) {
    if (id == L"network") return L"网络访问";
    if (id == L"file_outside") return L"包外文件读写";
    if (id == L"exec") return L"执行外部程序";
    if (id == L"system") return L"系统访问";
    return id;
}

std::wstring JoinWide(const std::vector<std::wstring>& v, const wchar_t* sep) {
    std::wstring out;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) out += sep;
        out += v[i];
    }
    return out;
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

    auto stickerScroll = ScrollViewer();
    stickerScroll.Content(stickerRoot);

    // 壁纸页不套用贴纸页的"居中限宽"根：全屏/最大化时要吃满整个内容区，
    // 多出来的宽度给左侧网格（自动排更多列），高度给网格与右栏内部滚动。
    auto wallPaperRoot = Grid();
    wallPaperRoot.Padding(ThicknessHelper::FromLengths(36, 8, 28, 20));
    wallPaperRoot.HorizontalAlignment(HorizontalAlignment::Stretch);
    if (!micaOk) {
        wallPaperRoot.Background(dark ? Solid(0xFF, 0x20, 0x20, 0x20) : Solid(0xFF, 0xF3, 0xF3, 0xF3));
    }
    auto wallPaperScroll = ScrollViewer();
    wallPaperScroll.Content(wallPaperRoot);
    // 关闭外层滚动：ScrollViewer 只在滚动被禁用时才把内容约束到视口尺寸，
    // 这样内层两栏才能拿到真实高度各自滚动，而不是被无限高撑开。
    wallPaperScroll.HorizontalScrollMode(ScrollMode::Disabled);
    wallPaperScroll.VerticalScrollMode(ScrollMode::Disabled);
    wallPaperScroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
    wallPaperScroll.VerticalScrollBarVisibility(ScrollBarVisibility::Hidden);

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

    // 壁纸右栏专用的纵向属性卡：标题行（图标+标题+描述）在上，控件满宽在下。
    // 不走 makeCard 的左右两列——在固定宽度右栏里宽控件会把标题挤到裁切。
    // 返回卡内内容栈，控件由调用方 append 进去（自动占满一行）。
    auto makePropCard = [&](StackPanel const& into, const wchar_t* glyph,
                           const wchar_t* title, const wchar_t* desc) {
        auto border = Border();
        border.CornerRadius(CornerRadiusHelper::FromUniformRadius(8));
        border.Background(cardBg);
        border.BorderBrush(cardStroke);
        border.BorderThickness(ThicknessHelper::FromLengths(1, 1, 1, 1));
        border.Padding(ThicknessHelper::FromLengths(16, 12, 16, 12));

        auto body = StackPanel();
        body.Spacing(8);

        auto head = StackPanel();
        head.Orientation(Orientation::Horizontal);
        head.Spacing(12);
        if (glyph && *glyph) {
            auto icon = FontIcon();
            icon.Glyph(glyph);
            icon.FontSize(18);
            icon.FontFamily(Media::FontFamily(L"Segoe Fluent Icons"));
            icon.VerticalAlignment(VerticalAlignment::Center);
            head.Children().Append(icon);
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
        head.Children().Append(texts);
        body.Children().Append(head);

        border.Child(body);
        into.Children().Append(border);
        return body;
    };
    // 把控件放进纵向卡的内容栈（与标题文字左对齐缩进，满宽）
    auto placeProp = [](StackPanel const& body, FrameworkElement const& ctl) {
        ctl.Margin(ThicknessHelper::FromLengths(30, 0, 0, 0));
        body.Children().Append(ctl);
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

    dblClickCleanSwitch_ = ToggleSwitch();
    placeRight(makeCard(widgetGroup, L"\uE762", L"双击空白处隐藏磁贴",
                        L"在桌面空白处双击鼠标左键，隐藏/再次显示磁贴、时钟与图标"),
               dblClickCleanSwitch_);
    dblClickCleanSwitch_.Toggled([this](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { SaveConfig(); });

    // ============ 第二个二级页面：桌面壁纸 ============
    // 参照 Wallpaper Engine 的两栏布局：左侧搜索条 + 壁纸网格，右侧预览大图 + 属性面板。
    // 页面自身不滚动（滚动发生在网格与右栏内部），构建内容挂在 layout 上。
    auto wallPaperLayout = Grid();
    {
        wallPaperLayout.ColumnSpacing(16);
        auto colLeft = ColumnDefinition();
        colLeft.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        auto colRight = ColumnDefinition();
        colRight.Width(GridLengthHelper::FromValueAndType(380, GridUnitType::Pixel));
        wallPaperLayout.ColumnDefinitions().Append(colLeft);
        wallPaperLayout.ColumnDefinitions().Append(colRight);
    }

    // —— 左栏：标题 / 启用开关 + 工具按钮 / 搜索条 / 壁纸网格（网格吃掉剩余高度）——
    auto left = Grid();
    left.RowSpacing(10);
    Grid::SetColumn(left, 0);
    {
        auto rTitle = RowDefinition(); rTitle.Height(GridLengthHelper::Auto());
        auto rBar = RowDefinition(); rBar.Height(GridLengthHelper::Auto());
        auto rSearch = RowDefinition(); rSearch.Height(GridLengthHelper::Auto());
        auto rGrid = RowDefinition(); rGrid.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        left.RowDefinitions().Append(rTitle);
        left.RowDefinitions().Append(rBar);
        left.RowDefinitions().Append(rSearch);
        left.RowDefinitions().Append(rGrid);
    }

    {
        auto wpTitle = TextBlock();
        wpTitle.Text(L"桌面壁纸");
        wpTitle.FontSize(28);
        wpTitle.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
        wpTitle.Margin(ThicknessHelper::FromLengths(0, 0, 0, 4));
        Grid::SetRow(wpTitle, 0);
        left.Children().Append(wpTitle);
    }
    {
        auto bar = Grid();
        auto c0 = ColumnDefinition();
        c0.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        auto c1 = ColumnDefinition();
        c1.Width(GridLengthHelper::Auto());
        bar.ColumnDefinitions().Append(c0);
        bar.ColumnDefinitions().Append(c1);

        wallPaperSwitch_ = ToggleSwitch();
        wallPaperSwitch_.Header(box_value(L"启用动态壁纸"));
        wallPaperSwitch_.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(wallPaperSwitch_, 0);
        bar.Children().Append(wallPaperSwitch_);
        wallPaperSwitch_.Toggled([this](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { SaveWallPaper(); });

        auto tools = StackPanel();
        tools.Orientation(Orientation::Horizontal);
        tools.Spacing(8);
        tools.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(tools, 1);

        wallPaperImportButton_ = Button();
        wallPaperImportButton_.Content(box_value(L"添加壁纸"));
        wallPaperImportButton_.MinWidth(92);
        tools.Children().Append(wallPaperImportButton_);
        wallPaperImportButton_.Click([this](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { ImportWallPaper(); });

        // 目录型来源（图片序列 / 网页 / 着色器）只能选文件夹，类型由目录内容自动判定
        wallPaperImportFolderButton_ = Button();
        wallPaperImportFolderButton_.Content(box_value(L"导入文件夹"));
        wallPaperImportFolderButton_.MinWidth(92);
        tools.Children().Append(wallPaperImportFolderButton_);
        wallPaperImportFolderButton_.Click([this](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { ImportWallPaperFolder(); });

        wallPaperOpenConfigButton_ = Button();
        wallPaperOpenConfigButton_.Content(box_value(L"打开目录"));
        wallPaperOpenConfigButton_.MinWidth(80);
        tools.Children().Append(wallPaperOpenConfigButton_);
        wallPaperOpenConfigButton_.Click([this](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { OpenWallPaperConfigDir(); });

        wallPaperChangeRootButton_ = Button();
        wallPaperChangeRootButton_.Content(box_value(L"存储位置"));
        wallPaperChangeRootButton_.MinWidth(80);
        tools.Children().Append(wallPaperChangeRootButton_);
        wallPaperChangeRootButton_.Click([this](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { ChangeWallPaperRoot(); });

        bar.Children().Append(tools);
        Grid::SetRow(bar, 1);
        left.Children().Append(bar);
    }
    {
        auto searchRow = Grid();
        searchRow.ColumnSpacing(8);
        auto c0 = ColumnDefinition();
        c0.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        auto c1 = ColumnDefinition();
        c1.Width(GridLengthHelper::Auto());
        searchRow.ColumnDefinitions().Append(c0);
        searchRow.ColumnDefinitions().Append(c1);

        wallPaperSearchBox_ = TextBox();
        wallPaperSearchBox_.PlaceholderText(L" 搜索");
        wallPaperSearchBox_.HorizontalAlignment(HorizontalAlignment::Stretch);
        Grid::SetColumn(wallPaperSearchBox_, 0);
        searchRow.Children().Append(wallPaperSearchBox_);
        wallPaperSearchBox_.TextChanged([this](winrt::Windows::Foundation::IInspectable const&, auto const&) {
            PopulateWallPaperGrid(false);
        });

        wallPaperStatus_ = TextBlock(); // 兼作"共 x 个壁纸 · 存储位置"计数行
        wallPaperStatus_.FontSize(12);
        wallPaperStatus_.Foreground(textSecondary);
        wallPaperStatus_.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(wallPaperStatus_, 1);
        searchRow.Children().Append(wallPaperStatus_);

        Grid::SetRow(searchRow, 2);
        left.Children().Append(searchRow);
    }
    wallPaperGrid_ = GridView();
    // GridView 默认面板就是 ItemsWrapGrid；卡片尺寸由 PopulateWallPaperGrid 里的固定缩略图决定
    wallPaperGrid_.SelectionMode(ListViewSelectionMode::Single);
    wallPaperGrid_.HorizontalAlignment(HorizontalAlignment::Stretch);
    wallPaperGrid_.VerticalAlignment(VerticalAlignment::Stretch);
    ScrollViewer::SetHorizontalScrollBarVisibility(wallPaperGrid_, ScrollBarVisibility::Disabled);
    Grid::SetRow(wallPaperGrid_, 3);
    left.Children().Append(wallPaperGrid_);
    wallPaperGrid_.SelectionChanged([this](winrt::Windows::Foundation::IInspectable const&, SelectionChangedEventArgs const&) {
        SaveWallPaper();
        UpdateWallPaperDetails();
    });

    wallPaperLayout.Children().Append(left);

    // —— 右栏：预览 + 元信息 + 属性 + 操作，整栏内部滚动 ——
    auto rightInner = StackPanel();
    rightInner.Spacing(4);
    rightInner.Padding(ThicknessHelper::FromLengths(0, 0, 4, 20));
    {
        auto rightScroll = ScrollViewer();
        rightScroll.Content(rightInner);
        rightScroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
        rightScroll.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
        Grid::SetColumn(rightScroll, 1);
        wallPaperLayout.Children().Append(rightScroll);
    }
    {
        // 预览卡：大图（选中项封面）+ 名称 + 类型/大小/副本元信息
        auto border = Border();
        border.CornerRadius(CornerRadiusHelper::FromUniformRadius(8));
        border.Background(cardBg);
        border.BorderBrush(cardStroke);
        border.BorderThickness(ThicknessHelper::FromLengths(1, 1, 1, 1));
        border.Padding(ThicknessHelper::FromLengths(16, 16, 16, 16));

        auto stack = StackPanel();
        stack.Spacing(8);

        wallPaperPreview_ = Border();
        wallPaperPreview_.Height(190);
        wallPaperPreview_.CornerRadius(CornerRadiusHelper::FromUniformRadius(6));
        wallPaperPreview_.Background(Solid(0x38, 0x80, 0x80, 0x80));
        stack.Children().Append(wallPaperPreview_);

        wallPaperDetailName_ = TextBlock();
        wallPaperDetailName_.FontSize(16);
        wallPaperDetailName_.FontWeight(Microsoft::UI::Text::FontWeights::SemiBold());
        wallPaperDetailName_.TextWrapping(TextWrapping::Wrap);
        stack.Children().Append(wallPaperDetailName_);

        wallPaperDetailMeta_ = TextBlock();
        wallPaperDetailMeta_.FontSize(12);
        wallPaperDetailMeta_.Foreground(textSecondary);
        wallPaperDetailMeta_.TextWrapping(TextWrapping::Wrap);
        stack.Children().Append(wallPaperDetailMeta_);

        border.Child(stack);
        rightInner.Children().Append(border);
    }

    // —— 属性区：全局播放设置（按选中条目的类型灰化不适用的行）——
    section(rightInner, L"属性");
    auto wallPaperGroup = group(rightInner);

    wallPaperVariantCombo_ = ComboBox();
    wallPaperVariantCombo_.HorizontalAlignment(HorizontalAlignment::Stretch);
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
    placeProp(makePropCard(wallPaperGroup, L"\uE9D9", L"播放档位",
                           L"原画优先；所选副本不存在时自动回落到原画"),
              wallPaperVariantCombo_);
    wallPaperVariantCombo_.SelectionChanged([this](winrt::Windows::Foundation::IInspectable const&, SelectionChangedEventArgs const&) { SaveWallPaper(); });

    {
        // 速度：滑块吃掉整行宽度，右侧直接显示当前倍率（8 档离散刻度）
        // 用两列 Grid（滑块 Star / 标签 Auto）——横向 StackPanel 不会拉伸滑块，只会给期望宽度
        auto panel = Grid();
        panel.ColumnSpacing(8);
        panel.VerticalAlignment(VerticalAlignment::Center);
        auto sc0 = ColumnDefinition();
        sc0.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        auto sc1 = ColumnDefinition();
        sc1.Width(GridLengthHelper::Auto());
        panel.ColumnDefinitions().Append(sc0);
        panel.ColumnDefinitions().Append(sc1);

        wallPaperSpeedSlider_ = Slider();
        wallPaperSpeedSlider_.Minimum(0);
        wallPaperSpeedSlider_.Maximum(static_cast<double>(std::size(kSpeeds)) - 1);
        wallPaperSpeedSlider_.StepFrequency(1);
        Grid::SetColumn(wallPaperSpeedSlider_, 0);
        panel.Children().Append(wallPaperSpeedSlider_);

        wallPaperSpeedLabel_ = TextBlock();
        wallPaperSpeedLabel_.FontSize(12);
        wallPaperSpeedLabel_.MinWidth(34);
        wallPaperSpeedLabel_.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(wallPaperSpeedLabel_, 1);
        panel.Children().Append(wallPaperSpeedLabel_);

        placeProp(makePropCard(wallPaperGroup, L"\uE916", L"播放速度",
                               L"对视频/动图/图片序列生效；网页的节奏由页面自己决定，故不可调"),
                  panel);
        // 规格 §11：0.25×–4× 档位；调速由渲染侧的 frame_advance_policy 缩放播放节奏
        wallPaperSpeedSlider_.ValueChanged([this](winrt::Windows::Foundation::IInspectable const&,
                                                  Microsoft::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& e) {
            if (wallPaperSpeedLabel_) {
                // 滑块连续取值，读数时四舍五入到档位刻度
                const int idx = std::clamp(static_cast<int>(std::lround(e.NewValue())), 0,
                                           static_cast<int>(std::size(kSpeeds)) - 1);
                wchar_t text[32]{};
                swprintf_s(text, L"%g\u00D7", kSpeeds[idx]);
                wallPaperSpeedLabel_.Text(text);
            }
            SaveWallPaper();
        });
    }

    wallPaperAudioSwitch_ = ToggleSwitch();
    placeProp(makePropCard(wallPaperGroup, L"\uE767", L"播放声音",
                           L"默认关闭；只有带音轨的视频和网页才会出声"),
              wallPaperAudioSwitch_);
    wallPaperAudioSwitch_.Toggled([this](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { SaveWallPaper(); });

    wallPaperDecodePathCombo_ = ComboBox();
    wallPaperDecodePathCombo_.HorizontalAlignment(HorizontalAlignment::Stretch);
    {
        // 四档：自动 / FFmpeg+Vulkan 硬解 / MF+D3D11 硬解 / CPU 软解。
        // 名字与顺序必须与 DecodePath 的持久化映射一致（见 DecodePath.h）。
        for (auto p : kDecodePaths) {
            auto entry = ComboBoxItem();
            entry.Content(box_value(desktopsticker::wallpaper::decode_path_name(p)));
            wallPaperDecodePathCombo_.Items().Append(entry);
        }
    }
    placeProp(makePropCard(wallPaperGroup, L"\uE950", L"播放方式",
                           L"换一条解码/呈现路径，切换后立刻重开（不必重启）；某条路不可用会自动回落"),
              wallPaperDecodePathCombo_);
    wallPaperDecodePathCombo_.SelectionChanged([this](winrt::Windows::Foundation::IInspectable const&, SelectionChangedEventArgs const&) { SaveWallPaper(); });

    {
        // 音量：滑块吃掉整行宽度 + 百分比文字（两列 Grid，同速度行）
        auto panel = Grid();
        panel.ColumnSpacing(8);
        panel.VerticalAlignment(VerticalAlignment::Center);
        auto vc0 = ColumnDefinition();
        vc0.Width(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
        auto vc1 = ColumnDefinition();
        vc1.Width(GridLengthHelper::Auto());
        panel.ColumnDefinitions().Append(vc0);
        panel.ColumnDefinitions().Append(vc1);

        wallPaperVolumeSlider_ = Slider();
        wallPaperVolumeSlider_.Minimum(0);
        wallPaperVolumeSlider_.Maximum(100);
        wallPaperVolumeSlider_.StepFrequency(5);
        Grid::SetColumn(wallPaperVolumeSlider_, 0);
        panel.Children().Append(wallPaperVolumeSlider_);

        wallPaperVolumeLabel_ = TextBlock();
        wallPaperVolumeLabel_.FontSize(12);
        wallPaperVolumeLabel_.MinWidth(38);
        wallPaperVolumeLabel_.VerticalAlignment(VerticalAlignment::Center);
        Grid::SetColumn(wallPaperVolumeLabel_, 1);
        panel.Children().Append(wallPaperVolumeLabel_);

        placeProp(makePropCard(wallPaperGroup, L"\uE995", L"音量",
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
    placeProp(makePropCard(wallPaperGroup, L"\uE740", L"全屏时暂停",
                           L"检测到覆盖整个屏幕的应用时停止播放，切回桌面自动恢复"),
              wallPaperPauseFullscreenSwitch_);
    wallPaperPauseFullscreenSwitch_.Toggled([this](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { SaveWallPaper(); });

    wallPaperPauseLockSwitch_ = ToggleSwitch();
    placeProp(makePropCard(wallPaperGroup, L"\uE72E", L"锁屏或息屏时暂停",
                           L"会话锁定或显示器关闭时停止播放"),
              wallPaperPauseLockSwitch_);
    wallPaperPauseLockSwitch_.Toggled([this](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { SaveWallPaper(); });

    wallPaperUserPauseSwitch_ = ToggleSwitch();
    placeProp(makePropCard(wallPaperGroup, L"\uE769", L"手动暂停",
                           L"临时停止播放，不影响其他设置"),
              wallPaperUserPauseSwitch_);
    wallPaperUserPauseSwitch_.Toggled([this](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { SaveWallPaper(); });

    // 当前播放状态：整宽一张卡，放在属性区末尾，由计时器每秒刷新
    {
        auto border = Border();
        border.CornerRadius(CornerRadiusHelper::FromUniformRadius(8));
        border.Background(cardBg);
        border.BorderBrush(cardStroke);
        border.BorderThickness(ThicknessHelper::FromLengths(1, 1, 1, 1));
        border.Padding(ThicknessHelper::FromLengths(16, 12, 16, 12));

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

    // —— 操作区：只作用于当前选中条目，放在右栏底部 ——
    section(rightInner, L"操作");
    {
        auto row = StackPanel();
        row.Orientation(Orientation::Horizontal);
        row.Spacing(8);
        row.Margin(ThicknessHelper::FromLengths(0, 4, 0, 4));

        wallPaperRemoveButton_ = Button();
        wallPaperRemoveButton_.Content(box_value(L"删除选中"));
        wallPaperRemoveButton_.MinWidth(96);
        row.Children().Append(wallPaperRemoveButton_);
        wallPaperRemoveButton_.Click([this](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { RemoveSelectedWallPaper(); });

        wallPaperVariantButton_ = Button();
        wallPaperVariantButton_.Content(box_value(L"重新生成副本"));
        wallPaperVariantButton_.MinWidth(116);
        row.Children().Append(wallPaperVariantButton_);
        wallPaperVariantButton_.Click([this](winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { RegenerateSelectedVariant(); });

        rightInner.Children().Append(row);
    }

    wallPaperRoot.Children().Append(wallPaperLayout);

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
    dblClickCleanSwitch_.IsOn(cfg.dblClickCleanMode);
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
    if (dblClickCleanSwitch_) cfg.dblClickCleanMode = dblClickCleanSwitch_.IsOn();

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
    if (wallPaperSpeedSlider_) wallPaperSpeedSlider_.IsEnabled(available);
    if (wallPaperAudioSwitch_) wallPaperAudioSwitch_.IsEnabled(available);
    if (wallPaperVolumeSlider_) wallPaperVolumeSlider_.IsEnabled(available);
    if (wallPaperGrid_) wallPaperGrid_.IsEnabled(available);
    if (wallPaperSearchBox_) wallPaperSearchBox_.IsEnabled(available);
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
        wpItems_.clear();
        if (wallPaperGrid_) wallPaperGrid_.Items().Clear();
        if (wallPaperSearchBox_) wallPaperSearchBox_.IsEnabled(false);
        UpdateWallPaperDetails();
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
    if (wallPaperSearchBox_) wallPaperSearchBox_.IsEnabled(true);
    {
        // 滑块档位与 kSpeeds 一致；找不到就落到 1×
        int index = 3;
        for (int i = 0; i < static_cast<int>(std::size(kSpeeds)); ++i) {
            if (std::abs(kSpeeds[i] - settings.speed) < 0.01) { index = i; break; }
        }
        if (wallPaperSpeedSlider_) wallPaperSpeedSlider_.Value(index);
        if (wallPaperSpeedLabel_) {
            wchar_t text[32]{};
            swprintf_s(text, L"%g\u00D7", kSpeeds[index]);
            wallPaperSpeedLabel_.Text(text);
        }
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
    wpItems_ = wp->ListItems(); // 缓存一份：搜索过滤 / 右侧详情 / 网格都从这里取
    {
        for (const auto& it : wpItems_) {
            if (it.id == settings.activeId) { activeKind = it.kind; hasActive = true; break; }
        }
    }
    if (hasActive) {
        if (wallPaperVariantCombo_) wallPaperVariantCombo_.IsEnabled(desktopsticker::wallpaper::backend_kind_supports_variants(activeKind));
        if (wallPaperVariantButton_) wallPaperVariantButton_.IsEnabled(desktopsticker::wallpaper::backend_kind_supports_variants(activeKind));
        if (wallPaperSpeedSlider_) wallPaperSpeedSlider_.IsEnabled(desktopsticker::wallpaper::backend_kind_supports_speed(activeKind));
        // 没有音轨的类型（动图/序列/着色器）置灰；网页能出声但音量不归我们管
        const bool audioOk = desktopsticker::wallpaper::backend_kind_has_audio(activeKind);
        if (wallPaperAudioSwitch_) wallPaperAudioSwitch_.IsEnabled(audioOk);
        if (wallPaperVolumeSlider_) {
            wallPaperVolumeSlider_.IsEnabled(audioOk && activeKind == BackendKind::Video);
        }
    }

    if (wallPaperGrid_) {
        PopulateWallPaperGrid(true);
        UpdateWallPaperDetails();
    }

    if (wallPaperStatus_) {
        wallPaperStatus_.Text(L"共 " + std::to_wstring(wpItems_.size()) + L" 个壁纸\n"
                              + L"存储位置：" + settings.libraryRoot);
    }
    wallpaperLoading_ = false;
    RefreshPlaybackText();   // 控件刷新时顺手刷一次回显，不必等下一个 Tick
}

// 按缓存条目 + 搜索关键字重建左侧网格。格子样式对齐参考截图：
// 缩略图铺满 + 底部名称条，当前播放项右上角绿点、选中项左侧绿色竖条。
void SettingsController::PopulateWallPaperGrid(bool keepSelection) {
    if (!wallPaperGrid_) return;
    auto* wp = host_ ? host_->WallPaper() : nullptr;
    const auto settings = wp ? wp->GetSettings() : desktopsticker::WallPaperSettings{};
    const std::wstring needle = wallPaperSearchBox_ ? wallPaperSearchBox_.Text().c_str() : L"";
    const auto rootPath = std::filesystem::path(settings.libraryRoot);
    const auto placeholder = Solid(0x38, 0x80, 0x80, 0x80);

    int shown = 0;
    wallPaperGrid_.Items().Clear();
    for (const auto& item : wpItems_) {
        if (!needle.empty() && item.name.find(needle) == std::wstring::npos) continue;
        ++shown;

        auto tile = Grid();
        tile.Tag(box_value(item.id)); // Tag 挂在 tile 上，供选中项读取 id
        tile.Width(204);              // 略宽于缩略图，留出选中框

        auto frame = Border();
        frame.Width(196);
        frame.Height(110); // 16:9 缩略图，卡片墙尺寸统一
        frame.CornerRadius(CornerRadiusHelper::FromUniformRadius(6));
        frame.Background(placeholder);
        Grid::SetRow(frame, 0);
        if (auto src = MakePosterSource(rootPath, item)) {
            auto image = Microsoft::UI::Xaml::Controls::Image();
            image.Stretch(Microsoft::UI::Xaml::Media::Stretch::UniformToFill);
            image.Source(src.as<Microsoft::UI::Xaml::Media::ImageSource>());
            frame.Child(image);
        }
        tile.Children().Append(frame);

        auto nameStrip = Border(); // 底部半透明名称条，字压在封面上
        nameStrip.Background(Solid(0xB0, 0x1B, 0x1B, 0x1B));
        nameStrip.VerticalAlignment(VerticalAlignment::Bottom);
        nameStrip.CornerRadius(CornerRadiusHelper::FromUniformRadius(6));
        nameStrip.Padding(ThicknessHelper::FromLengths(8, 2, 8, 3));
        auto label = TextBlock();
        label.Text(item.name);
        label.FontSize(12);
        label.Foreground(Solid(0xFF, 0xFF, 0xFF, 0xFF));
        label.TextTrimming(Microsoft::UI::Xaml::TextTrimming::CharacterEllipsis);
        nameStrip.Child(label);
        tile.Children().Append(nameStrip);

        if (item.id == settings.activeId) { // "正在播放"绿点
            auto dot = Border();
            dot.Width(10);
            dot.Height(10);
            dot.CornerRadius(CornerRadiusHelper::FromUniformRadius(5));
            dot.Background(Solid(0xFF, 0x6B, 0xBD, 0x5E));
            dot.HorizontalAlignment(HorizontalAlignment::Right);
            dot.VerticalAlignment(VerticalAlignment::Top);
            dot.Margin(ThicknessHelper::FromLengths(0, 6, 6, 0));
            tile.Children().Append(dot);
        }

        wallPaperGrid_.Items().Append(tile);
    }

    // 选中当前壁纸（被搜索过滤掉时不强制选中）
    int selectedIndex = -1;
    for (uint32_t i = 0; i < wallPaperGrid_.Items().Size(); ++i) {
        auto tile = wallPaperGrid_.Items().GetAt(i).try_as<FrameworkElement>();
        if (!tile) continue;
        if (std::wstring(unbox_value_or<hstring>(tile.Tag(), L"").c_str()) == settings.activeId) {
            selectedIndex = static_cast<int>(i);
            break;
        }
    }
    wallPaperGrid_.SelectedIndex(selectedIndex);
    (void)keepSelection; // 搜索后不强制滚动，保持简单（无 XAML 页面可拿容器）

    if (wallPaperStatus_ && wp) {
        std::wstring text = needle.empty()
            ? L"共 " + std::to_wstring(wpItems_.size()) + L" 个壁纸"
            : L"筛选结果（" + std::to_wstring(wpItems_.size()) + L" 中有 " + std::to_wstring(shown) + L" 个）";
        text += L"\n存储位置：" + wp->GetSettings().libraryRoot;
        wallPaperStatus_.Text(text);
    }
}

// 右栏预览卡跟随网格选中项（未选中时回落到当前播放项）。
void SettingsController::UpdateWallPaperDetails() {
    if (!wallPaperPreview_ || !wallPaperDetailName_ || !wallPaperDetailMeta_) return;
    auto* wp = host_ ? host_->WallPaper() : nullptr;
    const auto settings = wp ? wp->GetSettings() : desktopsticker::WallPaperSettings{};

    std::wstring id = SelectedWallPaperId();
    if (id.empty()) id = settings.activeId;

    const desktopsticker::WallPaperItem* found = nullptr;
    for (const auto& it : wpItems_) {
        if (it.id == id) { found = &it; break; }
    }

    const auto placeholder = Solid(0x38, 0x80, 0x80, 0x80);
    if (!found) {
        wallPaperPreview_.Background(placeholder);
        wallPaperPreview_.Child(nullptr);
        wallPaperDetailName_.Text(L"未选择壁纸");
        wallPaperDetailMeta_.Text(wp ? L"在左侧列表中点选一个壁纸查看详情" : L"壁纸模块不可用");
        return;
    }

    const auto rootPath = std::filesystem::path(settings.libraryRoot);
    if (auto src = MakePosterSource(rootPath, *found)) {
        auto image = Microsoft::UI::Xaml::Controls::Image();
        image.Stretch(Microsoft::UI::Xaml::Media::Stretch::UniformToFill);
        image.Source(src.as<Microsoft::UI::Xaml::Media::ImageSource>());
        wallPaperPreview_.Child(image);
        wallPaperPreview_.Background(placeholder);
    } else {
        wallPaperPreview_.Child(nullptr); // 封面未生成：留占位底色
        wallPaperPreview_.Background(placeholder);
    }

    wallPaperDetailName_.Text(found->name);
    std::wstring meta = std::wstring(desktopsticker::wallpaper::backend_kind_name(found->kind));
    if (found->sourceBytes > 0) meta += L" · " + FormatSize(found->sourceBytes);
    if (found->hasBalanced || found->hasPowerSaver) {
        meta += std::wstring(L" · 有性能副本") +
                ((found->hasBalanced && found->hasPowerSaver) ? L"（均衡+省电）"
                 : found->hasBalanced                     ? L"（均衡）"
                                                          : L"（省电）");
    }
    if (!settings.activeId.empty() && found->id == settings.activeId) meta += L" · 正在播放";

    // 场景壁纸包携带的作者元数据（其它来源这些字段为空，不额外占行）。
    if (!found->author.empty()) meta += L"\n作者：" + found->author;
    if (!found->tags.empty()) meta += L"\n标签：" + JoinWide(found->tags, L"、");
    if (!found->categories.empty()) meta += L"\n分类：" + JoinWide(found->categories, L"、");
    if (!found->requestedPermissions.empty()) {
        std::vector<std::wstring> names;
        names.reserve(found->requestedPermissions.size());
        for (const auto& p : found->requestedPermissions) names.push_back(PermissionDisplayName(p));
        meta += L"\n请求权限：" + JoinWide(names, L"、") + L"（仅声明，未强制）";
    }
    if (!found->description.empty()) meta += L"\n" + found->description;
    wallPaperDetailMeta_.Text(meta);
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
    if (wallPaperSpeedSlider_) {
        const int idx = std::clamp(static_cast<int>(std::lround(wallPaperSpeedSlider_.Value())), 0,
                                   static_cast<int>(std::size(kSpeeds)) - 1);
        settings.speed = kSpeeds[idx];
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
        { L"场景壁纸包 (*.dswall)", L"*.dswall" },
        { L"视频 / 动图 / 图片", L"*.mp4;*.mkv;*.mov;*.avi;*.webm;*.wmv;*.m4v;*.mpg;*.mpeg;*.ts"
                                 L";*.gif;*.webp;*.apng;*.png;*.jpg;*.jpeg;*.bmp;*.tif;*.tiff" },
        { L"所有文件", L"*.*" },
    };
    dialog->SetFileTypes(3, filters);
    dialog->SetTitle(L"选择壁纸文件（类型自动识别；场景壁纸包从编辑器导出）");
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