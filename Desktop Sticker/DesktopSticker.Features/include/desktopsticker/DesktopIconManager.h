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

    HWND ListView() const { return shell_->Windows().listView; }

private:
    std::wstring ResolvePathFromLabel(const std::wstring& label);

    DesktopShellIntegration* shell_;
};

} // namespace desktopsticker
