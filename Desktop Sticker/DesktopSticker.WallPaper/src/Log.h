#pragma once

#include <filesystem>
#include <fstream>
#include <string>

namespace desktopsticker::wallpaper {

// 与 Features 的 dstklog / EXE 的 AppLog 写同一份 %APPDATA%\DesktopSticker\debug.log，
// 以 [wallpaper] 标记来源，便于混排排查。
inline std::wstring log_file_path() {
    PWSTR appData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData))) return {};
    std::filesystem::path root(appData);
    CoTaskMemFree(appData);
    root /= L"DesktopSticker";
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    return (root / L"debug.log").wstring();
}

inline void wp_log(const std::string& msg) {
    const std::wstring path = log_file_path();
    if (path.empty()) return;
    std::ofstream out(path, std::ios::app);
    out << "[wallpaper] " << msg << std::endl;
}

} // namespace desktopsticker::wallpaper
