#include "pch.h"
#include "LauncherController.h"

#include <microsoft.ui.xaml.window.h>

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
            host_->Module()->OpenItem(results_[0].path);
            Hide();
        } else if (e.Key() == Windows::System::VirtualKey::Escape) {
            Hide();
        }
    });

    resultList_ = ListView();
    resultList_.MaxHeight(420);
    resultList_.IsItemClickEnabled(true);
    resultList_.ItemClick([this](winrt::Windows::Foundation::IInspectable const&, ItemClickEventArgs const&) {
        if (!results_.empty()) {
            host_->Module()->OpenItem(results_[0].path);
            Hide();
        }
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
    window_.Activate();
    searchBox_.Focus(FocusState::Programmatic);
}

void LauncherController::Hide() {
    if (!window_) return;
    visible_ = false;
    window_.Close();
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
    for (const auto& r : results_) {
        auto tb = TextBlock();
        tb.Text(r.name + L"  —  " + r.source);
        resultList_.Items().Append(tb);
    }
}

} // namespace desktopsticker::app
