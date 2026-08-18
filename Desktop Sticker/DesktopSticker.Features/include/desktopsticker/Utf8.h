#pragma once
#include <string>
#include <windows.h>

namespace desktopsticker {

// UTF-8 <-> UTF-16 转换（nlohmann::json 使用 UTF-8，Windows API 使用 UTF-16）
inline std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                                         nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()),
                        &out[0], size, nullptr, nullptr);
    return out;
}

inline std::wstring FromUtf8(const std::string& s) {
    if (s.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                                         nullptr, 0);
    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                        &out[0], size);
    return out;
}

} // namespace desktopsticker
