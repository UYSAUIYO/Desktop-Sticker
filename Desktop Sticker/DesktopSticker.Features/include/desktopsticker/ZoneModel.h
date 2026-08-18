#pragma once
#include <filesystem>
#include <map>
#include <string>
#include <vector>
#include <windows.h>

#include "desktopsticker/Export.h"

namespace desktopsticker {

struct DESKTOPSTICKER_API Zone {
    std::wstring id;
    std::wstring name;
    RECT rect{};          // 相对所在显示器工作区
    int monitorIndex = 0;
    bool collapsed = false;
    std::vector<std::wstring> itemPaths; // 被收纳项完整路径
};

struct DESKTOPSTICKER_API DesktopLayout {
    std::vector<Zone> zones;
    // 被收纳原生图标 -> 原始屏幕坐标（用于恢复）
    std::map<std::wstring, POINT> originalIconPositions;
    // 收纳前是否开启“自动排列图标”
    bool autoArrangeWasEnabled = false;
};

class DESKTOPSTICKER_API ZoneModel {
public:
    bool Load(const std::filesystem::path& layoutPath);
    bool Save(const std::filesystem::path& layoutPath) const;

    const DesktopLayout& Layout() const { return layout_; }
    DesktopLayout& Layout() { return layout_; }

    Zone* FindZone(const std::wstring& id);
    Zone* FindZoneByName(const std::wstring& name);
    void AddZone(Zone zone);
    bool RemoveZone(const std::wstring& id);
    bool MoveItem(const std::wstring& itemPath, const std::wstring& fromZoneId, const std::wstring& toZoneId);
    std::wstring GenerateZoneId() const;

private:
    DesktopLayout layout_;
};

} // namespace desktopsticker
