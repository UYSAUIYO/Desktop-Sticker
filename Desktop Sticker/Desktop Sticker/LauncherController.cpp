#include "pch.h"
#include "LauncherController.h"

#include "AppLog.h"
#include "WindowChrome.h"

#include <microsoft.ui.xaml.window.h>
#include <shellapi.h>
#include <shlobj_core.h>
#include <wincodec.h>

#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Xaml.Media.Imaging.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Storage.Streams.h>

#include <cstring>
#include <cstdio>
#include <fstream>

#pragma comment(lib, "gdi32.lib")

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Input;
using namespace Microsoft::UI::Xaml::Media;
using namespace Microsoft::UI::Xaml::Media::Imaging;

namespace desktopsticker::app {
namespace {

// 网格每行磁贴数与单元尺寸（DIP）
constexpr int kColumns = 8;
constexpr float kTileWidth = 76.0f;
constexpr float kTileHeight = 78.0f;

Windows::Foundation::Collections::IVector<UIElement> PanelChildren(
    winrt::Microsoft::UI::Xaml::Controls::StackPanel const& panel) {
    return panel.Children();
}

} // namespace

LauncherController::LauncherController(Host* host) : host_(host) {}

void LauncherController::EnsureWindow() {
    if (window_) {
        if (!closed_) return;
        // 窗口曾被外部 WM_CLOSE 销毁：释放失效引用并全量重建，
        // 对已销毁的 XAML Window 调 Show 会卡死并抛异常
        window_ = nullptr;
        closed_ = false;
    }
    window_ = Window();
    // 订阅 Closed：无论窗口因什么原因被销毁，下次 Show 都能重建
    window_.Closed([this](auto&&, auto&&) {
        closed_ = true;
        visible_ = false;
    });
    dispatcher_ = window_.DispatcherQueue();

    // 无边框无标题栏的深色浮层（Win11 自带系统圆角）
    auto presenter = window_.AppWindow().Presenter().as<winrt::Microsoft::UI::Windowing::OverlappedPresenter>();
    presenter.SetBorderAndTitleBar(false, false);
    presenter.IsResizable(false);
    presenter.IsMaximizable(false);
    presenter.IsMinimizable(false);

    auto root = Grid();
    auto dark = SolidColorBrush(winrt::Windows::UI::Color{0xF4, 0x20, 0x20, 0x20});
    root.Background(dark);
    auto rows = root.RowDefinitions();
    auto r0 = RowDefinition();
    r0.Height(GridLengthHelper::Auto());
    auto r1 = RowDefinition();
    r1.Height(GridLengthHelper::FromValueAndType(1, GridUnitType::Star));
    rows.Append(r0);
    rows.Append(r1);

    searchBox_ = TextBox();
    searchBox_.Margin(ThicknessHelper::FromLengths(14, 14, 14, 10));
    searchBox_.CornerRadius(CornerRadiusHelper::FromUniformRadius(8));
    searchBox_.FontSize(20);
    searchBox_.MinHeight(44);
    searchBox_.VerticalContentAlignment(VerticalAlignment::Center);
    searchBox_.PlaceholderText(L"搜索应用、文件…");
    searchBox_.Background(SolidColorBrush(winrt::Windows::UI::Color{0xFF, 0x2D, 0x2D, 0x2D}));
    searchBox_.BorderBrush(SolidColorBrush(winrt::Windows::UI::Color{0x00, 0, 0, 0}));
    searchBox_.Foreground(SolidColorBrush(winrt::Windows::UI::Color{0xFF, 0xF2, 0xF2, 0xF2}));
    Grid::SetRow(searchBox_, 0);
    searchBox_.TextChanged([this](winrt::Windows::Foundation::IInspectable const&, TextChangedEventArgs const&) {
        RunSearch();
    });
    searchBox_.KeyDown([this](winrt::Windows::Foundation::IInspectable const&, KeyRoutedEventArgs const& e) {
        const auto key = e.Key();
        const int count = static_cast<int>(tiles_.size());
        if (key == Windows::System::VirtualKey::Up || key == Windows::System::VirtualKey::Down ||
            key == Windows::System::VirtualKey::Left || key == Windows::System::VirtualKey::Right) {
            e.Handled(true);
            if (count == 0) return;
            const int delta = key == Windows::System::VirtualKey::Up    ? -kColumns
                              : key == Windows::System::VirtualKey::Down ? kColumns
                              : key == Windows::System::VirtualKey::Left ? -1
                                                                          : 1;
            int v = (selectedIndex_ < 0 ? 0 : selectedIndex_) + delta;
            v = (std::max)(0, (std::min)(v, count - 1));
            selectedIndex_ = v;
            UpdateSelection();
        } else if (key == Windows::System::VirtualKey::Enter) {
            if (!results_.empty()) {
                OpenIndex(selectedIndex_ >= 0 && selectedIndex_ < static_cast<int>(results_.size()) ? selectedIndex_ : 0);
            }
        } else if (key == Windows::System::VirtualKey::Escape) {
            Hide();
        }
    });
    root.Children().Append(searchBox_);

    resultsPanel_ = StackPanel();
    resultsPanel_.Padding(ThicknessHelper::FromLengths(12, 2, 12, 14));

    auto scroll = ScrollViewer();
    scroll.Content(resultsPanel_);
    scroll.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
    scroll.VerticalScrollMode(ScrollMode::Auto);
    Grid::SetRow(scroll, 1);
    root.Children().Append(scroll);

    window_.Content(root);
    window_.Title(L"Desktop Sticker 搜索");

    // 选中/悬停底色
    selectedBrush_ = SolidColorBrush(winrt::Windows::UI::Color{0x38, 0xFF, 0xFF, 0xFF});
    hoverBrush_ = SolidColorBrush(winrt::Windows::UI::Color{0x16, 0xFF, 0xFF, 0xFF});
    idleBrush_ = SolidColorBrush(winrt::Windows::UI::Color{0x00, 0, 0, 0});

    // 尺寸与位置：主屏水平居中、垂直约 1/5 处（AppWindow 使用物理像素）
    const float scale = static_cast<float>(GetDpiForSystem()) / 96.0f;
    const int width = static_cast<int>(kColumns * kTileWidth * scale + 44 * scale);
    const int height = static_cast<int>(430 * scale);
    window_.AppWindow().Resize({width, height});
    const int screenW = GetSystemMetrics(SM_CXSCREEN);
    const int screenH = GetSystemMetrics(SM_CYSCREEN);
    window_.AppWindow().Move({(screenW - width) / 2, screenH / 5});

    // 不在任务栏显示，且常驻置顶（显示层高于所有其他程序）；去边框白边 + 系统圆角
    HWND hwnd = nullptr;
    window_.as<::IWindowNative>()->get_WindowHandle(&hwnd);
    MakeTopmostToolWindow(hwnd);
    ApplyBorderlessRounded(hwnd);
}

void LauncherController::Show() {
    try {
        EnsureWindow();
        if (!window_) { AppLog("launcher", "launcher Show: no window!"); return; }
        visible_ = true;
        AppLog("launcher", "launcher Show begin");
        HWND hwnd = nullptr;
        window_.as<::IWindowNative>()->get_WindowHandle(&hwnd);

    try {
        window_.AppWindow().Show();
        AppLog("launcher", "launcher Show: AppWindow.Show ok");
    } catch (...) { AppLog("launcher", "launcher Show: AppWindow.Show THREW"); }

    // 置顶并压过其他置顶窗口（GamePP 等 overlay）
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    // 抢前台：桌面/其他程序为前台时，前台锁会拦住普通 Activate，
    // 先把输入队列临时附加到前台线程再 SetForegroundWindow 即可绕过
    HWND fg = GetForegroundWindow();
    const DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    const DWORD curThread = GetCurrentThreadId();
    const bool attached = fgThread && fgThread != curThread &&
                          AttachThreadInput(curThread, fgThread, TRUE);
    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);
    if (attached) AttachThreadInput(curThread, fgThread, FALSE);

    try {
        window_.Activate(); // XAML 层激活，焦点进搜索框
        AppLog("launcher", "launcher Show: Activate ok");
    } catch (...) { AppLog("launcher", "launcher Show: Activate THREW"); }
    try {
        searchBox_.Text(L"");
        RunSearch();
        searchBox_.Focus(FocusState::Programmatic);
        AppLog("launcher", "launcher Show: focus ok");
    } catch (...) { AppLog("launcher", "launcher Show: focus/search THREW"); }

    RECT rc{};
    GetWindowRect(hwnd, &rc);
    char buf[192]{};
    snprintf(buf, sizeof(buf),
             "launcher Show end: hwnd=%p vis=%d topmost=%d fg=%d rect=(%ld,%ld)-(%ld,%ld)",
             (void*)hwnd, IsWindowVisible(hwnd) ? 1 : 0,
             (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) ? 1 : 0,
             GetForegroundWindow() == hwnd ? 1 : 0, rc.left, rc.top, rc.right, rc.bottom);
    AppLog("launcher", buf);
    } catch (...) {
        // 双击空格回调经 DLL 钩子线程转 UI 线程进入这里：异常不允许逃逸
        AppLog("launcher", "launcher Show FAILED (exception)");
    }
}

void LauncherController::Hide() {
    try {
        if (!window_) return;
        visible_ = false;
        // 用 Hide 而不是 Close：Close 会销毁 Window，再次 Show 会崩溃
        window_.AppWindow().Hide();
        AppLog("launcher", "Hide");
    } catch (...) {
        AppLog("launcher", "Hide FAILED (exception)");
    }
}

void LauncherController::RunSearch() {
    results_.clear();
    tiles_.clear();
    selectedIndex_ = -1;
    PanelChildren(resultsPanel_).Clear();
    const auto query = searchBox_.Text();
    if (query.empty() || !host_ || !host_->Module()) return;

    const auto raw = host_->Module()->Search(query.c_str(), 30);
    if (raw.empty()) return;

    // 按来源分组展示（组标题 + 图标网格）；来源标识以 IFeatureModule.h 的 sources 常量为准
    static const struct { const wchar_t* source; const wchar_t* title; } kGroups[] = {
        {desktopsticker::sources::kApps, L"手动添加"},
        {desktopsticker::sources::kStartMenu, L"开始菜单"},
        {desktopsticker::sources::kDesktop, L"桌面"},
        {desktopsticker::sources::kDocuments, L"文档"},
        {desktopsticker::sources::kDownloads, L"下载"},
        {desktopsticker::sources::kPictures, L"图片"},
        {desktopsticker::sources::kVideos, L"视频"},
        {desktopsticker::sources::kMusic, L"音乐"},
    };
    for (const auto& g : kGroups) {
        BuildGroup(g.source, g.title, raw);
    }
    UpdateSelection();
}

void LauncherController::BuildGroup(const std::wstring& source, const wchar_t* title,
                                    std::vector<desktopsticker::SearchResult> const& raw) {
    std::vector<int> hits;
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i].source == source) hits.push_back(static_cast<int>(i));
    }
    if (hits.empty()) return;

    auto header = TextBlock();
    header.Text(title);
    header.FontSize(13);
    header.Margin(ThicknessHelper::FromLengths(4, 10, 0, 4));
    header.Foreground(SolidColorBrush(winrt::Windows::UI::Color{0xB3, 0xFF, 0xFF, 0xFF}));
    PanelChildren(resultsPanel_).Append(header);

    const int count = static_cast<int>(hits.size());
    const int rows = (count + kColumns - 1) / kColumns;
    auto grid = Grid();
    grid.Margin(ThicknessHelper::FromLengths(0, 0, 0, 6));
    for (int c = 0; c < kColumns; ++c) {
        auto col = ColumnDefinition();
        col.Width(GridLengthHelper::FromPixels(kTileWidth));
        grid.ColumnDefinitions().Append(col);
    }
    for (int r = 0; r < rows; ++r) {
        auto row = RowDefinition();
        row.Height(GridLengthHelper::FromPixels(kTileHeight));
        grid.RowDefinitions().Append(row);
    }

    for (int k = 0; k < count; ++k) {
        const int globalIdx = static_cast<int>(results_.size());
        results_.push_back(raw[static_cast<size_t>(hits[static_cast<size_t>(k)])]);

        auto img = Image();
        img.Width(40);
        img.Height(40);
        img.Stretch(Stretch::Uniform);

        auto name = TextBlock();
        name.Text(results_[static_cast<size_t>(globalIdx)].name);
        name.FontSize(11);
        name.TextAlignment(TextAlignment::Center);
        name.TextWrapping(TextWrapping::Wrap);
        name.MaxLines(2);
        name.TextTrimming(TextTrimming::CharacterEllipsis);
        name.Foreground(SolidColorBrush(winrt::Windows::UI::Color{0xE6, 0xFF, 0xFF, 0xFF}));

        auto stack = StackPanel();
        stack.HorizontalAlignment(HorizontalAlignment::Center);
        stack.VerticalAlignment(VerticalAlignment::Center);
        stack.Spacing(4);
        stack.Children().Append(img);
        stack.Children().Append(name);

        auto tile = Border();
        tile.Background(idleBrush_);
        tile.CornerRadius(CornerRadiusHelper::FromUniformRadius(6));
        tile.Width(kTileWidth);
        tile.Height(kTileHeight);
        tile.Child(stack);
        Grid::SetColumn(tile, k % kColumns);
        Grid::SetRow(tile, k / kColumns);
        tile.Tapped([this, globalIdx](auto const&, auto const&) {
            OpenIndex(globalIdx);
        });
        // RunSearch 会整体重建 tiles_/results_：事件若与重建交错，globalIdx 可能越界。
        // 捕获 tile 自身的弱引用来摸背景，越界直接放弃。
        winrt::weak_ref<Border> weakTile = tile;
        tile.PointerEntered([this, weakTile, globalIdx](winrt::Windows::Foundation::IInspectable const&,
                                              winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&) {
            if (globalIdx != selectedIndex_ && globalIdx >= 0 &&
                static_cast<size_t>(globalIdx) < tiles_.size()) {
                if (auto t = weakTile.get()) t.Background(hoverBrush_);
            }
        });
        tile.PointerExited([this, weakTile, globalIdx](winrt::Windows::Foundation::IInspectable const&,
                                             winrt::Microsoft::UI::Xaml::Input::PointerRoutedEventArgs const&) {
            if (globalIdx != selectedIndex_ && globalIdx >= 0 &&
                static_cast<size_t>(globalIdx) < tiles_.size()) {
                if (auto t = weakTile.get()) t.Background(idleBrush_);
            }
        });

        LoadTileIconAsync(results_[static_cast<size_t>(globalIdx)].path, img);
        grid.Children().Append(tile);
        tiles_.push_back(tile);
    }
    PanelChildren(resultsPanel_).Append(grid);
}

void LauncherController::UpdateSelection() {
    if (tiles_.empty()) return;
    if (selectedIndex_ < 0 || selectedIndex_ >= static_cast<int>(tiles_.size())) selectedIndex_ = 0;
    for (size_t i = 0; i < tiles_.size(); ++i) {
        tiles_[i].Background(i == static_cast<size_t>(selectedIndex_) ? selectedBrush_ : idleBrush_);
    }
}

void LauncherController::OpenIndex(int index) {
    if (index < 0 || index >= static_cast<int>(results_.size())) return;
    if (host_ && host_->Module()) {
        host_->Module()->OpenItem(results_[static_cast<size_t>(index)].path);
    }
    Hide();
}

winrt::fire_and_forget LauncherController::LoadTileIconAsync(std::wstring path, Image image) {
    apartment_context ui;
    using winrt::Windows::Graphics::Imaging::SoftwareBitmap;

    // 图标提取统一走功能模块的三级兜底管线（SHDefExtract → SHGetFileInfo → Shell 图像工厂），
    // EXE 只负责 HICON → SoftwareBitmap 的格式转换；HICON 归模块缓存，不得 DestroyIcon
    if (iconCache_.find(path) == iconCache_.end()) {
        SoftwareBitmap sb{ nullptr };
        auto makeFromBytes = [&](std::vector<uint8_t>& bits, UINT w, UINT h) {
            auto buffer = winrt::Windows::Storage::Streams::Buffer(static_cast<uint32_t>(bits.size()));
            std::memcpy(buffer.data(), bits.data(), bits.size());
            buffer.Length(static_cast<uint32_t>(bits.size()));
            sb = SoftwareBitmap::CreateCopyFromBuffer(
                buffer,
                winrt::Windows::Graphics::Imaging::BitmapPixelFormat::Bgra8,
                static_cast<int>(w), static_cast<int>(h),
                winrt::Windows::Graphics::Imaging::BitmapAlphaMode::Premultiplied);
        };
        co_await winrt::resume_background();
        {
            HICON icon = (host_ && host_->Module()) ? host_->Module()->GetIcon(path, 48) : nullptr;
            if (icon) {
                winrt::com_ptr<IWICImagingFactory> wic;
                if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                               IID_PPV_ARGS(wic.put())))) {
                    winrt::com_ptr<IWICBitmap> wicBmp;
                    winrt::com_ptr<IWICFormatConverter> conv;
                    if (SUCCEEDED(wic->CreateBitmapFromHICON(icon, wicBmp.put())) &&
                        SUCCEEDED(wic->CreateFormatConverter(conv.put())) &&
                        SUCCEEDED(conv->Initialize(wicBmp.get(), GUID_WICPixelFormat32bppPBGRA,
                                                   WICBitmapDitherTypeNone, nullptr, 0.0,
                                                   WICBitmapPaletteTypeCustom))) {
                        UINT w = 0, h = 0;
                        conv->GetSize(&w, &h);
                        const UINT stride = w * 4;
                        std::vector<uint8_t> bits(static_cast<size_t>(stride) * h);
                        if (w > 0 && h > 0 &&
                            SUCCEEDED(conv->CopyPixels(nullptr, stride, static_cast<UINT>(bits.size()), bits.data()))) {
                            makeFromBytes(bits, w, h);
                        }
                    }
                }
            }
        }
        co_await ui; // 回 UI 线程写入缓存并设置图像源
        if (!sb) co_return;
        if (iconCache_.size() > 256) iconCache_.clear(); // 简单上限防膨胀
        iconCache_.emplace(path, std::move(sb));
    }

    auto it = iconCache_.find(path);
    if (it == iconCache_.end()) co_return;
    SoftwareBitmapSource src;
    co_await src.SetBitmapAsync(it->second);
    image.Source(src);
}

} // namespace desktopsticker::app
