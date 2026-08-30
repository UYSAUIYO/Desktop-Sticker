#pragma once
#include <algorithm>
#include <string>
#include <cwctype>

namespace desktopsticker {

inline std::wstring ToLowerCopy(std::wstring s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
    return s;
}

} // namespace desktopsticker
