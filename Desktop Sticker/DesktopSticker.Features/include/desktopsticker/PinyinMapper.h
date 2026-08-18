#pragma once
#include <string>
#include <unordered_map>

#include "desktopsticker/Export.h"

namespace desktopsticker {

class DESKTOPSTICKER_API PinyinMapper {
public:
    // 返回中文文件名的拼音首字母（小写）；非中文字符原样小写保留
    static std::wstring GetInitials(const std::wstring& text);

private:
    static const std::unordered_map<wchar_t, wchar_t>& Table();
};

} // namespace desktopsticker
