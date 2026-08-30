#pragma once
#include <filesystem>
#include <fstream>
#include <string>

#include "desktopsticker/KnownFolders.h"
#include "desktopsticker/Utf8.h"

namespace desktopsticker {
namespace dstklog {

// 统一调试日志：%APPDATA%\DesktopSticker\debug.log。
// 全部子系统（workspace/zone/hotkey/module/open-fail…）只经此一处写入，
// 按来源打 tag，避免同名多份实现各自演化；超 5MB 清空重写，防止无限膨胀。
inline const std::filesystem::path& LogDir() {
    static const std::filesystem::path dir = [] {
        const std::wstring appData = GetKnownPath(FOLDERID_RoamingAppData);
        if (!appData.empty()) return std::filesystem::path(appData) / L"DesktopSticker";
        return std::filesystem::temp_directory_path() / L"DesktopSticker";
    }();
    return dir;
}

inline void Write(const wchar_t* tag, const std::wstring& msg) {
    std::error_code ec;
    const std::filesystem::path path = LogDir() / L"debug.log";
    std::filesystem::create_directories(LogDir(), ec);
    const auto size = std::filesystem::file_size(path, ec);
    std::ofstream out(path, (!ec && size > 5ull * 1024 * 1024) ? std::ios::trunc : std::ios::app);
    if (!out.is_open()) return;
    if (tag && *tag) out << "[" << ToUtf8(tag) << "] ";
    out << ToUtf8(msg) << std::endl;
}

} // namespace dstklog
} // namespace desktopsticker
