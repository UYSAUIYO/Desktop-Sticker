#pragma once

#include <functional>
#include <string>
#include <vector>

#include "WallPaperStore.h"
#include "desktopsticker/wallpaper/Types.h"

namespace desktopsticker::wallpaper {

// 中等范围媒体库：导入（逐字节复制源文件）、列表、重命名、删除。
// 不做分组、回收站事务、库迁移事务、内容去重。
// 硬约束：源文件永不被改写，删除只作用于库内副本。
class DESKTOPSTICKER_WALLPAPER_API MediaLibrary {
public:
    MediaLibrary(std::wstring root, WallPaperStore& store);

    std::vector<WallPaperItem> List() const;
    // 导入来源可以是文件（视频/动图/图片）也可以是目录（图片序列/网页/着色器），
    // 类型由 classify_file / classify_directory 自动判定
    bool Import(const std::wstring& srcPath, std::wstring& outId);
    // 目录型来源：图片文件夹 → frames/、网页文件夹 → web/、着色器文件夹 → shader/
    bool ImportDirectory(const std::wstring& srcDir, std::wstring& outId);
    bool Rename(const std::wstring& id, const std::wstring& name);
    bool Remove(const std::wstring& id);
    bool Update(const std::wstring& id, const std::function<void(WallPaperItem&)>& mutate);

    std::wstring ItemDir(const std::wstring& id) const;
    std::wstring SourcePath(const WallPaperItem& item) const;
    std::wstring VariantsDir(const std::wstring& id) const;
    std::wstring PosterPath(const std::wstring& id) const;
    std::wstring VariantPath(const std::wstring& id, VariantKind kind, int revision) const;
    std::wstring LibraryJsonPath() const;

    // 只含字母数字与短横的 id 才允许拼进路径，防止路径逃逸
    static bool IsSafeId(const std::wstring& id);
    static std::wstring NewId();

    const std::wstring& Root() const { return root_; }

private:
    std::wstring root_;
    WallPaperStore& store_;
};

} // namespace desktopsticker::wallpaper
