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

    // 壁纸模块在后台线程回调（库变更 / 播放状态变化），宿主已切回 UI 线程；
    // 对已销毁的 XAML Window 必须先判断有效性，不能直接访问控件。
    void RefreshWallPaper();

private:
    void EnsureWindow();
    void RefreshApps();
    void SaveConfig();
    void SaveWallPaper();
    void RefreshWallPaperControls();
    // 每秒刷一次"当前播放方式 + 实时帧率"（由 DispatcherTimer 驱动）
    void RefreshPlaybackText();
    void ImportWallPaper();
    void ImportWallPaperFolder();
    void OpenWallPaperConfigDir();
    void RemoveSelectedWallPaper();
    void RegenerateSelectedVariant();
    void ChangeWallPaperRoot();
    // 取壁纸网格当前选中项的 id；未选中返回空
    std::wstring SelectedWallPaperId() const;

    Host* host_ = nullptr;
    bool visible_ = false;
    bool loading_ = false; // 初始化控件赋值期间抑制 SaveConfig，防止把未初始化值写进配置
    bool closed_ = false;  // 标题栏 X 已销毁 XAML Window，下次 Show 需全量重建
    winrt::Microsoft::UI::Xaml::Window window_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch searchDesktopSwitch_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch searchKnownFoldersSwitch_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch startMenuSwitch_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch clockSwitch_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch followThemeSwitch_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::ComboBox hotkeyModeCombo_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::ComboBox zoneCardsCombo_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::NumberBox columnSpacingBox_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::NumberBox rowSpacingBox_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::TextBox appPathBox_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::ListView appList_{ nullptr };

    // 动态壁纸卡片
    bool wallpaperLoading_ = false;
    winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch wallPaperSwitch_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch wallPaperPauseFullscreenSwitch_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch wallPaperPauseLockSwitch_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch wallPaperUserPauseSwitch_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::ComboBox wallPaperVariantCombo_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::ComboBox wallPaperSpeedCombo_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::ComboBox wallPaperDecodePathCombo_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::TextBlock wallPaperPlaybackText_{ nullptr };
    winrt::Microsoft::UI::Xaml::DispatcherTimer playbackTimer_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch wallPaperAudioSwitch_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::Slider wallPaperVolumeSlider_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::TextBlock wallPaperVolumeLabel_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::GridView wallPaperGrid_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::TextBlock wallPaperStatus_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::Button wallPaperImportButton_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::Button wallPaperImportFolderButton_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::Button wallPaperRemoveButton_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::Button wallPaperVariantButton_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::Button wallPaperChangeRootButton_{ nullptr };
    winrt::Microsoft::UI::Xaml::Controls::Button wallPaperOpenConfigButton_{ nullptr };
};

} // namespace desktopsticker::app
