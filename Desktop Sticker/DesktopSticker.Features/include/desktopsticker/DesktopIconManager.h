#pragma once
#include <map>
#include <string>
#include <vector>
#include <windows.h>

#include "desktopsticker/DesktopShellIntegration.h"
#include "desktopsticker/Export.h"

namespace desktopsticker {

struct DESKTOPSTICKER_API DesktopIconInfo {
    int index = -1;
    std::wstring label;
    std::wstring path;
    POINT position{};
};

class DESKTOPSTICKER_API DesktopIconManager {
public:
    explicit DesktopIconManager(DesktopShellIntegration* shell);

    std::vector<DesktopIconInfo> EnumIcons();
    bool MoveIconOffscreen(int index);
    bool RestoreIcon(int index, POINT position);
    bool SetAutoArrange(bool enable);
    // 直接隐藏/显示整个桌面图标列表窗口（最简单可靠的原生图标隐藏方式）
    bool HideAllIcons(bool hide);

    HWND ListView() const { return shell_->Windows().listView; }

private:
    std::wstring ResolvePathFromLabel(const std::wstring& label);

    DesktopShellIntegration* shell_;
};

} // namespace desktopsticker
