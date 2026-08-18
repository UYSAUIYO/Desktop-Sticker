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

private:
    std::wstring ClassifyPath(const std::wstring& path);
    size_t CountZoneItems() const;
    void AutoClassify(const std::vector<DesktopIconInfo>& icons);
    void CollectIntoZone(const std::wstring& zoneId, const std::wstring& path);
    void RemoveFromZone(const std::wstring& zoneId, const std::wstring& path);
    void CreateZoneWindows();
    void DestroyZoneWindows();
    void SaveLayout();
    void StartDesktopWatcher();

    void StartMouseHook();
    void StopMouseHook();
    bool IsPointOverZone(POINT pt) const;

    static LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam);

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
};

} // namespace desktopsticker
