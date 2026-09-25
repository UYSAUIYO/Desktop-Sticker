#include "pch.h"
#include "test_framework.h"
#include "DriveInventory.h"

using namespace desktopsticker;
using namespace desktopsticker::wallpaper;

TEST(DriveIdentity_MapDriveType) {
    ASSERT_TRUE(map_drive_type(DRIVE_FIXED) == DriveKind::Fixed);
    ASSERT_TRUE(map_drive_type(DRIVE_REMOVABLE) == DriveKind::Removable);
    ASSERT_TRUE(map_drive_type(DRIVE_REMOTE) == DriveKind::Remote);
    ASSERT_TRUE(map_drive_type(DRIVE_CDROM) == DriveKind::Optical);
    ASSERT_TRUE(map_drive_type(DRIVE_RAMDISK) == DriveKind::Ram);
    ASSERT_TRUE(map_drive_type(0x7FFFFFFF) == DriveKind::Unknown);
}

TEST(DriveIdentity_EnumerateFindsAtLeastOneFixedDrive) {
    // 跑测试的机器必然有一个可写的系统盘
    const auto drives = enumerate_drives();
    ASSERT_FALSE(drives.empty());

    bool hasFixed = false;
    for (const auto& d : drives) {
        if (d.kind == DriveKind::Fixed && d.freeAvailable > 0) hasFixed = true;
    }
    ASSERT_TRUE(hasFixed);
}

TEST(DriveIdentity_ProbeCurrentDirectory) {
    VolumeIdentity id{};
    ASSERT_TRUE(probe_volume_identity(std::filesystem::current_path().wstring(), id));
    ASSERT_TRUE(id.serial != 0 || id.rootFileId != 0);
}

TEST(DriveIdentity_VerifyAcceptsItsOwnIdentity) {
    const std::wstring here = std::filesystem::current_path().wstring();
    VolumeIdentity id{};
    ASSERT_TRUE(probe_volume_identity(here, id));
    ASSERT_TRUE(verify_volume_identity(here, id));
}

TEST(DriveIdentity_VerifyRejectsForgedSerial) {
    const std::wstring here = std::filesystem::current_path().wstring();
    VolumeIdentity id{};
    ASSERT_TRUE(probe_volume_identity(here, id));

    VolumeIdentity forged = id;
    forged.serial = id.serial + 1;              // 盘符被复用指向别的卷
    ASSERT_FALSE(verify_volume_identity(here, forged));
}

TEST(DriveIdentity_VerifyRejectsForgedFileId) {
    const std::wstring here = std::filesystem::current_path().wstring();
    VolumeIdentity id{};
    ASSERT_TRUE(probe_volume_identity(here, id));

    VolumeIdentity forged = id;
    forged.rootFileId = id.rootFileId + 1;
    ASSERT_FALSE(verify_volume_identity(here, forged));
}
