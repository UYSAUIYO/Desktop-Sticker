#include "pch.h"
#include "desktopsticker/IconService.h"

#include <shlobj_core.h>

namespace desktopsticker {

IconService::~IconService() {
    ClearCache();
}

namespace {

HICON IconFromHBITMAP(HBITMAP hbm) {
    // IShellItemImageFactory 返回 32bpp HBITMAP，包成带 Alpha 的 HICON（内部会复制位图）
    BITMAP bm{};
    if (!GetObjectW(hbm, sizeof(bm), &bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0) return nullptr;
    HBITMAP mask = CreateBitmap(bm.bmWidth, std::abs(bm.bmHeight), 1, 1, nullptr);
    if (!mask) return nullptr;
    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmMask = mask;
    ii.hbmColor = hbm;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(mask); // 位图所有权归 CreateIconIndirect 复制件，原件自行释放
    return icon;
}

} // namespace

HICON IconService::ExtractWithShell(const std::wstring& path, int size) {
    HICON icon = nullptr;
    // 精确尺寸提取：普通文件与 .lnk 目标程序都走这里
    if (SUCCEEDED(SHDefExtractIconW(path.c_str(), 0, 0, &icon, nullptr, size)) && icon) return icon;

    SHFILEINFOW sfi{};
    UINT flags = SHGFI_ICON;
    if (size >= 32) flags |= SHGFI_LARGEICON;
    else flags |= SHGFI_SMALLICON;
    if (SHGetFileInfoW(path.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi), flags)) {
        icon = sfi.hIcon;
    }
    return icon;
}

HICON IconService::ExtractWithImageFactory(const std::wstring& path, int size) {
    // Shell 图像工厂能解析 UWP 应用关联的文件类型（仅取图标不取缩略图）
    IShellItemImageFactory* factory = nullptr;
    HICON icon = nullptr;
    if (SUCCEEDED(SHCreateItemFromParsingName(path.c_str(), nullptr, IID_PPV_ARGS(&factory)))) {
        HBITMAP hbm = nullptr;
        const SIZE dims{size, size};
        if (SUCCEEDED(factory->GetImage(dims, SIIGBF_ICONONLY | SIIGBF_BIGGERSIZEOK, &hbm)) && hbm) {
            icon = IconFromHBITMAP(hbm);
            DeleteObject(hbm);
        }
        factory->Release();
    }
    return icon;
}

HICON IconService::GetIcon(const std::wstring& path, int size) {
    const auto key = std::make_pair(path, size);
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second;

    HICON icon = ExtractWithShell(path, size);
    if (!icon) icon = ExtractWithImageFactory(path, size);
    if (!icon) {
        // 通用图标兜底
        SHFILEINFOW sfi{};
        UINT flags = SHGFI_ICON;
        if (size >= 32) flags |= SHGFI_LARGEICON;
        else flags |= SHGFI_SMALLICON;
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
