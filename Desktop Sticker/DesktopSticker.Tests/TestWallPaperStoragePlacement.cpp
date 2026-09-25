#include "pch.h"
#include "test_framework.h"
#include <desktopsticker/wallpaper/StoragePlacement.h>

using namespace desktopsticker::wallpaper;

TEST(StoragePlacement_ExcludesNonFixedDrives) {
    std::vector<DriveInfo> drives = {
        {'C', DriveKind::Fixed,     100ull},
        {'D', DriveKind::Removable, 999999ull},   // 大容量移动盘，必须排除
        {'E', DriveKind::Remote,    888888ull},
        {'F', DriveKind::Optical,   777777ull},
    };
    auto picked = choose_library_drive(drives);
    ASSERT_TRUE(picked.has_value());
    ASSERT_EQ(L'C', *picked);
}

TEST(StoragePlacement_PicksLargestFreeSpace) {
    std::vector<DriveInfo> drives = {
        {'C', DriveKind::Fixed, 10ull},
        {'D', DriveKind::Fixed, 500ull},
        {'E', DriveKind::Fixed, 20ull},
    };
    auto picked = choose_library_drive(drives);
    ASSERT_TRUE(picked.has_value());
    ASSERT_EQ(L'D', *picked);
}

TEST(StoragePlacement_TieBreaksByLowestLetter) {
    std::vector<DriveInfo> drives = {
        {'F', DriveKind::Fixed, 300ull},
        {'D', DriveKind::Fixed, 300ull},
        {'C', DriveKind::Fixed, 300ull},
    };
    auto picked = choose_library_drive(drives);
    ASSERT_TRUE(picked.has_value());
    ASSERT_EQ(L'C', *picked);
}

TEST(StoragePlacement_NoFixedDrive_ReturnsNullopt) {
    std::vector<DriveInfo> drives = {
        {'D', DriveKind::Removable, 999999ull},
        {'E', DriveKind::Remote,    888888ull},
    };
    ASSERT_FALSE(choose_library_drive(drives).has_value());
}

TEST(StoragePlacement_EmptyInput_ReturnsNullopt) {
    ASSERT_FALSE(choose_library_drive({}).has_value());
}
