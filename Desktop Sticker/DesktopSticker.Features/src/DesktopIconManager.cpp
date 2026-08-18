#include "pch.h"
#include "desktopsticker/DesktopIconManager.h"

#include <commctrl.h>
#include <shlobj.h>
#include <set>

namespace desktopsticker {

DesktopIconManager::DesktopIconManager(DesktopShellIntegration* shell) : shell_(shell) {}

std::vector<DesktopIconInfo> DesktopIconManager::EnumIcons() {
    std::vector<DesktopIconInfo> icons;
    HWND lv = ListView();
    if (!lv) return icons;

    const int count = static_cast<int>(SendMessageW(lv, LVM_GETITEMCOUNT, 0, 0));
    for (int i = 0; i < count; ++i) {
        wchar_t buf[512]{};
        LVITEMW item{};
        item.mask = LVIF_TEXT;
        item.iItem = i;
        item.pszText = buf;
        item.cchTextMax = 512;
        SendMessageW(lv, LVM_GETITEMTEXTW, i, reinterpret_cast<LPARAM>(&item));

        POINT pt{};
        SendMessageW(lv, LVM_GETITEMPOSITION, i, reinterpret_cast<LPARAM>(&pt));

        DesktopIconInfo info;
        info.index = i;
        info.label = buf;
        info.position = pt;
        info.path = ResolvePathFromLabel(buf);
        icons.push_back(std::move(info));
    }
    return icons;
}

bool DesktopIconManager::MoveIconOffscreen(int index) {
    HWND lv = ListView();
    if (!lv) return false;
    return SendMessageW(lv, LVM_SETITEMPOSITION, index, MAKELPARAM(-32000, -32000)) != FALSE;
}

bool DesktopIconManager::RestoreIcon(int index, POINT position) {
    HWND lv = ListView();
    if (!lv) return false;
    return SendMessageW(lv, LVM_SETITEMPOSITION, index, MAKELPARAM(position.x, position.y)) != FALSE;
}

bool DesktopIconManager::SetAutoArrange(bool enable) {
    HWND lv = ListView();
    if (!lv) return false;
    const LONG_PTR style = GetWindowLongPtrW(lv, GWL_STYLE);
    LONG_PTR newStyle = style;
    if (enable) newStyle |= LVS_AUTOARRANGE;
    else newStyle &= ~LVS_AUTOARRANGE;
    if (newStyle != style) {
        SetWindowLongPtrW(lv, GWL_STYLE, newStyle);
    }
    return true;
}

std::wstring DesktopIconManager::ResolvePathFromLabel(const std::wstring& label) {
    PWSTR desktopPath = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktopPath))) {
        return L"";
    }
    std::wstring result;
    std::filesystem::path desktop(desktopPath);
    CoTaskMemFree(desktopPath);

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(desktop, ec)) {
        const auto& p = entry.path();
        SHFILEINFOW sfi{};
        if (SHGetFileInfoW(p.c_str(), 0, &sfi, sizeof(sfi), SHGFI_DISPLAYNAME)) {
            std::wstring displayName = sfi.szDisplayName;
            if (_wcsicmp(displayName.c_str(), label.c_str()) == 0) {
                result = p.wstring();
                break;
            }
        }
    }
    return result;
}

} // namespace desktopsticker
