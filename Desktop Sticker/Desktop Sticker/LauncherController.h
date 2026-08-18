#pragma once
#include <string>
#include <vector>

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

    Host* host_ = nullptr;
    bool visible_ = false;
    winrt::Microsoft::UI::Xaml::Window window_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::TextBox searchBox_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::ListView resultList_{ nullptr };
    std::vector<desktopsticker::SearchResult> results_;
};

} // namespace desktopsticker::app
