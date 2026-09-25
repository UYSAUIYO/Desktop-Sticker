#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "desktopsticker/WallPaperExport.h"
#include "desktopsticker/wallpaper/Types.h"

namespace desktopsticker::wallpaper {

// 库根的身份记录：盘符可被复用，必须靠卷序列号 + 根目录文件 ID 才能确认
// 记录的路径仍指向当初那个卷（见规格 6.2 / 6.3）。
struct StorageRecord {
    std::wstring root;
    uint32_t volumeSerial = 0;
    uint64_t rootFileId = 0;
    bool bound = false;
};

struct PersistedState {
    WallPaperSettings settings;
    StorageRecord storage;
};

// wallpaper.json / library.json 的读写。
// 契约：损坏或缺失一律回落到默认值并备份原文件，绝不抛异常；
//       写入一律"临时文件 + 原子替换"。
class DESKTOPSTICKER_WALLPAPER_API WallPaperStore {
public:
    explicit WallPaperStore(std::wstring configDir);

    PersistedState LoadState() const;
    bool SaveState(const PersistedState& state) const;

    std::vector<WallPaperItem> LoadLibrary(const std::wstring& libraryRoot) const;
    bool SaveLibrary(const std::wstring& libraryRoot, const std::vector<WallPaperItem>& items) const;

    const std::wstring& ConfigDir() const { return configDir_; }

private:
    std::wstring configDir_;
};

// 供测试与实现共用的原子写：先写 .tmp，再 MoveFileExW 替换。
DESKTOPSTICKER_WALLPAPER_API bool WriteFileAtomic(const std::wstring& path,
                                                  const std::string& content);

} // namespace desktopsticker::wallpaper
