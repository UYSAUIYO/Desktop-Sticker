#pragma once
#include <map>
#include <string>

#include "desktopsticker/Export.h"

namespace desktopsticker {

class DESKTOPSTICKER_API IconService {
public:
    // 返回调用方无需释放的 HICON（由本类缓存并统一销毁）
    HICON GetIcon(const std::wstring& path, int size);
    void ClearCache();
    ~IconService();

private:
    std::map<std::pair<std::wstring, int>, HICON> cache_;
};

} // namespace desktopsticker
