#pragma once
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "desktopsticker/ConfigStore.h"
#include "desktopsticker/DesktopIconManager.h"
#include "desktopsticker/DesktopShellIntegration.h"
#include "desktopsticker/DirectoryWatcher.h"
#include "desktopsticker/DropTarget.h"
#include "desktopsticker/IconClassifier.h"
#include "desktopsticker/IconService.h"
#include "desktopsticker/MouseInputForwarder.h"
#include "desktopsticker/ZoneModel.h"
#include "desktopsticker/ZoneWindow.h"
#include "ClockWidget.h" // 小组件公共头（src/widgets/include）

namespace desktopsticker {

class DesktopWorkspace {
public:
    // icons 由 FeatureModule 持有并传入（磁贴渲染与 EXE 搜索面板共用同一图标缓存）
    DesktopWorkspace(ConfigStore* config, IconService* icons, const std::function<void()>& zonesChanged);
    ~DesktopWorkspace();

    bool Initialize();
    void Shutdown();

    void ToggleCleanDesktop();
    void RestoreDesktop();
    void Refresh();
    void SetZoneSpacing(int columnSpacing, int rowSpacing);
    // 按当前配置的每列卡片数重排初始布局（设置页改"每列卡片数"时调用，覆盖手动位置）
    void RelayoutZones();
    // 桌面时钟小组件显隐（设置页开关时调用；未创建过则首次创建并嵌入）
    void SetClockVisible(bool show);

private:
    size_t CountZoneItems() const;
    void AutoClassify(const std::vector<DesktopIconInfo>& icons);
    void ApplyCompactColumnLayout(); // 左右各两列贴边、动态列高对齐任务栏（布局版本 7）
    void CollectIntoZone(const std::wstring& zoneId, const std::wstring& path);
    void RemoveFromZone(const std::wstring& zoneId, const std::wstring& path);
    void CreateZoneWindows();
    void DestroyZoneWindows();
    void DestroyZoneWindow(ZoneWindow* window);
    void DetachDropTarget(HWND hwnd);
    void SyncZoneWindows();
    void SaveLayout();
    void StartDesktopWatcher();
    void CollectNewDesktopIcons();
    void CreateMessageWindow();
    void CreateClock();

    ZoneWindow* ZoneAtPoint(POINT pt) const;

    static LRESULT CALLBACK WorkspaceMsgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    ConfigStore* config_;
    std::function<void()> zonesChanged_;
    std::filesystem::path layoutPath_;

    DesktopShellIntegration shell_;
    std::unique_ptr<DesktopIconManager> iconManager_;
    IconService* iconService_ = nullptr; // 非所有权：归 FeatureModule
    IconClassifier classifier_;          // 表驱动的桌面图标分类
    std::unique_ptr<DirectoryWatcher> desktopWatcher_;
    ZoneModel model_;
    std::vector<std::unique_ptr<ZoneWindow>> zoneWindows_;
    std::vector<DropTarget*> dropTargets_;
    std::unique_ptr<MouseInputForwarder> inputForwarder_; // 低级鼠标钩子：降级转发 + 空白双击
    std::unique_ptr<ClockWidget> clock_;                  // 桌面时钟小组件（点击穿透）
    bool cleanMode_ = false;
    bool oleInitialized_ = false;
    // message-only 窗口：把 watcher 后台线程回调封送回 UI 线程，避免跨线程并发改 model
    HWND msgHwnd_ = nullptr;
};

} // namespace desktopsticker
