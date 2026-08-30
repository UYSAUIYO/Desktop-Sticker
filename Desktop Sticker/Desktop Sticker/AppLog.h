#pragma once
#include <filesystem>
#include <fstream>
#include <shlobj_core.h>

namespace desktopsticker::app {

// EXE 侧调试日志：与 DLL 的 dstklog 写同一份 %APPDATA%\DesktopSticker\debug.log
// （UTF-8，按 tag 区分来源）。EXE 侧只此一个入口，禁止再写私设的日志函数。
inline void AppLog(const char* tag, const char* msg) {
    PWSTR appData = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData))) return;
    std::filesystem::path root(appData);
    CoTaskMemFree(appData);
    root /= L"DesktopSticker";
    std::error_code ec;
    std::filesystem::create_directories(root, ec);
    std::ofstream out(root / L"debug.log", std::ios::app);
    out << "[" << tag << "] " << msg << std::endl;
}

} // namespace desktopsticker::app
