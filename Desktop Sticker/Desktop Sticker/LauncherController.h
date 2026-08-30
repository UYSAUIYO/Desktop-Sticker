#pragma once
#include <map>
#include <string>
#include <vector>

#include <winrt/Windows.Graphics.Imaging.h>

#include "Host.h"

namespace desktopsticker::app {

class LauncherController {
public:
    explicit LauncherController(Host* host);

    void Show();
    void Hide();
    bool Visible() const { return visible_; }

private:
    void EnsureWindow();
    void RunSearch();
    void BuildGroup(const std::wstring& source, const wchar_t* title,
                    std::vector<desktopsticker::SearchResult> const& raw);
    void UpdateSelection();
    void OpenIndex(int index);
    winrt::fire_and_forget LoadTileIconAsync(std::wstring path,
                                             winrt::Microsoft::UI::Xaml::Controls::Image image);

    Host* host_ = nullptr;
    bool visible_ = false;
    bool closed_ = false; // 窗口被外部 WM_CLOSE 销毁过，下次 Show 需全量重建
    winrt::Microsoft::UI::Xaml::Window window_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::TextBox searchBox_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::StackPanel resultsPanel_{ nullptr };
    // 结果按网格顺序存放；tiles_ 与 results_ 一一对应
    std::vector<desktopsticker::SearchResult> results_;
    std::vector<winrt::Microsoft::UI::Xaml::Controls::Border> tiles_;
    int selectedIndex_ = -1;
    winrt::Microsoft::UI::Xaml::Media::SolidColorBrush selectedBrush_{ nullptr };
    winrt::Microsoft::UI::Xaml::Media::SolidColorBrush hoverBrush_{ nullptr };
    winrt::Microsoft::UI::Xaml::Media::SolidColorBrush idleBrush_{ nullptr };
    winrt::Microsoft::UI::Dispatching::DispatcherQueue dispatcher_{ nullptr };
    // 图标位图缓存（路径 → 48px SoftwareBitmap）：重复搜索/重复结果不再走提取管线
    std::map<std::wstring, winrt::Windows::Graphics::Imaging::SoftwareBitmap> iconCache_;
};

} // namespace desktopsticker::app
