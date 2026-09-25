#pragma once

// 盘符选择策略：纯函数，不触碰 OS，便于单测。
// OS 枚举（GetLogicalDriveStringsW / GetDriveTypeW / GetDiskFreeSpaceExW）在 DriveInventory.cpp。

#include <cstdint>
#include <optional>
#include <vector>

namespace desktopsticker::wallpaper {

enum class DriveKind { Fixed, Removable, Remote, Optical, Ram, Unknown };

struct DriveInfo {
    wchar_t letter = 0;          // 'C'
    DriveKind kind = DriveKind::Unknown;
    uint64_t freeAvailable = 0;  // GetDiskFreeSpaceExW 的 ullFreeAvailable（调用者可用空间）
};

// 只考虑固定盘（排除可移动盘/网络盘/光驱，否则插着的大容量移动盘会被选中，拔盘即失效）；
// 取可用空间最大者；并列时取盘符最小者以保证结果确定。无合格固定盘返回 nullopt。
inline std::optional<wchar_t> choose_library_drive(const std::vector<DriveInfo>& drives) {
    std::optional<wchar_t> best;
    uint64_t bestFree = 0;
    for (const auto& d : drives) {
        if (d.kind != DriveKind::Fixed) continue;
        const bool better = !best.has_value() || d.freeAvailable > bestFree ||
                            (d.freeAvailable == bestFree && d.letter < *best);
        if (better) {
            best = d.letter;
            bestFree = d.freeAvailable;
        }
    }
    return best;
}

} // namespace desktopsticker::wallpaper
