#pragma once

#include "desktopsticker/wallpaper/Types.h"

namespace desktopsticker::wallpaper {

struct VariantAvailability {
    bool hasBalanced = false;
    bool hasPowerSaver = false;
};

// 首选档位的副本存在则用它，否则回落到原画（原画路径再由 MF→FFmpeg 兜底）。
inline VariantKind resolve_effective_variant(VariantKind preferred,
                                             const VariantAvailability& a) {
    if (preferred == VariantKind::Balanced && a.hasBalanced) return VariantKind::Balanced;
    if (preferred == VariantKind::PowerSaver && a.hasPowerSaver) return VariantKind::PowerSaver;
    return VariantKind::Original;
}

} // namespace desktopsticker::wallpaper
