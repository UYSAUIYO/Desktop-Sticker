#include "pch.h"
#include "desktopsticker/DesktopIconManager.h"

#include <commctrl.h>
#include <shlobj.h>
#include <set>

#include "desktopsticker/Utf8.h"

namespace desktopsticker {

namespace {

HANDLE OpenListViewProcess(HWND lv) {
    DWORD pid = 0;
    GetWindowThreadProcessId(lv, &pid);
    if (pid == 0) return nullptr;
    return OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE,
                       FALSE, pid);
}

std::wstring GetItemTextRemote(HWND lv, HANDLE hProc, int index) {
    constexpr size_t kBufChars = 512;
    const SIZE_T lvSize = sizeof(LVITEMW);
    const SIZE_T total = lvSize + kBufChars * sizeof(wchar_t);
    LPVOID remote = VirtualAllocEx(hProc, nullptr, total, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) return {};

    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = index;
    item.iSubItem = 0;
    item.pszText = reinterpret_cast<PWSTR>(static_cast<BYTE*>(remote) + lvSize);
    item.cchTextMax = static_cast<int>(kBufChars);
    WriteProcessMemory(hProc, remote, &item, sizeof(item), nullptr);

    SendMessageW(lv, LVM_GETITEMTEXTW, index, reinterpret_cast<LPARAM>(remote));

    wchar_t buf[kBufChars]{};
    ReadProcessMemory(hProc, static_cast<BYTE*>(remote) + lvSize, buf, sizeof(buf), nullptr);
    VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
    return std::wstring(buf);
}

bool GetItemPositionRemote(HWND lv, HANDLE hProc, int index, POINT& pt) {
    LPVOID remote = VirtualAllocEx(hProc, nullptr, sizeof(POINT), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) return false;

    SendMessageW(lv, LVM_GETITEMPOSITION, index, reinterpret_cast<LPARAM>(remote));

    ReadProcessMemory(hProc, remote, &pt, sizeof(pt), nullptr);
    VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
    return true;
}

} // namespace

DesktopIconManager::DesktopIconManager(DesktopShellIntegration* shell) : shell_(shell) {}

std::vector<DesktopIconInfo> DesktopIconManager::EnumIcons() {
    std::vector<DesktopIconInfo> icons;
    HWND lv = ListView();
    if (!lv || !IsWindow(lv)) return icons;

    const int count = static_cast<int>(SendMessageW(lv, LVM_GETITEMCOUNT, 0, 0));
    if (count <= 0) return icons;

    HANDLE hProc = OpenListViewProcess(lv);
    if (!hProc) return icons;

    for (int i = 0; i < count; ++i) {
        std::wstring label = GetItemTextRemote(lv, hProc, i);
        if (label.empty()) continue;

        POINT pt{};
        GetItemPositionRemote(lv, hProc, i, pt);

        DesktopIconInfo info;
        info.index = i;
        info.label = label;
        info.position = pt;
        info.path = ResolvePathFromLabel(label);
        icons.push_back(std::move(info));
    }

    CloseHandle(hProc);
    return icons;
}

bool DesktopIconManager::MoveIconOffscreen(int index) {
    HWND lv = ListView();
    if (!lv || !IsWindow(lv)) return false;
    return SendMessageW(lv, LVM_SETITEMPOSITION, index, MAKELPARAM(-32000, -32000)) != FALSE;
}

bool DesktopIconManager::RestoreIcon(int index, POINT position) {
    HWND lv = ListView();
    if (!lv || !IsWindow(lv)) return false;
    return SendMessageW(lv, LVM_SETITEMPOSITION, index, MAKELPARAM(position.x, position.y)) != FALSE;
}

bool DesktopIconManager::SetAutoArrange(bool enable) {
    HWND lv = ListView();
    if (!lv || !IsWindow(lv)) return false;
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
    PWSTR publicPath = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktopPath))) {
        return L"";
    }
    SHGetKnownFolderPath(FOLDERID_PublicDesktop, 0, nullptr, &publicPath);

    std::vector<std::filesystem::path> dirs;
    dirs.emplace_back(desktopPath);
    if (publicPath) dirs.emplace_back(publicPath);
    CoTaskMemFree(desktopPath);
    if (publicPath) CoTaskMemFree(publicPath);

    for (const auto& desktop : dirs) {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(desktop, ec)) {
            const auto& p = entry.path();
            const std::wstring fileName = p.filename().wstring();
            const std::wstring stem = p.stem().wstring();
            // 快捷方式在桌面上显示为去掉 .lnk 的名称；普通文件显示完整文件名
            if (_wcsicmp(fileName.c_str(), label.c_str()) == 0 ||
                _wcsicmp(stem.c_str(), label.c_str()) == 0) {
                return p.wstring();
            }
            SHFILEINFOW sfi{};
            if (SHGetFileInfoW(p.c_str(), 0, &sfi, sizeof(sfi), SHGFI_DISPLAYNAME)) {
                std::wstring displayName = sfi.szDisplayName;
                if (_wcsicmp(displayName.c_str(), label.c_str()) == 0) {
                    return p.wstring();
                }
            }
        }
    }
    return L"";
}

} // namespace desktopsticker
