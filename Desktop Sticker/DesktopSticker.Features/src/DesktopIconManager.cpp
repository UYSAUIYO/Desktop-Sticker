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

bool GetItemTextRemote(HWND lv, HANDLE hProc, int index, std::wstring& out) {
    constexpr size_t kBufChars = 512;
    const SIZE_T lvSize = sizeof(LVITEMW);
    const SIZE_T total = lvSize + kBufChars * sizeof(wchar_t);
    LPVOID remote = VirtualAllocEx(hProc, nullptr, total, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) return false;

    LVITEMW item{};
    item.mask = LVIF_TEXT;
    item.iItem = index;
    item.iSubItem = 0;
    item.pszText = reinterpret_cast<PWSTR>(static_cast<BYTE*>(remote) + lvSize);
    item.cchTextMax = static_cast<int>(kBufChars);
    WriteProcessMemory(hProc, remote, &item, sizeof(item), nullptr);

    DWORD_PTR result = 0;
    if (!SendMessageTimeoutW(lv, LVM_GETITEMTEXTW, index, reinterpret_cast<LPARAM>(remote),
                             SMTO_ABORTIFHUNG, 500, &result)) {
        VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
        return false;
    }

    wchar_t buf[kBufChars]{};
    ReadProcessMemory(hProc, static_cast<BYTE*>(remote) + lvSize, buf, sizeof(buf), nullptr);
    VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
    out = buf;
    return true;
}

bool GetItemPositionRemote(HWND lv, HANDLE hProc, int index, POINT& pt) {
    LPVOID remote = VirtualAllocEx(hProc, nullptr, sizeof(POINT), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) return false;

    DWORD_PTR result = 0;
    if (!SendMessageTimeoutW(lv, LVM_GETITEMPOSITION, index, reinterpret_cast<LPARAM>(remote),
                             SMTO_ABORTIFHUNG, 500, &result)) {
        VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
        return false;
    }

    ReadProcessMemory(hProc, remote, &pt, sizeof(pt), nullptr);
    VirtualFreeEx(hProc, remote, 0, MEM_RELEASE);
    return true;
}

struct DesktopEntry {
    std::wstring label;
    std::wstring path;
};

std::vector<DesktopEntry> BuildDesktopEntries() {
    std::vector<DesktopEntry> entries;
    PWSTR desktopPath = nullptr;
    PWSTR publicPath = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktopPath))) return entries;
    SHGetKnownFolderPath(FOLDERID_PublicDesktop, 0, nullptr, &publicPath);

    std::vector<std::filesystem::path> dirs;
    dirs.emplace_back(desktopPath);
    if (publicPath) dirs.emplace_back(publicPath);
    CoTaskMemFree(desktopPath);
    if (publicPath) CoTaskMemFree(publicPath);

    for (const auto& d : dirs) {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(d, ec)) {
            const auto& p = entry.path();
            const std::wstring stem = p.stem().wstring();
            const std::wstring fileName = p.filename().wstring();
            entries.push_back({stem, p.wstring()});
            entries.push_back({fileName, p.wstring()});
            SHFILEINFOW sfi{};
            if (SHGetFileInfoW(p.c_str(), 0, &sfi, sizeof(sfi), SHGFI_DISPLAYNAME)) {
                const std::wstring dn = sfi.szDisplayName;
                if (_wcsicmp(dn.c_str(), stem.c_str()) != 0 && _wcsicmp(dn.c_str(), fileName.c_str()) != 0) {
                    entries.push_back({dn, p.wstring()});
                }
            }
        }
    }
    return entries;
}

std::wstring FindPathByLabel(const std::vector<DesktopEntry>& entries, const std::wstring& label) {
    for (const auto& e : entries) {
        if (_wcsicmp(e.label.c_str(), label.c_str()) == 0) return e.path;
    }
    return L"";
}

} // namespace

DesktopIconManager::DesktopIconManager(DesktopShellIntegration* shell) : shell_(shell) {}

std::vector<DesktopIconInfo> DesktopIconManager::EnumIcons() {
    std::vector<DesktopIconInfo> icons;
    HWND lv = ListView();
    if (!lv || !IsWindow(lv)) return icons;

    DWORD_PTR countResult = 0;
    if (!SendMessageTimeoutW(lv, LVM_GETITEMCOUNT, 0, 0, SMTO_ABORTIFHUNG, 1000, &countResult)) {
        return icons;
    }
    const int count = static_cast<int>(countResult);
    if (count <= 0) return icons;

    HANDLE hProc = OpenListViewProcess(lv);
    if (!hProc) return icons;

    const auto desktopEntries = BuildDesktopEntries();

    for (int i = 0; i < count; ++i) {
        std::wstring label;
        if (!GetItemTextRemote(lv, hProc, i, label)) break; // Explorer 无响应则放弃枚举，避免长时间卡死
        if (label.empty()) continue;

        POINT pt{};
        GetItemPositionRemote(lv, hProc, i, pt);

        DesktopIconInfo info;
        info.index = i;
        info.label = label;
        info.position = pt;
        info.path = FindPathByLabel(desktopEntries, label);
        icons.push_back(std::move(info));
    }

    CloseHandle(hProc);
    return icons;
}

bool DesktopIconManager::MoveIconOffscreen(int index) {
    HWND lv = ListView();
    if (!lv || !IsWindow(lv)) return false;
    DWORD_PTR result = 0;
    return SendMessageTimeoutW(lv, LVM_SETITEMPOSITION, index, MAKELPARAM(-32000, -32000),
                               SMTO_ABORTIFHUNG, 500, &result) != FALSE;
}

bool DesktopIconManager::RestoreIcon(int index, POINT position) {
    HWND lv = ListView();
    if (!lv || !IsWindow(lv)) return false;
    DWORD_PTR result = 0;
    return SendMessageTimeoutW(lv, LVM_SETITEMPOSITION, index, MAKELPARAM(position.x, position.y),
                               SMTO_ABORTIFHUNG, 500, &result) != FALSE;
}

bool DesktopIconManager::HideAllIcons(bool hide) {
    HWND lv = ListView();
    if (!lv || !IsWindow(lv)) return false;
    ShowWindow(lv, hide ? SW_HIDE : SW_SHOW);
    return true;
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
