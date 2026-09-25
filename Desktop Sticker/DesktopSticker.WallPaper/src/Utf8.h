#pragma once

#include <string>

namespace desktopsticker::wallpaper {

// JSON 以 UTF-8 存储，模块内部统一用 wstring。
inline std::string to_utf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int need = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (need <= 0) return {};
    std::string out(static_cast<size_t>(need), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                        out.data(), need, nullptr, nullptr);
    return out;
}

inline std::wstring from_utf8(const std::string& s) {
    if (s.empty()) return {};
    const int need = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                                         nullptr, 0);
    if (need <= 0) return {};
    std::wstring out(static_cast<size_t>(need), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                        out.data(), need);
    return out;
}

} // namespace desktopsticker::wallpaper
