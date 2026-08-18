#include "pch.h"
#include "desktopsticker/IconService.h"

namespace desktopsticker {

IconService::~IconService() {
    ClearCache();
}

HICON IconService::GetIcon(const std::wstring& path, int size) {
    const auto key = std::make_pair(path, size);
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second;

    HICON icon = nullptr;
    SHFILEINFOW sfi{};
    UINT flags = SHGFI_ICON;
    if (size >= 32) flags |= SHGFI_LARGEICON;
    else flags |= SHGFI_SMALLICON;

    if (SHGetFileInfoW(path.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi), flags)) {
        icon = sfi.hIcon;
    }
    if (!icon) {
        // 通用图标兜底
        SHGetFileInfoW(L".exe", FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi), flags);
        icon = sfi.hIcon;
    }
    if (icon) cache_[key] = icon;
    return icon;
}

void IconService::ClearCache() {
    for (auto& [key, icon] : cache_) {
        if (icon) DestroyIcon(icon);
    }
    cache_.clear();
}

} // namespace desktopsticker
