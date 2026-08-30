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
#include "desktopsticker/IconService.h"
#include "desktopsticker/ZoneModel.h"
#include "desktopsticker/ZoneWindow.h"

namespace desktopsticker {

class DesktopWorkspace {
public:
    DesktopWorkspace(ConfigStore* config, const std::function<void()>& zonesChanged);
    ~DesktopWorkspace();

    bool Initialize();
    void Shutdown();

    void ToggleCleanDesktop();
    void RestoreDesktop();
    void Refresh();
    void SetZoneSpacing(int columnSpacing, int rowSpacing);

private:
    std::wstring ClassifyPath(const std::wstring& path);
    size_t CountZoneItems() const;
    void AutoClassify(const std::vector<DesktopIconInfo>& icons);
    void ApplySideColumnLayout(); // 左右两列贴边、沿垂直中线均匀分布（布局版本 4）
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

    void StartMouseHook();
    void StopMouseHook();
    ZoneWindow* ZoneAtPoint(POINT pt) const;
    LRESULT ForwardMouseToZone(ZoneWindow* zone, UINT msg, const POINT& pt);
    void DetectDesktopBlankDoubleClick(const POINT& pt);

    static LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK WorkspaceMsgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    ConfigStore* config_;
    std::function<void()> zonesChanged_;
    std::filesystem::path layoutPath_;

    DesktopShellIntegration shell_;
    std::unique_ptr<DesktopIconManager> iconManager_;
    std::unique_ptr<IconService> iconService_;
    std::unique_ptr<DirectoryWatcher> desktopWatcher_;
    ZoneModel model_;
    std::vector<std::unique_ptr<ZoneWindow>> zoneWindows_;
    std::vector<DropTarget*> dropTargets_;
    HHOOK mouseHook_ = nullptr;
    bool cleanMode_ = false;
    bool oleInitialized_ = false;
    // message-only 窗口：把 watcher 后台线程回调封送回 UI 线程，避免跨线程并发改 model
    HWND msgHwnd_ = nullptr;
    // hook 转发/空白双击判定状态（hook 回调与窗口操作同在 UI 线程，无并发）
    ZoneWindow* forwardDownZone_ = nullptr;
    DWORD forwardDownTick_ = 0;
    POINT forwardDownPt_{};
    DWORD blankDownTick_ = 0;
    POINT blankDownPt_{};
};

} // namespace desktopsticker
