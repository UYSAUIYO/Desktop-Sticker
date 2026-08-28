#include "pch.h"
#include "LauncherController.h"

#include <microsoft.ui.xaml.window.h>
#include <winrt/Microsoft.UI.Windowing.h>

using namespace winrt;
using namespace Microsoft::UI::Xaml;
using namespace Microsoft::UI::Xaml::Controls;
using namespace Microsoft::UI::Xaml::Input;

namespace desktopsticker::app {

LauncherController::LauncherController(Host* host) : host_(host) {}

void LauncherController::EnsureWindow() {
    if (window_) return;

    window_ = Window();

    auto root = StackPanel();
    root.Padding(ThicknessHelper::FromLengths(12, 12, 12, 12));
    root.Spacing(8);

    searchBox_ = TextBox();
    searchBox_.PlaceholderText(L"搜索应用、文件…");
    searchBox_.FontSize(18);
    searchBox_.TextChanged([this](winrt::Windows::Foundation::IInspectable const&, TextChangedEventArgs const&) {
        RunSearch();
    });
    searchBox_.KeyDown([this](winrt::Windows::Foundation::IInspectable const&, KeyRoutedEventArgs const& e) {
        if (e.Key() == Windows::System::VirtualKey::Enter && !results_.empty()) {
            int idx = resultList_.SelectedIndex(); // 优先打开当前选中项，否则第一条
            if (idx < 0 || idx >= static_cast<int>(results_.size())) idx = 0;
            host_->Module()->OpenItem(results_[idx].path);
            Hide();
        } else if (e.Key() == Windows::System::VirtualKey::Escape) {
            Hide();
        }
    });

    resultList_ = ListView();
    resultList_.MaxHeight(420);
    resultList_.IsItemClickEnabled(true);
    resultList_.ItemClick([this](winrt::Windows::Foundation::IInspectable const&, ItemClickEventArgs const& e) {
        if (results_.empty()) return;
        int idx = -1;
        if (auto item = e.ClickedItem().try_as<TextBlock>()) {
            idx = winrt::unbox_value_or<int>(item.Tag(), -1); // 打开实际点击的那一条
        }
        if (idx < 0 || idx >= static_cast<int>(results_.size())) idx = 0;
        host_->Module()->OpenItem(results_[idx].path);
        Hide();
    });

    root.Children().Append(searchBox_);
    root.Children().Append(resultList_);
    window_.Content(root);
    window_.Title(L"Desktop Sticker 搜索");

    // 无边框、不显示任务栏
    HWND hwnd = nullptr;
    window_.as<::IWindowNative>()->get_WindowHandle(&hwnd);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE,
                      GetWindowLongPtrW(hwnd, GWL_EXSTYLE) | WS_EX_TOOLWINDOW);
}

void LauncherController::Show() {
    EnsureWindow();
    if (!window_) return;
    visible_ = true;
    window_.AppWindow().Show();
    window_.Activate();
    searchBox_.Focus(FocusState::Programmatic);
}

void LauncherController::Hide() {
    if (!window_) return;
    visible_ = false;
    // 用 Hide 而不是 Close：Close 会销毁 Window，再次 Show 会崩溃
    window_.AppWindow().Hide();
}

void LauncherController::RunSearch() {
    const auto query = searchBox_.Text();
    if (query.empty()) {
        resultList_.Items().Clear();
        results_.clear();
        return;
    }
    results_ = host_->Module()->Search(query.c_str(), 30);
    resultList_.Items().Clear();
    for (size_t i = 0; i < results_.size(); ++i) {
        auto tb = TextBlock();
        tb.Text(results_[i].name + L"  —  " + results_[i].source);
        tb.Tag(box_value(static_cast<int32_t>(i))); // ItemClick 用 Tag 找回结果索引
        resultList_.Items().Append(tb);
    }
}

} // namespace desktopsticker::app
