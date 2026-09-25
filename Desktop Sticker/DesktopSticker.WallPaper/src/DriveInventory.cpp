#include "pch.h"
#include "DriveInventory.h"

#include "Log.h"
#include "Utf8.h"

namespace desktopsticker::wallpaper {

DriveKind map_drive_type(unsigned long win32Type) {
    switch (win32Type) {
        case DRIVE_FIXED:     return DriveKind::Fixed;
        case DRIVE_REMOVABLE: return DriveKind::Removable;
        case DRIVE_REMOTE:    return DriveKind::Remote;
        case DRIVE_CDROM:     return DriveKind::Optical;
        case DRIVE_RAMDISK:   return DriveKind::Ram;
        default:              return DriveKind::Unknown;
    }
}

std::vector<DriveInfo> enumerate_drives() {
    std::vector<DriveInfo> out;
    wchar_t buffer[512]{};
    const DWORD len = GetLogicalDriveStringsW(static_cast<DWORD>(std::size(buffer)), buffer);
    if (len == 0) return out;

    for (const wchar_t* p = buffer; *p; p += wcslen(p) + 1) {
        if (p[0] == L'\0' || p[1] != L':') continue;

        DriveInfo info{};
        info.letter = p[0];
        info.kind = map_drive_type(GetDriveTypeW(p));
        if (info.kind == DriveKind::Fixed) {
            ULARGE_INTEGER freeAvailable{};
            if (GetDiskFreeSpaceExW(p, &freeAvailable, nullptr, nullptr)) {
                info.freeAvailable = freeAvailable.QuadPart;
            }
        }
        out.push_back(info);
    }
    return out;
}

bool probe_volume_identity(const std::wstring& path, VolumeIdentity& out) {
    out = VolumeIdentity{};

    wchar_t volumeRoot[MAX_PATH]{};
    if (!GetVolumePathNameW(path.c_str(), volumeRoot, MAX_PATH)) return false;

    DWORD serial = 0;
    if (!GetVolumeInformationW(volumeRoot, nullptr, 0, &serial, nullptr, nullptr, nullptr, 0)) {
        return false;
    }

    HANDLE h = CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        // 目录尚不存在时至少能拿到卷序列号，文件 ID 留 0 并由调用方视为未绑定
        out.serial = serial;
        return false;
    }

    BY_HANDLE_FILE_INFORMATION fi{};
    const BOOL ok = GetFileInformationByHandle(h, &fi);
    CloseHandle(h);
    if (!ok) { out.serial = serial; return false; }

    out.serial = serial;
    out.rootFileId = (static_cast<uint64_t>(fi.nFileIndexHigh) << 32) | fi.nFileIndexLow;
    return true;
}

bool verify_volume_identity(const std::wstring& path, const VolumeIdentity& expected) {
    VolumeIdentity actual{};
    if (!probe_volume_identity(path, actual)) return false;
    return actual.serial == expected.serial && actual.rootFileId == expected.rootFileId;
}

std::optional<std::wstring> resolve_library_root() {
    const auto drives = enumerate_drives();
    const auto letter = choose_library_drive(drives);
    if (!letter) {
        wp_log("no suitable fixed drive for wallpaper library");
        return std::nullopt;
    }

    std::wstring root;
    root += *letter;
    root += L":\\DesktopSticker\\Wallpaper";

    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    if (ec) {
        wp_log("create library root failed: " + to_utf8(root) + " : " + ec.message());
        return std::nullopt;
    }
    return root;
}

} // namespace desktopsticker::wallpaper
