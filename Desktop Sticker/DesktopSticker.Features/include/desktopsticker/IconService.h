#pragma once
#include <map>
#include <mutex>
#include <string>

#include "desktopsticker/Export.h"

namespace desktopsticker {

// 全项目唯一的图标提取入口（磁贴渲染与搜索面板共用同一条管线）。
// 提取链：SHDefExtractIcon（精确尺寸、解析 .lnk 目标）→ SHGetFileInfo →
// IShellItemImageFactory(SIIGBF_ICONONLY，兜住 mp4/mp3 等 UWP 关联类型) → 通用 exe 图标。
class DESKTOPSTICKER_API IconService {
public:
    // 返回调用方无需释放的 HICON（由本类缓存并统一销毁，调用方不得 DestroyIcon）
    HICON GetIcon(const std::wstring& path, int size);
    void ClearCache();
    ~IconService();

private:
    HICON ExtractWithShell(const std::wstring& path, int size);
    HICON ExtractWithImageFactory(const std::wstring& path, int size);

    // 缓存可能被 UI 线程（磁贴渲染）与宿主搜索面板并发访问，map 必须有锁
    mutable std::mutex cacheMutex_;
    std::map<std::pair<std::wstring, int>, HICON> cache_;
};

} // namespace desktopsticker
