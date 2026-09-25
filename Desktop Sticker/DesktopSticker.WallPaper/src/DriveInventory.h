#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "desktopsticker/WallPaperExport.h"
#include "desktopsticker/wallpaper/StoragePlacement.h"

namespace desktopsticker::wallpaper {

struct VolumeIdentity {
    uint32_t serial = 0;
    uint64_t rootFileId = 0;
};

// GetDriveTypeW 的返回值映射（纯函数，可单测）
DESKTOPSTICKER_WALLPAPER_API DriveKind map_drive_type(unsigned long win32Type);

// 枚举所有逻辑盘及其类型与调用者可用空间
DESKTOPSTICKER_WALLPAPER_API std::vector<DriveInfo> enumerate_drives();

// 读取某路径所在卷的序列号与该路径目录的文件 ID
DESKTOPSTICKER_WALLPAPER_API bool probe_volume_identity(const std::wstring& path,
                                                       VolumeIdentity& out);

// 启动校验：盘符可能被复用而指向另一个卷，必须两项都比对
DESKTOPSTICKER_WALLPAPER_API bool verify_volume_identity(const std::wstring& path,
                                                        const VolumeIdentity& expected);

// 首次启用：枚举 → 选择固定盘 → 建 DesktopSticker\Wallpaper\ → 返回库根
// 无合格固定盘或创建失败返回 nullopt（调用方据此降级）
DESKTOPSTICKER_WALLPAPER_API std::optional<std::wstring> resolve_library_root();

} // namespace desktopsticker::wallpaper
